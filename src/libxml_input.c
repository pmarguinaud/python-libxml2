#include "libxml_input.h"
#include "libxml_utils.h"
#include <libxml/xmlIO.h>
#include <libxml/parserInternals.h>

static __thread LibXML_ParseState *current = NULL;

static xmlExternalEntityLoader base_loader = NULL;
static PyObject *global_loader = NULL;      /* externalEntityLoader() */
static PyObject *global_callbacks = NULL;   /* (match, open, read, close), or NULL */

void
LibXML_state_push(LibXML_ParseState *state, PyObject *callbacks, PyObject *handler,
                  int no_network)
{
    Py_XINCREF(handler);
    state->prev = current;
    state->callbacks = callbacks;
    state->handler = handler;
    state->matched = NULL;
    state->no_network = no_network;
    current = state;
}

void
LibXML_state_pop(LibXML_ParseState *state)
{
    current = state->prev;
    Py_CLEAR(state->callbacks);
    Py_CLEAR(state->handler);
    Py_CLEAR(state->matched);
}

/* What a callback does in a parser of its own stays out of the errors of this one. */
static PyObject *
call(PyObject *func, PyObject *args)
{
    if (args == NULL) return NULL;

    PyObject *errors = LibXML_errors_hold();
    PyObject *result = PyObject_CallObject(func, args);
    LibXML_errors_resume(errors);
    Py_DECREF(args);
    return result;
}

static PyObject *
filename_object(const char *filename)
{
    return PyUnicode_DecodeUTF8(filename, (Py_ssize_t)strlen(filename), "replace");
}

/* bytes from what a callback returned, or NULL with an exception. */
static PyObject *
content_bytes(PyObject *value)
{
    if (PyUnicode_Check(value))
        return PyUnicode_AsUTF8String(value);
    if (PyBytes_Check(value)) {
        Py_INCREF(value);
        return value;
    }
    if (PyObject_CheckBuffer(value))
        return PyBytes_FromObject(value);
    PyErr_SetString(PyExc_TypeError, "a callback must return str or bytes");
    return NULL;
}

/* ------------------------------------------------------------------ */
/* External entities                                                   */
/* ------------------------------------------------------------------ */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static xmlParserInputPtr
input_from_handler(PyObject *handler, const char *url, const char *id,
                   xmlParserCtxtPtr ctxt)
{
    /* The handler can replace itself. */
    Py_INCREF(handler);
    PyObject *result = call(handler, Py_BuildValue("(ss)", url ? url : "", id ? id : ""));
    Py_DECREF(handler);
    if (result == NULL) return NULL;

    PyObject *bytes = content_bytes(result);
    Py_DECREF(result);
    if (bytes == NULL) return NULL;

    if (PyBytes_GET_SIZE(bytes) > INT_MAX) {
        Py_DECREF(bytes);
        PyErr_SetString(LibXML_Error, "the content of the entity is too big");
        return NULL;
    }

    xmlParserInputBufferPtr buffer = xmlParserInputBufferCreateMem(
        PyBytes_AS_STRING(bytes), (int)PyBytes_GET_SIZE(bytes), XML_CHAR_ENCODING_NONE);
    Py_DECREF(bytes);
    if (buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    xmlParserInputPtr input = xmlNewIOInputStream(ctxt, buffer, XML_CHAR_ENCODING_NONE);
    if (input == NULL) {
        xmlFreeParserInputBuffer(buffer);
        PyErr_NoMemory();
    }
    return input;
}

/* A loader given to externalEntityLoader decides for everything, network included.
 * Without one, the handler of the parser does. */
static xmlParserInputPtr
entity_loader(const char *url, const char *id, xmlParserCtxtPtr ctxt)
{
    if (PyErr_Occurred())
        return NULL;

    LibXML_ParseState *state = current;
    PyObject *handler = global_loader;
    if (handler == NULL && state != NULL)
        handler = state->handler;
    if (handler != NULL && ctxt != NULL)
        return input_from_handler(handler, url, id, ctxt);

    if (state != NULL && state->no_network)
        return xmlNoNetExternalEntityLoader(url, id, ctxt);
    return base_loader(url, id, ctxt);
}

/* ------------------------------------------------------------------ */
/* Input callbacks                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject *set;          /* the callbacks that opened it */
    PyObject *handle;       /* what open returned */
    PyObject *pending;      /* what read returned beyond the buffer of libxml2 */
    Py_ssize_t offset;
} InputContext;

static int
input_match(const char *filename)
{
    LibXML_ParseState *state = current;
    if (state == NULL || state->callbacks == NULL || PyErr_Occurred())
        return 0;

    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(state->callbacks); i++) {
        PyObject *set = PyList_GET_ITEM(state->callbacks, i);
        PyObject *result = call(PyTuple_GET_ITEM(set, 0),
                                Py_BuildValue("(N)", filename_object(filename)));
        if (result == NULL) return 0;

        int truth = PyObject_IsTrue(result);
        Py_DECREF(result);
        if (truth < 0) return 0;
        if (truth) {
            Py_INCREF(set);
            Py_XSETREF(state->matched, set);
            return 1;
        }
    }
    return 0;
}

static void *
input_open(const char *filename)
{
    LibXML_ParseState *state = current;
    if (state == NULL || state->matched == NULL)
        return NULL;

    PyObject *set = state->matched;
    state->matched = NULL;

    PyObject *handle = call(PyTuple_GET_ITEM(set, 1),
                            Py_BuildValue("(N)", filename_object(filename)));

    /* None or 0: not opened, so libxml2 tries what comes next. */
    if (handle == NULL || handle == Py_None
        || (PyLong_Check(handle) && PyLong_AsLong(handle) == 0)) {
        Py_XDECREF(handle);
        Py_DECREF(set);
        return NULL;
    }

    InputContext *context = PyMem_Calloc(1, sizeof(InputContext));
    if (context == NULL) {
        Py_DECREF(handle);
        Py_DECREF(set);
        PyErr_NoMemory();
        return NULL;
    }
    context->set = set;
    context->handle = handle;
    return context;
}

/* A callback may return more than asked for. */
static int
input_read(void *ctx, char *buffer, int len)
{
    InputContext *context = (InputContext *)ctx;

    if (PyErr_Occurred())
        return -1;

    if (context->pending == NULL) {
        PyObject *result = call(PyTuple_GET_ITEM(context->set, 2),
                                Py_BuildValue("(Oi)", context->handle, len));
        if (result == NULL) return -1;
        if (result == Py_None) {
            Py_DECREF(result);
            return 0;
        }
        context->pending = content_bytes(result);
        Py_DECREF(result);
        if (context->pending == NULL) return -1;
        context->offset = 0;
    }

    Py_ssize_t left = PyBytes_GET_SIZE(context->pending) - context->offset;
    Py_ssize_t count = left < len ? left : len;
    memcpy(buffer, PyBytes_AS_STRING(context->pending) + context->offset, count);
    context->offset += count;
    if (context->offset == PyBytes_GET_SIZE(context->pending))
        Py_CLEAR(context->pending);
    return (int)count;
}

/* This also runs after a callback raised. The first exception is the one to keep. */
static int
input_close(void *ctx)
{
    InputContext *context = (InputContext *)ctx;
    PyObject *type, *value, *tb;

    PyErr_Fetch(&type, &value, &tb);

    PyObject *close = PyTuple_GET_ITEM(context->set, 3);
    if (close != Py_None) {
        PyObject *result = call(close, Py_BuildValue("(O)", context->handle));
        Py_XDECREF(result);
    }

    if (type != NULL) {
        PyErr_Clear();
        PyErr_Restore(type, value, tb);
    }

    Py_XDECREF(context->pending);
    Py_DECREF(context->handle);
    Py_DECREF(context->set);
    PyMem_Free(context);
    return 0;
}

PyObject *
LibXML_collect_callbacks(PyObject *extra)
{
    PyObject *list = PyList_New(0);
    if (list == NULL) return NULL;

    /* The callbacks set on the parser itself */
    static const char *const keys[] = {
        "match_callback", "open_callback", "read_callback", "close_callback"
    };
    PyObject *own[4];
    int complete = 1;
    for (int i = 0; i < 4; i++) {
        own[i] = PyDict_GetItemString(extra, keys[i]);
        if (own[i] == NULL || own[i] == Py_None) complete = 0;
    }
    if (complete) {
        PyObject *set = PyTuple_Pack(4, own[0], own[1], own[2], own[3]);
        if (set == NULL || PyList_Append(list, set) < 0) {
            Py_XDECREF(set);
            Py_DECREF(list);
            return NULL;
        }
        Py_DECREF(set);
    }

    /* Then those of its InputCallback, latest first */
    PyObject *stack = PyDict_GetItemString(extra, "input_callbacks");
    if (stack != NULL && stack != Py_None) {
        PyObject *sets = PyObject_GetAttrString(stack, "_callbacks");
        Py_ssize_t count = sets ? PySequence_Size(sets) : -1;
        for (Py_ssize_t i = 0; i < count; i++) {
            PyObject *item = PySequence_GetItem(sets, i);
            PyObject *set = item ? PySequence_Tuple(item) : NULL;
            Py_XDECREF(item);
            if (set == NULL || PyTuple_GET_SIZE(set) != 4 || PyList_Append(list, set) < 0) {
                Py_XDECREF(set);
                Py_DECREF(sets);
                Py_DECREF(list);
                return NULL;
            }
            Py_DECREF(set);
        }
        Py_XDECREF(sets);
        if (count < 0) {
            Py_DECREF(list);
            return NULL;
        }
    }

    /* Then the global ones */
    if (global_callbacks != NULL && PyList_Append(list, global_callbacks) < 0) {
        Py_DECREF(list);
        return NULL;
    }

    if (PyList_GET_SIZE(list) == 0) {
        Py_DECREF(list);
        return NULL;
    }
    return list;
}

/* ------------------------------------------------------------------ */
/* Module                                                              */
/* ------------------------------------------------------------------ */

/* externalEntityLoader(loader): returns the one it replaces. None removes it. */
static PyObject *
module_externalEntityLoader(PyObject *module, PyObject *args)
{
    PyObject *loader = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &loader)) return NULL;
    if (loader != Py_None && !PyCallable_Check(loader)) {
        PyErr_SetString(PyExc_TypeError, "the loader must be callable");
        return NULL;
    }

    PyObject *previous = global_loader;
    if (loader == Py_None) {
        global_loader = NULL;
    } else {
        Py_INCREF(loader);
        global_loader = loader;
    }
    if (previous == NULL) Py_RETURN_NONE;
    return previous;
}

static PyObject *
module_set_global_callbacks(PyObject *module, PyObject *set)
{
    if (set != Py_None && !(PyTuple_Check(set) && PyTuple_GET_SIZE(set) == 4)) {
        PyErr_SetString(PyExc_TypeError, "expected four callbacks or None");
        return NULL;
    }
    PyObject *previous = global_callbacks;
    if (set == Py_None) {
        global_callbacks = NULL;
    } else {
        Py_INCREF(set);
        global_callbacks = set;
    }
    Py_XDECREF(previous);
    Py_RETURN_NONE;
}

static PyMethodDef input_functions[] = {
    {"externalEntityLoader", (PyCFunction)module_externalEntityLoader, METH_VARARGS,
     "Set the loader of all external entities, and return the previous one"},
    {"_set_global_callbacks", (PyCFunction)module_set_global_callbacks, METH_O,
     "Set the global input callbacks"},
    {NULL, NULL}
};

int
LibXML_input_init(PyObject *module)
{
    base_loader = xmlGetExternalEntityLoader();
    xmlSetExternalEntityLoader(entity_loader);

    xmlRegisterDefaultInputCallbacks();
    if (xmlRegisterInputCallbacks(input_match, input_open, input_read, input_close) < 0) {
        PyErr_SetString(PyExc_RuntimeError, "cannot register input callbacks");
        return -1;
    }
    return PyModule_AddFunctions(module, input_functions);
}

#pragma GCC diagnostic pop

#include <stdlib.h>
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_document.h"
#include "libxml_element.h"
#include "libxml_attr.h"
#include "libxml_text.h"
#include "libxml_dtd.h"

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

#define ERRORS_KEY "xml_libxml.errors"

static PyObject *error_module = NULL;

int
LibXML_errors_init(void)
{
    error_module = PyImport_ImportModule("xml_libxml._error");
    if (error_module == NULL) return -1;
    LibXML_Error = PyObject_GetAttrString(error_module, "Error");
    if (LibXML_Error == NULL) return -1;
    return 0;
}

/* The list of this thread. Returns a borrowed reference. */
static PyObject *
thread_errors(void)
{
    PyObject *dict = PyThreadState_GetDict();
    if (dict == NULL) return NULL;
    PyObject *list = PyDict_GetItemString(dict, ERRORS_KEY);
    if (list == NULL) {
        list = PyList_New(0);
        if (list == NULL || PyDict_SetItemString(dict, ERRORS_KEY, list) < 0) {
            Py_XDECREF(list);
            return NULL;
        }
        Py_DECREF(list);
    }
    return list;
}

void
LibXML_errors_begin(void)
{
    /* libxml2 keeps the handler per thread. */
    xmlSetStructuredErrorFunc(NULL, LibXML_error_handler);

    PyObject *list = thread_errors();
    if (list != NULL && PyList_GET_SIZE(list) > 0)
        PyList_SetSlice(list, 0, PyList_GET_SIZE(list), NULL);
}

PyObject *
LibXML_errors_hold(void)
{
    PyObject *list = thread_errors();
    if (list == NULL) return NULL;
    PyObject *held = PyList_GetSlice(list, 0, PyList_GET_SIZE(list));
    PyList_SetSlice(list, 0, PyList_GET_SIZE(list), NULL);
    return held;
}

void
LibXML_errors_resume(PyObject *held)
{
    PyObject *list = thread_errors();
    if (list != NULL && held != NULL)
        PyList_SetSlice(list, 0, PyList_GET_SIZE(list), held);
    Py_XDECREF(held);
}

static void
record_string(PyObject *record, const char *key, const char *value)
{
    if (value == NULL) return;
    PyObject *text = PyUnicode_DecodeUTF8(value, (Py_ssize_t)strlen(value), "replace");
    if (text != NULL) {
        PyDict_SetItemString(record, key, text);
        Py_DECREF(text);
    }
}

static void
record_int(PyObject *record, const char *key, long value)
{
    PyObject *number = PyLong_FromLong(value);
    if (number != NULL) {
        PyDict_SetItemString(record, key, number);
        Py_DECREF(number);
    }
}

/* The line of the input where a parser error is, and the column in it. */
static void
record_context(PyObject *record, LIBXML_CONST_ERROR xmlError *error)
{
    switch (error->domain) {
    case XML_FROM_PARSER:
    case XML_FROM_HTML:
    case XML_FROM_DTD:
    case XML_FROM_NAMESPACE:
    case XML_FROM_IO:
    case XML_FROM_VALID:
        break;
    default:
        return;
    }

    xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr)error->ctxt;
    if (ctxt == NULL) return;

    xmlParserInputPtr input = ctxt->input;
    if (input != NULL && input->filename == NULL && ctxt->inputNr > 1)
        input = ctxt->inputTab[ctxt->inputNr - 2];
    if (input == NULL || input->cur == NULL || input->base == NULL) return;

    const xmlChar *base = input->base, *cur = input->cur;
    char content[81];
    size_t n = 0;

    while (cur > base && (*cur == '\n' || *cur == '\r')) cur--;

    while (n++ < sizeof(content) - 1 && cur > base && *cur != '\n' && *cur != '\r')
        cur--;

    const xmlChar *line = cur;
    while (line > base && *line != '\n' && *line != '\r') line--;
    if (*cur == '\n' || *cur == '\r') cur++;
    if (*line == '\n' || *line == '\r') line++;

    n = 0;
    while (*cur != 0 && *cur != '\n' && *cur != '\r' && n < sizeof(content) - 1)
        content[n++] = (char)*cur++;
    content[n] = 0;

    record_string(record, "context", content);
    record_int(record, "column", (long)(input->cur - line));
}

void
LibXML_error_handler(void *ctx, LIBXML_CONST_ERROR xmlError *error)
{
    (void)ctx;

    /* A Python exception may be pending: a callback failed during the parse. */
    PyObject *type, *value, *tb;
    PyErr_Fetch(&type, &value, &tb);

    PyObject *list = thread_errors();
    PyObject *record = list ? PyDict_New() : NULL;
    if (record != NULL) {
        record_int(record, "domain", error->domain);
        record_int(record, "level", error->level);
        record_int(record, "code", error->code);
        record_int(record, "line", error->line);
        record_int(record, "num1", error->int1);
        record_int(record, "num2", error->int2);
        record_string(record, "message", error->message);
        record_string(record, "file", error->file);
        record_string(record, "str1", error->str1);
        record_string(record, "str2", error->str2);
        record_string(record, "str3", error->str3);
        record_context(record, error);
        PyList_Append(list, record);
        Py_DECREF(record);
    }

    PyErr_Clear();
    PyErr_Restore(type, value, tb);
}

/* Returns the chain of errors, Py_None, or NULL. Empties the list. */
static PyObject *
take_errors(void)
{
    PyObject *list = thread_errors();
    if (list == NULL || PyList_GET_SIZE(list) == 0)
        Py_RETURN_NONE;

    PyObject *chain = PyObject_CallMethod(error_module, "_chain", "O", list);
    PyList_SetSlice(list, 0, PyList_GET_SIZE(list), NULL);
    return chain;
}

int
LibXML_report_errors(int recover)
{
    PyObject *chain = take_errors();
    if (chain == NULL) return -1;
    if (chain == Py_None || recover == 2) {
        Py_DECREF(chain);
        return 0;
    }

    int rc = 0;
    if (recover == 0) {
        PyErr_SetObject(LibXML_Error, chain);
        rc = -1;
    } else {
        PyObject *text = PyObject_Str(chain);
        if (text != NULL) {
            rc = PyErr_WarnEx(PyExc_RuntimeWarning, PyUnicode_AsUTF8(text), 1);
            Py_DECREF(text);
        } else {
            rc = -1;
        }
    }
    Py_DECREF(chain);
    return rc;
}

void
LibXML_raise_last_error(void)
{
    if (LibXML_report_errors(0) == 0)
        PyErr_SetString(LibXML_Error, "libxml2 error");
}

/* ------------------------------------------------------------------ */
/* What is written in Python                                           */
/* ------------------------------------------------------------------ */

PyObject *
LibXML_dom_call(const char *name, PyObject *arg1, PyObject *arg2)
{
    static PyObject *module = NULL;

    if (module == NULL) {
        module = PyImport_ImportModule("xml_libxml._dom");
        if (module == NULL) return NULL;
    }
    PyObject *func = PyObject_GetAttrString(module, name);
    if (func == NULL) return NULL;
    PyObject *result = PyObject_CallFunctionObjArgs(func, arg1, arg2, NULL);
    Py_DECREF(func);
    return result;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

int
LibXML_check_name(const char *name)
{
    if (name == NULL || name[0] == '\0' || xmlValidateName(BAD_CAST name, 0) != 0) {
        PyErr_Format(LibXML_Error, "bad name: '%s'", name ? name : "");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Node wrapping factory                                               */
/* ------------------------------------------------------------------ */

static PyTypeObject *
wrapper_type(xmlNodePtr node)
{
    switch (node->type) {
    case XML_ELEMENT_NODE:        return &LibXML_ElementType;
    case XML_TEXT_NODE:           return &LibXML_TextType;
    case XML_COMMENT_NODE:        return &LibXML_CommentType;
    case XML_CDATA_SECTION_NODE:  return &LibXML_CDATASectionType;
    case XML_ATTRIBUTE_NODE:      return &LibXML_AttrType;
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:  return &LibXML_DocumentType;
    case XML_PI_NODE:             return &LibXML_PIType;
    case XML_DOCUMENT_FRAG_NODE:  return &LibXML_DocumentFragmentType;
    case XML_DTD_NODE:            return &LibXML_DtdType;
    default:                      return &LibXML_NodeType;
    }
}

PyObject *
LibXML_Node_Wrap(xmlNodePtr node)
{
    return LibXML_Node_Wrap_As(node, NULL);
}

PyObject *
LibXML_Node_Wrap_As(xmlNodePtr node, PyTypeObject *type)
{
    if (node == NULL) Py_RETURN_NONE;

    if (node->type == XML_NAMESPACE_DECL) {
        PyErr_SetString(LibXML_Error, "a namespace declaration is not a node");
        return NULL;
    }

    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (priv && priv->wrapper != NULL) {
        Py_INCREF(priv->wrapper);
        return priv->wrapper;
    }

    PyObject *doc_obj = NULL, *owner = NULL;

    if (node->type != XML_DOCUMENT_NODE && node->type != XML_HTML_DOCUMENT_NODE) {
        if (node->doc == NULL) {
            PyErr_SetString(LibXML_Error, "node has no document");
            return NULL;
        }
        doc_obj = LibXML_Node_Wrap((xmlNodePtr)node->doc);
        if (doc_obj == NULL) return NULL;

        xmlNodePtr top = LibXML_tree_top(node);
        if (top != node) {
            owner = LibXML_Node_Wrap(top);
            if (owner == NULL) { Py_DECREF(doc_obj); return NULL; }
        }
    }

    if (type == NULL)
        type = wrapper_type(node);
    LibXML_NodeObject *self = (LibXML_NodeObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        Py_XDECREF(doc_obj);
        Py_XDECREF(owner);
        return NULL;
    }

    if (!priv) {
        priv = (LibXML_NodePrivate *)calloc(1, sizeof(LibXML_NodePrivate));
        if (!priv) {
            Py_XDECREF(doc_obj);
            Py_XDECREF(owner);
            Py_DECREF(self);
            return PyErr_NoMemory();
        }
        node->_private = priv;
    }
    priv->wrapper = (PyObject *)self;  /* borrowed ref */

    self->node    = node;
    self->doc_obj = doc_obj;
    self->owner   = owner;

    return (PyObject *)self;
}

PyObject *
LibXML_new_hidden_doc(void)
{
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (doc == NULL) return PyErr_NoMemory();

    PyObject *wrapper = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (wrapper == NULL) {
        xmlFreeDoc(doc);
        return NULL;
    }
    ((LibXML_NodeObject *)wrapper)->hidden = 1;
    return wrapper;
}

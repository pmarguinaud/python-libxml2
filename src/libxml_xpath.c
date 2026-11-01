#include "libxml_xpath.h"
#include "libxml_utils.h"
#include "libxml_node.h"
#include "libxml_nodelist.h"

/* ------------------------------------------------------------------ */
/* Helpers (also used by libxml_node.c)                               */
/* ------------------------------------------------------------------ */

PyObject *
LibXML_xpath_to_python(xmlXPathObjectPtr xobj)
{
    switch (xobj->type) {
        case XPATH_NODESET:
            return LibXML_NodeList_FromNodeSet(xobj->nodesetval);
        case XPATH_BOOLEAN:
            return PyBool_FromLong(xobj->boolval);
        case XPATH_NUMBER:
            return PyFloat_FromDouble(xobj->floatval);
        case XPATH_STRING:
            return PyUnicode_FromString(
                xobj->stringval ? (const char *)xobj->stringval : "");
        default:
            Py_RETURN_NONE;
    }
}

/* ------------------------------------------------------------------ */
/* tp_dealloc                                                          */
/* ------------------------------------------------------------------ */

/* Concatenate text content of all nodes in a nodeset (Perl to_literal). */
static xmlChar *
nodeset_to_literal(xmlNodeSetPtr ns)
{
    if (!ns || ns->nodeNr == 0) return xmlStrdup(BAD_CAST "");
    xmlChar *result = xmlStrdup(BAD_CAST "");
    for (int i = 0; i < ns->nodeNr; i++) {
        xmlChar *content = xmlNodeGetContent(ns->nodeTab[i]);
        if (content) {
            xmlChar *tmp = xmlStrcat(result, content);
            xmlFree(content);
            result = tmp;
        }
    }
    return result;
}

static void
LibXML_XPathContext_dealloc(LibXML_XPathContextObject *self)
{
    if (self->ctx) {
        xmlXPathFreeContext(self->ctx);
        self->ctx = NULL;
    }
    Py_XDECREF(self->doc_obj);
    Py_XDECREF(self->ns_dict);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* GC                                                                  */
/* ------------------------------------------------------------------ */

static int
xpc_traverse(LibXML_XPathContextObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->doc_obj);
    Py_VISIT(self->ns_dict);
    return 0;
}

static int
xpc_clear(LibXML_XPathContextObject *self)
{
    Py_CLEAR(self->doc_obj);
    Py_CLEAR(self->ns_dict);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Construction: XPathContext(node_or_doc)                            */
/* ------------------------------------------------------------------ */

/* Helper: replay ns_dict registrations onto a fresh context. */
static void
xpc_replay_ns(xmlXPathContextPtr ctx, PyObject *ns_dict)
{
    if (!ns_dict) return;
    PyObject *key, *val;
    Py_ssize_t pos = 0;
    while (PyDict_Next(ns_dict, &pos, &key, &val)) {
        const char *prefix = PyUnicode_AsUTF8(key);
        const char *uri    = PyUnicode_AsUTF8(val);
        if (prefix && uri)
            xmlXPathRegisterNs(ctx, BAD_CAST prefix, BAD_CAST uri);
    }
}

/* Apply a temporary context node for a single eval.
 * Returns the previously saved xmlNodePtr (to restore after eval),
 * or (xmlNodePtr)-1 on error (Python exception already set).
 * Also updates self->doc_obj if the document changes. */
static xmlNodePtr
xpc_apply_node(LibXML_XPathContextObject *self, LibXML_NodeObject *ctx_node)
{
    xmlDocPtr doc;
    PyObject *new_doc_obj;
    if (ctx_node->doc_obj == NULL) {
        doc = (xmlDocPtr)ctx_node->node;
        new_doc_obj = (PyObject *)ctx_node;
    } else {
        doc = (xmlDocPtr)LibXML_NODE(ctx_node->doc_obj);
        new_doc_obj = ctx_node->doc_obj;
    }

    if (self->ctx == NULL) {
        self->ctx = xmlXPathNewContext(doc);
        if (!self->ctx) { PyErr_NoMemory(); return (xmlNodePtr)-1; }
        xpc_replay_ns(self->ctx, self->ns_dict);
        Py_XINCREF(new_doc_obj);
        Py_XDECREF(self->doc_obj);
        self->doc_obj = new_doc_obj;
    } else if (self->ctx->doc != doc) {
        void *userData = self->ctx->userData;
        self->ctx->userData = NULL;
        xmlXPathFreeContext(self->ctx);
        self->ctx = xmlXPathNewContext(doc);
        if (!self->ctx) { PyErr_NoMemory(); return (xmlNodePtr)-1; }
        self->ctx->userData = userData;
        xpc_replay_ns(self->ctx, self->ns_dict);
        Py_XINCREF(new_doc_obj);
        Py_XDECREF(self->doc_obj);
        self->doc_obj = new_doc_obj;
    }

    xmlNodePtr saved = self->ctx->node;
    self->ctx->node = ctx_node->node;
    return saved;
}

static PyObject *
LibXML_XPathContext_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *node_arg_raw = Py_None;
    static char *kwlist[] = {"node", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &node_arg_raw))
        return NULL;

    LibXML_NodeObject *node_arg = NULL;
    if (node_arg_raw != Py_None) {
        if (!PyObject_IsInstance(node_arg_raw, (PyObject *)&LibXML_NodeType)) {
            PyErr_SetString(PyExc_TypeError, "XPathContext argument must be a Node or None");
            return NULL;
        }
        node_arg = (LibXML_NodeObject *)node_arg_raw;
    }

    xmlDocPtr doc = NULL;
    xmlNodePtr ctx_node = NULL;

    if (node_arg) {
        if (node_arg->doc_obj == NULL) {
            doc = (xmlDocPtr)node_arg->node;
            ctx_node = (xmlNodePtr)doc;
        } else {
            doc = (xmlDocPtr)LibXML_NODE(node_arg->doc_obj);
            ctx_node = node_arg->node;
        }
    }

    /* doc may be NULL for empty XPathContext — ctx will be created on setContextNode */
    xmlXPathContextPtr ctx = NULL;
    if (doc) {
        ctx = xmlXPathNewContext(doc);
        if (!ctx) return PyErr_NoMemory();
        if (ctx_node) ctx->node = ctx_node;
    }

    LibXML_XPathContextObject *self =
        (LibXML_XPathContextObject *)type->tp_alloc(type, 0);
    if (!self) { if (ctx) xmlXPathFreeContext(ctx); return NULL; }

    self->ctx = ctx;

    /* doc_obj: the Document Python wrapper */
    PyObject *doc_obj;
    if (node_arg && node_arg->doc_obj == NULL) {
        doc_obj = (PyObject *)node_arg;
    } else if (node_arg) {
        doc_obj = node_arg->doc_obj;
    } else {
        doc_obj = NULL;
    }
    Py_XINCREF(doc_obj);
    self->doc_obj = doc_obj;

    self->ns_dict = PyDict_New();
    if (!self->ns_dict) {
        Py_DECREF(self);
        return NULL;
    }

    return (PyObject *)self;
}

/* ------------------------------------------------------------------ */
/* Methods                                                             */
/* ------------------------------------------------------------------ */

static PyObject *
xpc_registerNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix, *uri;
    if (!PyArg_ParseTuple(args, "ss", &prefix, &uri)) return NULL;
    /* Always store in dict so we can replay on context recreation */
    PyObject *k = PyUnicode_FromString(prefix);
    PyObject *v = PyUnicode_FromString(uri);
    if (!k || !v) { Py_XDECREF(k); Py_XDECREF(v); return NULL; }
    int r = PyDict_SetItem(self->ns_dict, k, v);
    Py_DECREF(k); Py_DECREF(v);
    if (r != 0) return NULL;
    if (self->ctx) {
        if (xmlXPathRegisterNs(self->ctx, BAD_CAST prefix, BAD_CAST uri) != 0) {
            PyErr_SetString(LibXML_Error, "failed to register namespace");
            return NULL;
        }
    }
    Py_RETURN_NONE;
}

static PyObject *
xpc_unregisterNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "s", &prefix)) return NULL;
    PyObject *k = PyUnicode_FromString(prefix);
    if (!k) return NULL;
    PyDict_DelItem(self->ns_dict, k);  /* ignore if not found */
    Py_DECREF(k);
    if (self->ctx)
        xmlXPathRegisterNs(self->ctx, BAD_CAST prefix, NULL);
    Py_RETURN_NONE;
}

static PyObject *
xpc_lookupNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "s", &prefix)) return NULL;
    PyObject *k = PyUnicode_FromString(prefix);
    if (!k) return NULL;
    PyObject *v = PyDict_GetItem(self->ns_dict, k);
    Py_DECREF(k);
    if (!v) Py_RETURN_NONE;
    Py_INCREF(v);
    return v;
}

/* Core eval helper */
static xmlXPathObjectPtr
xpc_eval(LibXML_XPathContextObject *self, const char *expr)
{
    if (!self->ctx || !self->ctx->node) {
        PyErr_SetString(LibXML_Error, "XPathContext has no context node");
        return NULL;
    }
    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, self->ctx);
    if (!xobj) {
        LibXML_raise_last_error();
        return NULL;
    }
    return xobj;
}

static PyObject *
xpc_findnodes(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *expr;
    LibXML_NodeObject *ctx_node = NULL;
    if (!PyArg_ParseTuple(args, "s|O!", &expr, &LibXML_NodeType, &ctx_node))
        return NULL;

    xmlNodePtr saved = self->ctx ? self->ctx->node : NULL;
    if (ctx_node) {
        saved = xpc_apply_node(self, ctx_node);
        if (saved == (xmlNodePtr)-1) return NULL;
    }

    xmlXPathObjectPtr xobj = xpc_eval(self, expr);
    if (self->ctx) self->ctx->node = saved;
    if (!xobj) return NULL;

    PyObject *result;
    if (xobj->type == XPATH_NODESET)
        result = LibXML_NodeList_FromNodeSet(xobj->nodesetval);
    else
        result = LibXML_NodeList_FromNodeSet(NULL);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
xpc_find(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *expr;
    LibXML_NodeObject *ctx_node = NULL;
    if (!PyArg_ParseTuple(args, "s|O!", &expr, &LibXML_NodeType, &ctx_node))
        return NULL;

    xmlNodePtr saved = self->ctx ? self->ctx->node : NULL;
    if (ctx_node) {
        saved = xpc_apply_node(self, ctx_node);
        if (saved == (xmlNodePtr)-1) return NULL;
    }

    xmlXPathObjectPtr xobj = xpc_eval(self, expr);
    if (self->ctx) self->ctx->node = saved;
    if (!xobj) return NULL;

    PyObject *result = LibXML_xpath_to_python(xobj);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
xpc_findvalue(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *expr;
    LibXML_NodeObject *ctx_node = NULL;
    if (!PyArg_ParseTuple(args, "s|O!", &expr, &LibXML_NodeType, &ctx_node))
        return NULL;

    xmlNodePtr saved = self->ctx ? self->ctx->node : NULL;
    if (ctx_node) {
        saved = xpc_apply_node(self, ctx_node);
        if (saved == (xmlNodePtr)-1) return NULL;
    }

    xmlXPathObjectPtr xobj = xpc_eval(self, expr);
    if (self->ctx) self->ctx->node = saved;
    if (!xobj) return NULL;

    xmlChar *str;
    if (xobj->type == XPATH_NODESET) {
        str = nodeset_to_literal(xobj->nodesetval);
    } else {
        str = xmlXPathCastToString(xobj);
    }
    xmlXPathFreeObject(xobj);
    if (!str) return PyUnicode_FromString("");
    PyObject *result = PyUnicode_FromString((const char *)str);
    xmlFree(str);
    return result;
}

static PyObject *
xpc_exists(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *expr;
    LibXML_NodeObject *ctx_node = NULL;
    if (!PyArg_ParseTuple(args, "s|O!", &expr, &LibXML_NodeType, &ctx_node))
        return NULL;

    xmlNodePtr saved = self->ctx ? self->ctx->node : NULL;
    if (ctx_node && self->ctx) self->ctx->node = ctx_node->node;

    xmlXPathObjectPtr xobj = xpc_eval(self, expr);
    if (self->ctx) self->ctx->node = saved;
    if (!xobj) return NULL;

    int val = xmlXPathCastToBoolean(xobj);
    xmlXPathFreeObject(xobj);
    return PyBool_FromLong(val);
}

static PyObject *
xpc_setContextNode(LibXML_XPathContextObject *self, PyObject *args)
{
    PyObject *arg;
    if (!PyArg_ParseTuple(args, "O", &arg)) return NULL;

    if (arg == Py_None) {
        if (self->ctx) self->ctx->node = NULL;
        Py_RETURN_NONE;
    }

    if (!PyObject_IsInstance(arg, (PyObject *)&LibXML_NodeType)) {
        PyErr_SetString(PyExc_TypeError, "argument must be a Node or None");
        return NULL;
    }
    LibXML_NodeObject *node = (LibXML_NodeObject *)arg;

    xmlDocPtr doc;
    PyObject *new_doc_obj;
    if (node->doc_obj == NULL) {
        doc = (xmlDocPtr)node->node;
        new_doc_obj = (PyObject *)node;
    } else {
        doc = (xmlDocPtr)LibXML_NODE(node->doc_obj);
        new_doc_obj = node->doc_obj;
    }

    if (self->ctx == NULL) {
        /* Lazy creation */
        self->ctx = xmlXPathNewContext(doc);
        if (!self->ctx) return PyErr_NoMemory();
        xpc_replay_ns(self->ctx, self->ns_dict);
    } else if (self->ctx->doc != doc) {
        /* Doc changed: recreate context preserving namespace dict */
        void *userData = self->ctx->userData;
        self->ctx->userData = NULL;
        xmlXPathFreeContext(self->ctx);
        self->ctx = xmlXPathNewContext(doc);
        if (!self->ctx) return PyErr_NoMemory();
        self->ctx->userData = userData;
        xpc_replay_ns(self->ctx, self->ns_dict);
    }
    self->ctx->node = node->node;

    Py_XINCREF(new_doc_obj);
    Py_XDECREF(self->doc_obj);
    self->doc_obj = new_doc_obj;

    Py_RETURN_NONE;
}

static PyObject *
xpc_getContextNode(LibXML_XPathContextObject *self, PyObject *args)
{
    if (!self->ctx || !self->ctx->node) Py_RETURN_NONE;
    return LibXML_Node_Wrap(self->ctx->node);
}

/* ------------------------------------------------------------------ */
/* Custom function registration                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject *callback;
} XPathFuncData;

static void
xpc_python_function(xmlXPathParserContextPtr ctxt, int nargs)
{
    /* We store the Python callback via the context's user data */
    /* This is a simplified implementation: pop string args, call Python */
    XPathFuncData *data = (XPathFuncData *)ctxt->context->userData;
    if (!data || !data->callback) {
        xmlXPathReturnString(ctxt, xmlStrdup(BAD_CAST ""));
        return;
    }

    PyObject *py_args = PyTuple_New(nargs);
    for (int i = nargs - 1; i >= 0; i--) {
        xmlXPathObjectPtr xobj = valuePop(ctxt);
        PyObject *val = LibXML_xpath_to_python(xobj);
        xmlXPathFreeObject(xobj);
        PyTuple_SET_ITEM(py_args, i, val);
    }

    PyObject *result = PyObject_CallObject(data->callback, py_args);
    Py_DECREF(py_args);

    if (!result) {
        PyErr_Clear();
        xmlXPathReturnString(ctxt, xmlStrdup(BAD_CAST ""));
        return;
    }

    if (PyBool_Check(result)) {
        xmlXPathReturnBoolean(ctxt, PyObject_IsTrue(result));
    } else if (PyLong_Check(result) || PyFloat_Check(result)) {
        xmlXPathReturnNumber(ctxt, PyFloat_AsDouble(result));
    } else if (PyUnicode_Check(result)) {
        const char *s = PyUnicode_AsUTF8(result);
        xmlXPathReturnString(ctxt, s ? xmlStrdup(BAD_CAST s) : xmlStrdup(BAD_CAST ""));
    } else {
        xmlXPathReturnString(ctxt, xmlStrdup(BAD_CAST ""));
    }
    Py_DECREF(result);
}

static PyObject *
xpc_registerFunction(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name;
    PyObject *callback;
    if (!PyArg_ParseTuple(args, "sO", &name, &callback)) return NULL;
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }

    if (self->ctx == NULL) {
        PyErr_SetString(LibXML_Error, "XPathContext has no context node");
        return NULL;
    }

    /* Store the callback; we use a simple approach: store in userData */
    /* For proper multi-function support a hash would be needed;
     * for now we support one function at a time */
    XPathFuncData *data = self->ctx->userData;
    if (!data) {
        data = xmlMalloc(sizeof(XPathFuncData));
        if (!data) return PyErr_NoMemory();
        data->callback = NULL;
        self->ctx->userData = data;
    }
    Py_XDECREF(data->callback);
    Py_INCREF(callback);
    data->callback = callback;

    if (xmlXPathRegisterFunc(self->ctx, BAD_CAST name, xpc_python_function) != 0) {
        PyErr_SetString(LibXML_Error, "failed to register function");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
xpc_registerFunctionNS(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name, *ns_uri;
    PyObject *callback;
    if (!PyArg_ParseTuple(args, "ssO", &name, &ns_uri, &callback)) return NULL;
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }

    if (self->ctx == NULL) {
        PyErr_SetString(LibXML_Error, "XPathContext has no context node");
        return NULL;
    }

    XPathFuncData *data = self->ctx->userData;
    if (!data) {
        data = xmlMalloc(sizeof(XPathFuncData));
        if (!data) return PyErr_NoMemory();
        data->callback = NULL;
        self->ctx->userData = data;
    }
    Py_XDECREF(data->callback);
    Py_INCREF(callback);
    data->callback = callback;

    if (xmlXPathRegisterFuncNS(self->ctx, BAD_CAST name, BAD_CAST ns_uri,
                                xpc_python_function) != 0) {
        PyErr_SetString(LibXML_Error, "failed to register NS function");
        return NULL;
    }
    Py_RETURN_NONE;
}

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_XPathContext_methods[] = {
    {"registerNs",         (PyCFunction)xpc_registerNs,         METH_VARARGS, "Register namespace prefix"},
    {"unregisterNs",       (PyCFunction)xpc_unregisterNs,       METH_VARARGS, "Unregister namespace prefix"},
    {"lookupNs",           (PyCFunction)xpc_lookupNs,           METH_VARARGS, "Look up namespace URI"},
    {"findnodes",          (PyCFunction)xpc_findnodes,           METH_VARARGS, "Evaluate XPath, return NodeList"},
    {"find",               (PyCFunction)xpc_find,               METH_VARARGS, "Evaluate XPath, return typed result"},
    {"findvalue",          (PyCFunction)xpc_findvalue,           METH_VARARGS, "Evaluate XPath, return string value"},
    {"exists",             (PyCFunction)xpc_exists,             METH_VARARGS, "Return True if XPath matches"},
    {"setContextNode",     (PyCFunction)xpc_setContextNode,     METH_VARARGS, "Set context node"},
    {"getContextNode",     (PyCFunction)xpc_getContextNode,     METH_NOARGS,  "Get context node"},
    {"registerFunction",   (PyCFunction)xpc_registerFunction,   METH_VARARGS, "Register custom XPath function"},
    {"registerFunctionNS", (PyCFunction)xpc_registerFunctionNS, METH_VARARGS, "Register namespaced custom function"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_XPathContextType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.XPathContext",
    .tp_basicsize = sizeof(LibXML_XPathContextObject),
    .tp_dealloc   = (destructor)LibXML_XPathContext_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse  = (traverseproc)xpc_traverse,
    .tp_clear     = (inquiry)xpc_clear,
    .tp_new       = LibXML_XPathContext_new,
    .tp_methods   = LibXML_XPathContext_methods,
    .tp_doc       = "XPath evaluation context",
};

#include "libxml_xpath.h"
#include "libxml_utils.h"
#include "libxml_node.h"
#include "libxml_nodelist.h"
#include "libxml_tree.h"
#include <libxml/hash.h>

/* The classes of _xpath.py */
static PyObject *literal_class = NULL;
static PyObject *number_class  = NULL;
static PyObject *boolean_class = NULL;

PyObject *
LibXML_new_literal(PyObject *text)
{
    return PyObject_CallFunctionObjArgs(literal_class, text, NULL);
}

PyObject *
LibXML_new_number(PyObject *text)
{
    return PyObject_CallFunctionObjArgs(number_class, text, NULL);
}

/* ------------------------------------------------------------------ */
/* Compiled expressions                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    xmlXPathCompExprPtr comp;
} LibXML_XPathExpressionObject;

static PyObject *
expression_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *expr;
    static char *kwlist[] = {"expression", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &expr)) return NULL;

    LibXML_errors_begin();
    xmlXPathCompExprPtr comp = xmlXPathCompile(BAD_CAST expr);
    if (comp == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }

    LibXML_XPathExpressionObject *self =
        (LibXML_XPathExpressionObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        xmlXPathFreeCompExpr(comp);
        return NULL;
    }
    self->comp = comp;
    return (PyObject *)self;
}

static void
expression_dealloc(LibXML_XPathExpressionObject *self)
{
    if (self->comp) xmlXPathFreeCompExpr(self->comp);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyTypeObject LibXML_XPathExpressionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.XPathExpression",
    .tp_basicsize = sizeof(LibXML_XPathExpressionObject),
    .tp_dealloc   = (destructor)expression_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_new       = expression_new,
    .tp_doc       = "Compiled XPath expression",
};

/* ------------------------------------------------------------------ */
/* Between libxml2 and Python                                          */
/* ------------------------------------------------------------------ */

static PyObject *
xpath_string(const xmlChar *value)
{
    return PyUnicode_FromString(value ? (const char *)value : "");
}

/* The value as a callback gets it: plain Python types. */
static PyObject *
xpath_to_plain(xmlXPathObjectPtr xobj)
{
    switch (xobj->type) {
    case XPATH_NODESET:
        return LibXML_NodeList_FromNodeSet(xobj->nodesetval);
    case XPATH_BOOLEAN:
        return PyBool_FromLong(xobj->boolval);
    case XPATH_NUMBER:
        return PyFloat_FromDouble(xobj->floatval);
    case XPATH_STRING:
        return xpath_string(xobj->stringval);
    default:
        Py_RETURN_NONE;
    }
}

/* The value as find() gives it. */
static PyObject *
xpath_to_typed(xmlXPathObjectPtr xobj)
{
    PyObject *plain = xpath_to_plain(xobj);
    PyObject *cls = NULL;

    if (plain == NULL) return NULL;
    switch (xobj->type) {
    case XPATH_BOOLEAN: cls = boolean_class; break;
    case XPATH_NUMBER:  cls = number_class;  break;
    case XPATH_STRING:  cls = literal_class; break;
    default:            return plain;
    }

    PyObject *typed = PyObject_CallFunctionObjArgs(cls, plain, NULL);
    Py_DECREF(plain);
    return typed;
}

static PyObject *
xpath_value(xmlXPathObjectPtr xobj)
{
    if (xobj->type == XPATH_NODESET) {
        xmlNodeSetPtr set = xobj->nodesetval;
        xmlChar *text = xmlStrdup(BAD_CAST "");
        for (int i = 0; set != NULL && i < set->nodeNr; i++) {
            xmlNodePtr node = set->nodeTab[i];
            /* xmlNodeGetContent reads a namespace node as well. */
            xmlChar *content = xmlNodeGetContent(node);
            if (content) {
                text = xmlStrcat(text, content);
                xmlFree(content);
            }
        }
        PyObject *result = xpath_string(text);
        xmlFree(text);
        return result;
    }

    xmlChar *text = xmlXPathCastToString(xobj);
    PyObject *result = xpath_string(text);
    xmlFree(text);
    return result;
}

static PyObject *
xpath_result(xmlXPathObjectPtr xobj, LibXML_XPathMode mode)
{
    switch (mode) {
    case LIBXML_XP_NODES:
        return LibXML_NodeList_FromNodeSet(
            xobj->type == XPATH_NODESET ? xobj->nodesetval : NULL);
    case LIBXML_XP_FIND:
        return xpath_to_typed(xobj);
    case LIBXML_XP_VALUE:
        return xpath_value(xobj);
    case LIBXML_XP_EXISTS:
        return PyBool_FromLong(xmlXPathCastToBoolean(xobj));
    default: {
        PyObject *truth = PyBool_FromLong(xmlXPathCastToBoolean(xobj));
        PyObject *typed = PyObject_CallFunctionObjArgs(boolean_class, truth, NULL);
        Py_DECREF(truth);
        return typed;
    }
    }
}

/* libxml2 crashes when it sorts a node set holding one of these. */
static int
is_detached_attr(xmlNodePtr node)
{
    return node->type == XML_ATTRIBUTE_NODE && node->parent == NULL;
}

static int
check_context_node(xmlNodePtr node)
{
    if (is_detached_attr(node)) {
        PyErr_SetString(LibXML_Error, "an attribute without element cannot be a context node");
        return -1;
    }
    return 0;
}

/* Add the nodes of a sequence to a node set. Each is kept once, as a set needs
 * it, and alive in pool until the evaluation ends. */
static int
add_nodes(xmlNodeSetPtr set, PyObject *items, PyObject *pool)
{
    PyObject *seen = PySet_New(NULL);
    if (seen == NULL) return -1;

    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(items); i++) {
        PyObject *item = PySequence_Fast_GET_ITEM(items, i);

        if (!LibXML_Node_Check(item) || is_detached_attr(LibXML_NODE(item))) {
            if (PyErr_WarnEx(PyExc_RuntimeWarning,
                             "XPathContext: ignoring non-node member of a nodelist", 1) < 0) {
                Py_DECREF(seen);
                return -1;
            }
            continue;
        }

        PyObject *key = PyLong_FromVoidPtr(LibXML_NODE(item));
        int known = key ? PySet_Contains(seen, key) : -1;
        if (known == 0) {
            xmlXPathNodeSetAddUnique(set, LibXML_NODE(item));
            if (PySet_Add(seen, key) < 0) known = -1;
        }
        Py_XDECREF(key);
        if (known < 0 || (pool && PyList_Append(pool, item) < 0)) {
            Py_DECREF(seen);
            return -1;
        }
    }
    Py_DECREF(seen);
    return 0;
}

/* A value from Python as an XPath value. pool keeps nodes alive. */
static xmlXPathObjectPtr
python_to_xpath(PyObject *value, PyObject *pool)
{
    if (value == Py_None)
        return xmlXPathNewCString("");

    if (LibXML_Node_Check(value) || PyList_Check(value) || PyTuple_Check(value)) {
        PyObject *items = LibXML_Node_Check(value)
            ? PyTuple_Pack(1, value) : PySequence_Fast(value, "not a sequence");
        if (items == NULL) return NULL;

        xmlXPathObjectPtr result = xmlXPathNewNodeSet(NULL);
        if (result == NULL || add_nodes(result->nodesetval, items, pool) < 0) {
            if (result) xmlXPathFreeObject(result);
            Py_DECREF(items);
            return NULL;
        }
        Py_DECREF(items);
        return result;
    }

    if (PyBool_Check(value) || PyObject_IsInstance(value, boolean_class) > 0)
        return xmlXPathNewBoolean(PyObject_IsTrue(value));

    if (PyFloat_Check(value) || PyLong_Check(value)) {
        double number = PyFloat_AsDouble(value);
        if (number == -1.0 && PyErr_Occurred()) return NULL;
        return xmlXPathNewFloat(number);
    }

    PyObject *text = PyObject_Str(value);
    if (text == NULL) return NULL;
    const char *utf8 = PyUnicode_AsUTF8(text);
    xmlXPathObjectPtr result = utf8 ? xmlXPathNewCString(utf8) : NULL;
    Py_DECREF(text);
    return result;
}

/* ------------------------------------------------------------------ */
/* Functions available in every evaluation                             */
/* ------------------------------------------------------------------ */

/* document(uri). The parsed document stays in the pool of the evaluation. */
static void
xpath_document(xmlXPathParserContextPtr pctxt, int nargs)
{
    if (nargs < 1 || nargs > 2) {
        xmlXPathSetArityError(pctxt);
        return;
    }
    if (nargs == 2)
        xmlXPathFreeObject(valuePop(pctxt));

    xmlXPathObjectPtr arg = valuePop(pctxt);
    xmlChar *uri = xmlXPathCastToString(arg);
    xmlXPathFreeObject(arg);

    /* Options of a default parser: no entities, no DTD, no network. */
    xmlDocPtr doc = uri ? xmlReadFile((const char *)uri, NULL, XML_PARSE_NONET) : NULL;
    xmlFree(uri);

    if (doc == NULL) {
        valuePush(pctxt, xmlXPathNewNodeSet(NULL));
        return;
    }

    PyObject *wrapper = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (wrapper == NULL) {
        xmlFreeDoc(doc);
        pctxt->error = XPATH_MEMORY_ERROR;
        return;
    }
    PyObject *pool = (PyObject *)pctxt->context->extra;
    if (PyList_Append(pool, wrapper) < 0)
        pctxt->error = XPATH_MEMORY_ERROR;
    Py_DECREF(wrapper);

    valuePush(pctxt, xmlXPathNewNodeSet((xmlNodePtr)doc));
}

/* Evaluate with the context prepared. On failure, the exception is set. */
static xmlXPathObjectPtr
run_expression(xmlXPathContextPtr ctx, PyObject *expr)
{
    xmlXPathObjectPtr xobj;

    LibXML_errors_begin();

    if (PyObject_TypeCheck(expr, &LibXML_XPathExpressionType)) {
        xobj = xmlXPathCompiledEval(((LibXML_XPathExpressionObject *)expr)->comp, ctx);
    } else {
        const char *text = PyUnicode_AsUTF8(expr);
        if (text == NULL) return NULL;
        xobj = xmlXPathEvalExpression(BAD_CAST text, ctx);
    }

    /* An exception from a callback wins over what libxml2 says about it. */
    if (PyErr_Occurred()) {
        if (xobj) xmlXPathFreeObject(xobj);
        return NULL;
    }
    if (xobj == NULL)
        LibXML_raise_last_error();
    return xobj;
}

static int
check_expression(PyObject *expr)
{
    if (PyUnicode_Check(expr) || PyObject_TypeCheck(expr, &LibXML_XPathExpressionType))
        return 0;
    PyErr_SetString(PyExc_TypeError, "the expression must be a str or XPathExpression");
    return -1;
}

/* The in-scope declarations at node, for a context. For a document, those of the
 * root element. What is registered on the context goes first in Perl, so with
 * nsHash set, the prefixes in it are left out. Sets ctx->namespaces. */
static void
set_scope(xmlXPathContextPtr ctx, xmlNodePtr node)
{
    ctx->namespaces = NULL;
    ctx->nsNr = 0;
    if (node == NULL) return;

    if (node->type == XML_DOCUMENT_NODE || node->type == XML_HTML_DOCUMENT_NODE)
        node = xmlDocGetRootElement((xmlDocPtr)node);
    if (node == NULL) return;

    xmlNsPtr *list = xmlGetNsList(node->doc, node);
    if (list == NULL) return;

    int kept = 0;
    for (int i = 0; list[i] != NULL; i++) {
        xmlNsPtr ns = list[i];
        list[i] = NULL;
        if (ns->prefix != NULL
            && (ctx->nsHash == NULL || xmlHashLookup(ctx->nsHash, ns->prefix) == NULL))
            list[kept++] = ns;
    }
    ctx->namespaces = list;
    ctx->nsNr = kept;
}

PyObject *
LibXML_node_xpath(LibXML_NodeObject *self, PyObject *expr, LibXML_XPathMode mode)
{
    if (check_expression(expr) < 0 || check_context_node(self->node) < 0) return NULL;

    xmlXPathContextPtr ctx = xmlXPathNewContext(self->node->doc);
    if (ctx == NULL) return PyErr_NoMemory();

    PyObject *pool = PyList_New(0);
    if (pool == NULL) {
        xmlXPathFreeContext(ctx);
        return NULL;
    }

    ctx->node = self->node;
    ctx->extra = pool;
    xmlXPathRegisterFunc(ctx, BAD_CAST "document", xpath_document);
    set_scope(ctx, self->node);

    LibXML_eval_begin();
    xmlXPathObjectPtr xobj = run_expression(ctx, expr);

    xmlFree(ctx->namespaces);
    ctx->namespaces = NULL;
    ctx->nsNr = 0;
    xmlXPathFreeContext(ctx);

    PyObject *result = NULL;
    if (xobj != NULL) {
        result = xpath_result(xobj, mode);
        xmlXPathFreeObject(xobj);
    }
    Py_DECREF(pool);
    LibXML_eval_end();
    return result;
}

/* ------------------------------------------------------------------ */
/* XPathContext: callbacks                                             */
/* ------------------------------------------------------------------ */

/* Calls a Python function. On failure the exception stays set, and the
 * evaluation stops with an XPath error. */
static void
call_function(xmlXPathParserContextPtr pctxt, int nargs)
{
    xmlXPathContextPtr ctx = pctxt->context;
    LibXML_XPathContextObject *self = (LibXML_XPathContextObject *)ctx->userData;

    PyObject *key = Py_BuildValue("(ss)", ctx->function, ctx->functionURI);
    PyObject *func = key ? PyDict_GetItem(self->functions, key) : NULL;
    Py_XDECREF(key);

    PyObject *args = func ? PyTuple_New(nargs) : NULL;
    if (args == NULL) {
        if (!PyErr_Occurred())
            PyErr_SetString(LibXML_Error, "XPathContext: lost function");
        pctxt->error = XPATH_UNKNOWN_FUNC_ERROR;
        return;
    }

    for (int i = nargs - 1; i >= 0; i--) {
        xmlXPathObjectPtr xobj = valuePop(pctxt);
        PyObject *value = xobj ? xpath_to_plain(xobj) : NULL;
        if (xobj) xmlXPathFreeObject(xobj);
        if (value == NULL) {
            Py_DECREF(args);
            pctxt->error = XPATH_INVALID_OPERAND;
            return;
        }
        PyTuple_SET_ITEM(args, i, value);
    }

    PyObject *result = PyObject_CallObject(func, args);
    Py_DECREF(args);
    if (result == NULL) {
        pctxt->error = XPATH_EXPR_ERROR;
        return;
    }

    xmlXPathObjectPtr value = python_to_xpath(result, self->pool);
    Py_DECREF(result);
    if (value == NULL) {
        pctxt->error = XPATH_EXPR_ERROR;
        return;
    }
    valuePush(pctxt, value);
}

static xmlXPathObjectPtr
lookup_variable(void *data, const xmlChar *name, const xmlChar *uri)
{
    LibXML_XPathContextObject *self = (LibXML_XPathContextObject *)data;

    PyObject *result = PyObject_CallFunction(self->var_func, "Oss",
        self->var_data ? self->var_data : Py_None,
        (const char *)name, (const char *)uri);
    if (result == NULL) return NULL;

    xmlXPathObjectPtr value = NULL;
    if (result != Py_None)
        value = python_to_xpath(result, self->pool);
    Py_DECREF(result);
    return value;
}

/* ------------------------------------------------------------------ */
/* XPathContext: construction                                          */
/* ------------------------------------------------------------------ */

static PyObject *
xpc_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *node = Py_None;
    static char *kwlist[] = {"node", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &node)) return NULL;

    if (node != Py_None && !LibXML_Node_Check(node)) {
        PyErr_SetString(PyExc_TypeError, "XPathContext argument must be a Node or None");
        return NULL;
    }

    LibXML_XPathContextObject *self = (LibXML_XPathContextObject *)type->tp_alloc(type, 0);
    if (self == NULL) return NULL;

    self->ctx = xmlXPathNewContext(NULL);
    self->functions = PyDict_New();
    self->pool = PyList_New(0);
    if (self->ctx == NULL || self->functions == NULL || self->pool == NULL) {
        Py_DECREF(self);
        return NULL;
    }

    self->ctx->userData = self;
    self->ctx->extra = self->pool;
    xmlXPathRegisterFunc(self->ctx, BAD_CAST "document", xpath_document);

    if (node != Py_None) {
        Py_INCREF(node);
        self->node = node;
    }
    return (PyObject *)self;
}

static int
xpc_traverse(LibXML_XPathContextObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->node);
    Py_VISIT(self->functions);
    Py_VISIT(self->var_func);
    Py_VISIT(self->var_data);
    Py_VISIT(self->pool);
    return 0;
}

static int
xpc_clear(LibXML_XPathContextObject *self)
{
    Py_CLEAR(self->node);
    Py_CLEAR(self->var_func);
    Py_CLEAR(self->var_data);
    if (self->functions) PyDict_Clear(self->functions);
    return 0;
}

static void
xpc_dealloc(LibXML_XPathContextObject *self)
{
    PyObject_GC_UnTrack(self);
    if (self->ctx) xmlXPathFreeContext(self->ctx);
    Py_XDECREF(self->node);
    Py_XDECREF(self->functions);
    Py_XDECREF(self->var_func);
    Py_XDECREF(self->var_data);
    Py_XDECREF(self->pool);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* XPathContext: evaluation                                            */
/* ------------------------------------------------------------------ */

/* What an evaluation changes in the context. Saved and put back, because a
 * callback can run an evaluation on the same context. */
typedef struct {
    xmlNodePtr node;
    xmlDocPtr doc;
    xmlNsPtr *namespaces;
    int nsNr;
    int contextSize;
    int proximityPosition;
    const xmlChar *function;
    const xmlChar *functionURI;
} SavedContext;

static void
context_enter(LibXML_XPathContextObject *self, SavedContext *saved)
{
    xmlXPathContextPtr ctx = self->ctx;

    saved->node = ctx->node;
    saved->doc = ctx->doc;
    saved->namespaces = ctx->namespaces;
    saved->nsNr = ctx->nsNr;
    saved->contextSize = ctx->contextSize;
    saved->proximityPosition = ctx->proximityPosition;
    saved->function = ctx->function;
    saved->functionURI = ctx->functionURI;

    xmlNodePtr node = self->node ? LibXML_NODE(self->node) : NULL;
    ctx->node = node;
    ctx->doc = node ? node->doc : NULL;
    set_scope(ctx, node);
}

static void
context_leave(LibXML_XPathContextObject *self, const SavedContext *saved)
{
    xmlXPathContextPtr ctx = self->ctx;

    xmlFree(ctx->namespaces);
    ctx->node = saved->node;
    ctx->doc = saved->doc;
    ctx->namespaces = saved->namespaces;
    ctx->nsNr = saved->nsNr;
    ctx->contextSize = saved->contextSize;
    ctx->proximityPosition = saved->proximityPosition;
    ctx->function = saved->function;
    ctx->functionURI = saved->functionURI;
}

/* args: expression, and optionally a node to use as context this time. */
static PyObject *
xpc_run(LibXML_XPathContextObject *self, PyObject *args, LibXML_XPathMode mode)
{
    PyObject *expr, *node = Py_None;
    if (!PyArg_ParseTuple(args, "O|O", &expr, &node)) return NULL;
    if (check_expression(expr) < 0) return NULL;

    if (node != Py_None && !LibXML_Node_Check(node)) {
        PyErr_SetString(PyExc_TypeError, "the context node must be a Node or None");
        return NULL;
    }

    PyObject *old_node = self->node;
    if (node != Py_None) {
        Py_INCREF(node);
        self->node = node;
    }

    PyObject *result = NULL;
    if (self->node == NULL) {
        PyErr_SetString(LibXML_Error, "XPathContext has no context node");
    } else if (check_context_node(LibXML_NODE(self->node)) == 0) {
        SavedContext saved;
        context_enter(self, &saved);
        self->depth++;
        LibXML_eval_begin();

        xmlXPathObjectPtr xobj = run_expression(self->ctx, expr);
        context_leave(self, &saved);

        if (xobj != NULL) {
            result = xpath_result(xobj, mode);
            xmlXPathFreeObject(xobj);
        }

        /* Now the results hold what they need. */
        if (--self->depth == 0 && PyList_GET_SIZE(self->pool) > 0)
            PyList_SetSlice(self->pool, 0, PyList_GET_SIZE(self->pool), NULL);
        LibXML_eval_end();
    }

    if (node != Py_None) {
        PyObject *temporary = self->node;
        self->node = old_node;
        Py_DECREF(temporary);
    }
    return result;
}

static PyObject *
xpc_findnodes(LibXML_XPathContextObject *self, PyObject *args)
{
    return xpc_run(self, args, LIBXML_XP_NODES);
}

static PyObject *
xpc_find(LibXML_XPathContextObject *self, PyObject *args)
{
    return xpc_run(self, args, LIBXML_XP_FIND);
}

static PyObject *
xpc_findvalue(LibXML_XPathContextObject *self, PyObject *args)
{
    return xpc_run(self, args, LIBXML_XP_VALUE);
}

static PyObject *
xpc_exists(LibXML_XPathContextObject *self, PyObject *args)
{
    return xpc_run(self, args, LIBXML_XP_EXISTS);
}

/* ------------------------------------------------------------------ */
/* XPathContext: settings                                              */
/* ------------------------------------------------------------------ */

static PyObject *
xpc_registerNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix, *uri;
    if (!PyArg_ParseTuple(args, "sz", &prefix, &uri)) return NULL;
    if (xmlXPathRegisterNs(self->ctx, BAD_CAST prefix, BAD_CAST uri) != 0) {
        PyErr_SetString(LibXML_Error, "failed to register namespace");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
xpc_unregisterNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "s", &prefix)) return NULL;
    xmlXPathRegisterNs(self->ctx, BAD_CAST prefix, NULL);
    Py_RETURN_NONE;
}

static PyObject *
xpc_lookupNs(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "s", &prefix)) return NULL;

    SavedContext saved;
    context_enter(self, &saved);
    const xmlChar *uri = xmlXPathNsLookup(self->ctx, BAD_CAST prefix);
    PyObject *result = uri ? PyUnicode_FromString((const char *)uri)
                           : (Py_INCREF(Py_None), Py_None);
    context_leave(self, &saved);
    return result;
}

static PyObject *
xpc_setContextNode(LibXML_XPathContextObject *self, PyObject *args)
{
    PyObject *node;
    if (!PyArg_ParseTuple(args, "O", &node)) return NULL;

    if (node != Py_None && !LibXML_Node_Check(node)) {
        PyErr_SetString(PyExc_TypeError, "argument must be a Node or None");
        return NULL;
    }

    PyObject *old = self->node;
    self->node = NULL;
    if (node != Py_None) {
        Py_INCREF(node);
        self->node = node;
    }
    Py_XDECREF(old);
    Py_RETURN_NONE;
}

static PyObject *
xpc_getContextNode(LibXML_XPathContextObject *self, PyObject *args)
{
    if (self->node == NULL) Py_RETURN_NONE;
    Py_INCREF(self->node);
    return self->node;
}

static PyObject *
xpc_getContextPosition(LibXML_XPathContextObject *self, PyObject *args)
{
    return PyLong_FromLong(self->ctx->proximityPosition);
}

static PyObject *
xpc_getContextSize(LibXML_XPathContextObject *self, PyObject *args)
{
    return PyLong_FromLong(self->ctx->contextSize);
}

static PyObject *
xpc_setContextPosition(LibXML_XPathContextObject *self, PyObject *args)
{
    int position;
    if (!PyArg_ParseTuple(args, "i", &position)) return NULL;
    if (position < -1 || position > self->ctx->contextSize) {
        PyErr_SetString(LibXML_Error, "XPathContext: invalid position");
        return NULL;
    }
    self->ctx->proximityPosition = position;
    Py_RETURN_NONE;
}

static PyObject *
xpc_setContextSize(LibXML_XPathContextObject *self, PyObject *args)
{
    int size;
    if (!PyArg_ParseTuple(args, "i", &size)) return NULL;
    if (size < -1) {
        PyErr_SetString(LibXML_Error, "XPathContext: invalid size");
        return NULL;
    }
    self->ctx->contextSize = size;
    self->ctx->proximityPosition = size == 0 ? 0 : (size > 0 ? 1 : -1);
    Py_RETURN_NONE;
}

/* func None removes the function. */
static PyObject *
set_function(LibXML_XPathContextObject *self, const char *name, const char *uri,
             PyObject *func)
{
    if (func != Py_None && !PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }

    PyObject *key = Py_BuildValue("(ss)", name, uri);
    if (key == NULL) return NULL;

    int rc;
    if (func == Py_None) {
        rc = PyDict_DelItem(self->functions, key);
        if (rc < 0 && PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
            rc = 0;
        }
        if (rc == 0)
            xmlXPathRegisterFuncNS(self->ctx, BAD_CAST name, BAD_CAST uri, NULL);
    } else {
        rc = PyDict_SetItem(self->functions, key, func);
        if (rc == 0 && xmlXPathRegisterFuncNS(self->ctx, BAD_CAST name, BAD_CAST uri,
                                              call_function) != 0) {
            PyErr_SetString(LibXML_Error, "failed to register function");
            rc = -1;
        }
    }
    Py_DECREF(key);
    if (rc < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
xpc_registerFunction(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name;
    PyObject *func;
    if (!PyArg_ParseTuple(args, "sO", &name, &func)) return NULL;
    return set_function(self, name, NULL, func);
}

static PyObject *
xpc_registerFunctionNS(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name, *uri;
    PyObject *func;
    if (!PyArg_ParseTuple(args, "szO", &name, &uri, &func)) return NULL;
    return set_function(self, name, uri, func);
}

static PyObject *
xpc_unregisterFunction(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return set_function(self, name, NULL, Py_None);
}

static PyObject *
xpc_unregisterFunctionNS(LibXML_XPathContextObject *self, PyObject *args)
{
    const char *name, *uri;
    if (!PyArg_ParseTuple(args, "sz", &name, &uri)) return NULL;
    return set_function(self, name, uri, Py_None);
}

static PyObject *
xpc_registerVarLookupFunc(LibXML_XPathContextObject *self, PyObject *args)
{
    PyObject *func, *data = Py_None;
    if (!PyArg_ParseTuple(args, "O|O", &func, &data)) return NULL;

    if (func != Py_None && !PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }

    Py_CLEAR(self->var_func);
    Py_CLEAR(self->var_data);
    if (func == Py_None) {
        xmlXPathRegisterVariableLookup(self->ctx, NULL, NULL);
    } else {
        Py_INCREF(func);
        self->var_func = func;
        if (data != Py_None) {
            Py_INCREF(data);
            self->var_data = data;
        }
        xmlXPathRegisterVariableLookup(self->ctx, lookup_variable, self);
    }
    Py_RETURN_NONE;
}

static PyObject *
xpc_unregisterVarLookupFunc(LibXML_XPathContextObject *self, PyObject *args)
{
    Py_CLEAR(self->var_func);
    Py_CLEAR(self->var_data);
    xmlXPathRegisterVariableLookup(self->ctx, NULL, NULL);
    Py_RETURN_NONE;
}

static PyObject *
xpc_getVarLookupFunc(LibXML_XPathContextObject *self, PyObject *args)
{
    if (self->var_func == NULL) Py_RETURN_NONE;
    Py_INCREF(self->var_func);
    return self->var_func;
}

static PyObject *
xpc_getVarLookupData(LibXML_XPathContextObject *self, PyObject *args)
{
    if (self->var_data == NULL) Py_RETURN_NONE;
    Py_INCREF(self->var_data);
    return self->var_data;
}

static PyMethodDef xpc_methods[] = {
    {"registerNs",              (PyCFunction)xpc_registerNs,              METH_VARARGS, "Register a namespace prefix, or remove it with None"},
    {"unregisterNs",            (PyCFunction)xpc_unregisterNs,            METH_VARARGS, "Remove a namespace prefix"},
    {"lookupNs",                (PyCFunction)xpc_lookupNs,                METH_VARARGS, "Look up a namespace prefix"},
    {"findnodes",               (PyCFunction)xpc_findnodes,               METH_VARARGS, "Evaluate XPath, return a NodeList"},
    {"find",                    (PyCFunction)xpc_find,                    METH_VARARGS, "Evaluate XPath, return a typed result"},
    {"findvalue",               (PyCFunction)xpc_findvalue,               METH_VARARGS, "Evaluate XPath, return a string"},
    {"exists",                  (PyCFunction)xpc_exists,                  METH_VARARGS, "Evaluate XPath, return a bool"},
    {"setContextNode",          (PyCFunction)xpc_setContextNode,          METH_VARARGS, "Set the context node"},
    {"getContextNode",          (PyCFunction)xpc_getContextNode,          METH_NOARGS,  "Get the context node"},
    {"setContextPosition",      (PyCFunction)xpc_setContextPosition,      METH_VARARGS, "Set the value of position()"},
    {"getContextPosition",      (PyCFunction)xpc_getContextPosition,      METH_NOARGS,  "Get the value of position()"},
    {"setContextSize",          (PyCFunction)xpc_setContextSize,          METH_VARARGS, "Set the value of last()"},
    {"getContextSize",          (PyCFunction)xpc_getContextSize,          METH_NOARGS,  "Get the value of last()"},
    {"registerFunction",        (PyCFunction)xpc_registerFunction,        METH_VARARGS, "Register an XPath function"},
    {"registerFunctionNS",      (PyCFunction)xpc_registerFunctionNS,      METH_VARARGS, "Register an XPath function in a namespace"},
    {"unregisterFunction",      (PyCFunction)xpc_unregisterFunction,      METH_VARARGS, "Remove an XPath function"},
    {"unregisterFunctionNS",    (PyCFunction)xpc_unregisterFunctionNS,    METH_VARARGS, "Remove an XPath function in a namespace"},
    {"registerVarLookupFunc",   (PyCFunction)xpc_registerVarLookupFunc,   METH_VARARGS, "Set the function that gives XPath variables"},
    {"unregisterVarLookupFunc", (PyCFunction)xpc_unregisterVarLookupFunc, METH_NOARGS,  "Remove the variable lookup function"},
    {"getVarLookupFunc",        (PyCFunction)xpc_getVarLookupFunc,        METH_NOARGS,  "Get the variable lookup function"},
    {"getVarLookupData",        (PyCFunction)xpc_getVarLookupData,        METH_NOARGS,  "Get the data of the variable lookup function"},
    {NULL, NULL}
};

PyTypeObject LibXML_XPathContextType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.XPathContext",
    .tp_basicsize = sizeof(LibXML_XPathContextObject),
    .tp_dealloc   = (destructor)xpc_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_traverse  = (traverseproc)xpc_traverse,
    .tp_clear     = (inquiry)xpc_clear,
    .tp_new       = xpc_new,
    .tp_methods   = xpc_methods,
    .tp_doc       = "XPath evaluation context",
};

/* ------------------------------------------------------------------ */
/* Canonical XML                                                       */
/* ------------------------------------------------------------------ */

#define C14N_ALL_NODES "(. | .//node() | .//@* | .//namespace::*)"

/* The nodes to output, which the c14n code wants listed one by one. */
static xmlXPathObjectPtr
c14n_nodes(LibXML_NodeObject *self, PyObject *xpath, PyObject *xpc)
{
    xmlXPathContextPtr ctx;
    PyObject *pool = NULL;
    SavedContext saved;
    LibXML_XPathContextObject *context = (LibXML_XPathContextObject *)xpc;
    PyObject *previous_node = NULL;

    if (xpc != Py_None) {
        previous_node = context->node;
        Py_INCREF(self);
        context->node = (PyObject *)self;
        context_enter(context, &saved);
        ctx = context->ctx;
    } else {
        ctx = xmlXPathNewContext(self->node->doc);
        pool = PyList_New(0);
        if (ctx == NULL || pool == NULL) {
            if (ctx) xmlXPathFreeContext(ctx);
            Py_XDECREF(pool);
            PyErr_NoMemory();
            return NULL;
        }
        ctx->node = self->node;
        ctx->extra = pool;
        set_scope(ctx, self->node);
    }

    LibXML_eval_begin();
    xmlXPathObjectPtr xobj = run_expression(ctx, xpath);
    LibXML_eval_end();

    if (xpc != Py_None) {
        context_leave(context, &saved);
        Py_DECREF(context->node);
        context->node = previous_node;
    } else {
        xmlFree(ctx->namespaces);
        ctx->namespaces = NULL;
        ctx->nsNr = 0;
        xmlXPathFreeContext(ctx);
        Py_DECREF(pool);
    }

    if (xobj != NULL && (xobj->type != XPATH_NODESET || xobj->nodesetval == NULL)) {
        xmlXPathFreeObject(xobj);
        PyErr_SetString(LibXML_Error, "cannot canonize empty nodeset");
        return NULL;
    }
    return xobj;
}

PyObject *
LibXML_node_c14n(LibXML_NodeObject *self, int comments, PyObject *xpath, int mode,
                 PyObject *prefixes, PyObject *xpc)
{
    int is_doc = self->node->type == XML_DOCUMENT_NODE
              || self->node->type == XML_HTML_DOCUMENT_NODE;

    if (xpc != Py_None && !PyObject_TypeCheck(xpc, &LibXML_XPathContextType)) {
        PyErr_SetString(PyExc_TypeError, "xpc must be an XPathContext");
        return NULL;
    }
    if (prefixes != Py_None && !PyList_Check(prefixes) && !PyTuple_Check(prefixes)) {
        PyErr_SetString(LibXML_Error, "the inclusive prefix list must be a list or None");
        return NULL;
    }

    PyObject *expr = NULL;
    if (xpath != Py_None && !(PyUnicode_Check(xpath) && PyUnicode_GET_LENGTH(xpath) == 0)) {
        if (check_expression(xpath) < 0) return NULL;
        Py_INCREF(xpath);
        expr = xpath;
    } else if (!is_doc) {
        expr = PyUnicode_FromString(comments ? C14N_ALL_NODES
                                             : C14N_ALL_NODES "[not(self::comment())]");
        if (expr == NULL) return NULL;
    }

    if (check_context_node(self->node) < 0) {
        Py_XDECREF(expr);
        return NULL;
    }

    xmlXPathObjectPtr xobj = NULL;
    if (expr != NULL) {
        xobj = c14n_nodes(self, expr, xpc);
        Py_DECREF(expr);
        if (xobj == NULL) return NULL;
    }

    /* The list of prefixes, as C strings */
    xmlChar **list = NULL;
    Py_ssize_t count = prefixes == Py_None ? 0 : PySequence_Size(prefixes);
    if (prefixes != Py_None) {
        list = PyMem_Calloc(count + 1, sizeof(xmlChar *));
        if (list == NULL) {
            if (xobj) xmlXPathFreeObject(xobj);
            return PyErr_NoMemory();
        }
        for (Py_ssize_t i = 0; i < count; i++) {
            PyObject *item = PySequence_GetItem(prefixes, i);
            const char *text = item ? PyUnicode_AsUTF8(item) : NULL;
            /* Strings of the list stay alive: it is held by the caller. */
            Py_XDECREF(item);
            if (text == NULL) {
                PyMem_Free(list);
                if (xobj) xmlXPathFreeObject(xobj);
                return NULL;
            }
            list[i] = BAD_CAST text;
        }
    }

    xmlChar *result = NULL;
    LibXML_errors_begin();
    int size = xmlC14NDocDumpMemory(self->node->doc, xobj ? xobj->nodesetval : NULL,
                                    mode, list, comments, &result);
    PyMem_Free(list);
    if (xobj) xmlXPathFreeObject(xobj);

    if (size < 0 || result == NULL) {
        xmlFree(result);
        LibXML_raise_last_error();
        return NULL;
    }
    PyObject *text = PyUnicode_DecodeUTF8((const char *)result, size, "strict");
    xmlFree(result);
    return text;
}

/* ------------------------------------------------------------------ */
/* Module                                                              */
/* ------------------------------------------------------------------ */

int
LibXML_xpath_init(PyObject *module)
{
    PyObject *source = PyImport_ImportModule("xml_libxml._xpath");
    if (source == NULL) return -1;

    literal_class = PyObject_GetAttrString(source, "Literal");
    number_class  = PyObject_GetAttrString(source, "Number");
    boolean_class = PyObject_GetAttrString(source, "Boolean");
    PyObject *nodelist_class = PyObject_GetAttrString(source, "NodeList");
    Py_DECREF(source);
    if (!literal_class || !number_class || !boolean_class || !nodelist_class) return -1;

    LibXML_NodeList_Init(nodelist_class);

    struct { const char *name; PyObject *cls; } exports[] = {
        {"Literal", literal_class}, {"Number", number_class},
        {"Boolean", boolean_class}, {"NodeList", nodelist_class},
    };
    for (size_t i = 0; i < sizeof(exports) / sizeof(exports[0]); i++) {
        Py_INCREF(exports[i].cls);
        if (PyModule_AddObject(module, exports[i].name, exports[i].cls) < 0) return -1;
    }
    return 0;
}

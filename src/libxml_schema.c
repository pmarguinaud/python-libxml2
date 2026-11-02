#include "libxml_schema.h"
#include "libxml_utils.h"
#include "libxml_node.h"
#include "libxml_document.h"
#include "libxml_input.h"
#include <libxml/relaxng.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlregexp.h>

/* ------------------------------------------------------------------ */
/* Where a schema comes from                                           */
/* ------------------------------------------------------------------ */

/* location=, string= or DOM=. A string goes through the parser, which knows what
 * a declared encoding means for a str, so both end up as a document. */
typedef struct {
    PyObject *location;     /* bytes, or NULL */
    PyObject *dom;          /* a Document, or NULL */
    int no_network;
    int recover;
} Source;

static void
source_clear(Source *src)
{
    Py_CLEAR(src->location);
    Py_CLEAR(src->dom);
}

static PyObject *
parse_schema_string(PyObject *string, int no_network)
{
    PyObject *module = PyImport_ImportModule("xml_libxml");
    if (module == NULL) return NULL;
    PyObject *cls = PyObject_GetAttrString(module, "Parser");
    Py_DECREF(module);
    if (cls == NULL) return NULL;

    PyObject *kwds = Py_BuildValue("{s:i}", "no_network", no_network);
    PyObject *empty = PyTuple_New(0);
    PyObject *parser = (kwds && empty) ? PyObject_Call(cls, empty, kwds) : NULL;
    Py_XDECREF(kwds);
    Py_XDECREF(empty);
    Py_DECREF(cls);
    if (parser == NULL) return NULL;

    PyObject *doc = PyObject_CallMethod(parser, "parse_string", "O", string);
    Py_DECREF(parser);
    return doc;
}

static int
source_init(Source *src, PyObject *args, PyObject *kwds)
{
    PyObject *location = NULL, *string = NULL, *dom = NULL;
    PyObject *no_network = NULL, *recover = NULL;
    static char *kwlist[] = {"location", "string", "DOM", "no_network", "recover", NULL};

    src->location = src->dom = NULL;
    src->no_network = 1;
    src->recover = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|$OOOOO", kwlist, &location, &string,
                                     &dom, &no_network, &recover))
        return -1;

    if ((location != NULL) + (string != NULL) + (dom != NULL) != 1) {
        PyErr_SetString(PyExc_TypeError, "give one of location, string or DOM");
        return -1;
    }
    if (no_network != NULL) {
        src->no_network = PyObject_IsTrue(no_network);
        if (src->no_network < 0) return -1;
    }
    if (recover != NULL) {
        src->recover = PyObject_IsTrue(recover);
        if (src->recover < 0) return -1;
    }

    if (location != NULL) {
        if (!PyUnicode_FSConverter(location, &src->location))
            return -1;
    } else if (dom != NULL) {
        if (!PyObject_TypeCheck(dom, &LibXML_DocumentType)) {
            PyErr_SetString(PyExc_TypeError, "DOM must be a Document");
            return -1;
        }
        Py_INCREF(dom);
        src->dom = dom;
    } else {
        src->dom = parse_schema_string(string, src->no_network);
        if (src->dom == NULL) return -1;
    }
    return 0;
}

/* After loading a schema: 0 if it can be kept, -1 with an exception. */
static int
loaded(const void *schema, const Source *src)
{
    if (PyErr_Occurred()) {
        LibXML_errors_begin();
        return -1;
    }
    if (schema == NULL) {
        LibXML_raise_last_error();
        return -1;
    }
    return LibXML_report_errors(src->recover ? 1 : 0);
}

/* The result of validate(): 0 as in Perl, or an exception. */
static PyObject *
validated(int ret)
{
    if (ret != 0) {
        LibXML_raise_last_error();
        return NULL;
    }
    LibXML_errors_begin();
    return PyLong_FromLong(0);
}

/* ------------------------------------------------------------------ */
/* RelaxNG                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    xmlRelaxNGPtr schema;
} RelaxNGObject;

static PyObject *
relaxng_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Source src;
    if (source_init(&src, args, kwds) < 0) {
        source_clear(&src);
        return NULL;
    }

    LibXML_errors_begin();

    /* With a document, libxml2 works on its own copy. */
    xmlRelaxNGParserCtxtPtr ctxt = src.dom != NULL
        ? xmlRelaxNGNewDocParserCtxt(LibXML_DOC(src.dom))
        : xmlRelaxNGNewParserCtxt(PyBytes_AS_STRING(src.location));
    if (ctxt == NULL) {
        source_clear(&src);
        LibXML_raise_last_error();
        return NULL;
    }

    LibXML_ParseState state;
    LibXML_state_push(&state, NULL, NULL, src.no_network);
    xmlRelaxNGPtr schema = xmlRelaxNGParse(ctxt);
    LibXML_state_pop(&state);
    xmlRelaxNGFreeParserCtxt(ctxt);

    int rc = loaded(schema, &src);
    source_clear(&src);
    if (rc < 0) {
        if (schema != NULL) xmlRelaxNGFree(schema);
        return NULL;
    }

    RelaxNGObject *self = (RelaxNGObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        xmlRelaxNGFree(schema);
        return NULL;
    }
    self->schema = schema;
    return (PyObject *)self;
}

static void
relaxng_dealloc(RelaxNGObject *self)
{
    if (self->schema != NULL) xmlRelaxNGFree(self->schema);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
relaxng_validate(RelaxNGObject *self, PyObject *args)
{
    LibXML_NodeObject *doc;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_DocumentType, &doc)) return NULL;

    if (LibXML_check_unlocked(doc->node) < 0) return NULL;
    LibXML_errors_begin();
    xmlRelaxNGValidCtxtPtr ctxt = xmlRelaxNGNewValidCtxt(self->schema);
    if (ctxt == NULL) return PyErr_NoMemory();
    int ret = xmlRelaxNGValidateDoc(ctxt, LibXML_DOC(doc));
    xmlRelaxNGFreeValidCtxt(ctxt);
    return validated(ret);
}

static PyMethodDef relaxng_methods[] = {
    {"validate", (PyCFunction)relaxng_validate, METH_VARARGS,
     "Return 0 if the document is valid, raise an Error otherwise"},
    {NULL, NULL}
};

xmlRelaxNGPtr
LibXML_relaxng_ptr(PyObject *obj)
{
    return PyObject_TypeCheck(obj, &LibXML_RelaxNGType) ? ((RelaxNGObject *)obj)->schema : NULL;
}

PyTypeObject LibXML_RelaxNGType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.RelaxNG",
    .tp_basicsize = sizeof(RelaxNGObject),
    .tp_dealloc   = (destructor)relaxng_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = relaxng_new,
    .tp_methods   = relaxng_methods,
    .tp_doc       = "RelaxNG schema: RelaxNG(location=, string= or DOM=, no_network=, recover=)",
};

/* ------------------------------------------------------------------ */
/* XML Schema                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    xmlSchemaPtr schema;
    xmlDocPtr doc;          /* the schema points into it, if it comes from a document */
} SchemaObject;

static PyObject *
schema_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Source src;
    if (source_init(&src, args, kwds) < 0) {
        source_clear(&src);
        return NULL;
    }

    LibXML_errors_begin();

    /* A copy, as the schema needs it to stay as it is. */
    xmlDocPtr doc = NULL;
    if (src.dom != NULL) {
        doc = xmlCopyDoc(LibXML_DOC(src.dom), 1);
        if (doc == NULL) {
            source_clear(&src);
            return PyErr_NoMemory();
        }
    }

    xmlSchemaParserCtxtPtr ctxt = doc != NULL
        ? xmlSchemaNewDocParserCtxt(doc)
        : xmlSchemaNewParserCtxt(PyBytes_AS_STRING(src.location));
    if (ctxt == NULL) {
        if (doc != NULL) xmlFreeDoc(doc);
        source_clear(&src);
        LibXML_raise_last_error();
        return NULL;
    }

    LibXML_ParseState state;
    LibXML_state_push(&state, NULL, NULL, src.no_network);
    xmlSchemaPtr schema = xmlSchemaParse(ctxt);
    LibXML_state_pop(&state);
    xmlSchemaFreeParserCtxt(ctxt);

    int rc = loaded(schema, &src);
    source_clear(&src);

    SchemaObject *self = NULL;
    if (rc == 0)
        self = (SchemaObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        if (schema != NULL) xmlSchemaFree(schema);
        if (doc != NULL) xmlFreeDoc(doc);
        return NULL;
    }
    self->schema = schema;
    self->doc = doc;
    return (PyObject *)self;
}

static void
schema_dealloc(SchemaObject *self)
{
    if (self->schema != NULL) xmlSchemaFree(self->schema);
    if (self->doc != NULL) xmlFreeDoc(self->doc);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* A document, or one element of a tree. */
static PyObject *
schema_validate(SchemaObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &node)) return NULL;

    int is_doc = PyObject_TypeCheck(node, &LibXML_DocumentType);
    if (!is_doc && node->node->type != XML_ELEMENT_NODE) {
        PyErr_SetString(PyExc_TypeError, "validate takes a Document or an Element");
        return NULL;
    }

    if (LibXML_check_unlocked(node->node) < 0) return NULL;
    LibXML_errors_begin();
    xmlSchemaValidCtxtPtr ctxt = xmlSchemaNewValidCtxt(self->schema);
    if (ctxt == NULL) return PyErr_NoMemory();
    int ret = is_doc ? xmlSchemaValidateDoc(ctxt, LibXML_DOC(node))
                     : xmlSchemaValidateOneElement(ctxt, node->node);
    xmlSchemaFreeValidCtxt(ctxt);
    return validated(ret);
}

static PyMethodDef schema_methods[] = {
    {"validate", (PyCFunction)schema_validate, METH_VARARGS,
     "Return 0 if the document or element is valid, raise an Error otherwise"},
    {NULL, NULL}
};

xmlSchemaPtr
LibXML_schema_ptr(PyObject *obj)
{
    return PyObject_TypeCheck(obj, &LibXML_SchemaType) ? ((SchemaObject *)obj)->schema : NULL;
}

PyTypeObject LibXML_SchemaType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Schema",
    .tp_basicsize = sizeof(SchemaObject),
    .tp_dealloc   = (destructor)schema_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = schema_new,
    .tp_methods   = schema_methods,
    .tp_doc       = "XML Schema: Schema(location=, string= or DOM=, no_network=, recover=)",
};

/* ------------------------------------------------------------------ */
/* Pattern                                                             */
/* ------------------------------------------------------------------ */

/* Pattern(pattern, {prefix: uri}). libxml2 wants uri, prefix, uri, prefix, NULL. */
static PyObject *
pattern_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *text;
    PyObject *ns_map = Py_None;
    static char *kwlist[] = {"pattern", "ns_map", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|O", kwlist, &text, &ns_map))
        return NULL;
    if (ns_map != Py_None && !PyDict_Check(ns_map)) {
        PyErr_SetString(PyExc_TypeError, "ns_map must be a dict of prefix: uri");
        return NULL;
    }

    const xmlChar **namespaces = NULL;
    if (ns_map != Py_None) {
        namespaces = PyMem_Calloc(2 * PyDict_Size(ns_map) + 1, sizeof(xmlChar *));
        if (namespaces == NULL) return PyErr_NoMemory();

        PyObject *key, *value;
        Py_ssize_t pos = 0, i = 0;
        while (PyDict_Next(ns_map, &pos, &key, &value)) {
            const char *prefix = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
            const char *uri = PyUnicode_Check(value) ? PyUnicode_AsUTF8(value) : NULL;
            if (prefix == NULL || uri == NULL) {
                PyMem_Free(namespaces);
                if (!PyErr_Occurred())
                    PyErr_SetString(PyExc_TypeError, "ns_map must be a dict of prefix: uri");
                return NULL;
            }
            namespaces[i++] = BAD_CAST uri;
            namespaces[i++] = BAD_CAST prefix;
        }
    }

    LibXML_errors_begin();
    xmlPatternPtr pattern = xmlPatterncompile(BAD_CAST text, NULL, 0, namespaces);
    PyMem_Free(namespaces);
    if (pattern == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    LibXML_errors_begin();

    LibXML_PatternObject *self = (LibXML_PatternObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        xmlFreePattern(pattern);
        return NULL;
    }
    self->pattern = pattern;
    return (PyObject *)self;
}

static void
pattern_dealloc(LibXML_PatternObject *self)
{
    if (self->pattern != NULL) xmlFreePattern(self->pattern);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
pattern_matchesNode(LibXML_PatternObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &node)) return NULL;

    int ret = xmlPatternMatch(self->pattern, node->node);
    if (ret < 0) {
        PyErr_SetString(LibXML_Error, "cannot match this node");
        return NULL;
    }
    return PyBool_FromLong(ret);
}

static PyMethodDef pattern_methods[] = {
    {"matchesNode", (PyCFunction)pattern_matchesNode, METH_VARARGS, "Tell if the pattern matches a node"},
    {NULL, NULL}
};

PyTypeObject LibXML_PatternType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Pattern",
    .tp_basicsize = sizeof(LibXML_PatternObject),
    .tp_dealloc   = (destructor)pattern_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = pattern_new,
    .tp_methods   = pattern_methods,
    .tp_doc       = "Compiled pattern, a subset of XPath: Pattern(pattern, {prefix: uri})",
};

/* ------------------------------------------------------------------ */
/* RegExp                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    xmlRegexpPtr regexp;
} RegExpObject;

static PyObject *
regexp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *text;
    static char *kwlist[] = {"regexp", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &text)) return NULL;

    LibXML_errors_begin();
    xmlRegexpPtr regexp = xmlRegexpCompile(BAD_CAST text);
    if (regexp == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    LibXML_errors_begin();

    RegExpObject *self = (RegExpObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        xmlRegFreeRegexp(regexp);
        return NULL;
    }
    self->regexp = regexp;
    return (PyObject *)self;
}

static void
regexp_dealloc(RegExpObject *self)
{
    if (self->regexp != NULL) xmlRegFreeRegexp(self->regexp);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
regexp_matches(RegExpObject *self, PyObject *args)
{
    const char *value;
    if (!PyArg_ParseTuple(args, "s", &value)) return NULL;

    int ret = xmlRegexpExec(self->regexp, BAD_CAST value);
    if (ret < 0) {
        PyErr_SetString(LibXML_Error, "regular expression failed");
        return NULL;
    }
    return PyBool_FromLong(ret);
}

static PyObject *
regexp_isDeterministic(RegExpObject *self, PyObject *args)
{
    return PyBool_FromLong(xmlRegexpIsDeterminist(self->regexp) > 0);
}

static PyMethodDef regexp_methods[] = {
    {"matches",         (PyCFunction)regexp_matches,         METH_VARARGS, "Tell if the whole string matches"},
    {"isDeterministic", (PyCFunction)regexp_isDeterministic, METH_NOARGS,  "Tell if the expression is deterministic"},
    {NULL, NULL}
};

PyTypeObject LibXML_RegExpType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.RegExp",
    .tp_basicsize = sizeof(RegExpObject),
    .tp_dealloc   = (destructor)regexp_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = regexp_new,
    .tp_methods   = regexp_methods,
    .tp_doc       = "XML Schema regular expression",
};

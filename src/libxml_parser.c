#include "libxml_parser.h"
#include "libxml_utils.h"
#include "libxml_document.h"
#include "libxml_node.h"

/* ------------------------------------------------------------------ */
/* Build libxml2 parser option flags from a ParserObject              */
/* ------------------------------------------------------------------ */

int
LibXML_parser_options(LibXML_ParserObject *self)
{
    int opts = XML_PARSE_NOERROR | XML_PARSE_NOWARNING;
    if (self->recover)           opts |= XML_PARSE_RECOVER;
    if (self->no_blanks)         opts |= XML_PARSE_NOBLANKS;
    if (self->expand_entities)   opts |= XML_PARSE_NOENT;
    if (self->load_ext_dtd)      opts |= XML_PARSE_DTDLOAD;
    if (self->validation)        opts |= XML_PARSE_DTDVALID;
    if (!self->no_network)       opts &= ~XML_PARSE_NONET;
    else                         opts |= XML_PARSE_NONET;
    if (self->no_cdata)          opts |= XML_PARSE_NOCDATA;
    if (self->huge)              opts |= XML_PARSE_HUGE;
    if (self->pedantic)          opts |= XML_PARSE_PEDANTIC;
    if (self->suppress_errors)   opts |= XML_PARSE_NOERROR;
    if (self->suppress_warnings) opts |= XML_PARSE_NOWARNING;
    if (self->line_numbers)      opts |= XML_PARSE_OLDSAX;  /* not exactly, but no direct flag */
    return opts;
}

/* ------------------------------------------------------------------ */
/* Construction: Parser(**options)                                     */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Parser_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {
        "recover", "no_blanks", "expand_entities", "load_ext_dtd",
        "validation", "no_network", "no_cdata", "huge", "pedantic",
        "suppress_errors", "suppress_warnings", "line_numbers", NULL
    };
    LibXML_ParserObject *self = (LibXML_ParserObject *)type->tp_alloc(type, 0);
    if (!self) return NULL;

    /* Defaults */
    self->recover           = 0;
    self->no_blanks         = 0;
    self->expand_entities   = 1;  /* default: expand entities */
    self->load_ext_dtd      = 0;
    self->validation        = 0;
    self->no_network        = 1;  /* default: no network access */
    self->no_cdata          = 0;
    self->huge              = 0;
    self->pedantic          = 0;
    self->suppress_errors   = 0;
    self->suppress_warnings = 0;
    self->line_numbers      = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
            "|iiiiiiiiiiii", kwlist,
            &self->recover, &self->no_blanks, &self->expand_entities,
            &self->load_ext_dtd, &self->validation, &self->no_network,
            &self->no_cdata, &self->huge, &self->pedantic,
            &self->suppress_errors, &self->suppress_warnings,
            &self->line_numbers)) {
        Py_DECREF(self);
        return NULL;
    }
    return (PyObject *)self;
}

/* ------------------------------------------------------------------ */
/* parse_string(string, **options)                                     */
/* ------------------------------------------------------------------ */

static PyObject *
parser_parse_string(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    const char *string;
    Py_ssize_t  string_len;
    const char *url      = NULL;
    const char *encoding = NULL;

    static char *kwlist[] = {"string", "URL", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|zz", kwlist,
            &string, &string_len, &url, &encoding))
        return NULL;

    int opts = LibXML_parser_options(self);
    xmlDocPtr doc = xmlReadMemory(string, (int)string_len,
                                  url ? url : "",
                                  encoding,
                                  opts);
    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

/* ------------------------------------------------------------------ */
/* parse_file(filename, **options)                                     */
/* ------------------------------------------------------------------ */

static PyObject *
parser_parse_file(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    const char *filename;
    const char *encoding = NULL;
    static char *kwlist[] = {"filename", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|z", kwlist,
            &filename, &encoding))
        return NULL;

    int opts = LibXML_parser_options(self);
    xmlDocPtr doc = xmlReadFile(filename, encoding, opts);
    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

/* ------------------------------------------------------------------ */
/* load_xml(string=None, location=None, IO=None, **options)           */
/* ------------------------------------------------------------------ */

static PyObject *
parser_load_xml(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *string_obj   = Py_None;
    PyObject *location_obj = Py_None;
    PyObject *io_obj       = Py_None;
    const char *encoding   = NULL;

    static char *kwlist[] = {"string", "location", "IO", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOOz", kwlist,
            &string_obj, &location_obj, &io_obj, &encoding))
        return NULL;

    int opts = LibXML_parser_options(self);
    xmlDocPtr doc = NULL;

    if (string_obj != Py_None) {
        Py_ssize_t len;
        const char *s = PyUnicode_AsUTF8AndSize(string_obj, &len);
        if (!s) return NULL;
        doc = xmlReadMemory(s, (int)len, "", encoding, opts);
    } else if (location_obj != Py_None) {
        const char *loc = PyUnicode_AsUTF8(location_obj);
        if (!loc) return NULL;
        doc = xmlReadFile(loc, encoding, opts);
    } else if (io_obj != Py_None) {
        /* Read from a Python file-like object */
        PyObject *read_result = PyObject_CallMethod(io_obj, "read", NULL);
        if (!read_result) return NULL;
        Py_ssize_t len;
        const char *s = NULL;
        if (PyBytes_Check(read_result)) {
            s = PyBytes_AS_STRING(read_result);
            len = PyBytes_GET_SIZE(read_result);
        } else if (PyUnicode_Check(read_result)) {
            s = PyUnicode_AsUTF8AndSize(read_result, &len);
        }
        if (s) doc = xmlReadMemory(s, (int)len, "", encoding, opts);
        Py_DECREF(read_result);
    } else {
        PyErr_SetString(PyExc_ValueError,
            "load_xml requires string=, location=, or IO= argument");
        return NULL;
    }

    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

/* ------------------------------------------------------------------ */
/* HTML parsing                                                        */
/* ------------------------------------------------------------------ */

static int
html_parser_options(LibXML_ParserObject *self)
{
    int opts = HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING;
    if (self->recover)           opts |= HTML_PARSE_RECOVER;
    if (self->no_blanks)         opts |= HTML_PARSE_NOBLANKS;
    if (self->no_network)        opts |= HTML_PARSE_NONET;
    if (self->suppress_errors)   opts |= HTML_PARSE_NOERROR;
    if (self->suppress_warnings) opts |= HTML_PARSE_NOWARNING;
    return opts;
}

static PyObject *
parser_parse_html_string(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    const char *string;
    Py_ssize_t  string_len;
    const char *url      = NULL;
    const char *encoding = NULL;
    static char *kwlist[] = {"string", "URL", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|zz", kwlist,
            &string, &string_len, &url, &encoding))
        return NULL;

    int opts = html_parser_options(self);
    htmlDocPtr doc = htmlReadMemory(string, (int)string_len,
                                    url ? url : "", encoding, opts);
    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

static PyObject *
parser_parse_html_file(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    const char *filename;
    const char *encoding = NULL;
    static char *kwlist[] = {"filename", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|z", kwlist,
            &filename, &encoding))
        return NULL;

    int opts = html_parser_options(self);
    htmlDocPtr doc = htmlReadFile(filename, encoding, opts);
    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

static PyObject *
parser_load_html(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *string_obj   = Py_None;
    PyObject *location_obj = Py_None;
    const char *encoding   = NULL;
    static char *kwlist[] = {"string", "location", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOz", kwlist,
            &string_obj, &location_obj, &encoding))
        return NULL;

    int opts = html_parser_options(self);
    htmlDocPtr doc = NULL;

    if (string_obj != Py_None) {
        Py_ssize_t len;
        const char *s = PyUnicode_AsUTF8AndSize(string_obj, &len);
        if (!s) return NULL;
        doc = htmlReadMemory(s, (int)len, "", encoding, opts);
    } else if (location_obj != Py_None) {
        const char *loc = PyUnicode_AsUTF8(location_obj);
        if (!loc) return NULL;
        doc = htmlReadFile(loc, encoding, opts);
    } else {
        PyErr_SetString(PyExc_ValueError,
            "load_html requires string= or location= argument");
        return NULL;
    }

    if (!doc) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)doc);
}

/* ------------------------------------------------------------------ */
/* parse_balanced_chunk                                                */
/* ------------------------------------------------------------------ */

PyObject *
LibXML_parse_chunk(const char *chunk)
{
    /* Create a temporary document to hold the fragment */
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (!doc) return PyErr_NoMemory();

    xmlNodePtr list = NULL;
    int ret = xmlParseBalancedChunkMemory(doc, NULL, NULL, 0,
                                          BAD_CAST chunk, &list);
    if (ret != 0) {
        xmlFreeDoc(doc);
        LibXML_raise_last_error();
        return NULL;
    }

    /* The fragment owns its nodes, and the document holding them stays private */
    PyObject *doc_wrap = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (!doc_wrap) {
        if (list) xmlFreeNodeList(list);
        xmlFreeDoc(doc);
        return NULL;
    }
    ((LibXML_NodeObject *)doc_wrap)->hidden = 1;

    xmlNodePtr frag = xmlNewDocFragment(doc);
    if (!frag) {
        if (list) xmlFreeNodeList(list);
        Py_DECREF(doc_wrap);
        return PyErr_NoMemory();
    }
    xmlAddChildList(frag, list);

    PyObject *frag_wrap = LibXML_Node_Wrap(frag);
    if (!frag_wrap) xmlFreeNode(frag);
    Py_DECREF(doc_wrap);
    return frag_wrap;
}

static PyObject *
parser_parse_balanced_chunk(LibXML_ParserObject *self, PyObject *args)
{
    const char *chunk;
    if (!PyArg_ParseTuple(args, "s", &chunk)) return NULL;
    return LibXML_parse_chunk(chunk);
}

/* ------------------------------------------------------------------ */
/* Options accessors (get/set attributes on the parser object)        */
/* ------------------------------------------------------------------ */

#define PARSER_GETSET(name) \
static PyObject * \
parser_get_##name(LibXML_ParserObject *self, void *closure) { \
    return PyBool_FromLong(self->name); \
} \
static int \
parser_set_##name(LibXML_ParserObject *self, PyObject *value, void *closure) { \
    if (value == NULL) { \
        PyErr_SetString(PyExc_TypeError, "cannot delete parser option"); \
        return -1; \
    } \
    int truth = PyObject_IsTrue(value); \
    if (truth < 0) return -1; \
    self->name = truth; \
    return 0; \
}

PARSER_GETSET(recover)
PARSER_GETSET(no_blanks)
PARSER_GETSET(expand_entities)
PARSER_GETSET(load_ext_dtd)
PARSER_GETSET(validation)
PARSER_GETSET(no_network)
PARSER_GETSET(no_cdata)
PARSER_GETSET(huge)
PARSER_GETSET(pedantic)
PARSER_GETSET(suppress_errors)
PARSER_GETSET(suppress_warnings)
PARSER_GETSET(line_numbers)

#undef PARSER_GETSET

static PyGetSetDef LibXML_Parser_getset[] = {
    {"recover",           (getter)parser_get_recover,           (setter)parser_set_recover,           NULL, NULL},
    {"no_blanks",         (getter)parser_get_no_blanks,         (setter)parser_set_no_blanks,         NULL, NULL},
    {"expand_entities",   (getter)parser_get_expand_entities,   (setter)parser_set_expand_entities,   NULL, NULL},
    {"load_ext_dtd",      (getter)parser_get_load_ext_dtd,      (setter)parser_set_load_ext_dtd,      NULL, NULL},
    {"validation",        (getter)parser_get_validation,        (setter)parser_set_validation,        NULL, NULL},
    {"no_network",        (getter)parser_get_no_network,        (setter)parser_set_no_network,        NULL, NULL},
    {"no_cdata",          (getter)parser_get_no_cdata,          (setter)parser_set_no_cdata,          NULL, NULL},
    {"huge",              (getter)parser_get_huge,              (setter)parser_set_huge,              NULL, NULL},
    {"pedantic",          (getter)parser_get_pedantic,          (setter)parser_set_pedantic,          NULL, NULL},
    {"suppress_errors",   (getter)parser_get_suppress_errors,   (setter)parser_set_suppress_errors,   NULL, NULL},
    {"suppress_warnings", (getter)parser_get_suppress_warnings, (setter)parser_set_suppress_warnings, NULL, NULL},
    {"line_numbers",      (getter)parser_get_line_numbers,      (setter)parser_set_line_numbers,      NULL, NULL},
    {NULL}
};

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_Parser_methods[] = {
    {"parse_string",         (PyCFunction)parser_parse_string,         METH_VARARGS|METH_KEYWORDS, "Parse XML string"},
    {"parse_file",           (PyCFunction)parser_parse_file,           METH_VARARGS|METH_KEYWORDS, "Parse XML file"},
    {"load_xml",             (PyCFunction)parser_load_xml,             METH_VARARGS|METH_KEYWORDS, "Load XML (string= or location=)"},
    {"parse_html_string",    (PyCFunction)parser_parse_html_string,    METH_VARARGS|METH_KEYWORDS, "Parse HTML string"},
    {"parse_html_file",      (PyCFunction)parser_parse_html_file,      METH_VARARGS|METH_KEYWORDS, "Parse HTML file"},
    {"load_html",            (PyCFunction)parser_load_html,            METH_VARARGS|METH_KEYWORDS, "Load HTML (string= or location=)"},
    {"parse_balanced_chunk", (PyCFunction)parser_parse_balanced_chunk, METH_VARARGS,               "Parse a well-balanced XML fragment"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_ParserType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Parser",
    .tp_basicsize = sizeof(LibXML_ParserObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = LibXML_Parser_new,
    .tp_methods   = LibXML_Parser_methods,
    .tp_getset    = LibXML_Parser_getset,
    .tp_doc       = "XML/HTML parser",
};

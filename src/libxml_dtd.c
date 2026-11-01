#include "libxml_dtd.h"
#include "libxml_utils.h"
#include "libxml_tree.h"

/* A parsed DTD has no document. It gets a hidden one, like any other node. */
static PyObject *
wrap_parsed_dtd(PyTypeObject *type, xmlDtdPtr dtd)
{
    PyObject *doc = LibXML_new_hidden_doc();
    if (doc == NULL) {
        xmlFreeDtd(dtd);
        return NULL;
    }

    dtd->doc = NULL;
    xmlSetTreeDoc((xmlNodePtr)dtd, (xmlDocPtr)LibXML_NODE(doc));

    PyObject *result = LibXML_Node_Wrap_As((xmlNodePtr)dtd, type);
    if (result == NULL) xmlFreeDtd(dtd);
    Py_DECREF(doc);
    return result;
}

static PyObject *
parse_uri(PyTypeObject *type, const char *external, const char *system)
{
    LibXML_errors_begin();
    xmlDtdPtr dtd = xmlParseDTD(BAD_CAST external, BAD_CAST system);
    if (dtd == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    if (LibXML_report_errors(0) < 0) {
        xmlFreeDtd(dtd);
        return NULL;
    }
    return wrap_parsed_dtd(type, dtd);
}

/* Dtd(public_id, system_id): load it */
static PyObject *
dtd_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *external, *system;
    static char *kwlist[] = {"external", "system", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "zz", kwlist, &external, &system))
        return NULL;
    return parse_uri(type, external, system);
}

static PyObject *
dtd_parse_uri(PyObject *cls, PyObject *args, PyObject *kwds)
{
    const char *external, *system;
    static char *kwlist[] = {"external", "system", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "zz", kwlist, &external, &system))
        return NULL;
    return parse_uri((PyTypeObject *)cls, external, system);
}

/* Dtd.parse_string(text, encoding=None) */
static PyObject *
dtd_parse_string(PyObject *cls, PyObject *args, PyObject *kwds)
{
    const char *text, *encoding = NULL;
    Py_ssize_t size;
    static char *kwlist[] = {"string", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|z", kwlist, &text, &size, &encoding))
        return NULL;

    xmlCharEncoding enc = XML_CHAR_ENCODING_NONE;
    if (encoding != NULL) {
        enc = xmlParseCharEncoding(encoding);
        if (enc == XML_CHAR_ENCODING_ERROR) {
            PyErr_Format(LibXML_Error, "unknown encoding: %s", encoding);
            return NULL;
        }
    }

    LibXML_errors_begin();

    xmlParserInputBufferPtr buffer = xmlParserInputBufferCreateMem(text, (int)size, enc);
    if (buffer == NULL) return PyErr_NoMemory();

    /* xmlIOParseDTD frees the buffer. */
    xmlDtdPtr dtd = xmlIOParseDTD(NULL, buffer, enc);
    if (dtd == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    if (LibXML_report_errors(0) < 0) {
        xmlFreeDtd(dtd);
        return NULL;
    }
    return wrap_parsed_dtd((PyTypeObject *)cls, dtd);
}

static PyObject *
optional_string(const xmlChar *value)
{
    if (value == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)value);
}

static PyObject *
dtd_get_systemId(LibXML_NodeObject *self, void *closure)
{
    return optional_string(((xmlDtdPtr)self->node)->SystemID);
}

static PyObject *
dtd_get_publicId(LibXML_NodeObject *self, void *closure)
{
    return optional_string(((xmlDtdPtr)self->node)->ExternalID);
}

static PyObject *
dtd_getSystemId(LibXML_NodeObject *self, PyObject *args)
{
    return dtd_get_systemId(self, NULL);
}

static PyObject *
dtd_getPublicId(LibXML_NodeObject *self, PyObject *args)
{
    return dtd_get_publicId(self, NULL);
}

static PyMethodDef dtd_methods[] = {
    {"parse_string", (PyCFunction)dtd_parse_string, METH_VARARGS|METH_KEYWORDS|METH_CLASS, "Parse a DTD from a string"},
    {"parse_uri",    (PyCFunction)dtd_parse_uri,    METH_VARARGS|METH_KEYWORDS|METH_CLASS, "Load a DTD"},
    {"getSystemId",  (PyCFunction)dtd_getSystemId,  METH_NOARGS, "System identifier"},
    {"getPublicId",  (PyCFunction)dtd_getPublicId,  METH_NOARGS, "Public identifier"},
    {NULL, NULL}
};

static PyGetSetDef dtd_getset[] = {
    {"systemId", (getter)dtd_get_systemId, NULL, "System identifier", NULL},
    {"publicId", (getter)dtd_get_publicId, NULL, "Public identifier", NULL},
    {NULL}
};

PyTypeObject LibXML_DtdType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Dtd",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = dtd_new,
    .tp_methods   = dtd_methods,
    .tp_getset    = dtd_getset,
    .tp_doc       = "Document type definition",
};

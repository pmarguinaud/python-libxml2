#include "libxml_attr.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_document.h"

/* ------------------------------------------------------------------ */
/* Construction: Attr(name, value="")                                  */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Attr_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *name;
    const char *value = "";
    static char *kwlist[] = {"name", "value", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist, &name, &value))
        return NULL;

    PyObject *doc = LibXML_new_hidden_doc();
    if (doc == NULL) return NULL;

    xmlAttrPtr attr = xmlNewDocProp((xmlDocPtr)LibXML_NODE(doc), BAD_CAST name, NULL);
    if (attr == NULL) {
        Py_DECREF(doc);
        return PyErr_NoMemory();
    }
    LibXML_set_text((xmlNodePtr)attr, value);

    PyObject *result = LibXML_Node_Wrap_As((xmlNodePtr)attr, type);
    if (result == NULL) xmlFreeNode((xmlNodePtr)attr);
    Py_DECREF(doc);
    return result;
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static PyObject *
attr_get_value(LibXML_NodeObject *self, void *closure)
{
    xmlAttrPtr attr = (xmlAttrPtr)self->node;
    /* Children can mix text and entity references. */
    xmlChar *val = xmlNodeListGetString(attr->doc, attr->children, 1);
    if (!val) return PyUnicode_FromString("");
    PyObject *ret = PyUnicode_FromString((const char *)val);
    xmlFree(val);
    return ret;
}

static int
attr_set_value(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (LibXML_check_unlocked(self->node) < 0) return -1;
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "cannot delete attribute value");
        return -1;
    }
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    LibXML_set_text(self->node, s);
    return 0;
}

static PyObject *
attr_get_ownerElement(LibXML_NodeObject *self, void *closure)
{
    xmlAttrPtr attr = (xmlAttrPtr)self->node;
    if (!attr->parent) Py_RETURN_NONE;
    return LibXML_Node_Wrap(attr->parent);
}

static PyObject *
attr_get_name(LibXML_NodeObject *self, void *closure)
{
    xmlAttrPtr attr = (xmlAttrPtr)self->node;
    if (!attr->name) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)attr->name);
}

static PyObject *
attr_isId(LibXML_NodeObject *self, PyObject *args)
{
    xmlAttrPtr attr = (xmlAttrPtr)self->node;
    return PyBool_FromLong(xmlIsID(attr->doc, attr->parent, attr) == 1);
}

/* ------------------------------------------------------------------ */
/* setValue method                                                     */
/* ------------------------------------------------------------------ */

static PyObject *
attr_setValue(LibXML_NodeObject *self, PyObject *args)
{
    if (LibXML_check_unlocked(self->node) < 0) return NULL;
    PyObject *value;
    if (!PyArg_ParseTuple(args, "O", &value)) return NULL;
    if (attr_set_value(self, value, NULL) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
attr_getValue(LibXML_NodeObject *self, PyObject *args)
{
    return attr_get_value(self, NULL);
}

static PyObject *
attr_setNamespace(LibXML_NodeObject *self, PyObject *args)
{
    if (LibXML_check_unlocked(self->node) < 0) return NULL;
    const char *ns_uri = NULL, *prefix = NULL;
    if (!PyArg_ParseTuple(args, "z|z", &ns_uri, &prefix)) return NULL;

    xmlAttrPtr attr = (xmlAttrPtr)self->node;

    if (!ns_uri || ns_uri[0] == '\0') {
        attr->ns = NULL;
        return PyBool_FromLong(attr->parent != NULL);
    }

    /* As in Perl, the namespace must be declared already on the element. */
    if (attr->parent == NULL) Py_RETURN_FALSE;

    xmlNsPtr ns = xmlSearchNs(attr->doc, attr->parent, BAD_CAST prefix);
    if (ns == NULL || !xmlStrEqual(ns->href, BAD_CAST ns_uri))
        ns = xmlSearchNsByHref(attr->doc, attr->parent, BAD_CAST ns_uri);

    /* An attribute without a prefix has no namespace. */
    if (ns == NULL || ns->prefix == NULL) Py_RETURN_FALSE;

    attr->ns = ns;
    Py_RETURN_TRUE;
}

/* ------------------------------------------------------------------ */
/* serializeContent                                                    */
/* ------------------------------------------------------------------ */

static PyObject *
attr_serializeContent(LibXML_NodeObject *self, PyObject *args)
{
    xmlAttrPtr attr = (xmlAttrPtr)self->node;
    xmlChar *val = xmlNodeListGetString(attr->doc, attr->children, 0);
    if (!val) return PyUnicode_FromString("");
    PyObject *ret = PyUnicode_FromString((const char *)val);
    xmlFree(val);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Method/getset tables                                                */
/* ------------------------------------------------------------------ */

static PyGetSetDef LibXML_Attr_getset[] = {
    {"value",        (getter)attr_get_value,        (setter)attr_set_value, "Attribute value", NULL},
    {"ownerElement", (getter)attr_get_ownerElement,  NULL,                   "Owner element",   NULL},
    {"name",         (getter)attr_get_name,          NULL,                   "Attribute name",  NULL},
    {NULL}
};

static PyObject *
attr_getOwnerElement(LibXML_NodeObject *self, PyObject *args)
{
    return attr_get_ownerElement(self, NULL);
}

static PyMethodDef LibXML_Attr_methods[] = {
    {"getOwnerElement",  (PyCFunction)attr_getOwnerElement,  METH_NOARGS,  "Element of this attribute"},
    {"parentElement",    (PyCFunction)attr_getOwnerElement,  METH_NOARGS,  "Element of this attribute"},
    {"getValue",         (PyCFunction)attr_getValue,         METH_NOARGS,  "Get attribute value"},
    {"setValue",         (PyCFunction)attr_setValue,         METH_VARARGS, "Set attribute value"},
    {"setNamespace",     (PyCFunction)attr_setNamespace,     METH_VARARGS, "Set namespace on attribute"},
    {"isId",             (PyCFunction)attr_isId,             METH_NOARGS,  "Return True if this is an ID attribute"},
    {"serializeContent", (PyCFunction)attr_serializeContent, METH_NOARGS,  "Serialize attribute content (unescaped)"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_AttrType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Attr",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Attr_new,
    .tp_methods   = LibXML_Attr_methods,
    .tp_getset    = LibXML_Attr_getset,
    .tp_doc       = "DOM Attr node",
};

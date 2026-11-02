#include "libxml_namespace.h"

#define XMLNS_URI "http://www.w3.org/2000/xmlns/"

/* xmlCopyNamespace may refuse the built-in xml: namespace. */
static xmlNsPtr
copy_ns(const xmlChar *href, const xmlChar *prefix)
{
    xmlNsPtr copy = (xmlNsPtr)xmlMalloc(sizeof(xmlNs));
    if (copy == NULL) return NULL;
    memset(copy, 0, sizeof(xmlNs));
    copy->type = XML_LOCAL_NAMESPACE;
    if (href)   copy->href   = xmlStrdup(href);
    if (prefix) copy->prefix = xmlStrdup(prefix);
    return copy;
}

static PyObject *
wrap_ns(PyTypeObject *type, xmlNsPtr ns)
{
    LibXML_NamespaceObject *self = (LibXML_NamespaceObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        xmlFreeNs(ns);
        return NULL;
    }
    self->ns = ns;
    return (PyObject *)self;
}

PyObject *
LibXML_Namespace_Copy(xmlNsPtr orig)
{
    xmlNsPtr copy = copy_ns(orig->href, orig->prefix);
    if (copy == NULL) return PyErr_NoMemory();
    return wrap_ns(&LibXML_NamespaceType, copy);
}

static PyObject *
ns_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *uri, *prefix = NULL;
    static char *kwlist[] = {"namespaceURI", "prefix", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|z", kwlist, &uri, &prefix))
        return NULL;

    xmlNsPtr ns = copy_ns(BAD_CAST uri, BAD_CAST prefix);
    if (ns == NULL) return PyErr_NoMemory();
    return wrap_ns(type, ns);
}

static void
ns_dealloc(LibXML_NamespaceObject *self)
{
    if (self->ns) xmlFreeNs(self->ns);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
ns_uri(LibXML_NamespaceObject *self, void *closure)
{
    if (self->ns->href == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)self->ns->href);
}

static PyObject *
ns_declared_prefix(LibXML_NamespaceObject *self, void *closure)
{
    if (self->ns->prefix == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)self->ns->prefix);
}

static PyObject *
ns_node_name(LibXML_NamespaceObject *self, void *closure)
{
    if (self->ns->prefix == NULL || self->ns->prefix[0] == '\0')
        return PyUnicode_FromString("xmlns");
    return PyUnicode_FromFormat("xmlns:%s", (const char *)self->ns->prefix);
}

static PyObject *
ns_node_type(LibXML_NamespaceObject *self, void *closure)
{
    return PyLong_FromLong(XML_NAMESPACE_DECL);
}

static PyObject *
ns_prefix(LibXML_NamespaceObject *self, void *closure)
{
    return PyUnicode_FromString("xmlns");
}

static PyObject *
ns_namespace_uri(LibXML_NamespaceObject *self, void *closure)
{
    return PyUnicode_FromString(XMLNS_URI);
}

static PyObject *
ns_repr(LibXML_NamespaceObject *self)
{
    PyObject *name = ns_node_name(self, NULL);
    if (name == NULL) return NULL;
    PyObject *result = PyUnicode_FromFormat("<xml_libxml.Namespace %U='%s'>", name,
        self->ns->href ? (const char *)self->ns->href : "");
    Py_DECREF(name);
    return result;
}

/* Method forms, named as in Perl */
static PyObject *m_uri(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_uri(self, NULL); }
static PyObject *m_declared_prefix(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_declared_prefix(self, NULL); }
static PyObject *m_node_name(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_node_name(self, NULL); }
static PyObject *m_node_type(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_node_type(self, NULL); }
static PyObject *m_prefix(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_prefix(self, NULL); }
static PyObject *m_namespace_uri(LibXML_NamespaceObject *self, PyObject *args)
{ return ns_namespace_uri(self, NULL); }
static PyObject *m_namespaces(LibXML_NamespaceObject *self, PyObject *args)
{ return PyList_New(0); }

static int
ns_equal(LibXML_NamespaceObject *a, LibXML_NamespaceObject *b)
{
    return xmlStrEqual(a->ns->href, b->ns->href)
        && xmlStrEqual(a->ns->prefix, b->ns->prefix);
}

static PyObject *
m_is_equal(LibXML_NamespaceObject *self, PyObject *other)
{
    if (!PyObject_TypeCheck(other, &LibXML_NamespaceType))
        Py_RETURN_FALSE;
    return PyBool_FromLong(ns_equal(self, (LibXML_NamespaceObject *)other));
}

static PyObject *
m_is_same(LibXML_NamespaceObject *self, PyObject *other)
{
    return PyBool_FromLong(other == (PyObject *)self);
}

static PyObject *
m_unique_key(LibXML_NamespaceObject *self, PyObject *args)
{
    PyObject *uri = ns_uri(self, NULL);
    PyObject *prefix = ns_declared_prefix(self, NULL);
    PyObject *pair = (uri && prefix) ? PyTuple_Pack(2, uri, prefix) : NULL;
    Py_XDECREF(uri);
    Py_XDECREF(prefix);
    if (pair == NULL) return NULL;
    Py_hash_t hash = PyObject_Hash(pair);
    Py_DECREF(pair);
    if (hash == -1 && PyErr_Occurred()) return NULL;
    return PyLong_FromSsize_t(hash);
}

static PyMethodDef ns_methods[] = {
    {"getName",         (PyCFunction)m_node_name,       METH_NOARGS, "Declaration name, as xmlns:prefix"},
    {"getData",         (PyCFunction)m_uri,             METH_NOARGS, "Declared namespace URI"},
    {"getValue",        (PyCFunction)m_uri,             METH_NOARGS, "Declared namespace URI"},
    {"getType",         (PyCFunction)m_node_type,       METH_NOARGS, "XML_NAMESPACE_DECL"},
    {"getLocalName",    (PyCFunction)m_declared_prefix, METH_NOARGS, "Declared prefix"},
    {"getPrefix",       (PyCFunction)m_prefix,          METH_NOARGS, "Always xmlns"},
    {"getNamespaceURI", (PyCFunction)m_namespace_uri,   METH_NOARGS, "The xmlns namespace URI"},
    {"getNamespaces",   (PyCFunction)m_namespaces,      METH_NOARGS, "Always empty"},
    {"isEqualNode",     (PyCFunction)m_is_equal,        METH_O,      "Same prefix and URI"},
    {"isSameNode",      (PyCFunction)m_is_same,         METH_O,      "Same object"},
    {"unique_key",      (PyCFunction)m_unique_key,      METH_NOARGS, "Key shared by equal declarations"},
    {NULL, NULL}
};

static PyGetSetDef ns_getset[] = {
    {"nodeType",       (getter)ns_node_type,       NULL, "XML_NAMESPACE_DECL", NULL},
    {"nodeName",       (getter)ns_node_name,       NULL, "Declaration name, as xmlns:prefix", NULL},
    {"name",           (getter)ns_node_name,       NULL, "Declaration name, as xmlns:prefix", NULL},
    {"declaredURI",    (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"value",          (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"nodeValue",      (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"href",           (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"value2",         (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"textContent",    (getter)ns_uri,             NULL, "Declared namespace URI", NULL},
    {"declaredPrefix", (getter)ns_declared_prefix, NULL, "Declared prefix", NULL},
    {"localname",      (getter)ns_declared_prefix, NULL, "Declared prefix", NULL},
    {"localName",      (getter)ns_declared_prefix, NULL, "Declared prefix", NULL},
    {"prefix",         (getter)ns_prefix,          NULL, "Always xmlns", NULL},
    {"namespaceURI",   (getter)ns_namespace_uri,   NULL, "The xmlns namespace URI", NULL},
    {NULL}
};

PyTypeObject LibXML_NamespaceType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Namespace",
    .tp_basicsize = sizeof(LibXML_NamespaceObject),
    .tp_dealloc   = (destructor)ns_dealloc,
    .tp_repr      = (reprfunc)ns_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_new       = ns_new,
    .tp_methods   = ns_methods,
    .tp_getset    = ns_getset,
    .tp_doc       = "Namespace declaration",
};

#include "libxml_nodelist.h"
#include "libxml_utils.h"

/* ------------------------------------------------------------------ */
/* Allocation / deallocation                                           */
/* ------------------------------------------------------------------ */

static void
LibXML_NodeList_dealloc(LibXML_NodeListObject *self)
{
    if (self->nodes) {
        for (Py_ssize_t i = 0; i < self->length; i++)
            Py_XDECREF(self->nodes[i]);
        PyMem_Free(self->nodes);
        self->nodes = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* Factory helpers                                                     */
/* ------------------------------------------------------------------ */

PyObject *
LibXML_NodeList_FromNodeSet(xmlNodeSetPtr ns, PyObject *doc_obj)
{
    LibXML_NodeListObject *list =
        (LibXML_NodeListObject *)LibXML_NodeListType.tp_alloc(&LibXML_NodeListType, 0);
    if (!list) return NULL;

    Py_ssize_t count = (ns && ns->nodeNr > 0) ? ns->nodeNr : 0;
    list->length = count;
    if (count == 0) {
        list->nodes = NULL;
        return (PyObject *)list;
    }

    list->nodes = PyMem_Malloc(count * sizeof(PyObject *));
    if (!list->nodes) {
        Py_DECREF(list);
        return PyErr_NoMemory();
    }

    for (Py_ssize_t i = 0; i < count; i++) {
        xmlNodePtr n = ns->nodeTab[i];
        /* Namespace nodes from XPath are temporary copies freed with the
         * XPath result object, so each wrapper needs its own copy. */
        if (n && n->type == XML_NAMESPACE_DECL)
            list->nodes[i] = LibXML_Ns_WrapCopy((xmlNsPtr)n, doc_obj);
        else
            list->nodes[i] = LibXML_Node_Wrap(n, doc_obj);
        if (!list->nodes[i]) {
            /* Clear what we've set so far */
            for (Py_ssize_t j = 0; j < i; j++) Py_XDECREF(list->nodes[j]);
            PyMem_Free(list->nodes);
            list->nodes = NULL;
            Py_DECREF(list);
            return NULL;
        }
    }
    return (PyObject *)list;
}

PyObject *
LibXML_NodeList_FromNodePtr(xmlNodePtr first, PyObject *doc_obj)
{
    /* Count */
    Py_ssize_t count = 0;
    xmlNodePtr n = first;
    while (n) { count++; n = n->next; }

    LibXML_NodeListObject *list =
        (LibXML_NodeListObject *)LibXML_NodeListType.tp_alloc(&LibXML_NodeListType, 0);
    if (!list) return NULL;

    list->length = count;
    if (count == 0) {
        list->nodes = NULL;
        return (PyObject *)list;
    }

    list->nodes = PyMem_Malloc(count * sizeof(PyObject *));
    if (!list->nodes) {
        Py_DECREF(list);
        return PyErr_NoMemory();
    }

    n = first;
    for (Py_ssize_t i = 0; i < count; i++) {
        list->nodes[i] = LibXML_Node_Wrap(n, doc_obj);
        if (!list->nodes[i]) {
            for (Py_ssize_t j = 0; j < i; j++) Py_XDECREF(list->nodes[j]);
            PyMem_Free(list->nodes);
            list->nodes = NULL;
            Py_DECREF(list);
            return NULL;
        }
        n = n->next;
    }
    return (PyObject *)list;
}

/* ------------------------------------------------------------------ */
/* Sequence protocol                                                   */
/* ------------------------------------------------------------------ */

static Py_ssize_t
nodelist_length(LibXML_NodeListObject *self)
{
    return self->length;
}

static PyObject *
nodelist_item(LibXML_NodeListObject *self, Py_ssize_t i)
{
    if (i < 0) i += self->length;
    if (i < 0 || i >= self->length) {
        PyErr_SetString(PyExc_IndexError, "NodeList index out of range");
        return NULL;
    }
    Py_INCREF(self->nodes[i]);
    return self->nodes[i];
}

static PySequenceMethods nodelist_seq = {
    .sq_length = (lenfunc)nodelist_length,
    .sq_item   = (ssizeargfunc)nodelist_item,
};

/* ------------------------------------------------------------------ */
/* Iterator                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    LibXML_NodeListObject *list;
    Py_ssize_t pos;
} NodeListIterObject;

static void
nodelist_iter_dealloc(NodeListIterObject *self)
{
    Py_XDECREF(self->list);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
nodelist_iter_next(NodeListIterObject *self)
{
    if (self->pos >= self->list->length) return NULL;
    PyObject *item = self->list->nodes[self->pos++];
    Py_INCREF(item);
    return item;
}

static PyTypeObject NodeListIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.NodeListIterator",
    .tp_basicsize = sizeof(NodeListIterObject),
    .tp_dealloc   = (destructor)nodelist_iter_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_iternext  = (iternextfunc)nodelist_iter_next,
    .tp_iter      = PyObject_SelfIter,
};

static PyObject *
nodelist_iter(LibXML_NodeListObject *self)
{
    NodeListIterObject *iter =
        (NodeListIterObject *)NodeListIterType.tp_alloc(&NodeListIterType, 0);
    if (!iter) return NULL;
    Py_INCREF(self);
    iter->list = self;
    iter->pos  = 0;
    return (PyObject *)iter;
}

/* ------------------------------------------------------------------ */
/* Methods                                                             */
/* ------------------------------------------------------------------ */

static PyObject *
nodelist_size(LibXML_NodeListObject *self, PyObject *args)
{
    return PyLong_FromSsize_t(self->length);
}

static PyObject *
nodelist_get_node(LibXML_NodeListObject *self, PyObject *args)
{
    Py_ssize_t pos;
    if (!PyArg_ParseTuple(args, "n", &pos)) return NULL;
    /* get_node is 1-based in Perl, but we'll accept 0-based for Python */
    if (pos < 0 || pos >= self->length) {
        PyErr_SetString(PyExc_IndexError, "NodeList index out of range");
        return NULL;
    }
    Py_INCREF(self->nodes[pos]);
    return self->nodes[pos];
}

static PyObject *
nodelist_item_method(LibXML_NodeListObject *self, PyObject *args)
{
    Py_ssize_t pos;
    if (!PyArg_ParseTuple(args, "n", &pos)) return NULL;
    if (pos < 0 || pos >= self->length) Py_RETURN_NONE;
    Py_INCREF(self->nodes[pos]);
    return self->nodes[pos];
}

static PyObject *
nodelist_get_nodelist(LibXML_NodeListObject *self, PyObject *args)
{
    /* Return a plain Python list of all nodes */
    PyObject *list = PyList_New(self->length);
    for (Py_ssize_t i = 0; i < self->length; i++) {
        Py_INCREF(self->nodes[i]);
        PyList_SET_ITEM(list, i, self->nodes[i]);
    }
    return list;
}

static PyObject *
nodelist_string_value(LibXML_NodeListObject *self, PyObject *args)
{
    /* Return text content of the FIRST node (XPath string-value of nodeset) */
    if (self->length == 0)
        return PyUnicode_FromString("");
    PyObject *tc = PyObject_GetAttrString(self->nodes[0], "textContent");
    if (!tc) { PyErr_Clear(); return PyUnicode_FromString(""); }
    return tc;
}

static PyObject *
nodelist_to_literal(LibXML_NodeListObject *self, PyObject *args)
{
    /* Concatenate text content of ALL nodes */
    PyObject *parts = PyList_New(0);
    for (Py_ssize_t i = 0; i < self->length; i++) {
        PyObject *tc = PyObject_GetAttrString(self->nodes[i], "textContent");
        if (tc) { PyList_Append(parts, tc); Py_DECREF(tc); }
        else { PyErr_Clear(); }
    }
    PyObject *sep    = PyUnicode_FromString("");
    PyObject *result = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    Py_DECREF(parts);
    return result;
}

static PyObject *
nodelist_pop(LibXML_NodeListObject *self, PyObject *args)
{
    if (self->length == 0) Py_RETURN_NONE;
    self->length--;
    PyObject *item = self->nodes[self->length];
    self->nodes[self->length] = NULL;
    return item;  /* caller owns ref */
}

static PyObject *
nodelist_shift(LibXML_NodeListObject *self, PyObject *args)
{
    if (self->length == 0) Py_RETURN_NONE;
    PyObject *item = self->nodes[0];
    memmove(self->nodes, self->nodes + 1,
            (self->length - 1) * sizeof(PyObject *));
    self->length--;
    return item;
}

static PyObject *
nodelist_push(LibXML_NodeListObject *self, PyObject *args)
{
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;
    PyObject **new_nodes = PyMem_Realloc(self->nodes,
        (self->length + 1) * sizeof(PyObject *));
    if (!new_nodes) return PyErr_NoMemory();
    self->nodes = new_nodes;
    Py_INCREF(obj);
    self->nodes[self->length++] = obj;
    Py_RETURN_NONE;
}

static PyObject *
nodelist_unshift(LibXML_NodeListObject *self, PyObject *args)
{
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;
    PyObject **new_nodes = PyMem_Realloc(self->nodes,
        (self->length + 1) * sizeof(PyObject *));
    if (!new_nodes) return PyErr_NoMemory();
    self->nodes = new_nodes;
    memmove(self->nodes + 1, self->nodes, self->length * sizeof(PyObject *));
    Py_INCREF(obj);
    self->nodes[0] = obj;
    self->length++;
    Py_RETURN_NONE;
}

static PyObject *
nodelist_append(LibXML_NodeListObject *self, PyObject *args)
{
    LibXML_NodeListObject *other;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeListType, &other)) return NULL;
    Py_ssize_t new_len = self->length + other->length;
    PyObject **new_nodes = PyMem_Realloc(self->nodes,
        new_len * sizeof(PyObject *));
    if (!new_nodes) return PyErr_NoMemory();
    self->nodes = new_nodes;
    for (Py_ssize_t i = 0; i < other->length; i++) {
        Py_INCREF(other->nodes[i]);
        self->nodes[self->length + i] = other->nodes[i];
    }
    self->length = new_len;
    Py_RETURN_NONE;
}

static PyObject *
nodelist_reverse(LibXML_NodeListObject *self, PyObject *args)
{
    /* Return new reversed NodeList */
    LibXML_NodeListObject *result =
        (LibXML_NodeListObject *)LibXML_NodeListType.tp_alloc(&LibXML_NodeListType, 0);
    if (!result) return NULL;
    result->length = self->length;
    result->nodes = self->length ?
        PyMem_Malloc(self->length * sizeof(PyObject *)) : NULL;
    if (self->length && !result->nodes) {
        Py_DECREF(result); return PyErr_NoMemory();
    }
    for (Py_ssize_t i = 0; i < self->length; i++) {
        Py_INCREF(self->nodes[self->length - 1 - i]);
        result->nodes[i] = self->nodes[self->length - 1 - i];
    }
    return (PyObject *)result;
}

static PyMethodDef LibXML_NodeList_methods[] = {
    {"size",          (PyCFunction)nodelist_size,          METH_NOARGS,  "Return number of nodes"},
    {"get_node",      (PyCFunction)nodelist_get_node,      METH_VARARGS, "Get node at index (0-based)"},
    {"item",          (PyCFunction)nodelist_item_method,   METH_VARARGS, "Get node at index (0-based)"},
    {"get_nodelist",  (PyCFunction)nodelist_get_nodelist,  METH_NOARGS,  "Return Python list of nodes"},
    {"string_value",  (PyCFunction)nodelist_string_value,  METH_NOARGS,  "Concatenate text content of all nodes"},
    {"to_literal",    (PyCFunction)nodelist_to_literal,    METH_NOARGS,  "Concatenate text content (alias)"},
    {"pop",           (PyCFunction)nodelist_pop,           METH_NOARGS,  "Remove and return last node"},
    {"shift",         (PyCFunction)nodelist_shift,         METH_NOARGS,  "Remove and return first node"},
    {"push",          (PyCFunction)nodelist_push,          METH_VARARGS, "Append a node"},
    {"unshift",       (PyCFunction)nodelist_unshift,       METH_VARARGS, "Prepend a node"},
    {"append",        (PyCFunction)nodelist_append,        METH_VARARGS, "Append another NodeList"},
    {"reverse",       (PyCFunction)nodelist_reverse,       METH_NOARGS,  "Return reversed NodeList"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_NodeListType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.NodeList",
    .tp_basicsize = sizeof(LibXML_NodeListObject),
    .tp_dealloc   = (destructor)LibXML_NodeList_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_iter      = (getiterfunc)nodelist_iter,
    .tp_as_sequence = &nodelist_seq,
    .tp_methods   = LibXML_NodeList_methods,
    .tp_doc       = "Ordered list of DOM nodes",
};

int
LibXML_NodeList_Init(void)
{
    return PyType_Ready(&NodeListIterType);
}

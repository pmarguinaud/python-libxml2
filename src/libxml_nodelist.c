#include "libxml_nodelist.h"
#include "libxml_utils.h"
#include "libxml_namespace.h"

static PyObject *nodelist_class = NULL;

void
LibXML_NodeList_Init(PyObject *cls)
{
    nodelist_class = cls;
}

/* Takes the reference to items. */
static PyObject *
make_nodelist(PyObject *items)
{
    PyObject *result = PyObject_CallFunctionObjArgs(nodelist_class, items, NULL);
    Py_DECREF(items);
    return result;
}

PyObject *
LibXML_NodeList_FromNodeSet(xmlNodeSetPtr ns)
{
    Py_ssize_t count = ns ? ns->nodeNr : 0;
    PyObject *items = PyList_New(count);
    if (items == NULL) return NULL;

    for (Py_ssize_t i = 0; i < count; i++) {
        xmlNodePtr n = ns->nodeTab[i];
        /* Namespace nodes from XPath are temporary copies freed with the
         * XPath result object. */
        PyObject *item = n->type == XML_NAMESPACE_DECL
            ? LibXML_Namespace_Copy((xmlNsPtr)n) : LibXML_Node_Wrap(n);
        if (item == NULL) {
            Py_DECREF(items);
            return NULL;
        }
        PyList_SET_ITEM(items, i, item);
    }
    return make_nodelist(items);
}

PyObject *
LibXML_NodeList_FromNodePtr(xmlNodePtr first)
{
    PyObject *items = PyList_New(0);
    if (items == NULL) return NULL;

    for (xmlNodePtr n = first; n; n = n->next) {
        PyObject *item = LibXML_Node_Wrap(n);
        if (item == NULL || PyList_Append(items, item) < 0) {
            Py_XDECREF(item);
            Py_DECREF(items);
            return NULL;
        }
        Py_DECREF(item);
    }
    return make_nodelist(items);
}

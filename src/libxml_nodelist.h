#ifndef LIBXML_NODELIST_H
#define LIBXML_NODELIST_H

#include "libxml_module.h"
#include <libxml/xpath.h>

/*
 * NodeList: a snapshot sequence of Python node wrappers.
 * NOT a subtype of Node.
 */
typedef struct {
    PyObject_HEAD
    PyObject   **nodes;     /* array of strong refs to node wrappers */
    Py_ssize_t   length;
} LibXML_NodeListObject;

extern PyTypeObject LibXML_NodeListType;

/* Build a NodeList from an xmlNodeSetPtr (XPath nodeset result) */
PyObject *LibXML_NodeList_FromNodeSet(xmlNodeSetPtr ns);

/* Build a NodeList from a linked list of xmlNodePtr (children, siblings) */
PyObject *LibXML_NodeList_FromNodePtr(xmlNodePtr first);

#define LibXML_NodeList_Check(op)  PyObject_TypeCheck((op), &LibXML_NodeListType)

/* Must be called before NodeList can be iterated (initializes NodeListIterType) */
int LibXML_NodeList_Init(void);

#endif /* LIBXML_NODELIST_H */

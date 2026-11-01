#ifndef LIBXML_NODELIST_H
#define LIBXML_NODELIST_H

#include "libxml_module.h"
#include <libxml/xpath.h>

/* NodeList is a Python class, in _xpath.py. It is a list, not a Node. */

/* Build a NodeList from a node set. NULL gives an empty list. */
PyObject *LibXML_NodeList_FromNodeSet(xmlNodeSetPtr ns);

/* Build a NodeList from a chain of siblings */
PyObject *LibXML_NodeList_FromNodePtr(xmlNodePtr first);

/* Give the class to use. Takes the reference. */
void LibXML_NodeList_Init(PyObject *cls);

#endif /* LIBXML_NODELIST_H */

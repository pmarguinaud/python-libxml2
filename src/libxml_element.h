#ifndef LIBXML_ELEMENT_H
#define LIBXML_ELEMENT_H

#include "libxml_node.h"

extern PyTypeObject LibXML_ElementType;

#define LibXML_Element_Check(op)  PyObject_TypeCheck((op), &LibXML_ElementType)

/* Elements below top, as a NodeList. kind: 0 qualified name, 1 local name,
 * 2 namespace and local name. "*" matches any name or namespace. */
PyObject *LibXML_find_elements(xmlNodePtr top, int kind, const char *name, const char *uri);

#endif /* LIBXML_ELEMENT_H */

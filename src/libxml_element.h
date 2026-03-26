#ifndef LIBXML_ELEMENT_H
#define LIBXML_ELEMENT_H

#include "libxml_node.h"

extern PyTypeObject LibXML_ElementType;

#define LibXML_Element_Check(op)  PyObject_TypeCheck((op), &LibXML_ElementType)

#endif /* LIBXML_ELEMENT_H */

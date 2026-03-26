#ifndef LIBXML_ATTR_H
#define LIBXML_ATTR_H

#include "libxml_node.h"

extern PyTypeObject LibXML_AttrType;

#define LibXML_Attr_Check(op)  PyObject_TypeCheck((op), &LibXML_AttrType)

#endif /* LIBXML_ATTR_H */

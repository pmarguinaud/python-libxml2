#ifndef LIBXML_DOCUMENT_H
#define LIBXML_DOCUMENT_H

#include "libxml_node.h"

/* Document is a Node whose ->node is cast to xmlDocPtr */
#define LibXML_DOC(obj)   ((xmlDocPtr)(((LibXML_NodeObject *)(obj))->node))

extern PyTypeObject LibXML_DocumentType;

#define LibXML_Document_Check(op)  PyObject_TypeCheck((op), &LibXML_DocumentType)

#endif /* LIBXML_DOCUMENT_H */

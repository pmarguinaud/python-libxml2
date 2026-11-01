#ifndef LIBXML_NODE_H
#define LIBXML_NODE_H

#include "libxml_module.h"

/*
 * Base struct for all node wrappers.
 * Document, Element, Text, Comment, CDATASection, Attr, PI all use this same struct.
 * They differ only in their PyTypeObject (with tp_base = &LibXML_NodeType).
 */
typedef struct {
    PyObject_HEAD
    xmlNodePtr  node;      /* the wrapped libxml2 node (xmlDocPtr cast as needed) */
    PyObject   *doc_obj;   /* strong ref to the Document wrapper; NULL if this IS the document */
    PyObject   *owner;     /* strong ref to the root of this tree; NULL if this is a root */
    int         hidden;    /* Document only: holds nodes created without a document */
} LibXML_NodeObject;

extern PyTypeObject LibXML_NodeType;
extern PyTypeObject LibXML_DocumentFragmentType;

/* Convenience accessors */
#define LibXML_NODE(obj)     (((LibXML_NodeObject *)(obj))->node)
#define LibXML_DOCOBJ(obj)   (((LibXML_NodeObject *)(obj))->doc_obj)
#define LibXML_DOC_FROM_NODE(obj) (LibXML_NODE(obj)->doc)

/* Type check macros */
#define LibXML_Node_Check(op)  PyObject_TypeCheck((op), &LibXML_NodeType)

/* GC helper (shared with subtypes) */
int LibXML_Node_traverse(LibXML_NodeObject *self, visitproc visit, void *arg);

#endif /* LIBXML_NODE_H */

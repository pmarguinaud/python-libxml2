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
    PyObject   *doc_obj;   /* strong ref to LibXML_DocumentObject; NULL if this IS the document */
    int         owned;     /* 1 = we own the libxml2 node memory (unlinked standalone tree) */
} LibXML_NodeObject;

extern PyTypeObject LibXML_NodeType;
extern PyTypeObject LibXML_DocumentFragmentType;

/* Convenience accessors */
#define LibXML_NODE(obj)     (((LibXML_NodeObject *)(obj))->node)
#define LibXML_DOCOBJ(obj)   (((LibXML_NodeObject *)(obj))->doc_obj)
#define LibXML_DOC_FROM_NODE(obj) \
    ((xmlDocPtr)(LibXML_DOCOBJ(obj) ? LibXML_NODE(LibXML_DOCOBJ(obj)) : LibXML_NODE(obj)))

/* Type check macros */
#define LibXML_Node_Check(op)  PyObject_TypeCheck((op), &LibXML_NodeType)

/* GC helpers (shared with subtypes) */
int LibXML_Node_traverse(LibXML_NodeObject *self, visitproc visit, void *arg);
int LibXML_Node_clear(LibXML_NodeObject *self);

#endif /* LIBXML_NODE_H */

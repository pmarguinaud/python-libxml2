#ifndef LIBXML_UTILS_H
#define LIBXML_UTILS_H

#include "libxml_module.h"
#include "libxml_node.h"

/*
 * node->_private always points to one of these structs (or NULL).
 * The struct is heap-allocated lazily on first wrap or cargo set.
 * It is freed when both wrapper and cargo are NULL.
 *
 * Never used for XML_NAMESPACE_DECL nodes (xmlNs layout differs).
 */
typedef struct {
    PyObject *wrapper;  /* Python Node wrapper for this node, or NULL */
    PyObject *cargo;    /* User-supplied Python object, or NULL */
} LibXML_NodePrivate;

/*
 * Wrap an xmlNodePtr into the appropriate Python type.
 * Reuses existing wrapper via priv->wrapper cache.
 * doc_obj: strong ref to the owning Document Python object (NULL if wrapping a doc itself).
 */
PyObject *LibXML_Node_Wrap(xmlNodePtr node, PyObject *doc_obj);

/* Node deregister callback — install via xmlDeregisterNodeDefault() at module init */
void LibXML_node_deregister(xmlNodePtr node);

/* Clear _private structs recursively (used before xmlFreeDoc) */
void LibXML_clear_private_recursive(xmlNodePtr node);

/* Same but skips the root node itself (used by Document dealloc) */
void LibXML_clear_private_children(xmlNodePtr root);

/* Update doc_obj refs recursively when a subtree moves to a new document */
void LibXML_update_doc_refs(xmlNodePtr node, PyObject *new_doc_obj);

/* Raise a Python exception from the last libxml2 error */
void LibXML_raise_last_error(void);

/* Structured error handler — install at module init */
void LibXML_error_handler(void *ctx, xmlErrorPtr error);

#endif /* LIBXML_UTILS_H */

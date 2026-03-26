#ifndef LIBXML_UTILS_H
#define LIBXML_UTILS_H

#include "libxml_module.h"
#include "libxml_node.h"

/*
 * Wrap an xmlNodePtr into the appropriate Python type.
 * Reuses existing wrapper via node->_private cache.
 * doc_obj: strong ref to the owning Document Python object (NULL if wrapping a doc itself).
 */
PyObject *LibXML_Node_Wrap(xmlNodePtr node, PyObject *doc_obj);

/* Clear _private pointers recursively (used before xmlFreeDoc) */
void LibXML_clear_private_recursive(xmlNodePtr node);

/* Update doc_obj refs recursively when a subtree moves to a new document */
void LibXML_update_doc_refs(xmlNodePtr node, PyObject *new_doc_obj);

/* Raise a Python exception from the last libxml2 error */
void LibXML_raise_last_error(void);

/* Structured error handler — install at module init */
void LibXML_error_handler(void *ctx, xmlErrorPtr error);

#endif /* LIBXML_UTILS_H */

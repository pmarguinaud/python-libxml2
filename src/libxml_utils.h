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
 * Wrap a node. Reuses the live wrapper if there is one. The ownership rules
 * are in libxml_tree.h. A namespace declaration is not a node: use
 * LibXML_Namespace_Copy.
 */
PyObject *LibXML_Node_Wrap(xmlNodePtr node);

/* New hidden document, for nodes created without one. Returns its wrapper. */
PyObject *LibXML_new_hidden_doc(void);

/* Raise a Python exception from the last libxml2 error */
void LibXML_raise_last_error(void);

/* Structured error handler — install at module init.
 * libxml2 made the error argument const in 2.12. */
#if LIBXML_VERSION >= 21200
#define LIBXML_CONST_ERROR const
#else
#define LIBXML_CONST_ERROR
#endif
void LibXML_error_handler(void *ctx, LIBXML_CONST_ERROR xmlError *error);

#endif /* LIBXML_UTILS_H */

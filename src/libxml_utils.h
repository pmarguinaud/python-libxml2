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
    xmlNodePtr old_dtds;  /* Document only: DTDs that left it, chained by next */
} LibXML_NodePrivate;

/*
 * Wrap a node. Reuses the live wrapper if there is one. The ownership rules
 * are in libxml_tree.h. A namespace declaration is not a node: use
 * LibXML_Namespace_Copy.
 */
PyObject *LibXML_Node_Wrap(xmlNodePtr node);

/* Same, with a wrapper of this type, for constructors called on a subclass. */
PyObject *LibXML_Node_Wrap_As(xmlNodePtr node, PyTypeObject *type);

/* New hidden document, for nodes created without one. Returns its wrapper. */
PyObject *LibXML_new_hidden_doc(void);

/* Check an element or attribute name. Returns 0, or -1 with an exception set. */
int LibXML_check_name(const char *name);

/*
 * Errors of libxml2 are collected per Python thread, as a callback can let
 * another thread run in the middle of a parse.
 */

/* Start an operation that can report errors. Forgets earlier ones. */
void LibXML_errors_begin(void);

/* End it. With errors: recover 0 raises and returns -1, 1 warns, 2 ignores them. */
int LibXML_report_errors(int recover);

/* Raise what was reported, or a generic error if nothing was. */
void LibXML_raise_last_error(void);

/* Structured error handler. libxml2 made the error argument const in 2.12. */
#if LIBXML_VERSION >= 21200
#define LIBXML_CONST_ERROR const
#else
#define LIBXML_CONST_ERROR
#endif
void LibXML_error_handler(void *ctx, LIBXML_CONST_ERROR xmlError *error);

/* Load the Error class. Called once, at module init. */
int LibXML_errors_init(void);

#endif /* LIBXML_UTILS_H */

#ifndef LIBXML_XPATH_H
#define LIBXML_XPATH_H

#include "libxml_module.h"
#include "libxml_node.h"

typedef struct {
    PyObject_HEAD
    xmlXPathContextPtr ctx;
    PyObject *node;         /* the context node, kept alive; NULL if none */
    PyObject *functions;    /* {(name, uri): callable} */
    PyObject *var_func;     /* variable lookup function, or NULL */
    PyObject *var_data;     /* its first argument */
    PyObject *pool;         /* what results of callbacks need until the end */
    int depth;              /* evaluations running, as callbacks can start some */
} LibXML_XPathContextObject;

extern PyTypeObject LibXML_XPathContextType;
extern PyTypeObject LibXML_XPathExpressionType;

/* What an evaluation gives back */
typedef enum {
    LIBXML_XP_NODES,    /* a NodeList, empty if the result is no node set */
    LIBXML_XP_FIND,     /* the result as NodeList, Literal, Number or Boolean */
    LIBXML_XP_VALUE,    /* the result as a string */
    LIBXML_XP_EXISTS,   /* the result as a bool */
    LIBXML_XP_BOOL      /* the result as a Boolean */
} LibXML_XPathMode;

/* Evaluate expr, a str or XPathExpression, with node as context. */
PyObject *LibXML_node_xpath(LibXML_NodeObject *node, PyObject *expr, LibXML_XPathMode mode);

/* Canonical XML of a node, or of what xpath selects. mode is an xmlC14NMode,
 * prefixes None or a list, and xpc None or an XPathContext. */
PyObject *LibXML_node_c14n(LibXML_NodeObject *node, int comments, PyObject *xpath,
                           int mode, PyObject *prefixes, PyObject *xpc);

/* Make a Literal, Number from text. Takes no reference. */
PyObject *LibXML_new_literal(PyObject *text);
PyObject *LibXML_new_number(PyObject *text);

/* Load the classes of _xpath.py and add them to the module. */
int LibXML_xpath_init(PyObject *module);

#endif /* LIBXML_XPATH_H */

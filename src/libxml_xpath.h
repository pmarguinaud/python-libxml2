#ifndef LIBXML_XPATH_H
#define LIBXML_XPATH_H

#include "libxml_module.h"

typedef struct {
    PyObject_HEAD
    xmlXPathContextPtr ctx;      /* the libxml2 XPath context (may be NULL) */
    PyObject          *doc_obj;  /* strong ref to Document, keeps it alive */
    PyObject          *ns_dict;  /* Python dict {prefix: uri} for pending/current ns */
} LibXML_XPathContextObject;

extern PyTypeObject LibXML_XPathContextType;

/*
 * Evaluate an XPath expression on ctx with optional context node.
 * Returns the raw xmlXPathObjectPtr; caller must xmlXPathFreeObject().
 * On error, sets a Python exception and returns NULL.
 */
xmlXPathObjectPtr LibXML_xpath_eval(xmlXPathContextPtr ctx, const char *expr);

/*
 * Convert an xmlXPathObjectPtr to a Python object:
 *   nodeset → LibXML_NodeListObject
 *   boolean → PyBool
 *   number  → PyFloat
 *   string  → PyUnicode
 */
PyObject *LibXML_xpath_to_python(xmlXPathObjectPtr xobj, PyObject *doc_obj);

#endif /* LIBXML_XPATH_H */

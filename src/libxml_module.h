#ifndef LIBXML_MODULE_H
#define LIBXML_MODULE_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlerror.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/c14n.h>
#include <libxml/xmlsave.h>

/* The module-level Error exception class */
extern PyObject *LibXML_Error;

#endif /* LIBXML_MODULE_H */

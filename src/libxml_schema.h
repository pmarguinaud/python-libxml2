#ifndef LIBXML_SCHEMA_H
#define LIBXML_SCHEMA_H

#include "libxml_module.h"
#include <libxml/pattern.h>

typedef struct {
    PyObject_HEAD
    xmlPatternPtr pattern;
} LibXML_PatternObject;

extern PyTypeObject LibXML_RelaxNGType;

#include <libxml/relaxng.h>
#include <libxml/xmlschemas.h>

/* The compiled schema of a RelaxNG or Schema object, or NULL for another object */
xmlRelaxNGPtr LibXML_relaxng_ptr(PyObject *obj);
xmlSchemaPtr LibXML_schema_ptr(PyObject *obj);
extern PyTypeObject LibXML_SchemaType;
extern PyTypeObject LibXML_PatternType;
extern PyTypeObject LibXML_RegExpType;

#endif /* LIBXML_SCHEMA_H */

#ifndef LIBXML_SCHEMA_H
#define LIBXML_SCHEMA_H

#include "libxml_module.h"
#include <libxml/pattern.h>

typedef struct {
    PyObject_HEAD
    xmlPatternPtr pattern;
} LibXML_PatternObject;

extern PyTypeObject LibXML_RelaxNGType;
extern PyTypeObject LibXML_SchemaType;
extern PyTypeObject LibXML_PatternType;
extern PyTypeObject LibXML_RegExpType;

#endif /* LIBXML_SCHEMA_H */

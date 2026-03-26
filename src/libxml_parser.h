#ifndef LIBXML_PARSER_H
#define LIBXML_PARSER_H

#include "libxml_module.h"

typedef struct {
    PyObject_HEAD
    int recover;
    int no_blanks;
    int expand_entities;
    int load_ext_dtd;
    int validation;
    int no_network;
    int no_cdata;
    int huge;
    int pedantic;
    int suppress_errors;
    int suppress_warnings;
    int line_numbers;
} LibXML_ParserObject;

extern PyTypeObject LibXML_ParserType;

/* Build libxml2 parser options int from a ParserObject */
int LibXML_parser_options(LibXML_ParserObject *self);

#endif /* LIBXML_PARSER_H */

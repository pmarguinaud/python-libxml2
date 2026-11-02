#ifndef LIBXML_PARSER_H
#define LIBXML_PARSER_H

#include "libxml_module.h"

typedef struct {
    PyObject_HEAD
    int flags;          /* XML_PARSE_* options */
    int recover;        /* 0: errors raise, 1: warn, 2: silent */
    PyObject *extra;    /* line_numbers, base_uri, gdome, ext_ent_handler */
} LibXML_ParserObject;

extern PyTypeObject LibXML_ParserType;

/* doc.process_xinclude(options): options is a dict or NULL. */
PyObject *LibXML_process_xinclude(PyObject *doc, PyObject *options);

/* Parse a well-balanced chunk into a DocumentFragment. */
PyObject *LibXML_parse_chunk(const char *chunk, int keep_blanks);

#endif /* LIBXML_PARSER_H */

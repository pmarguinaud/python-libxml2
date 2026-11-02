#ifndef LIBXML_PARSER_H
#define LIBXML_PARSER_H

#include "libxml_module.h"

typedef struct {
    PyObject_HEAD
    int flags;          /* XML_PARSE_* options */
    int recover;        /* 0: errors raise, 1: warn, 2: silent */
    PyObject *extra;    /* line_numbers, base_uri, gdome, ext_ent_handler, callbacks */
    xmlParserCtxtPtr push_ctxt;     /* the push parse in progress, or NULL */
    int push_busy;                  /* libxml2 is working on it, and a callback runs */
    xmlChar *push_declared;         /* its declared encoding, if it was given as str */
    struct LibXML_SaxState *push_sax;   /* its SAX state, when it has a handler */
} LibXML_ParserObject;

extern PyTypeObject LibXML_ParserType;

/* Parser flags from a dict of options, from the defaults of Parser. Options that
 * are not flags go to extra. Applies what Parser does before calling libxml2.
 * Returns 0, or -1 with an exception. */
int LibXML_parser_options(PyObject *options, int *flags, int *recover, PyObject *extra);

/* doc.process_xinclude(options): options is a dict or NULL. */
PyObject *LibXML_process_xinclude(PyObject *doc, PyObject *options);

/* Parse a well-balanced chunk into a DocumentFragment. */
PyObject *LibXML_parse_chunk(const char *chunk, int keep_blanks);

#endif /* LIBXML_PARSER_H */

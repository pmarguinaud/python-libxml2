#ifndef LIBXML_SAX_H
#define LIBXML_SAX_H

#include "libxml_module.h"

/* What a SAX parse needs: the handler, the locator, and the namespaces each open
 * element declared. Perl: perl-libxml-sax.c. */
typedef struct LibXML_SaxState LibXML_SaxState;

/* A state for this handler. join: give consecutive character data in one event.
 * Returns NULL with an exception. */
LibXML_SaxState *LibXML_sax_new(PyObject *handler, int join);
void LibXML_sax_free(LibXML_SaxState *st);

/* The callbacks, to copy into a parser context. They expect the context as their
 * user data, and the state in ctxt->_private, or set by LibXML_sax_chunk_begin. */
const xmlSAXHandler *LibXML_sax_handler(void);

/* Give ctxt these callbacks and this state. */
void LibXML_sax_attach(LibXML_SaxState *st, xmlParserCtxtPtr ctxt);

/* The context is about to go: the state must not use it any more. */
void LibXML_sax_detach(LibXML_SaxState *st);

/* A chunk is parsed in a context that libxml2 makes: the state is found through
 * the thread instead. Returns what to give to LibXML_sax_chunk_end. */
LibXML_SaxState *LibXML_sax_chunk_begin(LibXML_SaxState *st);
void LibXML_sax_chunk_end(LibXML_SaxState *st, LibXML_SaxState *previous);

/* Deliver the character data that is still waiting. Returns 0, or -1 with an
 * exception, the one from the handler. */
int LibXML_sax_flush(LibXML_SaxState *st);

/* handler.end_document({}): returns what it returns, None if it has no such
 * method, or NULL with an exception. */
PyObject *LibXML_sax_end_document(LibXML_SaxState *st);

int LibXML_sax_traverse(LibXML_SaxState *st, visitproc visit, void *arg);
void LibXML_sax_clear(LibXML_SaxState *st);

#endif /* LIBXML_SAX_H */

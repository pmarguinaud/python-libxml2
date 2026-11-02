#ifndef LIBXML_INPUT_H
#define LIBXML_INPUT_H

#include "libxml_module.h"

/*
 * One external entity loader and one set of input callbacks are installed for the
 * life of the module, and they never change. What they do depends on the state of
 * the parse running in the thread, on a stack: parses can nest in callbacks, and a
 * callback can let other threads run. Nothing global changes around a parse, so
 * there is nothing to restore when one ends with an exception.
 *
 * A callback that raises leaves the exception set. libxml2 goes on to fail, and
 * no Python code runs until the operation returns and reports it.
 */
typedef struct LibXML_ParseState {
    struct LibXML_ParseState *prev;
    PyObject *callbacks;    /* list of (match, open, read, close), or NULL */
    PyObject *handler;      /* ext_ent_handler, or NULL */
    PyObject *matched;      /* what the last match found, for the open that follows */
    int no_network;         /* for what has no parser context with options */
} LibXML_ParseState;

/* Takes the reference to callbacks, not the one to handler. */
void LibXML_state_push(LibXML_ParseState *state, PyObject *callbacks, PyObject *handler,
                       int no_network);
void LibXML_state_pop(LibXML_ParseState *state);

/* Install the loader and the callbacks, and add the functions to the module. */
int LibXML_input_init(PyObject *module);

/* The callbacks of a parser, in the order they are tried. extra is the dict of the
 * options of the parser. Returns a new list, or NULL with no callbacks. */
PyObject *LibXML_collect_callbacks(PyObject *extra);

#endif /* LIBXML_INPUT_H */

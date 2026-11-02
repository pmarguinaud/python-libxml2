#include "libxml_sax.h"
#include "libxml_utils.h"
#include <libxml/SAX2.h>
#include <libxml/parserInternals.h>

/* The events and their arguments are those of perl-libxml-sax.c: dicts with the
 * keys of Perl's hashes. libxml2 parses with its SAX2 interface here, where Perl
 * uses SAX1 and keeps its own namespace stack: the events are the same.
 *
 * A handler is any object: an event it has no method for is skipped. When a
 * method raises, the parser stops and the exception comes out of the parse. The
 * declarations of the DTD go to the callbacks of libxml2, which keep them in a
 * document that is freed at the end: that is how entities are expanded. */

struct LibXML_SaxState {
    PyObject *handler;
    PyObject *locator;      /* dict, updated before each event */
    PyObject *ns_stack;     /* list: for each open element, a tuple of (prefix, uri) */
    PyObject *chars;        /* list of str waiting to be joined, or NULL */
    xmlParserCtxtPtr ctxt;  /* NULL when not parsing */
    int join;
    int failed;             /* the handler raised: no more events */
};

static __thread LibXML_SaxState *chunk_state = NULL;

LibXML_SaxState *
LibXML_sax_new(PyObject *handler, int join)
{
    LibXML_SaxState *st = PyMem_Calloc(1, sizeof(*st));
    if (st == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    st->ns_stack = PyList_New(0);
    if (st->ns_stack == NULL) {
        PyMem_Free(st);
        return NULL;
    }
    if (join && (st->chars = PyList_New(0)) == NULL) {
        Py_DECREF(st->ns_stack);
        PyMem_Free(st);
        return NULL;
    }
    Py_INCREF(handler);
    st->handler = handler;
    st->join = join;
    return st;
}

void
LibXML_sax_free(LibXML_SaxState *st)
{
    if (st == NULL) return;
    if (st->ctxt != NULL && st->ctxt->_private == st)
        st->ctxt->_private = NULL;
    Py_CLEAR(st->handler);
    Py_CLEAR(st->locator);
    Py_CLEAR(st->ns_stack);
    Py_CLEAR(st->chars);
    PyMem_Free(st);
}

int
LibXML_sax_traverse(LibXML_SaxState *st, visitproc visit, void *arg)
{
    if (st == NULL) return 0;
    Py_VISIT(st->handler);
    Py_VISIT(st->locator);
    return 0;
}

void
LibXML_sax_clear(LibXML_SaxState *st)
{
    if (st == NULL) return;
    Py_CLEAR(st->handler);
    Py_CLEAR(st->locator);
}

void
LibXML_sax_attach(LibXML_SaxState *st, xmlParserCtxtPtr ctxt)
{
    memcpy(ctxt->sax, LibXML_sax_handler(), sizeof(xmlSAXHandler));
    ctxt->userData = ctxt;
    ctxt->_private = st;
    st->ctxt = ctxt;
}

void
LibXML_sax_detach(LibXML_SaxState *st)
{
    if (st == NULL) return;
    if (st->ctxt != NULL && st->ctxt->_private == st)
        st->ctxt->_private = NULL;
    st->ctxt = NULL;
}

LibXML_SaxState *
LibXML_sax_chunk_begin(LibXML_SaxState *st)
{
    LibXML_SaxState *previous = chunk_state;
    chunk_state = st;
    return previous;
}

void
LibXML_sax_chunk_end(LibXML_SaxState *st, LibXML_SaxState *previous)
{
    st->ctxt = NULL;
    chunk_state = previous;
}

/* The state of the parse that ctx belongs to. */
static LibXML_SaxState *
state_of(void *ctx)
{
    xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr)ctx;
    if (ctxt != NULL && ctxt->_private != NULL)
        return (LibXML_SaxState *)ctxt->_private;
    if (chunk_state != NULL && ctxt != NULL)
        chunk_state->ctxt = ctxt;
    return chunk_state;
}

/* ------------------------------------------------------------------ */
/* Calling the handler                                                 */
/* ------------------------------------------------------------------ */

static void
fail(LibXML_SaxState *st)
{
    st->failed = 1;
    if (st->ctxt != NULL)
        xmlStopParser(st->ctxt);
}

/* Whether events still go to the handler. */
static int
active(LibXML_SaxState *st)
{
    if (st == NULL || st->failed || st->handler == NULL) return 0;
    if (PyErr_Occurred()) {
        /* A callback of the parse failed, an input callback for instance. */
        fail(st);
        return 0;
    }
    return 1;
}

/* handler.method(arg), or handler.method() if arg is NULL. Steals arg. Returns
 * what the method returns, Py_None if there is no such method, or NULL. */
static PyObject *
call(LibXML_SaxState *st, const char *method, PyObject *arg)
{
    PyObject *func = PyObject_GetAttrString(st->handler, method);
    if (func == NULL) {
        Py_XDECREF(arg);
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
            fail(st);
            return NULL;
        }
        PyErr_Clear();
        Py_RETURN_NONE;
    }

    /* The handler can parse: the errors of this parse wait meanwhile. */
    PyObject *held = LibXML_errors_hold();
    PyObject *result = arg ? PyObject_CallFunctionObjArgs(func, arg, NULL)
                           : PyObject_CallObject(func, NULL);
    LibXML_errors_resume(held);
    Py_DECREF(func);
    Py_XDECREF(arg);
    if (result == NULL)
        fail(st);
    return result;
}

/* An event: returns 0, or -1 when the handler raised. */
static int
event(LibXML_SaxState *st, const char *method, PyObject *arg)
{
    PyObject *result = call(st, method, arg);
    if (result == NULL) return -1;
    Py_DECREF(result);
    return 0;
}

static PyObject *
text(const xmlChar *s)
{
    if (s == NULL) return PyUnicode_FromString("");
    return PyUnicode_DecodeUTF8((const char *)s, (Py_ssize_t)strlen((const char *)s),
                                "replace");
}

static PyObject *
text_len(const xmlChar *s, Py_ssize_t len)
{
    return PyUnicode_DecodeUTF8((const char *)s, len, "replace");
}

/* Set key in dict to a new reference, which is stolen. Returns -1 on failure. */
static int
put(PyObject *dict, const char *key, PyObject *value)
{
    if (value == NULL) return -1;
    int rc = PyDict_SetItemString(dict, key, value);
    Py_DECREF(value);
    return rc;
}

static void
update_locator(LibXML_SaxState *st)
{
    xmlParserCtxtPtr ctxt = st->ctxt;
    if (st->locator == NULL || ctxt == NULL || ctxt->input == NULL) return;

    put(st->locator, "LineNumber", PyLong_FromLong(ctxt->input->line));
    put(st->locator, "ColumnNumber", PyLong_FromLong(ctxt->input->col));
    if (ctxt->encoding != NULL && ctxt->encoding[0])
        put(st->locator, "Encoding", text(ctxt->encoding));
    if (ctxt->version != NULL && ctxt->version[0])
        put(st->locator, "XMLVersion", text(ctxt->version));
}

/* Character data waits when it is joined; any other event delivers it first. */
static int
flush(LibXML_SaxState *st)
{
    if (st->chars == NULL || PyList_GET_SIZE(st->chars) == 0) return 0;

    PyObject *empty = PyUnicode_FromString("");
    PyObject *data = empty ? PyUnicode_Join(empty, st->chars) : NULL;
    Py_XDECREF(empty);
    PyList_SetSlice(st->chars, 0, PyList_GET_SIZE(st->chars), NULL);
    if (data == NULL) {
        fail(st);
        return -1;
    }

    PyObject *arg = PyDict_New();
    if (arg == NULL || (PyUnicode_GET_LENGTH(data) > 0 && PyDict_SetItemString(arg, "Data", data) < 0)) {
        Py_XDECREF(arg);
        Py_DECREF(data);
        fail(st);
        return -1;
    }
    Py_DECREF(data);
    return event(st, "characters", arg);
}

int
LibXML_sax_flush(LibXML_SaxState *st)
{
    if (!active(st)) return PyErr_Occurred() ? -1 : 0;
    return flush(st);
}

PyObject *
LibXML_sax_end_document(LibXML_SaxState *st)
{
    if (!active(st)) {
        if (PyErr_Occurred()) return NULL;
        Py_RETURN_NONE;
    }
    PyObject *arg = PyDict_New();
    if (arg == NULL) return NULL;
    return call(st, "end_document", arg);
}

/* Prepare an event: returns 1 to go on, 0 to skip it. */
static int
begin(LibXML_SaxState *st)
{
    if (!active(st)) return 0;
    update_locator(st);
    return flush(st) == 0;
}

/* ------------------------------------------------------------------ */
/* Document                                                            */
/* ------------------------------------------------------------------ */

static void
sax_start_document(void *ctx)
{
    /* The document keeps the declarations of the DTD. */
    xmlSAX2StartDocument(ctx);

    LibXML_SaxState *st = state_of(ctx);
    if (!active(st)) return;
    xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr)ctx;

    st->locator = PyDict_New();
    if (st->locator == NULL) {
        fail(st);
        return;
    }
    if (ctxt->input != NULL && ctxt->input->filename != NULL && ctxt->input->filename[0])
        put(st->locator, "SystemId", text(BAD_CAST ctxt->input->filename));
    update_locator(st);
    Py_INCREF(st->locator);
    if (event(st, "set_document_locator", st->locator) < 0) return;

    if (event(st, "start_document", PyDict_New()) < 0) return;

    PyObject *decl = PyDict_New();
    if (decl == NULL) {
        fail(st);
        return;
    }
    put(decl, "Version", text(ctxt->version ? ctxt->version : BAD_CAST "1.0"));
    if (ctxt->encoding != NULL)
        put(decl, "Encoding", text(ctxt->encoding));
    event(st, "xml_decl", decl);
}

static void
sax_external_subset(void *ctx, const xmlChar *name, const xmlChar *public_id,
                    const xmlChar *system_id)
{
    LibXML_SaxState *st = state_of(ctx);
    if (begin(st)) {
        PyObject *dtd = PyDict_New();
        if (dtd == NULL) {
            fail(st);
            return;
        }
        if (name != NULL && name[0]) put(dtd, "Name", text(name));
        if (public_id != NULL && public_id[0]) put(dtd, "PublicId", text(public_id));
        if (system_id != NULL && system_id[0]) put(dtd, "SystemId", text(system_id));
        if (event(st, "start_dtd", dtd) < 0) return;
        if (event(st, "end_dtd", PyDict_New()) < 0) return;
    }
    if (st == NULL || !st->failed)
        xmlSAX2ExternalSubset(ctx, name, public_id, system_id);
}

/* ------------------------------------------------------------------ */
/* Elements                                                            */
/* ------------------------------------------------------------------ */

#define XMLNS_URI "http://www.w3.org/2000/xmlns/"

static PyObject *
qualified(const xmlChar *prefix, const xmlChar *local)
{
    if (prefix == NULL || !prefix[0]) return text(local);
    return PyUnicode_FromFormat("%s:%s", (const char *)prefix, (const char *)local);
}

/* {Name, NamespaceURI, Prefix, LocalName} */
static PyObject *
element_dict(const xmlChar *local, const xmlChar *prefix, const xmlChar *uri)
{
    PyObject *el = PyDict_New();
    if (el == NULL) return NULL;
    if (put(el, "Name", qualified(prefix, local)) < 0
        || put(el, "NamespaceURI", text(uri)) < 0
        || put(el, "Prefix", text(prefix)) < 0
        || put(el, "LocalName", text(local)) < 0) {
        Py_DECREF(el);
        return NULL;
    }
    return el;
}

static int
attribute(PyObject *attrs, PyObject *name, PyObject *value, const char *uri,
          PyObject *prefix, PyObject *local)
{
    PyObject *attr = PyDict_New();
    PyObject *key = attr ? PyUnicode_FromFormat("{%s}%U", uri, local) : NULL;
    int rc = -1;
    if (key != NULL) {
        Py_INCREF(name);
        Py_INCREF(value);
        Py_INCREF(prefix);
        Py_INCREF(local);
        if (put(attr, "Name", name) == 0 && put(attr, "Value", value) == 0
            && put(attr, "NamespaceURI", PyUnicode_FromString(uri)) == 0
            && put(attr, "Prefix", prefix) == 0 && put(attr, "LocalName", local) == 0)
            rc = PyDict_SetItem(attrs, key, attr);
    }
    Py_XDECREF(key);
    Py_XDECREF(attr);
    return rc;
}

/* The declarations go in Attributes too, as in Perl. */
static int
namespace_attribute(PyObject *attrs, const xmlChar *prefix, const xmlChar *uri)
{
    PyObject *value = text(uri);
    PyObject *xmlns = PyUnicode_FromString("xmlns");
    PyObject *empty = PyUnicode_FromString("");
    PyObject *local = (prefix && prefix[0]) ? text(prefix) : NULL;
    PyObject *name = local ? PyUnicode_FromFormat("xmlns:%U", local) : NULL;
    int rc = -1;

    if (value && xmlns && empty && (prefix == NULL || !prefix[0]))
        rc = attribute(attrs, xmlns, value, "", empty, xmlns);
    else if (value && xmlns && local && name)
        rc = attribute(attrs, name, value, XMLNS_URI, xmlns, local);

    Py_XDECREF(value);
    Py_XDECREF(xmlns);
    Py_XDECREF(empty);
    Py_XDECREF(local);
    Py_XDECREF(name);
    return rc;
}

static void
sax_start_element(void *ctx, const xmlChar *localname, const xmlChar *prefix,
                  const xmlChar *uri, int nb_namespaces, const xmlChar **namespaces,
                  int nb_attributes, int nb_defaulted, const xmlChar **attributes)
{
    (void)nb_defaulted;
    LibXML_SaxState *st = state_of(ctx);
    if (!begin(st)) return;

    /* What end_prefix_mapping needs at the end of the element */
    PyObject *declared = PyTuple_New(nb_namespaces);
    if (declared == NULL) {
        fail(st);
        return;
    }
    for (int i = 0; i < nb_namespaces; i++) {
        PyObject *pair = Py_BuildValue("(NN)", text(namespaces[2 * i]),
                                       text(namespaces[2 * i + 1]));
        if (pair == NULL) {
            Py_DECREF(declared);
            fail(st);
            return;
        }
        PyTuple_SET_ITEM(declared, i, pair);
    }
    int rc = PyList_Append(st->ns_stack, declared);
    Py_DECREF(declared);
    if (rc < 0) {
        fail(st);
        return;
    }

    for (int i = 0; i < nb_namespaces; i++) {
        PyObject *map = PyDict_New();
        if (map == NULL || put(map, "NamespaceURI", text(namespaces[2 * i + 1])) < 0
            || put(map, "Prefix", text(namespaces[2 * i])) < 0) {
            Py_XDECREF(map);
            fail(st);
            return;
        }
        if (event(st, "start_prefix_mapping", map) < 0) return;
    }

    PyObject *el = element_dict(localname, prefix, uri);
    PyObject *attrs = el ? PyDict_New() : NULL;
    if (attrs == NULL) goto error;

    for (int i = 0; i < nb_namespaces; i++) {
        if (namespace_attribute(attrs, namespaces[2 * i], namespaces[2 * i + 1]) < 0)
            goto error;
    }
    for (int i = 0; i < nb_attributes; i++) {
        const xmlChar **a = attributes + 5 * i;
        PyObject *local = text(a[0]);
        PyObject *aprefix = text(a[1]);
        PyObject *name = qualified(a[1], a[0]);
        PyObject *value = text_len(a[3], a[4] - a[3]);
        int ok = local && aprefix && name && value
            && attribute(attrs, name, value, a[2] ? (const char *)a[2] : "", aprefix,
                         local) == 0;
        Py_XDECREF(local);
        Py_XDECREF(aprefix);
        Py_XDECREF(name);
        Py_XDECREF(value);
        if (!ok) goto error;
    }

    if (PyDict_SetItemString(el, "Attributes", attrs) < 0) goto error;
    Py_DECREF(attrs);
    event(st, "start_element", el);
    return;

error:
    Py_XDECREF(el);
    Py_XDECREF(attrs);
    fail(st);
}

static void
sax_end_element(void *ctx, const xmlChar *localname, const xmlChar *prefix,
                const xmlChar *uri)
{
    LibXML_SaxState *st = state_of(ctx);
    if (!begin(st)) return;

    PyObject *el = element_dict(localname, prefix, uri);
    if (el == NULL) {
        fail(st);
        return;
    }
    if (event(st, "end_element", el) < 0) return;

    Py_ssize_t n = PyList_GET_SIZE(st->ns_stack);
    if (n == 0) return;
    PyObject *declared = PyList_GET_ITEM(st->ns_stack, n - 1);
    Py_INCREF(declared);
    PyList_SetSlice(st->ns_stack, n - 1, n, NULL);

    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(declared); i++) {
        PyObject *pair = PyTuple_GET_ITEM(declared, i);
        PyObject *map = PyDict_New();
        if (map == NULL
            || PyDict_SetItemString(map, "NamespaceURI", PyTuple_GET_ITEM(pair, 1)) < 0
            || PyDict_SetItemString(map, "Prefix", PyTuple_GET_ITEM(pair, 0)) < 0) {
            Py_XDECREF(map);
            fail(st);
            break;
        }
        if (event(st, "end_prefix_mapping", map) < 0) break;
    }
    Py_DECREF(declared);
}

/* ------------------------------------------------------------------ */
/* Character data, comments, PIs                                       */
/* ------------------------------------------------------------------ */

/* {Data: ...}, or {} for nothing, as in Perl */
static PyObject *
data_dict(const xmlChar *s, Py_ssize_t len)
{
    PyObject *d = PyDict_New();
    if (d != NULL && s != NULL && len > 0 && put(d, "Data", text_len(s, len)) < 0) {
        Py_DECREF(d);
        return NULL;
    }
    return d;
}

static void
sax_characters(void *ctx, const xmlChar *ch, int len)
{
    LibXML_SaxState *st = state_of(ctx);
    if (!active(st) || ch == NULL) return;
    update_locator(st);

    if (st->chars != NULL) {
        PyObject *piece = text_len(ch, len);
        if (piece == NULL || PyList_Append(st->chars, piece) < 0)
            fail(st);
        Py_XDECREF(piece);
        return;
    }
    PyObject *d = data_dict(ch, len);
    if (d == NULL) {
        fail(st);
        return;
    }
    event(st, "characters", d);
}

static void
sax_comment(void *ctx, const xmlChar *value)
{
    LibXML_SaxState *st = state_of(ctx);
    if (value == NULL || !begin(st)) return;
    PyObject *d = data_dict(value, (Py_ssize_t)strlen((const char *)value));
    if (d == NULL) {
        fail(st);
        return;
    }
    event(st, "comment", d);
}

static void
sax_cdata(void *ctx, const xmlChar *value, int len)
{
    LibXML_SaxState *st = state_of(ctx);
    if (value == NULL || !begin(st)) return;
    if (event(st, "start_cdata", NULL) < 0) return;
    PyObject *d = data_dict(value, len);
    if (d == NULL) {
        fail(st);
        return;
    }
    if (event(st, "characters", d) < 0) return;
    event(st, "end_cdata", NULL);
}

static void
sax_pi(void *ctx, const xmlChar *target, const xmlChar *data)
{
    LibXML_SaxState *st = state_of(ctx);
    if (!begin(st)) return;
    PyObject *d = PyDict_New();
    if (d == NULL) {
        fail(st);
        return;
    }
    if (target != NULL && target[0]) {
        if (put(d, "Target", text(target)) < 0 || put(d, "Data", text(data)) < 0) {
            Py_DECREF(d);
            fail(st);
            return;
        }
    }
    event(st, "processing_instruction", d);
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

static PyObject *
parse_exception(LIBXML_CONST_ERROR xmlError *error)
{
    static PyObject *cls = NULL;
    if (cls == NULL) {
        PyObject *module = PyImport_ImportModule("xml_libxml.SAX");
        if (module == NULL) return NULL;
        cls = PyObject_GetAttrString(module, "ParseException");
        Py_DECREF(module);
        if (cls == NULL) return NULL;
    }

    PyObject *message = text(BAD_CAST(error->message ? error->message : ""));
    if (message == NULL) return NULL;
    PyObject *stripped = PyObject_CallMethod(message, "rstrip", "s", "\n");
    Py_DECREF(message);
    if (stripped == NULL) return NULL;

    PyObject *kwargs = Py_BuildValue("{s:N,s:i,s:i}", "Message", stripped,
                                     "LineNumber", error->line, "ColumnNumber", error->int2);
    if (kwargs == NULL) return NULL;
    if (error->file != NULL && put(kwargs, "SystemId", text(BAD_CAST error->file)) < 0) {
        Py_DECREF(kwargs);
        return NULL;
    }
    PyObject *args = PyTuple_New(0);
    PyObject *exc = args ? PyObject_Call(cls, args, kwargs) : NULL;
    Py_XDECREF(args);
    Py_DECREF(kwargs);
    return exc;
}

/* Kept as for any parse, then told to the handler. */
static void
sax_error(void *ctx, LIBXML_CONST_ERROR xmlError *error)
{
    LibXML_error_handler(NULL, error);

    LibXML_SaxState *st = state_of(ctx);
    if (!active(st)) return;

    const char *method = error->level == XML_ERR_WARNING ? "warning"
                       : error->level == XML_ERR_FATAL ? "fatal_error" : "error";
    PyObject *exc = parse_exception(error);
    if (exc == NULL) {
        fail(st);
        return;
    }
    event(st, method, exc);
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

static xmlSAXHandler handler_table;
static int handler_ready = 0;

const xmlSAXHandler *
LibXML_sax_handler(void)
{
    if (!handler_ready) {
        xmlSAXHandler *h = &handler_table;
        memset(h, 0, sizeof(*h));
        h->initialized = XML_SAX2_MAGIC;

        /* What the DTD declares stays with libxml2. */
        h->internalSubset     = xmlSAX2InternalSubset;
        h->isStandalone       = xmlSAX2IsStandalone;
        h->hasInternalSubset  = xmlSAX2HasInternalSubset;
        h->hasExternalSubset  = xmlSAX2HasExternalSubset;
        h->resolveEntity      = xmlSAX2ResolveEntity;
        h->getEntity          = xmlSAX2GetEntity;
        h->getParameterEntity = xmlSAX2GetParameterEntity;
        h->entityDecl         = xmlSAX2EntityDecl;
        h->notationDecl       = xmlSAX2NotationDecl;
        h->attributeDecl      = xmlSAX2AttributeDecl;
        h->elementDecl        = xmlSAX2ElementDecl;
        h->unparsedEntityDecl = xmlSAX2UnparsedEntityDecl;

        h->startDocument         = sax_start_document;
        h->externalSubset        = sax_external_subset;
        h->startElementNs        = sax_start_element;
        h->endElementNs          = sax_end_element;
        h->characters            = sax_characters;
        h->ignorableWhitespace   = sax_characters;
        h->comment               = sax_comment;
        h->cdataBlock            = sax_cdata;
        h->processingInstruction = sax_pi;
        h->serror                = sax_error;
        handler_ready = 1;
    }
    return &handler_table;
}

/*
 * XML::LibXML::Reader, over xmlTextReader.
 *
 * The reader builds the document as it goes and frees the nodes it has passed.
 * Python only gets copies of them (copyCurrentNode), until document() or
 * preserveNode() hands out the document itself. From then on, until the reading
 * ends, the nodes Python refers to are marked so that the reader keeps them, and
 * the document cannot change, as the reader holds pointers into it.
 */
#include "libxml_reader.h"
#include "libxml_utils.h"
#include "libxml_node.h"
#include "libxml_document.h"
#include "libxml_namespace.h"
#include "libxml_input.h"
#include "libxml_schema.h"
#include "libxml_xpath.h"
#include "libxml_tree.h"
#include "libxml_parser.h"
#include <libxml/xmlreader.h>

typedef struct {
    PyObject_HEAD
    xmlTextReaderPtr reader;
    PyObject *source;       /* what the reader reads from: bytes, file, Document */
    PyObject *doc;          /* wrapper of the document, once Python has it */
    PyObject *schema;       /* a RelaxNG or Schema object the reader validates with */
    PyObject *extra;        /* options that are not flags, such as ext_ent_handler */
    int recover;
    int busy;               /* libxml2 works on it, and Python code can run */
    int locked;             /* holds the lock on doc */
    int done;               /* the reading is over: no more node to go to */
} ReaderObject;

/* ------------------------------------------------------------------ */
/* Reading from a Python file object                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject *fh;
    PyObject *pending;
    Py_ssize_t offset;
} IOContext;

static int
io_read(void *ctx, char *buffer, int len)
{
    IOContext *io = (IOContext *)ctx;

    if (PyErr_Occurred())
        return -1;
    if (io->pending == NULL) {
        PyObject *errors = LibXML_errors_hold();
        PyObject *result = PyObject_CallMethod(io->fh, "read", "i", len);
        LibXML_errors_resume(errors);
        if (result == NULL) return -1;
        if (PyUnicode_Check(result)) {
            io->pending = PyUnicode_AsUTF8String(result);
        } else if (result == Py_None) {
            io->pending = PyBytes_FromStringAndSize(NULL, 0);
        } else {
            io->pending = PyBytes_FromObject(result);
        }
        Py_DECREF(result);
        if (io->pending == NULL) return -1;
        io->offset = 0;
    }

    Py_ssize_t left = PyBytes_GET_SIZE(io->pending) - io->offset;
    Py_ssize_t count = left < len ? left : len;
    memcpy(buffer, PyBytes_AS_STRING(io->pending) + io->offset, count);
    io->offset += count;
    if (io->offset >= PyBytes_GET_SIZE(io->pending))
        Py_CLEAR(io->pending);
    return (int)count;
}

static int
io_close(void *ctx)
{
    IOContext *io = (IOContext *)ctx;
    Py_XDECREF(io->pending);
    Py_DECREF(io->fh);
    PyMem_Free(io);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Running libxml2                                                     */
/* ------------------------------------------------------------------ */

/* Calls into the reader can parse, and run callbacks that use the reader. */
static int
reader_idle(ReaderObject *self)
{
    if (self->busy) {
        PyErr_SetString(LibXML_Error, "this Reader is in the middle of a read");
        return 0;
    }
    return 1;
}

/* Moving on frees nodes that an XPath evaluation in progress may hold. */
static int
reader_can_move(ReaderObject *self)
{
    if (!reader_idle(self)) return 0;
    if (LibXML_eval_running()) {
        PyErr_SetString(LibXML_Error, "a Reader cannot move during an XPath evaluation");
        return 0;
    }
    return 1;
}

static void
lock_doc(ReaderObject *self)
{
    if (!self->locked && self->doc != NULL) {
        ((LibXML_NodeObject *)self->doc)->readers++;
        self->locked = 1;
    }
}

static void
unlock_doc(ReaderObject *self)
{
    if (self->locked) {
        ((LibXML_NodeObject *)self->doc)->readers--;
        self->locked = 0;
    }
}

/* The document is free to change once the reading is over and the reader holds no
 * node of it. The input can be at its end long before, and a reader that walks a
 * document stays on its last node: only close() lets it go. */
static void
check_end(ReaderObject *self)
{
    int mode = xmlTextReaderReadState(self->reader);
    if (mode == XML_TEXTREADER_MODE_CLOSED)
        self->done = 1;
    if (self->done && xmlTextReaderCurrentNode(self->reader) == NULL)
        unlock_doc(self);
}

/* After a call that goes on through the document: 0 or -1 is the end. */
static void
moved(ReaderObject *self, int ret)
{
    if (ret <= 0)
        self->done = 1;
    check_end(self);
}

typedef struct {
    LibXML_ParseState state;
} Run;

static void
run_begin(ReaderObject *self, Run *run)
{
    PyObject *handler = PyDict_GetItemString(self->extra, "ext_ent_handler");
    if (handler == Py_None) handler = NULL;
    LibXML_errors_begin();
    LibXML_state_push(&run->state, LibXML_collect_callbacks(self->extra), handler, 0);
    self->busy = 1;
}

/* Returns 0, or -1 with an exception. */
static int
run_end(ReaderObject *self, Run *run)
{
    self->busy = 0;
    LibXML_state_pop(&run->state);
    check_end(self);

    if (PyErr_Occurred()) {
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        LibXML_errors_begin();
        PyErr_Restore(type, value, tb);
        return -1;
    }
    return LibXML_report_errors(self->recover);
}

#define RUN(self, expr, result) do {            \
        Run run_;                               \
        run_begin((self), &run_);               \
        result = (expr);                        \
        if (run_end((self), &run_) < 0)         \
            return NULL;                        \
    } while (0)

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static PyObject *
string_or_none(const xmlChar *value)
{
    if (value == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)value);
}

static PyObject *
owned_string(xmlChar *value)
{
    PyObject *result = string_or_none(value);
    xmlFree(value);
    return result;
}

static const char *
optional_text(PyObject *value, PyObject **hold)
{
    *hold = NULL;
    if (value == NULL || value == Py_None) return NULL;
    *hold = PyObject_Str(value);
    return *hold ? PyUnicode_AsUTF8(*hold) : NULL;
}

static int
declares_encoding(const char *data, Py_ssize_t size)
{
    if (size < 6 || strncmp(data, "<?xml", 5) != 0) return 0;
    const char *end = memchr(data, '>', size);
    if (end == NULL) return 0;
    for (const char *p = data; p + 8 <= end; p++) {
        if (strncmp(p, "encoding", 8) == 0) return 1;
    }
    return 0;
}

static int
set_schema(ReaderObject *self, PyObject *value, int relaxng)
{
    int rc;

    if (PyUnicode_Check(value) || PyBytes_Check(value) || PyObject_HasAttrString(value, "__fspath__")) {
        PyObject *path = NULL;
        if (!PyUnicode_FSConverter(value, &path)) return -1;
        Run run;
        run_begin(self, &run);
        rc = relaxng ? xmlTextReaderRelaxNGValidate(self->reader, PyBytes_AS_STRING(path))
                     : xmlTextReaderSchemaValidate(self->reader, PyBytes_AS_STRING(path));
        Py_DECREF(path);
        if (run_end(self, &run) < 0) return -1;
    } else if (relaxng) {
        xmlRelaxNGPtr schema = LibXML_relaxng_ptr(value);
        if (schema == NULL) {
            PyErr_SetString(PyExc_TypeError, "RelaxNG must be a path or a RelaxNG object");
            return -1;
        }
        rc = xmlTextReaderRelaxNGSetSchema(self->reader, schema);
    } else {
        xmlSchemaPtr schema = LibXML_schema_ptr(value);
        if (schema == NULL) {
            PyErr_SetString(PyExc_TypeError, "Schema must be a path or a Schema object");
            return -1;
        }
        rc = xmlTextReaderSetSchema(self->reader, schema);
    }
    if (rc != 0) {
        LibXML_raise_last_error();
        return -1;
    }
    if (!PyUnicode_Check(value) && !PyBytes_Check(value)) {
        Py_INCREF(value);
        Py_XSETREF(self->schema, value);
    }
    return 0;
}

static PyObject *
reader_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *options = PyDict_New();
    if (options == NULL) return NULL;
    if (PyTuple_GET_SIZE(args) == 1 && PyDict_Check(PyTuple_GET_ITEM(args, 0))) {
        if (PyDict_Update(options, PyTuple_GET_ITEM(args, 0)) < 0) goto fail_options;
    } else if (PyTuple_GET_SIZE(args) > 0) {
        PyErr_SetString(PyExc_TypeError, "Reader takes options as keywords, or a dict");
        goto fail_options;
    }
    if (kwds != NULL && PyDict_Update(options, kwds) < 0) goto fail_options;

    /* What is not a parser option */
    static const char *const own[] = {
        "location", "string", "IO", "DOM", "FD", "URI", "encoding", "RelaxNG", "Schema", NULL
    };
    PyObject *values[9] = {NULL};
    int sources = 0;
    for (int i = 0; own[i]; i++) {
        values[i] = PyDict_GetItemString(options, own[i]);
        if (values[i] == Py_None) values[i] = NULL;
        Py_XINCREF(values[i]);
        if (PyDict_GetItemString(options, own[i]) != NULL
            && PyDict_DelItemString(options, own[i]) < 0)
            goto fail_values;
        if (i < 5 && values[i] != NULL) sources++;
    }
    PyObject *location = values[0], *string = values[1], *io = values[2], *dom = values[3];
    PyObject *fd = values[4];
    if (sources != 1) {
        PyErr_SetString(PyExc_TypeError, "Reader: give one of location, string, IO, DOM or FD");
        goto fail_values;
    }

    ReaderObject *self = (ReaderObject *)type->tp_alloc(type, 0);
    if (self == NULL) goto fail_values;
    self->extra = PyDict_New();
    if (self->extra == NULL) goto fail_self;

    int flags;
    if (LibXML_parser_options(options, &flags, &self->recover, self->extra) < 0)
        goto fail_self;

    PyObject *uri_hold = NULL, *enc_hold = NULL;
    const char *uri = optional_text(values[5], &uri_hold);
    const char *encoding = optional_text(values[6], &enc_hold);
    if (PyErr_Occurred()) goto fail_text;

    /* Creating a reader already reads, with the callbacks of a parse. */
    Run run;
    run_begin(self, &run);
    if (location != NULL) {
        PyObject *path = NULL;
        if (PyUnicode_FSConverter(location, &path)) {
            self->reader = xmlReaderForFile(PyBytes_AS_STRING(path), encoding, flags);
            Py_DECREF(path);
        }
    } else if (string != NULL) {
        const char *data;
        Py_ssize_t size;
        if (PyUnicode_Check(string)) {
            self->source = PyUnicode_AsUTF8String(string);
            /* A str is text: only a declared encoding has to be overridden. */
            if (encoding == NULL && self->source != NULL
                && declares_encoding(PyBytes_AS_STRING(self->source),
                                     PyBytes_GET_SIZE(self->source)))
                encoding = "UTF-8";
        } else {
            self->source = PyBytes_FromObject(string);
        }
        if (self->source != NULL) {
            data = PyBytes_AS_STRING(self->source);
            size = PyBytes_GET_SIZE(self->source);
            if (size > INT_MAX)
                PyErr_SetString(LibXML_Error, "the string is too big");
            else
                self->reader = xmlReaderForMemory(data, (int)size, uri, encoding, flags);
        }
    } else if (io != NULL) {
        IOContext *ctx = PyMem_Calloc(1, sizeof(IOContext));
        if (ctx == NULL) {
            PyErr_NoMemory();
        } else {
            Py_INCREF(io);
            ctx->fh = io;
            /* libxml2 closes the context when it fails. */
            self->reader = xmlReaderForIO(io_read, io_close, ctx, uri, encoding, flags);
        }
    } else if (fd != NULL) {
        int number = PyObject_AsFileDescriptor(fd);
        if (number >= 0) {
            Py_INCREF(fd);
            self->source = fd;
            self->reader = xmlReaderForFd(number, uri, encoding, flags);
        }
    } else if (!PyObject_TypeCheck(dom, &LibXML_DocumentType)) {
        PyErr_SetString(PyExc_TypeError, "DOM must be a Document");
    } else {
        self->reader = xmlReaderWalker(LibXML_DOC(dom));
        if (self->reader != NULL) {
            Py_INCREF(dom);
            self->doc = dom;
            lock_doc(self);
        }
    }
    Py_XDECREF(uri_hold);
    Py_XDECREF(enc_hold);
    uri_hold = enc_hold = NULL;

    if (self->reader == NULL) {
        self->busy = 0;
        LibXML_state_pop(&run.state);
        if (!PyErr_Occurred())
            LibXML_raise_last_error();
        goto fail_self;
    }
    if (run_end(self, &run) < 0)
        goto fail_self;

    if (values[7] != NULL && set_schema(self, values[7], 1) < 0) goto fail_self;
    if (values[8] != NULL && set_schema(self, values[8], 0) < 0) goto fail_self;

    for (int i = 0; i < 9; i++) Py_XDECREF(values[i]);
    Py_DECREF(options);
    return (PyObject *)self;

fail_text:
    Py_XDECREF(uri_hold);
    Py_XDECREF(enc_hold);
fail_self:
    Py_DECREF(self);
fail_values:
    for (int i = 0; i < 9; i++) Py_XDECREF(values[i]);
fail_options:
    Py_DECREF(options);
    return NULL;
}

static void
reader_dealloc(ReaderObject *self)
{
    PyObject *type, *value, *tb;
    PyErr_Fetch(&type, &value, &tb);

    unlock_doc(self);
    if (self->reader != NULL) {
        /* Closing runs the close callbacks of what is open. */
        Run run;
        run_begin(self, &run);
        xmlFreeTextReader(self->reader);
        self->reader = NULL;
        self->busy = 0;
        LibXML_state_pop(&run.state);
        if (PyErr_Occurred())
            PyErr_WriteUnraisable((PyObject *)self);
        LibXML_errors_begin();
    }
    Py_CLEAR(self->doc);
    Py_CLEAR(self->source);
    Py_CLEAR(self->schema);
    Py_CLEAR(self->extra);
    PyErr_Restore(type, value, tb);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* Cursor                                                              */
/* ------------------------------------------------------------------ */

#define MOVE(name, call) \
static PyObject * \
reader_##name(ReaderObject *self, PyObject *args) \
{ \
    int ret; \
    if (!reader_can_move(self)) return NULL; \
    RUN(self, call(self->reader), ret); \
    if (to_end) moved(self, ret); \
    return PyLong_FromLong(ret); \
}

#define to_end 1
MOVE(read, xmlTextReaderRead)
MOVE(next, xmlTextReaderNext)
#undef to_end
#define to_end 0
MOVE(readAttributeValue, xmlTextReaderReadAttributeValue)

/* As in Perl: xmlTextReaderNextSibling only works when walking a document. */
static int
next_sibling(xmlTextReaderPtr reader)
{
    int ret = xmlTextReaderNextSibling(reader);
    if (ret == -1) {
        int depth = xmlTextReaderDepth(reader);
        ret = xmlTextReaderRead(reader);
        while (ret == 1 && xmlTextReaderDepth(reader) > depth)
            ret = xmlTextReaderNext(reader);
        if (ret == 1) {
            if (xmlTextReaderDepth(reader) != depth)
                ret = 0;
            else if (xmlTextReaderNodeType(reader) == XML_READER_TYPE_END_ELEMENT)
                ret = xmlTextReaderRead(reader);
        }
    }
    return ret;
}

MOVE(nextSibling, next_sibling)
#undef to_end

static int
is_element(xmlTextReaderPtr reader, const char *name, const char *uri)
{
    if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT)
        return 0;
    if (uri == NULL)
        return name == NULL || xmlStrEqual(BAD_CAST name, xmlTextReaderConstName(reader));
    return xmlStrEqual(BAD_CAST uri, xmlTextReaderConstNamespaceUri(reader))
        && (name == NULL || xmlStrEqual(BAD_CAST name, xmlTextReaderConstLocalName(reader)));
}

static PyObject *
element_search(ReaderObject *self, PyObject *args, int siblings)
{
    const char *name = NULL, *uri = NULL;
    if (!PyArg_ParseTuple(args, "|zz", &name, &uri)) return NULL;
    if (!reader_can_move(self)) return NULL;

    int ret;
    Run run;
    run_begin(self, &run);
    do {
        ret = siblings ? next_sibling(self->reader) : xmlTextReaderRead(self->reader);
        if (PyErr_Occurred()) break;
    } while (ret == 1 && !is_element(self->reader, name, uri));
    if (run_end(self, &run) < 0) return NULL;
    if (!siblings) moved(self, ret);
    return PyLong_FromLong(ret);
}

static PyObject *
reader_nextElement(ReaderObject *self, PyObject *args)
{
    return element_search(self, args, 0);
}

static PyObject *
reader_nextSiblingElement(ReaderObject *self, PyObject *args)
{
    return element_search(self, args, 1);
}

/* A namespace declaration as current node has no parent to match against. */
static int
pattern_match(PyObject *pattern, xmlNodePtr node)
{
    if (node == NULL || node->type == XML_NAMESPACE_DECL) return 0;
    return xmlPatternMatch(((LibXML_PatternObject *)pattern)->pattern, node) == 1;
}

static PyObject *
reader_nextPatternMatch(ReaderObject *self, PyObject *args)
{
    PyObject *pattern;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_PatternType, &pattern)) return NULL;
    if (!reader_can_move(self)) return NULL;

    int ret;
    Run run;
    run_begin(self, &run);
    do {
        ret = xmlTextReaderRead(self->reader);
        if (PyErr_Occurred()) break;
    } while (ret == 1 && !pattern_match(pattern, xmlTextReaderCurrentNode(self->reader)));
    if (run_end(self, &run) < 0) return NULL;
    moved(self, ret);
    return PyLong_FromLong(ret);
}

static PyObject *
reader_matchesPattern(ReaderObject *self, PyObject *args)
{
    PyObject *pattern;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_PatternType, &pattern)) return NULL;
    if (!reader_idle(self)) return NULL;
    return PyBool_FromLong(pattern_match(pattern, xmlTextReaderCurrentNode(self->reader)));
}

static PyObject *
reader_skipSiblings(ReaderObject *self, PyObject *args)
{
    if (!reader_can_move(self)) return NULL;

    int ret = -1;
    Run run;
    run_begin(self, &run);
    int depth = xmlTextReaderDepth(self->reader);
    if (depth > 0) {
        do {
            ret = xmlTextReaderNext(self->reader);
            if (PyErr_Occurred()) break;
        } while (ret == 1 && xmlTextReaderDepth(self->reader) >= depth);
        if (xmlTextReaderNodeType(self->reader) != XML_READER_TYPE_END_ELEMENT)
            ret = -1;
    }
    if (run_end(self, &run) < 0) return NULL;
    return PyLong_FromLong(ret);
}

/* True when the end is reached without error */
static PyObject *
reader_finish(ReaderObject *self, PyObject *args)
{
    if (!reader_can_move(self)) return NULL;

    int ret;
    Run run;
    run_begin(self, &run);
    do {
        ret = xmlTextReaderRead(self->reader);
    } while (ret == 1 && !PyErr_Occurred());
    moved(self, -1);
    if (run_end(self, &run) < 0) return NULL;
    return PyBool_FromLong(ret == 0);
}

static PyObject *
reader_close(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;

    int ret;
    RUN(self, xmlTextReaderClose(self->reader), ret);
    moved(self, -1);
    return PyBool_FromLong(ret == 0);
}

static PyObject *
reader_iternext(ReaderObject *self)
{
    if (!reader_can_move(self)) return NULL;
    int ret;
    RUN(self, xmlTextReaderRead(self->reader), ret);
    moved(self, ret);
    if (ret != 1) return NULL;
    Py_INCREF(self);
    return (PyObject *)self;
}

/* ------------------------------------------------------------------ */
/* Current node                                                        */
/* ------------------------------------------------------------------ */

#define INT_GETTER(name, call) \
static PyObject * \
reader_##name(ReaderObject *self, PyObject *args) \
{ \
    if (!reader_idle(self)) return NULL; \
    return PyLong_FromLong(call(self->reader)); \
}

#define BOOL_GETTER(name, call) \
static PyObject * \
reader_##name(ReaderObject *self, PyObject *args) \
{ \
    if (!reader_idle(self)) return NULL; \
    return PyBool_FromLong(call(self->reader) > 0); \
}

#define STRING_GETTER(name, call) \
static PyObject * \
reader_##name(ReaderObject *self, PyObject *args) \
{ \
    if (!reader_idle(self)) return NULL; \
    return string_or_none(call(self->reader)); \
}

INT_GETTER(attributeCount, xmlTextReaderAttributeCount)
INT_GETTER(depth, xmlTextReaderDepth)
INT_GETTER(nodeType, xmlTextReaderNodeType)
INT_GETTER(readState, xmlTextReaderReadState)
INT_GETTER(standalone, xmlTextReaderStandalone)
INT_GETTER(lineNumber, xmlTextReaderGetParserLineNumber)
INT_GETTER(columnNumber, xmlTextReaderGetParserColumnNumber)
INT_GETTER(moveToElement, xmlTextReaderMoveToElement)
INT_GETTER(moveToFirstAttribute, xmlTextReaderMoveToFirstAttribute)
INT_GETTER(moveToNextAttribute, xmlTextReaderMoveToNextAttribute)
BOOL_GETTER(hasAttributes, xmlTextReaderHasAttributes)
BOOL_GETTER(hasValue, xmlTextReaderHasValue)
BOOL_GETTER(isDefault, xmlTextReaderIsDefault)
BOOL_GETTER(isEmptyElement, xmlTextReaderIsEmptyElement)
BOOL_GETTER(isNamespaceDecl, xmlTextReaderIsNamespaceDecl)
BOOL_GETTER(isValid, xmlTextReaderIsValid)
STRING_GETTER(baseURI, xmlTextReaderConstBaseUri)
STRING_GETTER(encoding, xmlTextReaderConstEncoding)
STRING_GETTER(localName, xmlTextReaderConstLocalName)
STRING_GETTER(name, xmlTextReaderConstName)
STRING_GETTER(namespaceURI, xmlTextReaderConstNamespaceUri)
STRING_GETTER(prefix, xmlTextReaderConstPrefix)
STRING_GETTER(value, xmlTextReaderConstValue)
STRING_GETTER(xmlLang, xmlTextReaderConstXmlLang)
STRING_GETTER(xmlVersion, xmlTextReaderConstXmlVersion)

static PyObject *
reader_byteConsumed(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderByteConsumed(self->reader));
}

static PyObject *
reader_quoteChar(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;
    int c = xmlTextReaderQuoteChar(self->reader);
    if (c < 0) Py_RETURN_NONE;
    char text[2] = { (char)c, 0 };
    return PyUnicode_FromString(text);
}

static PyObject *
reader_getAttribute(ReaderObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name) || !reader_idle(self)) return NULL;
    return owned_string(xmlTextReaderGetAttribute(self->reader, BAD_CAST name));
}

static PyObject *
reader_getAttributeNo(ReaderObject *self, PyObject *args)
{
    int no;
    if (!PyArg_ParseTuple(args, "i", &no) || !reader_idle(self)) return NULL;
    return owned_string(xmlTextReaderGetAttributeNo(self->reader, no));
}

static PyObject *
reader_getAttributeNs(ReaderObject *self, PyObject *args)
{
    const char *local, *uri;
    if (!PyArg_ParseTuple(args, "sz", &local, &uri) || !reader_idle(self)) return NULL;
    return owned_string(xmlTextReaderGetAttributeNs(self->reader, BAD_CAST local, BAD_CAST uri));
}

static PyObject *
reader_lookupNamespace(ReaderObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "z", &prefix) || !reader_idle(self)) return NULL;
    return owned_string(xmlTextReaderLookupNamespace(self->reader, BAD_CAST prefix));
}

static PyObject *
reader_moveToAttribute(ReaderObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name) || !reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderMoveToAttribute(self->reader, BAD_CAST name));
}

static PyObject *
reader_moveToAttributeNo(ReaderObject *self, PyObject *args)
{
    int no;
    if (!PyArg_ParseTuple(args, "i", &no) || !reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderMoveToAttributeNo(self->reader, no));
}

static PyObject *
reader_moveToAttributeNs(ReaderObject *self, PyObject *args)
{
    const char *local, *uri;
    if (!PyArg_ParseTuple(args, "sz", &local, &uri) || !reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderMoveToAttributeNs(self->reader, BAD_CAST local,
                                                          BAD_CAST uri));
}

static PyObject *
reader_getAttributeHash(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;
    PyObject *dict = PyDict_New();
    if (dict == NULL) return NULL;

    xmlTextReaderPtr reader = self->reader;
    if (xmlTextReaderHasAttributes(reader) > 0 && xmlTextReaderMoveToFirstAttribute(reader) == 1) {
        do {
            PyObject *value = string_or_none(xmlTextReaderConstValue(reader));
            const xmlChar *name = xmlTextReaderConstName(reader);
            if (value == NULL || name == NULL
                || PyDict_SetItemString(dict, (const char *)name, value) < 0) {
                Py_XDECREF(value);
                xmlTextReaderMoveToElement(reader);
                Py_DECREF(dict);
                return NULL;
            }
            Py_DECREF(value);
        } while (xmlTextReaderMoveToNextAttribute(reader) == 1);
        xmlTextReaderMoveToElement(reader);
    }
    return dict;
}

static PyObject *
reader_getParserProp(ReaderObject *self, PyObject *args)
{
    int prop;
    if (!PyArg_ParseTuple(args, "i", &prop) || !reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderGetParserProp(self->reader, prop));
}

static PyObject *
reader_setParserProp(ReaderObject *self, PyObject *args)
{
    int prop, value;
    if (!PyArg_ParseTuple(args, "ip", &prop, &value) || !reader_idle(self)) return NULL;
    return PyLong_FromLong(xmlTextReaderSetParserProp(self->reader, prop, value));
}

static PyObject *
reader_readInnerXml(ReaderObject *self, PyObject *args)
{
    if (!reader_can_move(self)) return NULL;
    xmlChar *result;
    RUN(self, xmlTextReaderReadInnerXml(self->reader), result);
    return owned_string(result);
}

static PyObject *
reader_readOuterXml(ReaderObject *self, PyObject *args)
{
    if (!reader_can_move(self)) return NULL;
    xmlChar *result;
    RUN(self, xmlTextReaderReadOuterXml(self->reader), result);
    return owned_string(result);
}

/* With the positions in brackets, which Perl takes out in Python code. */
static PyObject *
reader_nodePath(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;
    xmlNodePtr node = xmlTextReaderCurrentNode(self->reader);
    if (node == NULL) Py_RETURN_NONE;
    return owned_string(xmlGetNodePath(node));
}

/* ------------------------------------------------------------------ */
/* Nodes                                                               */
/* ------------------------------------------------------------------ */

/* A copy in a document of its own: it does not depend on the reader. */
static PyObject *
reader_copyCurrentNode(ReaderObject *self, PyObject *args)
{
    int deep = 0;
    if (!PyArg_ParseTuple(args, "|p", &deep)) return NULL;
    if (!reader_can_move(self)) return NULL;

    xmlNodePtr node;
    if (deep) {
        RUN(self, xmlTextReaderExpand(self->reader), node);
    } else {
        node = xmlTextReaderCurrentNode(self->reader);
    }
    if (node == NULL) Py_RETURN_NONE;

    if (node->type == XML_NAMESPACE_DECL)
        return LibXML_Namespace_Copy((xmlNsPtr)node);
    if (node->type == XML_DOCUMENT_NODE || node->type == XML_HTML_DOCUMENT_NODE) {
        xmlDocPtr copy = xmlCopyDoc((xmlDocPtr)node, deep);
        if (copy == NULL) return PyErr_NoMemory();
        PyObject *result = LibXML_Node_Wrap((xmlNodePtr)copy);
        if (result == NULL) xmlFreeDoc(copy);
        return result;
    }

    PyObject *holder = LibXML_new_hidden_doc();
    if (holder == NULL) return NULL;
    xmlDocPtr doc = LibXML_DOC(holder);

    xmlNodePtr copy;
    if (node->type == XML_DTD_NODE) {
        copy = (xmlNodePtr)xmlCopyDtd((xmlDtdPtr)node);
        if (copy != NULL) xmlSetTreeDoc(copy, doc);
    } else {
        copy = xmlDocCopyNode(node, doc, deep ? 1 : 2);
    }
    if (copy == NULL) {
        Py_DECREF(holder);
        Py_RETURN_NONE;
    }
    PyObject *result = LibXML_Node_Wrap(copy);
    if (result == NULL) xmlFreeNode(copy);
    Py_DECREF(holder);
    return result;
}

/* The document itself: the reader keeps what Python refers to, and the document
 * cannot change before the end of the reading. */
static PyObject *
document_wrapper(ReaderObject *self)
{
    if (self->doc == NULL) {
        xmlDocPtr doc = xmlTextReaderCurrentDoc(self->reader);
        if (doc == NULL) Py_RETURN_NONE;
        self->doc = LibXML_Node_Wrap((xmlNodePtr)doc);
        if (self->doc == NULL) return NULL;
        if (!self->done)
            lock_doc(self);
    }
    Py_INCREF(self->doc);
    return self->doc;
}

static PyObject *
reader_document(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;
    return document_wrapper(self);
}

static PyObject *
reader_preserveNode(ReaderObject *self, PyObject *args)
{
    if (!reader_idle(self)) return NULL;

    PyObject *doc = document_wrapper(self);
    if (doc == NULL) return NULL;
    Py_DECREF(doc);
    if (doc == Py_None) Py_RETURN_NONE;

    xmlNodePtr node = xmlTextReaderPreserve(self->reader);
    if (node == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(node);
}

/* preservePattern(pattern, [uri, prefix, ...]): the Python side turns a dict. */
static PyObject *
reader_preservePattern(ReaderObject *self, PyObject *args)
{
    const char *pattern;
    PyObject *namespaces = Py_None;
    if (!PyArg_ParseTuple(args, "s|O", &pattern, &namespaces)) return NULL;
    if (!reader_idle(self)) return NULL;

    const xmlChar **list = NULL;
    if (namespaces != Py_None) {
        if (!PyList_Check(namespaces)) {
            PyErr_SetString(PyExc_TypeError, "namespaces must be a list");
            return NULL;
        }
        Py_ssize_t count = PyList_GET_SIZE(namespaces);
        list = PyMem_Calloc(count + 1, sizeof(xmlChar *));
        if (list == NULL) return PyErr_NoMemory();
        for (Py_ssize_t i = 0; i < count; i++) {
            const char *text = PyUnicode_AsUTF8(PyList_GET_ITEM(namespaces, i));
            if (text == NULL) {
                PyMem_Free(list);
                return NULL;
            }
            list[i] = BAD_CAST text;
        }
    }
    LibXML_errors_begin();
    int ret = xmlTextReaderPreservePattern(self->reader, BAD_CAST pattern, list);
    PyMem_Free(list);
    if (ret < 0) {
        LibXML_raise_last_error();
        return NULL;
    }
    return PyLong_FromLong(ret);
}

static PyMethodDef reader_methods[] = {
    {"read",                (PyCFunction)reader_read,                METH_NOARGS,  "Move to the next node"},
    {"next",                (PyCFunction)reader_next,                METH_NOARGS,  "Move to the next node, skipping the subtree"},
    {"nextSibling",         (PyCFunction)reader_nextSibling,         METH_NOARGS,  "Move to the next sibling"},
    {"nextElement",         (PyCFunction)reader_nextElement,         METH_VARARGS, "Move to the next element, with this name and namespace"},
    {"nextSiblingElement",  (PyCFunction)reader_nextSiblingElement,  METH_VARARGS, "Move to the next sibling element, with this name and namespace"},
    {"nextPatternMatch",    (PyCFunction)reader_nextPatternMatch,    METH_VARARGS, "Move to the next node the pattern matches"},
    {"skipSiblings",        (PyCFunction)reader_skipSiblings,        METH_NOARGS,  "Move to the end of the parent element"},
    {"finish",              (PyCFunction)reader_finish,              METH_NOARGS,  "Read to the end"},
    {"_close",              (PyCFunction)reader_close,               METH_NOARGS,  "Stop reading and release the input"},
    {"readAttributeValue",  (PyCFunction)reader_readAttributeValue,  METH_NOARGS,  "Read the value of the current attribute"},
    {"readInnerXml",        (PyCFunction)reader_readInnerXml,        METH_NOARGS,  "XML of the content of the current node"},
    {"readOuterXml",        (PyCFunction)reader_readOuterXml,        METH_NOARGS,  "XML of the current node"},
    {"matchesPattern",      (PyCFunction)reader_matchesPattern,      METH_VARARGS, "Tell if the pattern matches the current node"},
    {"attributeCount",      (PyCFunction)reader_attributeCount,      METH_NOARGS,  "Number of attributes"},
    {"depth",               (PyCFunction)reader_depth,               METH_NOARGS,  "Depth of the current node"},
    {"nodeType",            (PyCFunction)reader_nodeType,            METH_NOARGS,  "Type of the current node, XML_READER_TYPE_*"},
    {"readState",           (PyCFunction)reader_readState,           METH_NOARGS,  "State of the reader"},
    {"standalone",          (PyCFunction)reader_standalone,          METH_NOARGS,  "Standalone flag of the document"},
    {"lineNumber",          (PyCFunction)reader_lineNumber,          METH_NOARGS,  "Line of the parser"},
    {"columnNumber",        (PyCFunction)reader_columnNumber,        METH_NOARGS,  "Column of the parser"},
    {"byteConsumed",        (PyCFunction)reader_byteConsumed,        METH_NOARGS,  "Bytes consumed so far"},
    {"moveToElement",       (PyCFunction)reader_moveToElement,       METH_NOARGS,  "Back to the element of the current attribute"},
    {"moveToFirstAttribute",(PyCFunction)reader_moveToFirstAttribute,METH_NOARGS,  "Move to the first attribute"},
    {"moveToNextAttribute", (PyCFunction)reader_moveToNextAttribute, METH_NOARGS,  "Move to the next attribute"},
    {"moveToAttribute",     (PyCFunction)reader_moveToAttribute,     METH_VARARGS, "Move to the attribute of this name"},
    {"moveToAttributeNo",   (PyCFunction)reader_moveToAttributeNo,   METH_VARARGS, "Move to the attribute at this index"},
    {"moveToAttributeNs",   (PyCFunction)reader_moveToAttributeNs,   METH_VARARGS, "Move to the attribute of this name and namespace"},
    {"hasAttributes",       (PyCFunction)reader_hasAttributes,       METH_NOARGS,  "Tell if the current node has attributes"},
    {"hasValue",            (PyCFunction)reader_hasValue,            METH_NOARGS,  "Tell if the current node has a value"},
    {"isDefault",           (PyCFunction)reader_isDefault,           METH_NOARGS,  "Tell if the attribute comes from the DTD"},
    {"isEmptyElement",      (PyCFunction)reader_isEmptyElement,      METH_NOARGS,  "Tell if the element is empty, as <a/>"},
    {"isNamespaceDecl",     (PyCFunction)reader_isNamespaceDecl,     METH_NOARGS,  "Tell if the current node is a namespace declaration"},
    {"isValid",             (PyCFunction)reader_isValid,             METH_NOARGS,  "Tell if the document is valid so far"},
    {"baseURI",             (PyCFunction)reader_baseURI,             METH_NOARGS,  "Base URI of the current node"},
    {"encoding",            (PyCFunction)reader_encoding,            METH_NOARGS,  "Encoding of the document"},
    {"localName",           (PyCFunction)reader_localName,           METH_NOARGS,  "Local name of the current node"},
    {"name",                (PyCFunction)reader_name,                METH_NOARGS,  "Name of the current node"},
    {"namespaceURI",        (PyCFunction)reader_namespaceURI,        METH_NOARGS,  "Namespace of the current node"},
    {"prefix",              (PyCFunction)reader_prefix,              METH_NOARGS,  "Prefix of the current node"},
    {"value",               (PyCFunction)reader_value,               METH_NOARGS,  "Value of the current node"},
    {"xmlLang",             (PyCFunction)reader_xmlLang,             METH_NOARGS,  "xml:lang in scope"},
    {"xmlVersion",          (PyCFunction)reader_xmlVersion,          METH_NOARGS,  "XML version of the document"},
    {"quoteChar",           (PyCFunction)reader_quoteChar,           METH_NOARGS,  "Quote of the current attribute"},
    {"getAttribute",        (PyCFunction)reader_getAttribute,        METH_VARARGS, "Value of an attribute"},
    {"getAttributeNo",      (PyCFunction)reader_getAttributeNo,      METH_VARARGS, "Value of the attribute at this index"},
    {"getAttributeNs",      (PyCFunction)reader_getAttributeNs,      METH_VARARGS, "Value of the attribute of this name and namespace"},
    {"getAttributeHash",    (PyCFunction)reader_getAttributeHash,    METH_NOARGS,  "The attributes as a dict"},
    {"lookupNamespace",     (PyCFunction)reader_lookupNamespace,     METH_VARARGS, "Namespace of a prefix in scope"},
    {"_getParserProp",      (PyCFunction)reader_getParserProp,       METH_VARARGS, "Parser property by number"},
    {"_setParserProp",      (PyCFunction)reader_setParserProp,       METH_VARARGS, "Set a parser property by number"},
    {"_nodePath",           (PyCFunction)reader_nodePath,            METH_NOARGS,  "XPath of the current node, with positions"},
    {"copyCurrentNode",     (PyCFunction)reader_copyCurrentNode,     METH_VARARGS, "A copy of the current node, deep if asked"},
    {"document",            (PyCFunction)reader_document,            METH_NOARGS,  "The document the reader builds"},
    {"preserveNode",        (PyCFunction)reader_preserveNode,        METH_NOARGS,  "Keep the current node in the document"},
    {"_preservePattern",    (PyCFunction)reader_preservePattern,     METH_VARARGS, "Keep the nodes a pattern matches"},
    {NULL, NULL}
};

PyTypeObject LibXML_ReaderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml._libxml.Reader",
    .tp_basicsize = sizeof(ReaderObject),
    .tp_dealloc   = (destructor)reader_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = reader_new,
    .tp_iter      = PyObject_SelfIter,
    .tp_iternext  = (iternextfunc)reader_iternext,
    .tp_methods   = reader_methods,
    .tp_doc       = "xmlTextReader: the base of xml_libxml.Reader",
};

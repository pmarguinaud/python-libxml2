#include "libxml_parser.h"
#include "libxml_utils.h"
#include "libxml_document.h"
#include "libxml_node.h"
#include <libxml/parserInternals.h>

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int flag;
} ParserFlag;

/* The names are those of %PARSER_FLAGS in XML::LibXML. */
static const ParserFlag parser_flags[] = {
    {"expand_entities",     XML_PARSE_NOENT},
    {"load_ext_dtd",        XML_PARSE_DTDLOAD},
    {"complete_attributes", XML_PARSE_DTDATTR},
    {"validation",          XML_PARSE_DTDVALID},
    {"suppress_errors",     XML_PARSE_NOERROR},
    {"suppress_warnings",   XML_PARSE_NOWARNING},
    {"pedantic_parser",     XML_PARSE_PEDANTIC},
    {"pedantic",            XML_PARSE_PEDANTIC},
    {"no_blanks",           XML_PARSE_NOBLANKS},
    {"expand_xinclude",     XML_PARSE_XINCLUDE},
    {"xinclude",            XML_PARSE_XINCLUDE},
    {"no_network",          XML_PARSE_NONET},
    {"clean_namespaces",    XML_PARSE_NSCLEAN},
    {"no_cdata",            XML_PARSE_NOCDATA},
    {"no_xinclude_nodes",   XML_PARSE_NOXINCNODE},
    {"old10",               XML_PARSE_OLD10},
    {"no_base_fix",         XML_PARSE_NOBASEFIX},
    {"huge",                XML_PARSE_HUGE},
    {"oldsax",              XML_PARSE_OLDSAX},
    {NULL, 0}
};

/* Options kept as they are, in `extra`. Aliases point to the same key. */
static const char *
extra_key(const char *name)
{
    if (strcmp(name, "URI") == 0 || strcmp(name, "base_uri") == 0) return "base_uri";
    if (strcmp(name, "line_numbers") == 0)    return "line_numbers";
    if (strcmp(name, "gdome") == 0)           return "gdome";
    if (strcmp(name, "ext_ent_handler") == 0) return "ext_ent_handler";
    return NULL;
}

static int
find_flag(const char *name)
{
    for (const ParserFlag *f = parser_flags; f->name; f++) {
        if (strcmp(f->name, name) == 0) return f->flag;
    }
    return 0;
}

/* What one parse uses: the parser's options, changed by per-call ones. */
typedef struct {
    int flags;
    int recover;
    PyObject *extra;    /* owned */
    PyObject *held;     /* entity contents that must outlive the parse */
} ParseConfig;

static int
set_recover(int *flags, int *recover, PyObject *value)
{
    long v = (value == Py_None) ? 0 : PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        int truth = PyObject_IsTrue(value);
        if (truth < 0) return -1;
        v = truth;
    }
    *recover = (int)v;
    if (v) *flags |= XML_PARSE_RECOVER;
    else   *flags &= ~XML_PARSE_RECOVER;
    return 0;
}

/* Returns 0, or -1 with an exception. */
static int
apply_option(int *flags, int *recover, PyObject *extra, const char *name, PyObject *value)
{
    if (strcmp(name, "recover") == 0)
        return set_recover(flags, recover, value);

    if (strcmp(name, "keep_blanks") == 0) {
        int truth = PyObject_IsTrue(value);
        if (truth < 0) return -1;
        if (truth) *flags &= ~XML_PARSE_NOBLANKS;
        else       *flags |= XML_PARSE_NOBLANKS;
        return 0;
    }

    const char *key = extra_key(name);
    if (key != NULL)
        return PyDict_SetItemString(extra, key, value);

    int flag = find_flag(name);
    if (flag == 0) {
        PyErr_Format(PyExc_TypeError, "'%s' is an invalid parser option", name);
        return -1;
    }
    int truth = PyObject_IsTrue(value);
    if (truth < 0) return -1;
    if (truth) *flags |= flag;
    else       *flags &= ~flag;
    return 0;
}

/* Apply keyword options as Perl's constructor does: keep_blanks gives way to
 * no_blanks, and expand_entities turns load_ext_dtd on with it. */
static int
apply_options(int *flags, int *recover, PyObject *extra, PyObject *kwds)
{
    if (kwds == NULL) return 0;

    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(kwds, &pos, &key, &value)) {
        const char *name = PyUnicode_AsUTF8(key);
        if (name == NULL) return -1;
        if (strcmp(name, "keep_blanks") == 0
            && PyDict_GetItemString(kwds, "no_blanks") != NULL)
            continue;
        if (apply_option(flags, recover, extra, name, value) < 0) return -1;
    }

    PyObject *expand = PyDict_GetItemString(kwds, "expand_entities");
    if (expand != NULL && PyDict_GetItemString(kwds, "load_ext_dtd") == NULL)
        return apply_option(flags, recover, extra, "load_ext_dtd", expand);
    return 0;
}

static int
config_init(ParseConfig *cfg, LibXML_ParserObject *self, PyObject *kwds)
{
    cfg->flags = self->flags;
    cfg->recover = self->recover;
    cfg->held = NULL;
    cfg->extra = PyDict_Copy(self->extra);
    if (cfg->extra == NULL) return -1;
    if (apply_options(&cfg->flags, &cfg->recover, cfg->extra, kwds) < 0) {
        Py_DECREF(cfg->extra);
        return -1;
    }
    return 0;
}

static void
config_clear(ParseConfig *cfg)
{
    Py_CLEAR(cfg->extra);
    Py_CLEAR(cfg->held);
}

/* The flags given to libxml2. As in Perl, without load_ext_dtd all that needs
 * the DTD is off, which is what keeps external entities out by default. */
static int
xml_options(const ParseConfig *cfg)
{
    int opts = cfg->flags;
    if (!(opts & XML_PARSE_DTDLOAD))
        opts &= ~(XML_PARSE_DTDVALID | XML_PARSE_DTDATTR | XML_PARSE_NOENT);
    return opts;
}

/* ------------------------------------------------------------------ */
/* External entities                                                   */
/* ------------------------------------------------------------------ */

static xmlExternalEntityLoader saved_loader = NULL;

/* These calls are deprecated in recent libxml2, without a replacement that
 * also exists in older ones. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static xmlParserInputPtr
entity_loader(const char *url, const char *id, xmlParserCtxtPtr ctxt)
{
    ParseConfig *cfg = ctxt ? (ParseConfig *)ctxt->_private : NULL;
    PyObject *handler = cfg ? PyDict_GetItemString(cfg->extra, "ext_ent_handler") : NULL;

    if (handler == NULL || handler == Py_None || PyErr_Occurred())
        return saved_loader ? saved_loader(url, id, ctxt) : NULL;

    PyObject *result = PyObject_CallFunction(handler, "ss", url ? url : "", id ? id : "");
    if (result == NULL) return NULL;

    PyObject *bytes;
    if (PyUnicode_Check(result)) {
        bytes = PyUnicode_AsUTF8String(result);
        Py_DECREF(result);
    } else if (PyBytes_Check(result)) {
        bytes = result;
    } else {
        Py_DECREF(result);
        PyErr_SetString(PyExc_TypeError, "ext_ent_handler must return str or bytes");
        return NULL;
    }
    if (bytes == NULL) return NULL;

    /* An older libxml2 does not copy the string. */
    if (cfg->held == NULL) cfg->held = PyList_New(0);
    if (cfg->held == NULL || PyList_Append(cfg->held, bytes) < 0) {
        Py_DECREF(bytes);
        return NULL;
    }
    Py_DECREF(bytes);

    return xmlNewStringInputStream(ctxt, BAD_CAST PyBytes_AS_STRING(bytes));
}

static void
loader_begin(const ParseConfig *cfg)
{
    PyObject *handler = PyDict_GetItemString(cfg->extra, "ext_ent_handler");
    if (handler != NULL && handler != Py_None) {
        saved_loader = xmlGetExternalEntityLoader();
        xmlSetExternalEntityLoader(entity_loader);
    }
}

static void
loader_end(const ParseConfig *cfg)
{
    PyObject *handler = PyDict_GetItemString(cfg->extra, "ext_ent_handler");
    if (handler != NULL && handler != Py_None) {
        xmlSetExternalEntityLoader(saved_loader);
        saved_loader = NULL;
    }
}

/* Blanks in a chunk: the chunk API has no option for it. */
PyObject *
LibXML_parse_chunk(const char *chunk, int keep_blanks)
{
    if (chunk[0] == '\0') {
        PyErr_SetString(LibXML_Error, "Empty String");
        return NULL;
    }

    /* A temporary document holds the fragment */
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (!doc) return PyErr_NoMemory();

    LibXML_errors_begin();

    xmlNodePtr list = NULL;
    int old_blanks = xmlKeepBlanksDefault(keep_blanks);
    int ret = xmlParseBalancedChunkMemory(doc, NULL, NULL, 0, BAD_CAST chunk, &list);
    xmlKeepBlanksDefault(old_blanks);

    if (ret != 0) {
        if (list) xmlFreeNodeList(list);
        xmlFreeDoc(doc);
        LibXML_raise_last_error();
        return NULL;
    }

    /* The fragment owns its nodes, and the document holding them stays private */
    PyObject *doc_wrap = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (!doc_wrap) {
        if (list) xmlFreeNodeList(list);
        xmlFreeDoc(doc);
        return NULL;
    }
    ((LibXML_NodeObject *)doc_wrap)->hidden = 1;

    xmlNodePtr frag = xmlNewDocFragment(doc);
    if (!frag) {
        if (list) xmlFreeNodeList(list);
        Py_DECREF(doc_wrap);
        return PyErr_NoMemory();
    }
    xmlAddChildList(frag, list);

    PyObject *frag_wrap = LibXML_Node_Wrap(frag);
    if (!frag_wrap) xmlFreeNode(frag);
    Py_DECREF(doc_wrap);
    return frag_wrap;
}

#pragma GCC diagnostic pop

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    PyObject *hold;         /* keeps `data` valid */
    const char *data;
    Py_ssize_t size;
    int is_text;            /* came from a str, so it is UTF-8 */
} Input;

static void
input_clear(Input *in)
{
    Py_CLEAR(in->hold);
}

/* Accept str or a bytes-like object. */
static int
input_init(Input *in, PyObject *obj)
{
    memset(in, 0, sizeof(*in));

    if (PyUnicode_Check(obj)) {
        in->data = PyUnicode_AsUTF8AndSize(obj, &in->size);
        if (in->data == NULL) return -1;
        Py_INCREF(obj);
        in->hold = obj;
        in->is_text = 1;
    } else {
        in->hold = PyBytes_FromObject(obj);
        if (in->hold == NULL) return -1;
        in->data = PyBytes_AS_STRING(in->hold);
        in->size = PyBytes_GET_SIZE(in->hold);
    }

    if (in->size == 0) {
        input_clear(in);
        PyErr_SetString(LibXML_Error, "Empty String");
        return -1;
    }
    return 0;
}

/* The encoding in the XML declaration, or NULL. To free with xmlFree. */
static xmlChar *
declared_encoding(const char *data, Py_ssize_t size)
{
    if (size < 6 || strncmp(data, "<?xml", 5) != 0) return NULL;

    Py_ssize_t end = 5;
    while (end + 1 < size && !(data[end] == '?' && data[end + 1] == '>')) end++;

    for (Py_ssize_t i = 5; i + 8 < end; i++) {
        if (strncmp(data + i, "encoding", 8) != 0) continue;
        i += 8;
        while (i < end && (data[i] == ' ' || data[i] == '=' || data[i] == '\t')) i++;
        if (i >= end || (data[i] != '"' && data[i] != '\'')) return NULL;
        char quote = data[i++];
        Py_ssize_t start = i;
        while (i < end && data[i] != quote) i++;
        return i < end ? xmlStrndup(BAD_CAST data + start, (int)(i - start)) : NULL;
    }
    return NULL;
}

/* Read a file-like object. */
static int
input_from_file(Input *in, PyObject *fh)
{
    PyObject *content = PyObject_CallMethod(fh, "read", NULL);
    if (content == NULL) return -1;
    int rc = input_init(in, content);
    Py_DECREF(content);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Finishing a parse                                                   */
/* ------------------------------------------------------------------ */

static void
set_doc_uri(xmlDocPtr doc, const char *uri)
{
    char unknown[64];

    if (uri == NULL) {
        snprintf(unknown, sizeof(unknown), "unknown-%p", (void *)doc);
        uri = unknown;
    }
    if (doc->URL) xmlFree((xmlChar *)doc->URL);
    doc->URL = xmlStrdup(BAD_CAST uri);
}

/* With suppress_errors, a recovering parse has nothing to tell. libxml2 reports
 * some errors to the structured handler anyway. */
static int
quiet_recover(int recover, int flags)
{
    return (recover && (flags & XML_PARSE_NOERROR)) ? 2 : recover;
}

/* acceptable: the parse went well enough to keep the document. */
static PyObject *
finish_parse(xmlDocPtr doc, int acceptable, int recover)
{
    /* An exception from a callback wins. */
    if (PyErr_Occurred()) {
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        LibXML_errors_begin();
        PyErr_Restore(type, value, tb);
        if (doc) xmlFreeDoc(doc);
        return NULL;
    }

    if (LibXML_report_errors(recover) < 0) {
        if (doc) xmlFreeDoc(doc);
        return NULL;
    }

    if (doc == NULL || !acceptable) {
        if (doc) xmlFreeDoc(doc);
        PyErr_SetString(LibXML_Error, "parsing failed");
        return NULL;
    }

    PyObject *result = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (result == NULL) xmlFreeDoc(doc);
    return result;
}

static const char *
config_uri(const ParseConfig *cfg, PyObject **hold)
{
    *hold = NULL;
    PyObject *uri = PyDict_GetItemString(cfg->extra, "base_uri");
    if (uri == NULL || uri == Py_None) return NULL;
    *hold = PyObject_Str(uri);
    return *hold ? PyUnicode_AsUTF8(*hold) : NULL;
}

/* Parse from memory, or from a file if filename is set. */
static PyObject *
parse_xml(ParseConfig *cfg, const Input *in, const char *filename,
          const char *url, const char *encoding)
{
    PyObject *uri_hold = NULL;
    if (url == NULL)
        url = config_uri(cfg, &uri_hold);

    /* A str is text already: its declaration says nothing about these bytes. */
    xmlChar *declared = NULL;
    if (in != NULL && in->is_text && encoding == NULL) {
        declared = declared_encoding(in->data, in->size);
        if (declared != NULL)
            encoding = "UTF-8";
    }

    LibXML_errors_begin();

    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    if (ctxt == NULL) {
        xmlFree(declared);
        Py_XDECREF(uri_hold);
        return PyErr_NoMemory();
    }
    ctxt->_private = cfg;

    loader_begin(cfg);
    xmlDocPtr doc;
    if (filename != NULL)
        doc = xmlCtxtReadFile(ctxt, filename, encoding, xml_options(cfg));
    else
        doc = xmlCtxtReadMemory(ctxt, in->data, (int)in->size, url, encoding,
                                xml_options(cfg));
    loader_end(cfg);

    int well_formed = ctxt->wellFormed;
    int valid = ctxt->valid;
    int validate = (xml_options(cfg) & XML_PARSE_DTDVALID) != 0;
    xmlFreeParserCtxt(ctxt);

    int acceptable = 0;
    if (doc != NULL) {
        acceptable = cfg->recover
            || (well_formed && (!validate || valid
                                || (doc->intSubset == NULL && doc->extSubset == NULL)));
        if (filename == NULL || url != NULL)
            set_doc_uri(doc, url);
        if (declared != NULL) {
            xmlFree((xmlChar *)doc->encoding);
            doc->encoding = declared;
            declared = NULL;
        }
    }

    xmlFree(declared);
    Py_XDECREF(uri_hold);
    return finish_parse(doc, acceptable, quiet_recover(cfg->recover, cfg->flags));
}

/* ------------------------------------------------------------------ */
/* XML entry points                                                    */
/* ------------------------------------------------------------------ */

static PyObject *
parse_object(ParseConfig *cfg, PyObject *string, const char *url, const char *encoding)
{
    Input in;
    if (input_init(&in, string) < 0) return NULL;
    PyObject *result = parse_xml(cfg, &in, NULL, url, encoding);
    input_clear(&in);
    return result;
}

static PyObject *
parse_handle(ParseConfig *cfg, PyObject *fh, const char *url, const char *encoding)
{
    Input in;
    if (input_from_file(&in, fh) < 0) return NULL;
    PyObject *result = parse_xml(cfg, &in, NULL, url, encoding);
    input_clear(&in);
    return result;
}

static PyObject *
parse_path(ParseConfig *cfg, PyObject *path, const char *encoding)
{
    PyObject *bytes = NULL;
    if (!PyUnicode_FSConverter(path, &bytes)) return NULL;
    PyObject *result;
    if (PyBytes_GET_SIZE(bytes) == 0) {
        PyErr_SetString(LibXML_Error, "Empty filename");
        result = NULL;
    } else {
        result = parse_xml(cfg, NULL, PyBytes_AS_STRING(bytes), NULL, encoding);
    }
    Py_DECREF(bytes);
    return result;
}

static PyObject *
parser_parse_string(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *string;
    const char *url = NULL, *encoding = NULL;
    static char *kwlist[] = {"string", "URL", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|zz", kwlist, &string, &url, &encoding))
        return NULL;

    ParseConfig cfg;
    if (config_init(&cfg, self, NULL) < 0) return NULL;
    PyObject *result = parse_object(&cfg, string, url, encoding);
    config_clear(&cfg);
    return result;
}

static PyObject *
parser_parse_file(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *path;
    const char *encoding = NULL;
    static char *kwlist[] = {"filename", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|z", kwlist, &path, &encoding))
        return NULL;

    ParseConfig cfg;
    if (config_init(&cfg, self, NULL) < 0) return NULL;
    PyObject *result = parse_path(&cfg, path, encoding);
    config_clear(&cfg);
    return result;
}

static PyObject *
parser_parse_fh(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *fh;
    const char *url = NULL, *encoding = NULL;
    static char *kwlist[] = {"fh", "URL", "encoding", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|zz", kwlist, &fh, &url, &encoding))
        return NULL;

    ParseConfig cfg;
    if (config_init(&cfg, self, NULL) < 0) return NULL;
    PyObject *result = parse_handle(&cfg, fh, url, encoding);
    config_clear(&cfg);
    return result;
}

/* Take one key out of a dict of options. Returns a new reference, or NULL. */
static PyObject *
pop_option(PyObject *dict, const char *key)
{
    PyObject *value = PyDict_GetItemString(dict, key);
    if (value == NULL) return NULL;
    Py_INCREF(value);
    PyDict_DelItemString(dict, key);
    if (value == Py_None) {
        Py_DECREF(value);
        return NULL;
    }
    return value;
}

/* String form of an option, kept alive by *hold. */
static const char *
option_string(PyObject *value, PyObject **hold)
{
    *hold = NULL;
    if (value == NULL) return NULL;
    *hold = PyObject_Str(value);
    return *hold ? PyUnicode_AsUTF8(*hold) : NULL;
}

typedef struct {
    PyObject *string, *location, *io, *uri, *encoding;
    PyObject *uri_str, *encoding_str;
    const char *url, *enc;
    PyObject *options;  /* what is left */
} LoadArgs;

static void
load_args_clear(LoadArgs *a)
{
    Py_XDECREF(a->string);
    Py_XDECREF(a->location);
    Py_XDECREF(a->io);
    Py_XDECREF(a->uri);
    Py_XDECREF(a->encoding);
    Py_XDECREF(a->uri_str);
    Py_XDECREF(a->encoding_str);
    Py_XDECREF(a->options);
}

static int
load_args_init(LoadArgs *a, PyObject *args, PyObject *kwds)
{
    memset(a, 0, sizeof(*a));

    a->options = kwds ? PyDict_Copy(kwds) : PyDict_New();
    if (a->options == NULL) return -1;

    /* The first positional argument is the string, as in parse_string. */
    if (PyTuple_GET_SIZE(args) > 1) {
        PyErr_SetString(PyExc_TypeError, "takes keyword arguments after the string");
        return -1;
    }
    if (PyTuple_GET_SIZE(args) == 1) {
        a->string = PyTuple_GET_ITEM(args, 0);
        Py_INCREF(a->string);
    } else {
        a->string = pop_option(a->options, "string");
    }

    a->location = pop_option(a->options, "location");
    a->io       = pop_option(a->options, "IO");
    a->uri      = pop_option(a->options, "URI");
    a->encoding = pop_option(a->options, "encoding");
    if (a->uri == NULL) a->uri = pop_option(a->options, "URL");

    if (a->uri != NULL && (a->url = option_string(a->uri, &a->uri_str)) == NULL) return -1;
    if (a->encoding != NULL
        && (a->enc = option_string(a->encoding, &a->encoding_str)) == NULL) return -1;
    return 0;
}

static PyObject *
parser_load_xml(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    LoadArgs a;
    if (load_args_init(&a, args, kwds) < 0) {
        load_args_clear(&a);
        return NULL;
    }

    ParseConfig cfg;
    if (config_init(&cfg, self, a.options) < 0) {
        load_args_clear(&a);
        return NULL;
    }

    PyObject *result = NULL;
    if (a.location != NULL)
        result = parse_path(&cfg, a.location, a.enc);
    else if (a.string != NULL)
        result = parse_object(&cfg, a.string, a.url, a.enc);
    else if (a.io != NULL)
        result = parse_handle(&cfg, a.io, a.url, a.enc);
    else
        PyErr_SetString(PyExc_ValueError,
                        "load_xml requires string=, location=, or IO= argument");

    config_clear(&cfg);
    load_args_clear(&a);
    return result;
}

/* ------------------------------------------------------------------ */
/* HTML                                                                */
/* ------------------------------------------------------------------ */

/* Same values as in _html_options of XML::LibXML. */
#define HTML_OPT_RECOVER            1
#define HTML_OPT_NODEFDTD           4
#define HTML_OPT_NOERROR           32
#define HTML_OPT_NOWARNING         64
#define HTML_OPT_PEDANTIC         128
#define HTML_OPT_NOBLANKS         256
#define HTML_OPT_NONET           2048
#define HTML_OPT_NOCDATA        16384
#define HTML_OPT_HUGE          524288
#define HTML_OPT_OLDSAX       1048576

/* The option if it is in opts, else the default. Returns 0, 1, or -1. */
static int
html_flag(PyObject *opts, const char *name, int dflt)
{
    PyObject *value = PyDict_GetItemString(opts, name);
    if (value == NULL) return dflt;
    return PyObject_IsTrue(value);
}

static int
html_options(LibXML_ParserObject *self, PyObject *opts, int *recover)
{
    int flags = 0;

    int rec = self->recover;
    PyObject *value = PyDict_GetItemString(opts, "recover");
    int unused = 0;
    if (value != NULL && set_recover(&unused, &rec, value) < 0) return -1;

    *recover = rec;
    if (rec) flags |= HTML_OPT_RECOVER;
    if (rec == 2) flags |= HTML_OPT_NOERROR;
    if (self->recover == 2) flags |= HTML_OPT_NOERROR;

    static const struct { const char *name; int flag, self_flag; } table[] = {
        {"no_defdtd",         HTML_OPT_NODEFDTD,  0},
        {"suppress_errors",   HTML_OPT_NOERROR,   XML_PARSE_NOERROR},
        {"suppress_warnings", HTML_OPT_NOWARNING, 0},
        {"pedantic_parser",   HTML_OPT_PEDANTIC,  XML_PARSE_PEDANTIC},
        {"no_blanks",         HTML_OPT_NOBLANKS,  XML_PARSE_NOBLANKS},
        {"no_network",        HTML_OPT_NONET,     XML_PARSE_NONET},
        {"no_cdata",          HTML_OPT_NOCDATA,   0},
        {"huge",              HTML_OPT_HUGE,      0},
        {"oldsax",            HTML_OPT_OLDSAX,    0},
        {NULL, 0, 0}
    };

    for (int i = 0; table[i].name; i++) {
        int dflt = (self->flags & table[i].self_flag) != 0;
        int on = html_flag(opts, table[i].name, dflt);
        if (on < 0) return -1;
        if (on) flags |= table[i].flag;
    }

    /* "compact" is accepted and ignored: Perl warns that it breaks changing the
     * tree, and here nothing may crash. */
    return flags;
}

static int
html_known_option(const char *name)
{
    static const char *known[] = {
        "recover", "no_defdtd", "suppress_errors", "suppress_warnings",
        "pedantic_parser", "no_blanks", "no_network", "no_cdata", "compact",
        "huge", "oldsax", NULL
    };
    for (int i = 0; known[i]; i++) {
        if (strcmp(known[i], name) == 0) return 1;
    }
    return 0;
}

static int
html_check_options(PyObject *opts)
{
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(opts, &pos, &key, &value)) {
        const char *name = PyUnicode_AsUTF8(key);
        if (name == NULL) return -1;
        if (!html_known_option(name)) {
            PyErr_Format(PyExc_TypeError, "'%s' is an invalid HTML parser option", name);
            return -1;
        }
    }
    return 0;
}

static PyObject *
parse_html(LibXML_ParserObject *self, const Input *in, const char *filename,
           const char *url, const char *encoding, PyObject *opts)
{
    if (html_check_options(opts) < 0) return NULL;

    int recover = 0;
    int flags = html_options(self, opts, &recover);
    if (flags < 0) return NULL;

    /* A str is text already. */
    if (in != NULL && in->is_text && encoding == NULL)
        encoding = "UTF-8";

    LibXML_errors_begin();

    htmlParserCtxtPtr ctxt = htmlNewParserCtxt();
    if (ctxt == NULL) return PyErr_NoMemory();

    htmlDocPtr doc;
    if (filename != NULL)
        doc = htmlCtxtReadFile(ctxt, filename, encoding, flags);
    else
        doc = htmlCtxtReadMemory(ctxt, in->data, (int)in->size, url, encoding, flags);
    htmlFreeParserCtxt(ctxt);

    if (doc != NULL && (filename == NULL || url != NULL))
        set_doc_uri(doc, url);

    return finish_parse(doc, 1, quiet_recover(recover, flags));
}

/* Split keywords into URI, encoding and the HTML options. */
static PyObject *
html_entry(LibXML_ParserObject *self, PyObject *source, int source_is_path,
           PyObject *fh, PyObject *options, const char *url, const char *encoding)
{
    PyObject *result = NULL;

    if (source_is_path) {
        PyObject *bytes = NULL;
        if (!PyUnicode_FSConverter(source, &bytes)) return NULL;
        result = parse_html(self, NULL, PyBytes_AS_STRING(bytes), url, encoding, options);
        Py_DECREF(bytes);
    } else {
        Input in;
        if (fh != NULL) {
            if (input_from_file(&in, fh) < 0) return NULL;
        } else if (input_init(&in, source) < 0) {
            return NULL;
        }
        result = parse_html(self, &in, NULL, url, encoding, options);
        input_clear(&in);
    }
    return result;
}

static PyObject *
parser_html(LibXML_ParserObject *self, PyObject *args, PyObject *kwds, int kind)
{
    LoadArgs a;
    PyObject *result = NULL;
    if (load_args_init(&a, args, kwds) < 0) goto done;

    /* kind: 0 string, 1 file, 2 file handle, 3 load_html */
    if (kind == 1) {
        result = html_entry(self, a.string ? a.string : a.location, 1, NULL,
                            a.options, a.url, a.enc);
    } else if (kind == 2) {
        result = html_entry(self, NULL, 0, a.string ? a.string : a.io,
                            a.options, a.url, a.enc);
    } else if (kind == 0 || a.string != NULL) {
        result = html_entry(self, a.string, 0, NULL, a.options, a.url, a.enc);
    } else if (a.location != NULL) {
        result = html_entry(self, a.location, 1, NULL, a.options, a.url, a.enc);
    } else if (a.io != NULL) {
        result = html_entry(self, NULL, 0, a.io, a.options, a.url, a.enc);
    } else {
        PyErr_SetString(PyExc_ValueError,
                        "load_html requires string=, location=, or IO= argument");
    }

done:
    load_args_clear(&a);
    return result;
}

static PyObject *
parser_parse_html_string(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    return parser_html(self, args, kwds, 0);
}

static PyObject *
parser_parse_html_file(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    return parser_html(self, args, kwds, 1);
}

static PyObject *
parser_parse_html_fh(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    return parser_html(self, args, kwds, 2);
}

static PyObject *
parser_load_html(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    return parser_html(self, args, kwds, 3);
}

/* ------------------------------------------------------------------ */
/* parse_balanced_chunk                                                */
/* ------------------------------------------------------------------ */

static PyObject *
parser_parse_balanced_chunk(LibXML_ParserObject *self, PyObject *args)
{
    const char *chunk;
    if (!PyArg_ParseTuple(args, "s", &chunk)) return NULL;
    return LibXML_parse_chunk(chunk, !(self->flags & XML_PARSE_NOBLANKS));
}

/* ------------------------------------------------------------------ */
/* Options as attributes and through get_option                        */
/* ------------------------------------------------------------------ */

static PyObject *
parser_option_exists(LibXML_ParserObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return PyBool_FromLong(strcmp(name, "recover") == 0 || extra_key(name) != NULL
                           || find_flag(name) != 0);
}

static PyObject *
get_option(LibXML_ParserObject *self, const char *name)
{
    if (strcmp(name, "recover") == 0)
        return PyLong_FromLong(self->recover);

    const char *key = extra_key(name);
    if (key != NULL) {
        PyObject *value = PyDict_GetItemString(self->extra, key);
        if (value == NULL) Py_RETURN_NONE;
        Py_INCREF(value);
        return value;
    }

    int flag = find_flag(name);
    if (flag == 0) {
        PyErr_Format(PyExc_ValueError, "unknown parser option '%s'", name);
        return NULL;
    }
    return PyLong_FromLong((self->flags & flag) != 0);
}

static PyObject *
parser_get_option(LibXML_ParserObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return get_option(self, name);
}

static PyObject *
parser_set_option(LibXML_ParserObject *self, PyObject *args)
{
    const char *name;
    PyObject *value;
    if (!PyArg_ParseTuple(args, "sO", &name, &value)) return NULL;
    if (apply_option(&self->flags, &self->recover, self->extra, name, value) < 0)
        return NULL;
    return get_option(self, name);
}

static PyObject *
parser_set_options(LibXML_ParserObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *dict = NULL;
    if (!PyArg_ParseTuple(args, "|O!", &PyDict_Type, &dict)) return NULL;

    PyObject *sources[2] = {dict, kwds};
    for (int i = 0; i < 2; i++) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (sources[i] && PyDict_Next(sources[i], &pos, &key, &value)) {
            const char *name = PyUnicode_AsUTF8(key);
            if (name == NULL) return NULL;
            if (apply_option(&self->flags, &self->recover, self->extra, name, value) < 0)
                return NULL;
        }
    }
    Py_RETURN_NONE;
}

static int
reject_delete(PyObject *value)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "cannot delete parser option");
        return -1;
    }
    return 0;
}

/* closure: the name of the option */
static PyObject *
option_getter(LibXML_ParserObject *self, void *closure)
{
    return get_option(self, (const char *)closure);
}

static int
option_setter(LibXML_ParserObject *self, PyObject *value, void *closure)
{
    if (reject_delete(value) < 0) return -1;
    return apply_option(&self->flags, &self->recover, self->extra,
                        (const char *)closure, value);
}

static PyObject *
get_keep_blanks(LibXML_ParserObject *self, void *closure)
{
    return PyLong_FromLong(!(self->flags & XML_PARSE_NOBLANKS));
}

static PyObject *
get_recover_silently(LibXML_ParserObject *self, void *closure)
{
    return PyLong_FromLong(self->recover == 2);
}

static int
set_recover_silently(LibXML_ParserObject *self, PyObject *value, void *closure)
{
    if (reject_delete(value) < 0) return -1;
    int truth = PyObject_IsTrue(value);
    if (truth < 0) return -1;
    PyObject *level = PyLong_FromLong(truth ? 2 : 0);
    if (level == NULL) return -1;
    int rc = set_recover(&self->flags, &self->recover, level);
    Py_DECREF(level);
    return rc;
}

/* As in Perl, turning this on loads the DTD too, unlike set_option. */
static int
set_expand_entities(LibXML_ParserObject *self, PyObject *value, void *closure)
{
    if (reject_delete(value) < 0) return -1;
    int truth = PyObject_IsTrue(value);
    if (truth < 0) return -1;
    if (truth) self->flags |= XML_PARSE_NOENT | XML_PARSE_DTDLOAD;
    else       self->flags &= ~XML_PARSE_NOENT;
    return 0;
}

#define OPTION(name) \
    {name, (getter)option_getter, (setter)option_setter, NULL, name}

static PyGetSetDef LibXML_Parser_getset[] = {
    OPTION("recover"),
    OPTION("load_ext_dtd"),
    OPTION("complete_attributes"),
    OPTION("validation"),
    OPTION("suppress_errors"),
    OPTION("suppress_warnings"),
    OPTION("pedantic_parser"),
    OPTION("pedantic"),
    OPTION("no_blanks"),
    OPTION("expand_xinclude"),
    OPTION("xinclude"),
    OPTION("no_network"),
    OPTION("clean_namespaces"),
    OPTION("no_cdata"),
    OPTION("no_xinclude_nodes"),
    OPTION("old10"),
    OPTION("no_base_fix"),
    OPTION("huge"),
    OPTION("oldsax"),
    OPTION("line_numbers"),
    OPTION("base_uri"),
    OPTION("URI"),
    OPTION("gdome"),
    OPTION("ext_ent_handler"),
    {"expand_entities", (getter)option_getter, (setter)set_expand_entities, NULL,
     "expand_entities"},
    {"keep_blanks", (getter)get_keep_blanks, (setter)option_setter, NULL, "keep_blanks"},
    {"recover_silently", (getter)get_recover_silently, (setter)set_recover_silently,
     NULL, NULL},
    {NULL}
};

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Parser_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (PyTuple_GET_SIZE(args) > 1
        || (PyTuple_GET_SIZE(args) == 1 && !PyDict_Check(PyTuple_GET_ITEM(args, 0)))) {
        PyErr_SetString(PyExc_TypeError, "Parser takes options as keywords, or a dict");
        return NULL;
    }

    LibXML_ParserObject *self = (LibXML_ParserObject *)type->tp_alloc(type, 0);
    if (!self) return NULL;

    self->flags = XML_PARSE_NONET;
    self->extra = PyDict_New();
    if (self->extra == NULL) {
        Py_DECREF(self);
        return NULL;
    }

    PyObject *dict = PyTuple_GET_SIZE(args) ? PyTuple_GET_ITEM(args, 0) : NULL;
    if (apply_options(&self->flags, &self->recover, self->extra, dict) < 0
        || apply_options(&self->flags, &self->recover, self->extra, kwds) < 0) {
        Py_DECREF(self);
        return NULL;
    }
    return (PyObject *)self;
}

static int
parser_traverse(LibXML_ParserObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->extra);
    return 0;
}

static int
parser_clear(LibXML_ParserObject *self)
{
    Py_CLEAR(self->extra);
    return 0;
}

static void
parser_dealloc(LibXML_ParserObject *self)
{
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->extra);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_Parser_methods[] = {
    {"parse_string",         (PyCFunction)parser_parse_string,         METH_VARARGS|METH_KEYWORDS, "Parse XML from a str or bytes"},
    {"parse_file",           (PyCFunction)parser_parse_file,           METH_VARARGS|METH_KEYWORDS, "Parse an XML file"},
    {"parse_fh",             (PyCFunction)parser_parse_fh,             METH_VARARGS|METH_KEYWORDS, "Parse XML from a file object"},
    {"load_xml",             (PyCFunction)parser_load_xml,             METH_VARARGS|METH_KEYWORDS, "Load XML (string=, location= or IO=), with options"},
    {"parse_html_string",    (PyCFunction)parser_parse_html_string,    METH_VARARGS|METH_KEYWORDS, "Parse HTML from a str or bytes"},
    {"parse_html_file",      (PyCFunction)parser_parse_html_file,      METH_VARARGS|METH_KEYWORDS, "Parse an HTML file"},
    {"parse_html_fh",        (PyCFunction)parser_parse_html_fh,        METH_VARARGS|METH_KEYWORDS, "Parse HTML from a file object"},
    {"load_html",            (PyCFunction)parser_load_html,            METH_VARARGS|METH_KEYWORDS, "Load HTML (string=, location= or IO=), with options"},
    {"parse_balanced_chunk", (PyCFunction)parser_parse_balanced_chunk, METH_VARARGS,               "Parse a well-balanced XML fragment"},
    {"option_exists",        (PyCFunction)parser_option_exists,        METH_VARARGS,               "Tell if an option has this name"},
    {"get_option",           (PyCFunction)parser_get_option,           METH_VARARGS,               "Get an option"},
    {"set_option",           (PyCFunction)parser_set_option,           METH_VARARGS,               "Set an option"},
    {"set_options",          (PyCFunction)parser_set_options,          METH_VARARGS|METH_KEYWORDS, "Set options"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_ParserType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Parser",
    .tp_basicsize = sizeof(LibXML_ParserObject),
    .tp_dealloc   = (destructor)parser_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_traverse  = (traverseproc)parser_traverse,
    .tp_clear     = (inquiry)parser_clear,
    .tp_new       = LibXML_Parser_new,
    .tp_methods   = LibXML_Parser_methods,
    .tp_getset    = LibXML_Parser_getset,
    .tp_doc       = "XML/HTML parser",
};

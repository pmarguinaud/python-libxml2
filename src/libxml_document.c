#include <stdlib.h>
#include "libxml_document.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_dtd.h"
#include "libxml_nodelist.h"
#include "libxml_element.h"
#include "libxml_text.h"
#include "libxml_attr.h"

/* ------------------------------------------------------------------ */
/* tp_dealloc for Document                                             */
/* ------------------------------------------------------------------ */

static void
LibXML_Document_dealloc(LibXML_NodeObject *self)
{
    PyObject_GC_UnTrack(self);
    if (self->node != NULL) {
        xmlDocPtr doc = (xmlDocPtr)self->node;
        self->node = NULL;

        LibXML_NodePrivate *priv = (LibXML_NodePrivate *)doc->_private;
        if (priv)
            LibXML_free_old_dtds(&priv->old_dtds);

        /* Every wrapper of this document held a reference to it, so only the
         * private structs of nodes with cargo can remain. */
        LibXML_clear_private((xmlNodePtr)doc);
        if (doc->extSubset)
            LibXML_clear_private((xmlNodePtr)doc->extSubset);
        xmlFreeDoc(doc);
    }
    /* doc_obj and owner are always NULL for Document */
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* Construction: Document(version="1.0", encoding=None)               */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Document_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *version  = "1.0";
    const char *encoding = NULL;
    static char *kwlist[] = {"version", "encoding", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sz", kwlist,
                                     &version, &encoding))
        return NULL;

    xmlDocPtr doc = xmlNewDoc(BAD_CAST version);
    if (doc == NULL) {
        PyErr_SetString(PyExc_MemoryError, "xmlNewDoc failed");
        return NULL;
    }
    if (encoding) {
        if (doc->encoding) xmlFree((xmlChar *)doc->encoding);
        doc->encoding = xmlStrdup(BAD_CAST encoding);
    }

    return LibXML_Node_Wrap_As((xmlNodePtr)doc, type);
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static PyObject *
doc_get_documentElement(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr root = xmlDocGetRootElement(LibXML_DOC(self));
    if (root == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(root);
}

static int
doc_set_documentElement(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (value == NULL || !LibXML_Node_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "expected a Node");
        return -1;
    }
    return LibXML_set_root(LibXML_DOC(self), LibXML_NODE(value));
}

static PyObject *
doc_get_encoding(LibXML_NodeObject *self, void *closure)
{
    const xmlChar *enc = LibXML_DOC(self)->encoding;
    if (enc == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)enc);
}

static int
doc_set_encoding(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    xmlDocPtr doc = LibXML_DOC(self);
    if (doc->encoding) { xmlFree((xmlChar *)doc->encoding); doc->encoding = NULL; }
    if (value == Py_None || value == NULL) return 0;
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    doc->encoding = xmlStrdup(BAD_CAST s);
    return 0;
}

static PyObject *
doc_get_actualEncoding(LibXML_NodeObject *self, void *closure)
{
    const xmlChar *enc = LibXML_DOC(self)->encoding;
    return PyUnicode_FromString(enc && enc[0] ? (const char *)enc : "UTF-8");
}

static PyObject *
doc_get_version(LibXML_NodeObject *self, void *closure)
{
    const xmlChar *ver = LibXML_DOC(self)->version;
    if (ver == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)ver);
}

static int
doc_set_version(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    xmlDocPtr doc = LibXML_DOC(self);
    if (doc->version) { xmlFree((xmlChar *)doc->version); doc->version = NULL; }
    if (value == Py_None || value == NULL) return 0;
    PyObject *text = PyObject_Str(value);    /* a version can be given as 1.1 */
    const char *s = text ? PyUnicode_AsUTF8(text) : NULL;
    if (s != NULL) doc->version = xmlStrdup(BAD_CAST s);
    Py_XDECREF(text);
    return s ? 0 : -1;
}

static PyObject *
doc_get_standalone(LibXML_NodeObject *self, void *closure)
{
    return PyLong_FromLong(LibXML_DOC(self)->standalone);
}

static int
doc_set_standalone(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "cannot delete standalone");
        return -1;
    }
    long standalone = PyLong_AsLong(value);
    if (standalone == -1 && PyErr_Occurred()) return -1;
    LibXML_DOC(self)->standalone = (int)standalone;
    return 0;
}

static PyObject *
doc_get_URI(LibXML_NodeObject *self, void *closure)
{
    const char *uri = (const char *)LibXML_DOC(self)->URL;
    if (uri == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString(uri);
}

static int
doc_set_URI(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    xmlDocPtr doc = LibXML_DOC(self);
    if (doc->URL) { xmlFree((xmlChar *)doc->URL); doc->URL = NULL; }
    if (value == Py_None || value == NULL) return 0;
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    doc->URL = xmlStrdup(BAD_CAST s);
    return 0;
}

/* ------------------------------------------------------------------ */
/* create* factory methods                                             */
/* ------------------------------------------------------------------ */

static PyObject *
doc_createElement(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if (LibXML_check_name(name) < 0) return NULL;
    xmlNodePtr node = xmlNewDocNode(LibXML_DOC(self), NULL, BAD_CAST name, NULL);
    if (node == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createElementNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *qname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &qname)) return NULL;
    if (LibXML_check_name(qname) < 0) return NULL;

    xmlNodePtr node;
    if (ns_uri == NULL || ns_uri[0] == '\0') {
        node = xmlNewDocNode(LibXML_DOC(self), NULL, BAD_CAST qname, NULL);
    } else {
        xmlChar *prefix = NULL;
        xmlChar *local = xmlSplitQName2(BAD_CAST qname, &prefix);
        node = xmlNewDocNode(LibXML_DOC(self), NULL,
                             local ? local : BAD_CAST qname, NULL);
        if (node != NULL)
            xmlSetNs(node, xmlNewNs(node, BAD_CAST ns_uri, prefix));
        xmlFree(local);
        xmlFree(prefix);
    }
    if (node == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap(node);
}

static PyObject *
doc_createTextNode(LibXML_NodeObject *self, PyObject *args)
{
    const char *content;
    if (!PyArg_ParseTuple(args, "s", &content)) return NULL;
    xmlNodePtr node = xmlNewDocText(LibXML_DOC(self), BAD_CAST content);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createComment(LibXML_NodeObject *self, PyObject *args)
{
    const char *content;
    if (!PyArg_ParseTuple(args, "s", &content)) return NULL;
    xmlNodePtr node = xmlNewDocComment(LibXML_DOC(self), BAD_CAST content);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createCDATASection(LibXML_NodeObject *self, PyObject *args)
{
    const char *content;
    if (!PyArg_ParseTuple(args, "s", &content)) return NULL;
    xmlNodePtr node = xmlNewCDataBlock(LibXML_DOC(self), BAD_CAST content,
                                       (int)strlen(content));
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createPI(LibXML_NodeObject *self, PyObject *args)
{
    const char *target, *data = NULL;
    if (!PyArg_ParseTuple(args, "s|s", &target, &data)) return NULL;
    if (LibXML_check_name(target) < 0) return NULL;
    xmlNodePtr node = xmlNewDocPI(LibXML_DOC(self), BAD_CAST target,
                                   data ? BAD_CAST data : NULL);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createDocumentFragment(LibXML_NodeObject *self, PyObject *args)
{
    xmlNodePtr node = xmlNewDocFragment(LibXML_DOC(self));
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node);
    return result;
}

static PyObject *
doc_createAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    const char *value = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &name, &value)) return NULL;
    if (LibXML_check_name(name) < 0) return NULL;
    xmlDocPtr doc = LibXML_DOC(self);
    /* Create with NULL value to avoid entity-parsing the content */
    xmlAttrPtr attr = xmlNewDocProp(doc, BAD_CAST name, NULL);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
    /* Set value as raw text (no entity interpretation) */
    if (value && *value) {
        xmlNodePtr text = xmlNewDocText(doc, BAD_CAST value);
        if (text) {
            attr->children = attr->last = text;
            text->parent = (xmlNodePtr)attr;
        }
    }
    PyObject *result = LibXML_Node_Wrap((xmlNodePtr)attr);
    return result;
}

static PyObject *
doc_createAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *qname;
    const char *value = "";
    if (!PyArg_ParseTuple(args, "zs|s", &ns_uri, &qname, &value)) return NULL;
    if (LibXML_check_name(qname) < 0) return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    xmlNsPtr ns = NULL;
    xmlChar *prefix = NULL;
    xmlChar *local = NULL;

    if (ns_uri && ns_uri[0]) {
        local = xmlSplitQName2(BAD_CAST qname, &prefix);

        /* As in Perl, the root element declares it. Without a root, Perl
         * refuses, and here the document keeps it until the attribute is used. */
        xmlNodePtr root = xmlDocGetRootElement(doc);
        if (root != NULL) {
            ns = xmlSearchNsByHref(doc, root, BAD_CAST ns_uri);
            if (ns == NULL)
                ns = xmlNewNs(root, BAD_CAST ns_uri, prefix);
        } else {
            ns = LibXML_doc_ns(doc, BAD_CAST ns_uri, prefix);
        }
        if (ns == NULL) {
            xmlFree(local);
            xmlFree(prefix);
            PyErr_SetString(LibXML_Error, "cannot declare the namespace of this attribute");
            return NULL;
        }
    }

    xmlAttrPtr attr = xmlNewDocProp(doc, local ? local : BAD_CAST qname, NULL);
    xmlFree(local);
    xmlFree(prefix);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }

    attr->ns = ns;
    LibXML_set_text((xmlNodePtr)attr, value);
    return LibXML_Node_Wrap((xmlNodePtr)attr);
}

static PyObject *
doc_createEntityReference(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if (LibXML_check_name(name) < 0) return NULL;
    xmlNodePtr node = xmlNewReference(LibXML_DOC(self), BAD_CAST name);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    return LibXML_Node_Wrap(node);
}

/* ------------------------------------------------------------------ */
/* toString / toFile / toFH                                            */
/* ------------------------------------------------------------------ */

/* Serialize to bytes in the encoding of the document. */
static PyObject *
doc_dump(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    int format = 0, skip_xml_declaration = 0, tag_compression = 0, skip_dtd = 0;
    static char *kwlist[] = {"format", "skip_xml_declaration", "tag_compression",
                             "skip_dtd", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iiii", kwlist, &format,
                                     &skip_xml_declaration, &tag_compression, &skip_dtd))
        return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    int opts = 0;
    if (format > 0)           opts |= XML_SAVE_FORMAT;
    if (skip_xml_declaration) opts |= XML_SAVE_NO_DECL;
    if (tag_compression)      opts |= XML_SAVE_NO_EMPTY;

    xmlBufferPtr buf = xmlBufferCreate();
    if (buf == NULL) return PyErr_NoMemory();

    LibXML_errors_begin();
    xmlSaveCtxtPtr ctx = xmlSaveToBuffer(buf, (const char *)doc->encoding, opts);
    if (ctx == NULL) {
        xmlBufferFree(buf);
        PyErr_Format(LibXML_Error, "unknown encoding: %s",
                     doc->encoding ? (const char *)doc->encoding : "");
        return NULL;
    }

    /* As in Perl, the DTD leaves the document for the time of the output. */
    xmlDtdPtr dtd = skip_dtd ? xmlGetIntSubset(doc) : NULL;
    xmlNodePtr dtd_next = dtd ? dtd->next : NULL;
    if (dtd) xmlUnlinkNode((xmlNodePtr)dtd);

    long written = xmlSaveDoc(ctx, doc);
    int closed = xmlSaveClose(ctx);

    if (dtd) {
        dtd->parent = (xmlDocPtr)doc;
        dtd->next = dtd_next;
        dtd->prev = dtd_next ? dtd_next->prev : doc->last;
        if (dtd->prev) dtd->prev->next = (xmlNodePtr)dtd;
        else           doc->children = (xmlNodePtr)dtd;
        if (dtd_next)  dtd_next->prev = (xmlNodePtr)dtd;
        else           doc->last = (xmlNodePtr)dtd;
        doc->intSubset = dtd;
    }

    if (written < 0 || closed < 0) {
        xmlBufferFree(buf);
        LibXML_raise_last_error();
        return NULL;
    }

    PyObject *result = PyBytes_FromStringAndSize((const char *)xmlBufferContent(buf),
                                                 xmlBufferLength(buf));
    xmlBufferFree(buf);
    return result;
}

/* Decode the output of a document. Its encoding has a Python codec, or an Error. */
static PyObject *
doc_decode(xmlDocPtr doc, const char *data, Py_ssize_t size)
{
    const char *enc = doc->encoding ? (const char *)doc->encoding : "utf-8";
    PyObject *text = PyUnicode_Decode(data, size, enc, "strict");
    if (text == NULL && PyErr_ExceptionMatches(PyExc_LookupError)) {
        PyErr_Clear();
        PyErr_Format(LibXML_Error, "unknown encoding: %s", enc);
    }
    return text;
}

static PyObject *
doc_toBytes(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    return doc_dump(self, args, kwds);
}

static PyObject *
doc_toString(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *bytes = doc_dump(self, args, kwds);
    if (bytes == NULL) return NULL;
    PyObject *text = doc_decode(LibXML_DOC(self), PyBytes_AS_STRING(bytes),
                                PyBytes_GET_SIZE(bytes));
    Py_DECREF(bytes);
    return text;
}

static PyObject *
doc_str(LibXML_NodeObject *self)
{
    PyObject *args = PyTuple_New(0);
    if (args == NULL) return NULL;
    PyObject *result = doc_toString(self, args, NULL);
    Py_DECREF(args);
    return result;
}

static PyObject *
doc_toStringHTML(LibXML_NodeObject *self, PyObject *args)
{
    xmlChar *buf = NULL;
    int size = 0;
    htmlDocDumpMemory(LibXML_DOC(self), &buf, &size);
    if (buf == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    PyObject *text = doc_decode(LibXML_DOC(self), (const char *)buf, size);
    xmlFree(buf);
    return text;
}

static PyObject *
doc_toFile(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *path;
    int format = 0;
    static char *kwlist[] = {"filename", "format", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kwlist, &path, &format))
        return NULL;

    PyObject *bytes = NULL;
    if (!PyUnicode_FSConverter(path, &bytes)) return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    const char *enc = doc->encoding ? (const char *)doc->encoding : "UTF-8";
    int ret = xmlSaveFormatFileEnc(PyBytes_AS_STRING(bytes), doc, enc, format);
    if (ret < 0)
        PyErr_Format(PyExc_IOError, "failed to write to '%s'", PyBytes_AS_STRING(bytes));
    Py_DECREF(bytes);
    return ret < 0 ? NULL : PyLong_FromLong(ret);
}

/* Write to a file object: bytes, or text if it takes only that. */
static PyObject *
doc_toFH(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *fh;
    int format = 0;
    static char *kwlist[] = {"fh", "format", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kwlist, &fh, &format))
        return NULL;

    PyObject *dump_args = Py_BuildValue("(i)", format);
    if (dump_args == NULL) return NULL;
    PyObject *bytes = doc_dump(self, dump_args, NULL);
    Py_DECREF(dump_args);
    if (bytes == NULL) return NULL;

    PyObject *written = PyObject_CallMethod(fh, "write", "O", bytes);
    if (written == NULL && PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Clear();
        PyObject *text = doc_decode(LibXML_DOC(self), PyBytes_AS_STRING(bytes),
                                    PyBytes_GET_SIZE(bytes));
        if (text != NULL) {
            written = PyObject_CallMethod(fh, "write", "O", text);
            Py_DECREF(text);
        }
    }
    Py_DECREF(bytes);
    return written;
}

/* ------------------------------------------------------------------ */
/* What Perl names otherwise, or adds                                  */
/* ------------------------------------------------------------------ */

static PyObject *
doc_createRawElement(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlNodePtr node = xmlNewDocNode(LibXML_DOC(self), NULL, BAD_CAST name, NULL);
    if (node == NULL) return PyErr_NoMemory();
    return LibXML_Node_Wrap(node);
}

static PyObject *
doc_createRawElementNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *qname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &qname)) return NULL;

    xmlChar *prefix = NULL;
    xmlChar *local = ns_uri ? xmlSplitQName2(BAD_CAST qname, &prefix) : NULL;
    xmlNodePtr node = xmlNewDocNode(LibXML_DOC(self), NULL,
                                    local ? local : BAD_CAST qname, NULL);
    if (node != NULL && ns_uri != NULL && ns_uri[0])
        xmlSetNs(node, xmlNewNs(node, BAD_CAST ns_uri, prefix));
    xmlFree(local);
    xmlFree(prefix);
    if (node == NULL) return PyErr_NoMemory();
    return LibXML_Node_Wrap(node);
}

/* Before the root element, or last without one. */
static PyObject *
doc_insertPI(LibXML_NodeObject *self, PyObject *args)
{
    const char *target, *data = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &target, &data)) return NULL;
    if (LibXML_check_name(target) < 0) return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    xmlNodePtr pi = xmlNewDocPI(doc, BAD_CAST target, BAD_CAST data);
    if (pi == NULL) return PyErr_NoMemory();
    if (LibXML_insert_node(pi, (xmlNodePtr)doc, xmlDocGetRootElement(doc)) < 0) {
        xmlFreeNode(pi);
        return NULL;
    }
    return LibXML_Node_Wrap(pi);
}

/* createDocument(), (version, encoding), or as in the DOM (uri, qname, dtd) */
static PyObject *
doc_createDocument(PyObject *cls, PyObject *args)
{
    PyObject *first = NULL, *second = NULL, *dtd = NULL;
    if (!PyArg_ParseTuple(args, "|OOO", &first, &second, &dtd)) return NULL;

    const char *text = (first && PyUnicode_Check(first)) ? PyUnicode_AsUTF8(first) : NULL;
    int is_version = first == NULL || (text != NULL && strlen(text) == 3
                                       && isdigit((unsigned char)text[0]) && text[1] == '.'
                                       && isdigit((unsigned char)text[2]));
    if (is_version)
        return PyObject_CallObject(cls, args);

    PyObject *doc = PyObject_CallObject(cls, NULL);
    if (doc == NULL) return NULL;

    PyObject *root = PyObject_CallMethod(doc, "createElementNS", "OO", first,
                                         second ? second : Py_None);
    if (root == NULL) {
        Py_DECREF(doc);
        return NULL;
    }
    int rc = LibXML_set_root(LibXML_DOC(doc), LibXML_NODE(root));
    Py_DECREF(root);
    if (rc == 0 && dtd != NULL && dtd != Py_None)
        rc = PyObject_TypeCheck(dtd, &LibXML_DtdType)
            ? LibXML_set_ext_subset(LibXML_DOC(doc), LibXML_NODE(dtd)) : -1;
    if (rc < 0) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_TypeError, "the third argument must be a Dtd");
        Py_DECREF(doc);
        return NULL;
    }
    return doc;
}

static PyObject *
doc_getVersion(LibXML_NodeObject *self, PyObject *args)
{
    return doc_get_version(self, NULL);
}

static PyObject *
doc_setCompression(LibXML_NodeObject *self, PyObject *args)
{
    int level;
    if (!PyArg_ParseTuple(args, "i", &level)) return NULL;
    LibXML_DOC(self)->compression = level;
    Py_RETURN_NONE;
}

/* ------------------------------------------------------------------ */
/* DTDs                                                                */
/* ------------------------------------------------------------------ */

static PyObject *
doc_get_internalSubset(LibXML_NodeObject *self, void *closure)
{
    return LibXML_Node_Wrap((xmlNodePtr)xmlGetIntSubset(LibXML_DOC(self)));
}

static PyObject *
doc_get_externalSubset(LibXML_NodeObject *self, void *closure)
{
    return LibXML_Node_Wrap((xmlNodePtr)LibXML_DOC(self)->extSubset);
}

/* The Perl form: a method, here a property as well */
static PyObject *
doc_internalSubset(LibXML_NodeObject *self, PyObject *args)
{
    return doc_get_internalSubset(self, NULL);
}

static PyObject *
doc_externalSubset(LibXML_NodeObject *self, PyObject *args)
{
    return doc_get_externalSubset(self, NULL);
}

static PyObject *
doc_createInternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *external = NULL, *system = NULL;
    if (!PyArg_ParseTuple(args, "s|zz", &name, &external, &system)) return NULL;

    xmlDtdPtr dtd = xmlCreateIntSubset(LibXML_DOC(self), BAD_CAST name,
                                       BAD_CAST external, BAD_CAST system);
    if (dtd == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)dtd);
}

static PyObject *
doc_createExternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *external = NULL, *system = NULL;
    if (!PyArg_ParseTuple(args, "s|zz", &name, &external, &system)) return NULL;

    xmlDtdPtr dtd = xmlNewDtd(LibXML_DOC(self), BAD_CAST name,
                              BAD_CAST external, BAD_CAST system);
    if (dtd == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    return LibXML_Node_Wrap((xmlNodePtr)dtd);
}

/* A DTD in this document that is not attached to it yet */
static PyObject *
doc_createDTD(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *external = NULL, *system = NULL;
    if (!PyArg_ParseTuple(args, "s|zz", &name, &external, &system)) return NULL;

    xmlDtdPtr dtd = xmlNewDtd(NULL, BAD_CAST name, BAD_CAST external, BAD_CAST system);
    if (dtd == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    dtd->doc = LibXML_DOC(self);
    return LibXML_Node_Wrap((xmlNodePtr)dtd);
}

static PyObject *
doc_setInternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *dtd;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_DtdType, &dtd)) return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    if (LibXML_set_int_subset(doc, dtd->node, doc->children) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
doc_setExternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *dtd;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_DtdType, &dtd)) return NULL;
    if (LibXML_set_ext_subset(LibXML_DOC(self), dtd->node) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
remove_subset(xmlDtdPtr dtd)
{
    if (dtd == NULL) Py_RETURN_NONE;
    PyObject *wrapper = LibXML_Node_Wrap((xmlNodePtr)dtd);
    if (wrapper != NULL)
        LibXML_detach_node((xmlNodePtr)dtd);
    return wrapper;
}

static PyObject *
doc_removeInternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    return remove_subset(xmlGetIntSubset(LibXML_DOC(self)));
}

static PyObject *
doc_removeExternalSubset(LibXML_NodeObject *self, PyObject *args)
{
    return remove_subset(LibXML_DOC(self)->extSubset);
}

/* Recent versions of libxml2 empty the ID table of the document when they validate,
 * and keep only what the DTD of the document declares, or nothing with another DTD.
 * So note the ID attributes before, and put back what is gone. */
typedef struct {
    xmlAttrPtr *attrs;
    Py_ssize_t  count, size;
    int         failed;
} IdList;

static void
note_id(xmlNodePtr node, void *arg)
{
    IdList *ids = (IdList *)arg;

    if (node->type != XML_ATTRIBUTE_NODE || ((xmlAttrPtr)node)->atype != XML_ATTRIBUTE_ID)
        return;
    if (ids->count == ids->size) {
        Py_ssize_t size = ids->size ? 2 * ids->size : 16;
        xmlAttrPtr *attrs = PyMem_Realloc(ids->attrs, size * sizeof(xmlAttrPtr));
        if (attrs == NULL) {
            ids->failed = 1;
            return;
        }
        ids->attrs = attrs;
        ids->size = size;
    }
    ids->attrs[ids->count++] = (xmlAttrPtr)node;
}

static void
restore_ids(xmlDocPtr doc, IdList *ids)
{
    for (Py_ssize_t i = 0; i < ids->count; i++) {
        xmlAttrPtr attr = ids->attrs[i];
        xmlChar *value = xmlNodeListGetString(doc, attr->children, 1);
        if (value != NULL && xmlGetID(doc, value) == NULL)
            xmlAddID(NULL, doc, value, attr);
        xmlFree(value);
    }
}

/* Without a DTD argument, against the DTD of the document. */
static int
validate_doc(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *dtd = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &dtd)) return -1;
    if (dtd != Py_None && !PyObject_TypeCheck(dtd, &LibXML_DtdType)) {
        PyErr_SetString(PyExc_TypeError, "the argument must be a Dtd");
        return -1;
    }

    xmlValidCtxtPtr ctxt = xmlNewValidCtxt();
    if (ctxt == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    xmlDocPtr doc = LibXML_DOC(self);
    IdList ids = { NULL, 0, 0, 0 };
    LibXML_walk_tree((xmlNodePtr)doc, note_id, &ids);
    if (ids.failed) {
        xmlFreeValidCtxt(ctxt);
        PyMem_Free(ids.attrs);
        PyErr_NoMemory();
        return -1;
    }

    LibXML_errors_begin();
    int valid = dtd != Py_None
        ? xmlValidateDtd(ctxt, doc, (xmlDtdPtr)LibXML_NODE(dtd))
        : xmlValidateDocument(ctxt, doc);
    xmlFreeValidCtxt(ctxt);
    restore_ids(doc, &ids);
    PyMem_Free(ids.attrs);
    return valid ? 1 : 0;
}

static PyObject *
doc_is_valid(LibXML_NodeObject *self, PyObject *args)
{
    int valid = validate_doc(self, args);
    if (valid < 0) return NULL;
    LibXML_errors_begin();
    return PyBool_FromLong(valid);
}

/* Returns True, or raises what makes it invalid. */
static PyObject *
doc_validate(LibXML_NodeObject *self, PyObject *args)
{
    int valid = validate_doc(self, args);
    if (valid < 0) return NULL;
    if (!valid) {
        LibXML_raise_last_error();
        return NULL;
    }
    LibXML_errors_begin();
    Py_RETURN_TRUE;
}

/* ------------------------------------------------------------------ */
/* getElementsByTagName / getElementsByTagNameNS / getElementById      */
/* ------------------------------------------------------------------ */

static PyObject *
doc_getElementsByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return LibXML_find_elements(self->node, 0, name, NULL);
}

static PyObject *
doc_getElementsByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    return LibXML_find_elements(self->node, 2, localname, ns_uri ? ns_uri : "");
}

static PyObject *
doc_getElementsByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    const char *localname;
    if (!PyArg_ParseTuple(args, "s", &localname)) return NULL;
    return LibXML_find_elements(self->node, 1, localname, NULL);
}

static PyObject *
doc_getElementById(LibXML_NodeObject *self, PyObject *args)
{
    const char *id;
    if (!PyArg_ParseTuple(args, "s", &id)) return NULL;

    xmlAttrPtr attr = xmlGetID(LibXML_DOC(self), BAD_CAST id);
    if (attr == NULL || attr->parent == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(attr->parent);
}

/* ------------------------------------------------------------------ */
/* importNode / adoptNode                                              */
/* ------------------------------------------------------------------ */

static PyObject *
doc_importNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    int deep = 0;
    if (!PyArg_ParseTuple(args, "O!|i", &LibXML_NodeType, &node, &deep)) return NULL;

    /* extended=1: copy attrs+nsDef (shallow); extended=2: also copy children (deep) */
    xmlNodePtr imported = xmlDocCopyNode(node->node, LibXML_DOC(self), deep ? 2 : 1);
    if (imported == NULL) { LibXML_raise_last_error(); return NULL; }

    PyObject *result = LibXML_Node_Wrap(imported);
    return result;
}

static PyObject *
doc_adoptNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &node)) return NULL;

    if (LibXML_adopt_node(node->node, LibXML_DOC(self)) < 0) return NULL;

    Py_INCREF(node);
    return (PyObject *)node;
}

/* ------------------------------------------------------------------ */
/* is_valid / validate                                                 */
/* ------------------------------------------------------------------ */

/* Aliases: setEncoding / getEncoding / setVersion / setStandalone / setBaseURI / getDocumentElement / setDocumentElement */

static PyObject *
doc_setEncoding(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *enc_obj = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &enc_obj)) return NULL;
    if (enc_obj == Py_None || enc_obj == NULL) {
        xmlDocPtr doc = LibXML_DOC(self);
        if (doc->encoding) { xmlFree((xmlChar *)doc->encoding); doc->encoding = NULL; }
        Py_RETURN_NONE;
    }
    if (doc_set_encoding(self, enc_obj, NULL) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
doc_getEncoding(LibXML_NodeObject *self, PyObject *args)
{
    return doc_get_encoding(self, NULL);
}

static PyObject *
doc_setVersion(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *val;
    if (!PyArg_ParseTuple(args, "O", &val)) return NULL;
    if (doc_set_version(self, val, NULL) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
doc_setStandalone(LibXML_NodeObject *self, PyObject *args)
{
    int val;
    if (!PyArg_ParseTuple(args, "i", &val)) return NULL;
    LibXML_DOC(self)->standalone = val;
    Py_RETURN_NONE;
}

static PyObject *
doc_setBaseURI(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *val;
    if (!PyArg_ParseTuple(args, "O", &val)) return NULL;
    if (doc_set_URI(self, val, NULL) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
doc_getDocumentElement(LibXML_NodeObject *self, PyObject *args)
{
    return doc_get_documentElement(self, NULL);
}

static PyObject *
doc_setDocumentElement(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &node)) return NULL;
    if (LibXML_set_root(LibXML_DOC(self), node->node) < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
doc_compression(LibXML_NodeObject *self, PyObject *args)
{
    return PyLong_FromLong(LibXML_DOC(self)->compression);
}

/* ------------------------------------------------------------------ */
/* indexElements                                                       */
/* ------------------------------------------------------------------ */

static PyObject *
doc_indexElements(LibXML_NodeObject *self, PyObject *args)
{
    long count = xmlXPathOrderDocElems(LibXML_DOC(self));
    return PyLong_FromLong(count);
}

/* ------------------------------------------------------------------ */
/* cloneNode override: use xmlCopyDoc for document nodes              */
/* ------------------------------------------------------------------ */

static PyObject *
doc_cloneNode(LibXML_NodeObject *self, PyObject *args)
{
    int deep = 0;
    if (!PyArg_ParseTuple(args, "|i", &deep)) return NULL;

    xmlDocPtr clone = xmlCopyDoc(LibXML_DOC(self), deep ? 1 : 0);
    if (clone == NULL) {
        PyErr_SetString(LibXML_Error, "cloneNode failed");
        return NULL;
    }

    PyObject *result = LibXML_Node_Wrap((xmlNodePtr)clone);
    if (result == NULL) xmlFreeDoc(clone);
    return result;
}

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_Document_methods[] = {
    {"createElement",         (PyCFunction)doc_createElement,         METH_VARARGS, "Create element"},
    {"createElementNS",       (PyCFunction)doc_createElementNS,       METH_VARARGS, "Create namespaced element"},
    {"createTextNode",        (PyCFunction)doc_createTextNode,        METH_VARARGS, "Create text node"},
    {"createComment",         (PyCFunction)doc_createComment,         METH_VARARGS, "Create comment node"},
    {"createCDATASection",    (PyCFunction)doc_createCDATASection,    METH_VARARGS, "Create CDATA section"},
    {"createProcessingInstruction", (PyCFunction)doc_createPI,        METH_VARARGS, "Create processing instruction"},
    {"createPI",              (PyCFunction)doc_createPI,              METH_VARARGS, "Create processing instruction (alias)"},
    {"createDocumentFragment",(PyCFunction)doc_createDocumentFragment,METH_NOARGS,  "Create document fragment"},
    {"createAttribute",       (PyCFunction)doc_createAttribute,       METH_VARARGS, "Create attribute"},
    {"createAttributeNS",     (PyCFunction)doc_createAttributeNS,     METH_VARARGS, "Create namespaced attribute"},
    {"createEntityReference", (PyCFunction)doc_createEntityReference, METH_VARARGS, "Create entity reference"},
    {"cloneNode",             (PyCFunction)doc_cloneNode,             METH_VARARGS, "Clone document (deep=False for shallow)"},
    {"toString",              (PyCFunction)doc_toString,              METH_VARARGS|METH_KEYWORDS, "Serialize to string"},
    {"serialize",             (PyCFunction)doc_toString,              METH_VARARGS|METH_KEYWORDS, "Serialize to string (alias)"},
    {"toBytes",               (PyCFunction)doc_toBytes,               METH_VARARGS|METH_KEYWORDS, "Serialize to bytes in the document encoding"},
    {"toStringHTML",          (PyCFunction)doc_toStringHTML,          METH_NOARGS,  "Serialize as HTML"},
    {"serialize_html",        (PyCFunction)doc_toStringHTML,          METH_NOARGS,  "Serialize as HTML (alias)"},
    {"setURI",                (PyCFunction)doc_setBaseURI,            METH_VARARGS, "Set the URI of the document"},
    {"toFile",                (PyCFunction)doc_toFile,                METH_VARARGS|METH_KEYWORDS, "Save to file"},
    {"getElementsByTagName",   (PyCFunction)doc_getElementsByTagName,   METH_VARARGS, "Get elements by tag name"},
    {"getElementsByTagNameNS", (PyCFunction)doc_getElementsByTagNameNS, METH_VARARGS, "Get elements by namespace tag name"},
    {"getElementsByLocalName", (PyCFunction)doc_getElementsByLocalName, METH_VARARGS, "Get elements by local name"},
    {"getElementById",        (PyCFunction)doc_getElementById,        METH_VARARGS, "Get element by ID"},
    {"getElementsById",       (PyCFunction)doc_getElementById,        METH_VARARGS, "Get element by ID (alias)"},
    {"importNode",            (PyCFunction)doc_importNode,            METH_VARARGS, "Import node from another document"},
    {"adoptNode",             (PyCFunction)doc_adoptNode,             METH_VARARGS, "Adopt node into this document"},
    {"setEncoding",           (PyCFunction)doc_setEncoding,           METH_VARARGS, "Set document encoding"},
    {"getEncoding",           (PyCFunction)doc_getEncoding,           METH_NOARGS,  "Get document encoding"},
    {"setVersion",            (PyCFunction)doc_setVersion,            METH_VARARGS, "Set XML version"},
    {"setStandalone",         (PyCFunction)doc_setStandalone,         METH_VARARGS, "Set standalone flag"},
    {"setBaseURI",            (PyCFunction)doc_setBaseURI,            METH_VARARGS, "Set base URI"},
    {"getDocumentElement",    (PyCFunction)doc_getDocumentElement,    METH_NOARGS,  "Get root element (alias for documentElement)"},
    {"setDocumentElement",    (PyCFunction)doc_setDocumentElement,    METH_VARARGS, "Set root element"},
    {"compression",           (PyCFunction)doc_compression,           METH_NOARGS,  "Get compression level"},
    {"is_valid",              (PyCFunction)doc_is_valid,              METH_VARARGS, "Tell if the document follows its DTD, or the one given"},
    {"validate",              (PyCFunction)doc_validate,              METH_VARARGS, "Raise an Error unless the document follows its DTD, or the one given"},
    {"createDocument",        (PyCFunction)doc_createDocument,        METH_VARARGS|METH_CLASS, "New document: (version, encoding) or (uri, name, dtd)"},
    {"createRawElement",      (PyCFunction)doc_createRawElement,      METH_VARARGS, "Create an element without checking its name"},
    {"createRawElementNS",    (PyCFunction)doc_createRawElementNS,    METH_VARARGS, "Create an element in a namespace without checking its name"},
    {"insertProcessingInstruction", (PyCFunction)doc_insertPI,        METH_VARARGS, "Add a PI before the root element"},
    {"insertPI",              (PyCFunction)doc_insertPI,              METH_VARARGS, "Add a PI before the root element"},
    {"toFH",                  (PyCFunction)doc_toFH,                  METH_VARARGS|METH_KEYWORDS, "Write to a file object"},
    {"getVersion",            (PyCFunction)doc_getVersion,            METH_NOARGS,  "XML version"},
    {"setCompression",        (PyCFunction)doc_setCompression,        METH_VARARGS, "Set the compression of toFile"},
    {"getInternalSubset",     (PyCFunction)doc_internalSubset,        METH_NOARGS,  "Internal subset"},
    {"getExternalSubset",     (PyCFunction)doc_externalSubset,        METH_NOARGS,  "External subset"},
    {"createInternalSubset",  (PyCFunction)doc_createInternalSubset,  METH_VARARGS, "Create the internal subset"},
    {"createExternalSubset",  (PyCFunction)doc_createExternalSubset,  METH_VARARGS, "Create the external subset"},
    {"createDTD",             (PyCFunction)doc_createDTD,             METH_VARARGS, "Create a DTD not attached to the document"},
    {"setInternalSubset",     (PyCFunction)doc_setInternalSubset,     METH_VARARGS, "Set the internal subset"},
    {"setExternalSubset",     (PyCFunction)doc_setExternalSubset,     METH_VARARGS, "Set the external subset"},
    {"removeInternalSubset",  (PyCFunction)doc_removeInternalSubset,  METH_NOARGS,  "Remove and return the internal subset"},
    {"removeExternalSubset",  (PyCFunction)doc_removeExternalSubset,  METH_NOARGS,  "Remove and return the external subset"},
    {"indexElements",         (PyCFunction)doc_indexElements,         METH_NOARGS,  "Index elements for faster XPath"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* Getset table                                                        */
/* ------------------------------------------------------------------ */

static PyGetSetDef LibXML_Document_getset[] = {
    {"documentElement", (getter)doc_get_documentElement, (setter)doc_set_documentElement,
     "Root element", NULL},
    {"encoding",       (getter)doc_get_encoding,    (setter)doc_set_encoding,    "Encoding",       NULL},
    {"actualEncoding", (getter)doc_get_actualEncoding, NULL,                     "Actual encoding", NULL},
    {"xmlEncoding",    (getter)doc_get_encoding,    (setter)doc_set_encoding,    "Encoding",       NULL},
    {"xmlVersion",     (getter)doc_get_version,     (setter)doc_set_version,     "XML version",    NULL},
    {"xmlStandalone",  (getter)doc_get_standalone,  (setter)doc_set_standalone,  "Standalone flag",NULL},
    {"internalSubset", (getter)doc_get_internalSubset, NULL,                     "Internal subset", NULL},
    {"externalSubset", (getter)doc_get_externalSubset, NULL,                     "External subset", NULL},
    {"version",        (getter)doc_get_version,     (setter)doc_set_version,     "XML version",    NULL},
    {"standalone",     (getter)doc_get_standalone,  (setter)doc_set_standalone,  "Standalone flag",NULL},
    {"URI",            (getter)doc_get_URI,         (setter)doc_set_URI,         "Document URI",   NULL},
    {"documentURI",    (getter)doc_get_URI,         (setter)doc_set_URI,         "Document URI",   NULL},
    {NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_DocumentType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Document",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_dealloc   = (destructor)LibXML_Document_dealloc,
    .tp_str       = (reprfunc)doc_str,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Document_new,
    .tp_methods   = LibXML_Document_methods,
    .tp_getset    = LibXML_Document_getset,
    .tp_doc       = "DOM Document node",
};

#include <stdlib.h>
#include "libxml_document.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
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

    return LibXML_Node_Wrap((xmlNodePtr)doc);
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
    /* Same as encoding for our purposes */
    return doc_get_encoding(self, closure);
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
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    doc->version = xmlStrdup(BAD_CAST s);
    return 0;
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

static PyObject *
doc_toString(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    int format = 0;
    int skip_xml_declaration = 0;
    int tag_compression = 0;  /* 1 = no empty tags (force <bar></bar>) */
    static char *kwlist[] = {"format", "skip_xml_declaration",
                              "tag_compression", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iii", kwlist,
                                     &format, &skip_xml_declaration,
                                     &tag_compression))
        return NULL;

    const char *enc = LibXML_DOC(self)->encoding ?
        (const char *)LibXML_DOC(self)->encoding : "UTF-8";

    if (skip_xml_declaration || tag_compression) {
        /* Use xmlSaveCtxt for fine-grained control */
        xmlBufferPtr buf = xmlBufferCreate();
        if (!buf) return PyErr_NoMemory();
        int opts = 0;
        if (format)               opts |= XML_SAVE_FORMAT;
        if (skip_xml_declaration) opts |= XML_SAVE_NO_DECL;
        if (tag_compression)      opts |= XML_SAVE_NO_EMPTY;
        xmlSaveCtxtPtr ctx = xmlSaveToBuffer(buf, enc, opts);
        if (!ctx) { xmlBufferFree(buf); return PyErr_NoMemory(); }
        xmlSaveDoc(ctx, LibXML_DOC(self));
        xmlSaveClose(ctx);
        PyObject *result = PyUnicode_FromStringAndSize(
            (const char *)xmlBufferContent(buf),
            (Py_ssize_t)xmlBufferLength(buf));
        xmlBufferFree(buf);
        return result;
    }

    xmlChar *buf = NULL;
    int      size = 0;
    xmlDocDumpFormatMemoryEnc(LibXML_DOC(self), &buf, &size, enc, format);
    if (buf == NULL) {
        PyErr_SetString(PyExc_MemoryError, "xmlDocDumpFormatMemoryEnc failed");
        return NULL;
    }
    PyObject *result = PyUnicode_FromStringAndSize((const char *)buf, size);
    xmlFree(buf);
    return result;
}

static PyObject *
doc_toFile(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    const char *filename;
    int format = 0;
    static char *kwlist[] = {"filename", "format", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist, &filename, &format))
        return NULL;

    const char *enc = LibXML_DOC(self)->encoding ?
        (const char *)LibXML_DOC(self)->encoding : "UTF-8";

    int ret = xmlSaveFormatFileEnc(filename, LibXML_DOC(self), enc, format);
    if (ret < 0) {
        PyErr_Format(PyExc_IOError, "failed to write to '%s'", filename);
        return NULL;
    }
    return PyLong_FromLong(ret);
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

static PyObject *
doc_is_valid(LibXML_NodeObject *self, PyObject *args)
{
    /* Basic well-formedness check: root element present */
    xmlNodePtr root = xmlDocGetRootElement(LibXML_DOC(self));
    return PyBool_FromLong(root != NULL);
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
    {"is_valid",              (PyCFunction)doc_is_valid,              METH_NOARGS,  "Check basic document validity"},
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
    {"version",        (getter)doc_get_version,     (setter)doc_set_version,     "XML version",    NULL},
    {"standalone",     (getter)doc_get_standalone,  (setter)doc_set_standalone,  "Standalone flag",NULL},
    {"URI",            (getter)doc_get_URI,         (setter)doc_set_URI,         "Document URI",   NULL},
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
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Document_new,
    .tp_methods   = LibXML_Document_methods,
    .tp_getset    = LibXML_Document_getset,
    .tp_doc       = "DOM Document node",
};

#include <stdlib.h>
#include "libxml_document.h"
#include "libxml_utils.h"
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
        xmlNodePtr doc_node = self->node;

        /* Disconnect *this* wrapper from the document's priv struct without
         * nulling self->node — we still need it valid for xmlFreeDoc below. */
        LibXML_NodePrivate *priv = (LibXML_NodePrivate *)doc_node->_private;
        if (priv && priv->wrapper == (PyObject *)self) {
            priv->wrapper = NULL;
            if (priv->cargo == NULL) {
                free(priv);
                doc_node->_private = NULL;
            }
        }

        /* Clear priv structs for all child nodes and attributes.
         * Their Python wrappers (if any) may outlive the document — null out
         * their node pointer so they can't access freed memory. */
        LibXML_clear_private_children(doc_node);
        self->node = NULL;
        xmlFreeDoc((xmlDocPtr)doc_node);
    }
    /* doc_obj is always NULL for Document */
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

    /* Wrap: doc_obj = NULL because this IS the document */
    return LibXML_Node_Wrap((xmlNodePtr)doc, NULL);
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static PyObject *
doc_get_documentElement(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr root = xmlDocGetRootElement(LibXML_DOC(self));
    if (root == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(root, (PyObject *)self);
}

static int
doc_set_documentElement(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (!LibXML_Node_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "expected a Node");
        return -1;
    }
    LibXML_NodeObject *node = (LibXML_NodeObject *)value;
    xmlDocSetRootElement(LibXML_DOC(self), node->node);
    return 0;
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
    LibXML_DOC(self)->standalone = (int)PyLong_AsLong(value);
    if (PyErr_Occurred()) return -1;
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
    xmlNodePtr node = xmlNewDocNode(LibXML_DOC(self), NULL, BAD_CAST name, NULL);
    if (node == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createElementNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *qname;
    if (!PyArg_ParseTuple(args, "ss", &ns_uri, &qname)) return NULL;

    /* Split qname into prefix:localname */
    const char *colon = strchr(qname, ':');
    xmlChar *prefix = NULL, *localname;
    if (colon) {
        prefix    = xmlStrndup(BAD_CAST qname, (int)(colon - qname));
        localname = xmlStrdup(BAD_CAST (colon + 1));
    } else {
        localname = xmlStrdup(BAD_CAST qname);
    }

    xmlNodePtr node = xmlNewDocNode(LibXML_DOC(self), NULL, localname, NULL);
    xmlFree(localname);
    if (node == NULL) {
        xmlFree(prefix);
        LibXML_raise_last_error();
        return NULL;
    }

    xmlNsPtr ns = xmlNewNs(node, BAD_CAST ns_uri, prefix);
    xmlFree(prefix);
    xmlSetNs(node, ns);

    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createTextNode(LibXML_NodeObject *self, PyObject *args)
{
    const char *content;
    if (!PyArg_ParseTuple(args, "s", &content)) return NULL;
    xmlNodePtr node = xmlNewDocText(LibXML_DOC(self), BAD_CAST content);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createComment(LibXML_NodeObject *self, PyObject *args)
{
    const char *content;
    if (!PyArg_ParseTuple(args, "s", &content)) return NULL;
    xmlNodePtr node = xmlNewDocComment(LibXML_DOC(self), BAD_CAST content);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
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
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createPI(LibXML_NodeObject *self, PyObject *args)
{
    const char *target, *data = NULL;
    if (!PyArg_ParseTuple(args, "s|s", &target, &data)) return NULL;
    xmlNodePtr node = xmlNewDocPI(LibXML_DOC(self), BAD_CAST target,
                                   data ? BAD_CAST data : NULL);
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createDocumentFragment(LibXML_NodeObject *self, PyObject *args)
{
    xmlNodePtr node = xmlNewDocFragment(LibXML_DOC(self));
    if (node == NULL) { LibXML_raise_last_error(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    const char *value = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &name, &value)) return NULL;
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
    PyObject *result = LibXML_Node_Wrap((xmlNodePtr)attr, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_createAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *qname;
    const char *value = "";
    if (!PyArg_ParseTuple(args, "ss|s", &ns_uri, &qname, &value)) return NULL;

    /* We create the attribute standalone; ns is set when attached to an element */
    const char *colon = strchr(qname, ':');
    const char *localname = colon ? colon + 1 : qname;

    xmlAttrPtr attr = xmlNewDocProp(LibXML_DOC(self), BAD_CAST localname, BAD_CAST value);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }

    /* Store the namespace URI in the attr's ns */
    xmlChar *prefix = colon ?
        xmlStrndup(BAD_CAST qname, (int)(colon - qname)) : NULL;
    xmlNsPtr ns = xmlNewNs(NULL, BAD_CAST ns_uri, prefix);
    xmlFree(prefix);
    attr->ns = ns;

    PyObject *result = LibXML_Node_Wrap((xmlNodePtr)attr, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
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

/* Helper: run XPath from the document root */
static PyObject *
doc_xpath_query(LibXML_NodeObject *self, const char *xpath_expr)
{
    xmlDocPtr doc = LibXML_DOC(self);
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) return PyErr_NoMemory();
    ctx->node = (xmlNodePtr)doc;   /* set context to document node */

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath_expr, ctx);
    xmlXPathFreeContext(ctx);
    if (!xobj) { LibXML_raise_last_error(); return NULL; }

    PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval, (PyObject *)self);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
doc_getElementsByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;

    char xpath[512];
    if (strcmp(name, "*") == 0)
        snprintf(xpath, sizeof(xpath), "descendant::*");
    else
        snprintf(xpath, sizeof(xpath), "descendant::%s", name);

    return doc_xpath_query(self, xpath);
}

static PyObject *
doc_getElementsByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "ss", &ns_uri, &localname)) return NULL;

    xmlDocPtr doc = LibXML_DOC(self);
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) return PyErr_NoMemory();
    ctx->node = (xmlNodePtr)doc;

    /* wildcard ns: use local-name() approach */
    char xpath[1024];
    if (strcmp(ns_uri, "*") == 0 && strcmp(localname, "*") == 0) {
        snprintf(xpath, sizeof(xpath), "descendant::*");
        xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
        xmlXPathFreeContext(ctx);
        if (!xobj) { LibXML_raise_last_error(); return NULL; }
        PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval, (PyObject *)self);
        xmlXPathFreeObject(xobj);
        return result;
    }
    if (strcmp(ns_uri, "*") == 0) {
        /* any namespace, specific local name */
        if (strcmp(localname, "*") == 0)
            snprintf(xpath, sizeof(xpath),
                "descendant::*[namespace-uri()!='']");
        else
            snprintf(xpath, sizeof(xpath),
                "descendant::*[local-name()='%s']", localname);
        xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
        xmlXPathFreeContext(ctx);
        if (!xobj) { LibXML_raise_last_error(); return NULL; }
        PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval, (PyObject *)self);
        xmlXPathFreeObject(xobj);
        return result;
    }

    if (xmlXPathRegisterNs(ctx, BAD_CAST "ns_", BAD_CAST ns_uri) != 0) {
        xmlXPathFreeContext(ctx);
        PyErr_SetString(PyExc_RuntimeError, "failed to register namespace");
        return NULL;
    }

    if (strcmp(localname, "*") == 0)
        snprintf(xpath, sizeof(xpath), "descendant::ns_:*");
    else
        snprintf(xpath, sizeof(xpath), "descendant::ns_:%s", localname);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    xmlXPathFreeContext(ctx);
    if (!xobj) { LibXML_raise_last_error(); return NULL; }

    PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval, (PyObject *)self);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
doc_getElementsByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    const char *localname;
    if (!PyArg_ParseTuple(args, "s", &localname)) return NULL;

    char xpath[512];
    if (strcmp(localname, "*") == 0)
        snprintf(xpath, sizeof(xpath), "descendant::*");
    else
        snprintf(xpath, sizeof(xpath),
            "descendant::*[local-name()='%s']", localname);

    return doc_xpath_query(self, xpath);
}

static PyObject *
doc_getElementById(LibXML_NodeObject *self, PyObject *args)
{
    const char *id;
    if (!PyArg_ParseTuple(args, "s", &id)) return NULL;

    xmlAttrPtr attr = xmlGetID(LibXML_DOC(self), BAD_CAST id);
    if (attr == NULL || attr->parent == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(attr->parent, (PyObject *)self);
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

    PyObject *result = LibXML_Node_Wrap(imported, (PyObject *)self);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

static PyObject *
doc_adoptNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &node)) return NULL;

    xmlUnlinkNode(node->node);
    xmlSetTreeDoc(node->node, LibXML_DOC(self));
    /* Reconcile namespace declarations so the adopted node is self-contained */
    xmlReconciliateNs(LibXML_DOC(self), node->node);
    LibXML_update_doc_refs(node->node, (PyObject *)self);
    node->owned = 0;

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
    return doc_set_URI(self, val, NULL) < 0 ? NULL : Py_None;
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
    xmlDocPtr doc = LibXML_DOC(self);
    xmlUnlinkNode(node->node);
    if (node->node->doc != doc)
        xmlSetTreeDoc(node->node, doc);
    LibXML_update_doc_refs(node->node, (PyObject *)self);
    xmlDocSetRootElement(doc, node->node);
    node->owned = 0;
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

    /* Wrap clone as a new Document (no doc_obj since it IS the doc) */
    LibXML_NodeObject *result =
        (LibXML_NodeObject *)LibXML_DocumentType.tp_alloc(&LibXML_DocumentType, 0);
    if (!result) { xmlFreeDoc(clone); return NULL; }
    result->node    = (xmlNodePtr)clone;
    result->owned   = 1;
    result->doc_obj = NULL;
    /* Store wrapper in LibXML_NodePrivate struct */
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)calloc(1, sizeof(LibXML_NodePrivate));
    if (!priv) { Py_DECREF(result); xmlFreeDoc(clone); return PyErr_NoMemory(); }
    priv->wrapper   = (PyObject *)result;
    clone->_private = priv;
    return (PyObject *)result;
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

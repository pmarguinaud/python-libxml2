#include <stdlib.h>
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_document.h"
#include "libxml_element.h"
#include "libxml_attr.h"
#include "libxml_text.h"

/* Thread-local last error storage */
static xmlError last_error;
static int      last_error_set = 0;

void
LibXML_error_handler(void *ctx, LIBXML_CONST_ERROR xmlError *error)
{
    (void)ctx;
    xmlResetError(&last_error);
    xmlCopyError(error, &last_error);
    last_error_set = 1;
}

void
LibXML_raise_last_error(void)
{
    if (last_error_set && last_error.message) {
        PyErr_Format(LibXML_Error, "%s", last_error.message);
        xmlResetError(&last_error);
        last_error_set = 0;
    } else {
        PyErr_SetString(LibXML_Error, "libxml2 error");
    }
}

/* ------------------------------------------------------------------ */
/* Node wrapping factory                                               */
/* ------------------------------------------------------------------ */

static PyTypeObject *
wrapper_type(xmlNodePtr node)
{
    switch (node->type) {
    case XML_ELEMENT_NODE:        return &LibXML_ElementType;
    case XML_TEXT_NODE:           return &LibXML_TextType;
    case XML_COMMENT_NODE:        return &LibXML_CommentType;
    case XML_CDATA_SECTION_NODE:  return &LibXML_CDATASectionType;
    case XML_ATTRIBUTE_NODE:      return &LibXML_AttrType;
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:  return &LibXML_DocumentType;
    case XML_PI_NODE:             return &LibXML_PIType;
    case XML_DOCUMENT_FRAG_NODE:  return &LibXML_DocumentFragmentType;
    default:                      return &LibXML_NodeType;
    }
}

PyObject *
LibXML_Node_Wrap(xmlNodePtr node)
{
    if (node == NULL) Py_RETURN_NONE;

    if (node->type == XML_NAMESPACE_DECL) {
        PyErr_SetString(LibXML_Error, "a namespace declaration is not a node");
        return NULL;
    }

    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (priv && priv->wrapper != NULL) {
        Py_INCREF(priv->wrapper);
        return priv->wrapper;
    }

    PyObject *doc_obj = NULL, *owner = NULL;

    if (node->type != XML_DOCUMENT_NODE && node->type != XML_HTML_DOCUMENT_NODE) {
        if (node->doc == NULL) {
            PyErr_SetString(LibXML_Error, "node has no document");
            return NULL;
        }
        doc_obj = LibXML_Node_Wrap((xmlNodePtr)node->doc);
        if (doc_obj == NULL) return NULL;

        xmlNodePtr top = LibXML_tree_top(node);
        if (top != node) {
            owner = LibXML_Node_Wrap(top);
            if (owner == NULL) { Py_DECREF(doc_obj); return NULL; }
        }
    }

    PyTypeObject *type = wrapper_type(node);
    LibXML_NodeObject *self = (LibXML_NodeObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        Py_XDECREF(doc_obj);
        Py_XDECREF(owner);
        return NULL;
    }

    if (!priv) {
        priv = (LibXML_NodePrivate *)calloc(1, sizeof(LibXML_NodePrivate));
        if (!priv) {
            Py_XDECREF(doc_obj);
            Py_XDECREF(owner);
            Py_DECREF(self);
            return PyErr_NoMemory();
        }
        node->_private = priv;
    }
    priv->wrapper = (PyObject *)self;  /* borrowed ref */

    self->node    = node;
    self->doc_obj = doc_obj;
    self->owner   = owner;

    return (PyObject *)self;
}

PyObject *
LibXML_new_hidden_doc(void)
{
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (doc == NULL) return PyErr_NoMemory();

    PyObject *wrapper = LibXML_Node_Wrap((xmlNodePtr)doc);
    if (wrapper == NULL) {
        xmlFreeDoc(doc);
        return NULL;
    }
    ((LibXML_NodeObject *)wrapper)->hidden = 1;
    return wrapper;
}

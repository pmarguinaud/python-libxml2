#include "libxml_utils.h"
#include "libxml_document.h"
#include "libxml_element.h"
#include "libxml_attr.h"
#include "libxml_text.h"

/* Thread-local last error storage */
static xmlError last_error;
static int      last_error_set = 0;

void
LibXML_error_handler(void *ctx, xmlErrorPtr error)
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
/* Clear _private pointers before freeing a document tree             */
/* ------------------------------------------------------------------ */

static void
clear_private_node(xmlNodePtr node)
{
    if (node == NULL) return;
    node->_private = NULL;

    /* Recurse into attributes */
    if (node->type == XML_ELEMENT_NODE) {
        xmlAttrPtr attr = node->properties;
        while (attr) {
            ((xmlNodePtr)attr)->_private = NULL;
            /* attr children (text nodes) */
            xmlNodePtr ac = ((xmlNodePtr)attr)->children;
            while (ac) { ac->_private = NULL; ac = ac->next; }
            attr = attr->next;
        }
    }
    /* Recurse into children */
    xmlNodePtr child = node->children;
    while (child) {
        clear_private_node(child);
        child = child->next;
    }
}

void
LibXML_clear_private_recursive(xmlNodePtr node)
{
    clear_private_node(node);
}

/* ------------------------------------------------------------------ */
/* Update doc_obj refs when a subtree moves to a new document          */
/* ------------------------------------------------------------------ */

static void
update_doc_refs_node(xmlNodePtr node, PyObject *new_doc_obj)
{
    if (node == NULL) return;
    if (node->_private != NULL) {
        LibXML_NodeObject *wrapper = (LibXML_NodeObject *)node->_private;
        PyObject *old = wrapper->doc_obj;
        Py_XINCREF(new_doc_obj);
        wrapper->doc_obj = new_doc_obj;
        Py_XDECREF(old);
    }
    if (node->type == XML_ELEMENT_NODE) {
        xmlAttrPtr attr = node->properties;
        while (attr) {
            update_doc_refs_node((xmlNodePtr)attr, new_doc_obj);
            attr = attr->next;
        }
    }
    xmlNodePtr child = node->children;
    while (child) {
        update_doc_refs_node(child, new_doc_obj);
        child = child->next;
    }
}

void
LibXML_update_doc_refs(xmlNodePtr node, PyObject *new_doc_obj)
{
    update_doc_refs_node(node, new_doc_obj);
}

/* ------------------------------------------------------------------ */
/* Node wrapping factory                                               */
/* ------------------------------------------------------------------ */

PyObject *
LibXML_Node_Wrap(xmlNodePtr node, PyObject *doc_obj)
{
    if (node == NULL) Py_RETURN_NONE;

    /* Namespace declaration nodes: xmlNs struct layout differs from xmlNode.
     * node->_private (offset 0) would read xmlNs->next — do NOT use it. */
    if (node->type == XML_NAMESPACE_DECL) {
        LibXML_NodeObject *self =
            (LibXML_NodeObject *)LibXML_NodeType.tp_alloc(&LibXML_NodeType, 0);
        if (!self) return NULL;
        self->node    = node;
        self->owned   = 0;
        self->doc_obj = doc_obj;
        Py_XINCREF(doc_obj);
        /* Note: tp_alloc already handles GC tracking; do NOT call
         * PyObject_GC_Track again here. */
        return (PyObject *)self;
    }

    /* Reuse existing wrapper */
    if (node->_private != NULL) {
        PyObject *existing = (PyObject *)node->_private;
        Py_INCREF(existing);
        return existing;
    }

    /* Select Python type */
    PyTypeObject *type;
    switch (node->type) {
        case XML_ELEMENT_NODE:
            type = &LibXML_ElementType;
            break;
        case XML_TEXT_NODE:
            type = &LibXML_TextType;
            break;
        case XML_COMMENT_NODE:
            type = &LibXML_CommentType;
            break;
        case XML_CDATA_SECTION_NODE:
            type = &LibXML_CDATASectionType;
            break;
        case XML_ATTRIBUTE_NODE:
            type = &LibXML_AttrType;
            break;
        case XML_DOCUMENT_NODE:
        case XML_HTML_DOCUMENT_NODE:
            type = &LibXML_DocumentType;
            break;
        case XML_PI_NODE:
            type = &LibXML_PIType;
            break;
        case XML_DOCUMENT_FRAG_NODE:
            type = &LibXML_DocumentFragmentType;
            break;
        default:
            type = &LibXML_NodeType;
            break;
    }

    LibXML_NodeObject *self = (LibXML_NodeObject *)type->tp_alloc(type, 0);
    if (self == NULL) return NULL;

    self->node  = node;
    self->owned = 0;

    if (node->type == XML_DOCUMENT_NODE || node->type == XML_HTML_DOCUMENT_NODE) {
        /* Document: doc_obj = NULL (self IS the document) */
        self->doc_obj = NULL;
    } else {
        Py_XINCREF(doc_obj);
        self->doc_obj = doc_obj;
    }

    /* Cache in _private (borrowed ref — not ref-counted) */
    node->_private = (void *)self;

    return (PyObject *)self;
}

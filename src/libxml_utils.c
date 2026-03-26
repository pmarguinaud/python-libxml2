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
/* Node deregister callback (called by libxml2 on xmlFreeNode)        */
/* ------------------------------------------------------------------ */

/* Release the LibXML_NodePrivate struct for one node.
 * If a wrapper still exists (rare: direct xmlFreeNode call bypassing
 * our tp_dealloc), null out its node pointer to prevent use-after-free. */
static void
release_priv(xmlNodePtr node)
{
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (!priv) return;
    if (priv->wrapper) {
        ((LibXML_NodeObject *)priv->wrapper)->node = NULL;
        priv->wrapper = NULL;
    }
    if (priv->cargo) {
        Py_DECREF(priv->cargo);
        priv->cargo = NULL;
    }
    free(priv);
    node->_private = NULL;
}

void
LibXML_node_deregister(xmlNodePtr node)
{
    /* xmlNs nodes have a different struct layout — never touch their _private */
    if (node->type == XML_NAMESPACE_DECL) return;
    release_priv(node);
}

/* ------------------------------------------------------------------ */
/* Clear _private structs before freeing a document tree              */
/* ------------------------------------------------------------------ */

static void
clear_private_node(xmlNodePtr node)
{
    if (node == NULL) return;
    release_priv(node);

    /* Recurse into attributes */
    if (node->type == XML_ELEMENT_NODE) {
        xmlAttrPtr attr = node->properties;
        while (attr) {
            release_priv((xmlNodePtr)attr);
            xmlNodePtr ac = ((xmlNodePtr)attr)->children;
            while (ac) { release_priv(ac); ac = ac->next; }
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

/* Like LibXML_clear_private_recursive but skips the root node itself —
 * used by Document dealloc which handles the root's priv separately. */
void
LibXML_clear_private_children(xmlNodePtr root)
{
    if (root == NULL) return;
    /* Recurse into attributes of root */
    if (root->type == XML_ELEMENT_NODE || root->type == XML_DOCUMENT_NODE
        || root->type == XML_HTML_DOCUMENT_NODE) {
        xmlAttrPtr attr = root->properties;
        while (attr) {
            release_priv((xmlNodePtr)attr);
            xmlNodePtr ac = ((xmlNodePtr)attr)->children;
            while (ac) { release_priv(ac); ac = ac->next; }
            attr = attr->next;
        }
    }
    xmlNodePtr child = root->children;
    while (child) {
        clear_private_node(child);
        child = child->next;
    }
}

/* ------------------------------------------------------------------ */
/* Update doc_obj refs when a subtree moves to a new document          */
/* ------------------------------------------------------------------ */

static void
update_doc_refs_node(xmlNodePtr node, PyObject *new_doc_obj)
{
    if (node == NULL) return;
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (priv && priv->wrapper) {
        LibXML_NodeObject *wrapper = (LibXML_NodeObject *)priv->wrapper;
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

    /* Reuse existing wrapper if one is live */
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (priv && priv->wrapper != NULL) {
        Py_INCREF(priv->wrapper);
        return priv->wrapper;
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
        self->doc_obj = NULL;
    } else {
        Py_XINCREF(doc_obj);
        self->doc_obj = doc_obj;
    }

    /* Store wrapper in LibXML_NodePrivate (create struct lazily if needed) */
    if (!priv) {
        priv = (LibXML_NodePrivate *)calloc(1, sizeof(LibXML_NodePrivate));
        if (!priv) { Py_DECREF(self); return PyErr_NoMemory(); }
        node->_private = priv;
    }
    priv->wrapper = (PyObject *)self;  /* borrowed ref */

    return (PyObject *)self;
}

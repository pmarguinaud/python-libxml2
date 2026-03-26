#include "libxml_node.h"
#include "libxml_utils.h"
#include "libxml_document.h"
#include "libxml_nodelist.h"
#include "libxml_xpath.h"
#include <libxml/uri.h>

/* ------------------------------------------------------------------ */
/* tp_dealloc                                                          */
/* ------------------------------------------------------------------ */

static void
LibXML_Node_dealloc(LibXML_NodeObject *self)
{
    PyObject_GC_UnTrack(self);
    if (self->node != NULL) {
        /* Namespace declaration nodes: xmlNs struct layout differs from
         * xmlNode, so _private is at a different offset.  Never touch it. */
        if (self->node->type == XML_NAMESPACE_DECL) {
            /* xmlNs struct: _private is at a different offset than xmlNode.
             * Never touch node->_private here.  If we own a copy, free it. */
            if (self->owned)
                xmlFreeNs((xmlNsPtr)self->node);
        } else {
            /* Clear the _private back-pointer (borrowed ref) */
            if (self->node->_private == (void *)self)
                self->node->_private = NULL;
            /* Free libxml2 memory only if we own a detached subtree */
            if (self->owned)
                xmlFreeNode(self->node);
        }
        self->node = NULL;
    }
    Py_XDECREF(self->doc_obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* GC support                                                          */
/* ------------------------------------------------------------------ */

int
LibXML_Node_traverse(LibXML_NodeObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->doc_obj);
    return 0;
}

int
LibXML_Node_clear(LibXML_NodeObject *self)
{
    Py_CLEAR(self->doc_obj);
    return 0;
}

/* ------------------------------------------------------------------ */
/* tp_repr / tp_str                                                    */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Node_repr(LibXML_NodeObject *self)
{
    if (self->node == NULL)
        return PyUnicode_FromFormat("<%s (freed)>", Py_TYPE(self)->tp_name);
    /* Namespace nodes: self->node->name == ns->href (offset 16 matches) */
    return PyUnicode_FromFormat("<%s nodeName='%s'>",
        Py_TYPE(self)->tp_name,
        self->node->name ? (const char *)self->node->name : "(null)");
}

/* ------------------------------------------------------------------ */
/* Helper: get the owning doc_obj for self                             */
/* ------------------------------------------------------------------ */

static PyObject *
node_doc_obj(LibXML_NodeObject *self)
{
    /* If self IS the document, return self; otherwise return doc_obj */
    return self->doc_obj ? self->doc_obj : (PyObject *)self;
}

/* ------------------------------------------------------------------ */
/* Helper: wrap a node with the correct doc context                    */
/* ------------------------------------------------------------------ */

static PyObject *
wrap_with_doc(xmlNodePtr node, LibXML_NodeObject *self)
{
    return LibXML_Node_Wrap(node, node_doc_obj(self));
}

/* ------------------------------------------------------------------ */
/* Properties (getset)                                                 */
/* ------------------------------------------------------------------ */

static PyObject *
node_get_nodeName(LibXML_NodeObject *self, void *closure)
{
    /* Namespace declaration nodes: safe-cast to xmlNsPtr */
    if (self->node->type == XML_NAMESPACE_DECL) {
        xmlNsPtr ns = (xmlNsPtr)self->node;
        if (ns->prefix)
            return PyUnicode_FromString((const char *)ns->prefix);
        Py_RETURN_NONE;
    }

    const xmlChar *name = NULL;
    switch (self->node->type) {
        case XML_DOCUMENT_NODE:
        case XML_HTML_DOCUMENT_NODE:
            name = (const xmlChar *)"#document";
            break;
        case XML_TEXT_NODE:
            name = (const xmlChar *)"#text";
            break;
        case XML_COMMENT_NODE:
            name = (const xmlChar *)"#comment";
            break;
        case XML_CDATA_SECTION_NODE:
            name = (const xmlChar *)"#cdata-section";
            break;
        case XML_DOCUMENT_FRAG_NODE:
            name = (const xmlChar *)"#document-fragment";
            break;
        default:
            name = self->node->name;
            break;
    }
    if (name == NULL) Py_RETURN_NONE;

    /* Include namespace prefix for elements/attributes */
    if ((self->node->type == XML_ELEMENT_NODE || self->node->type == XML_ATTRIBUTE_NODE)
        && self->node->ns && self->node->ns->prefix) {
        return PyUnicode_FromFormat("%s:%s",
            (const char *)self->node->ns->prefix,
            (const char *)name);
    }
    return PyUnicode_FromString((const char *)name);
}

static PyObject *
node_get_localname(LibXML_NodeObject *self, void *closure)
{
    if (self->node->name == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)self->node->name);
}

static PyObject *
node_get_prefix(LibXML_NodeObject *self, void *closure)
{
    if (self->node->ns && self->node->ns->prefix)
        return PyUnicode_FromString((const char *)self->node->ns->prefix);
    Py_RETURN_NONE;
}

static PyObject *
node_get_namespaceURI(LibXML_NodeObject *self, void *closure)
{
    if (self->node->ns && self->node->ns->href && self->node->ns->href[0] != '\0')
        return PyUnicode_FromString((const char *)self->node->ns->href);
    Py_RETURN_NONE;
}

static PyObject *
node_get_nodeType(LibXML_NodeObject *self, void *closure)
{
    return PyLong_FromLong((long)self->node->type);
}

static PyObject *
node_get_nodeValue(LibXML_NodeObject *self, void *closure)
{
    /* DOM: nodeValue is null for Element, Document, DocumentFragment */
    xmlElementType t = self->node->type;
    if (t == XML_ELEMENT_NODE || t == XML_DOCUMENT_NODE ||
        t == XML_DOCUMENT_FRAG_NODE) {
        Py_RETURN_NONE;
    }
    xmlChar *content = xmlNodeGetContent(self->node);
    if (content == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)content);
    xmlFree(content);
    return ret;
}

static int
node_set_nodeValue(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (value == NULL || value == Py_None) {
        xmlNodeSetContent(self->node, NULL);
        return 0;
    }
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    xmlNodeSetContent(self->node, BAD_CAST s);
    return 0;
}

static PyObject *
node_get_textContent(LibXML_NodeObject *self, void *closure)
{
    xmlChar *content = xmlNodeGetContent(self->node);
    if (content == NULL) return PyUnicode_FromString("");
    PyObject *ret = PyUnicode_FromString((const char *)content);
    xmlFree(content);
    return ret;
}

static PyObject *
node_string_value(LibXML_NodeObject *self, PyObject *args)
{
    return node_get_textContent(self, NULL);
}

/* getNamespaceURI() method wrapping the namespaceURI property */
static PyObject *
node_getNamespaceURI(LibXML_NodeObject *self, PyObject *args)
{
    return node_get_namespaceURI(self, NULL);
}

/* getName() returns qualified node name (same as nodeName property) */
static PyObject *
node_getName(LibXML_NodeObject *self, PyObject *args)
{
    return node_get_nodeName(self, NULL);
}

static int
node_set_textContent(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (value == NULL || value == Py_None) {
        xmlNodeSetContent(self->node, BAD_CAST "");
        return 0;
    }
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    xmlNodeSetContent(self->node, BAD_CAST s);
    return 0;
}

static PyObject *
node_get_parentNode(LibXML_NodeObject *self, void *closure)
{
    if (!self->node) Py_RETURN_NONE;
    xmlNodePtr parent = self->node->parent;
    if (parent == NULL) Py_RETURN_NONE;
    return wrap_with_doc(parent, self);
}

static PyObject *
node_get_firstChild(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr child = self->node->children;
    if (child == NULL) Py_RETURN_NONE;
    return wrap_with_doc(child, self);
}

static PyObject *
node_get_lastChild(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr child = self->node->last;
    if (child == NULL) Py_RETURN_NONE;
    return wrap_with_doc(child, self);
}

static PyObject *
node_get_nextSibling(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr sib = self->node->next;
    if (sib == NULL) Py_RETURN_NONE;
    return wrap_with_doc(sib, self);
}

static PyObject *
node_get_previousSibling(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr sib = self->node->prev;
    if (sib == NULL) Py_RETURN_NONE;
    return wrap_with_doc(sib, self);
}

static PyObject *
node_get_ownerDocument(LibXML_NodeObject *self, void *closure)
{
    if (self->doc_obj == NULL) {
        /* self is the document */
        Py_INCREF(self);
        return (PyObject *)self;
    }
    Py_INCREF(self->doc_obj);
    return self->doc_obj;
}

static PyObject *
node_get_baseURI(LibXML_NodeObject *self, void *closure)
{
    xmlChar *uri = xmlNodeGetBase(
        self->node->doc ? self->node->doc :
            (self->doc_obj ? (xmlDocPtr)LibXML_NODE(self->doc_obj) : NULL),
        self->node);
    if (uri == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)uri);
    xmlFree(uri);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Methods                                                             */
/* ------------------------------------------------------------------ */

static PyObject *
node_childNodes(LibXML_NodeObject *self, PyObject *args)
{
    return LibXML_NodeList_FromNodePtr(self->node->children, node_doc_obj(self));
}

static PyObject *
node_hasChildNodes(LibXML_NodeObject *self, PyObject *args)
{
    /* DOM: attribute nodes have no children in the DOM sense */
    if (self->node->type == XML_ATTRIBUTE_NODE)
        return PyBool_FromLong(0);
    return PyBool_FromLong(self->node->children != NULL);
}

/* Link cur to parent->children at end WITHOUT merging adjacent text nodes.
 * Caller must have already unlinked cur from its previous parent. */
static xmlNodePtr
link_child_no_merge(xmlNodePtr parent, xmlNodePtr cur)
{
    cur->parent = parent;
    cur->next   = NULL;
    if (parent->last) {
        cur->prev        = parent->last;
        parent->last->next = cur;
        parent->last     = cur;
    } else {
        parent->children = parent->last = cur;
        cur->prev = NULL;
    }
    return cur;
}

/* Helper: adopt child into self's document if needed, then append */
static xmlNodePtr
do_append_child(LibXML_NodeObject *self, LibXML_NodeObject *child_wrap)
{
    xmlNodePtr child = child_wrap->node;
    xmlDocPtr  our_doc = LibXML_DOC_FROM_NODE(self);

    /* Unlink from current parent */
    xmlUnlinkNode(child);

    /* Move to our document if different */
    if (child->doc != our_doc) {
        xmlSetTreeDoc(child, our_doc);
        LibXML_update_doc_refs(child, node_doc_obj(self));
    }

    /* Use xmlAddChild for attribute nodes; manual link for others to
     * prevent libxml2 from merging adjacent text nodes. */
    xmlNodePtr result;
    if (child->type == XML_ATTRIBUTE_NODE)
        result = xmlAddChild(self->node, child);
    else
        result = link_child_no_merge(self->node, child);
    if (result == NULL) {
        PyErr_SetString(LibXML_Error, "appendChild failed");
        return NULL;
    }
    child_wrap->owned = 0;
    return result;
}

static PyObject *
node_appendChild(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *child;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &child)) return NULL;

    /* DocumentFragment: inline its children one by one */
    if (child->node->type == XML_DOCUMENT_FRAG_NODE) {
        xmlDocPtr our_doc = LibXML_DOC_FROM_NODE(self);
        xmlNodePtr fc = child->node->children;
        while (fc) {
            xmlNodePtr next = fc->next;
            xmlUnlinkNode(fc);
            if (fc->doc != our_doc) {
                xmlSetTreeDoc(fc, our_doc);
                LibXML_update_doc_refs(fc, node_doc_obj(self));
            }
            if (fc->type == XML_ATTRIBUTE_NODE) {
                if (xmlAddChild(self->node, fc) == NULL) {
                    PyErr_SetString(LibXML_Error, "appendChild fragment child failed");
                    return NULL;
                }
            } else {
                link_child_no_merge(self->node, fc);
            }
            fc = next;
        }
        Py_INCREF(child);
        return (PyObject *)child;
    }

    xmlNodePtr result = do_append_child(self, child);
    if (result == NULL) return NULL;

    return wrap_with_doc(result, self);
}

static PyObject *
node_insertBefore(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node, *ref_node;
    if (!PyArg_ParseTuple(args, "O!O!",
            &LibXML_NodeType, &new_node,
            &LibXML_NodeType, &ref_node))
        return NULL;

    xmlNodePtr newn = new_node->node;
    xmlDocPtr  our_doc = LibXML_DOC_FROM_NODE(self);

    xmlUnlinkNode(newn);
    if (newn->doc != our_doc) {
        xmlSetTreeDoc(newn, our_doc);
        LibXML_update_doc_refs(newn, node_doc_obj(self));
    }

    xmlNodePtr result = xmlAddPrevSibling(ref_node->node, newn);
    if (result == NULL) {
        PyErr_SetString(LibXML_Error, "insertBefore failed");
        return NULL;
    }
    new_node->owned = 0;
    return wrap_with_doc(result, self);
}

static PyObject *
node_insertAfter(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *new_node_obj, *ref_node_obj;
    if (!PyArg_ParseTuple(args, "O!O",
            &LibXML_NodeType, &new_node_obj, &ref_node_obj))
        return NULL;

    LibXML_NodeObject *new_node = (LibXML_NodeObject *)new_node_obj;
    xmlNodePtr newn = new_node->node;
    xmlDocPtr  our_doc = LibXML_DOC_FROM_NODE(self);

    xmlUnlinkNode(newn);
    if (newn->doc != our_doc) {
        xmlSetTreeDoc(newn, our_doc);
        LibXML_update_doc_refs(newn, node_doc_obj(self));
    }

    xmlNodePtr result;
    if (ref_node_obj == Py_None) {
        /* No reference node: append as last child */
        result = xmlAddChild(self->node, newn);
    } else {
        if (!PyObject_TypeCheck(ref_node_obj, &LibXML_NodeType)) {
            PyErr_SetString(PyExc_TypeError, "ref_node must be a Node or None");
            return NULL;
        }
        LibXML_NodeObject *ref_node = (LibXML_NodeObject *)ref_node_obj;
        result = xmlAddNextSibling(ref_node->node, newn);
    }
    if (result == NULL) {
        PyErr_SetString(LibXML_Error, "insertAfter failed");
        return NULL;
    }
    new_node->owned = 0;
    return wrap_with_doc(result, self);
}

static PyObject *
node_removeChild(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *child;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &child)) return NULL;

    /* Verify child is actually a child of self */
    if (child->node->parent != self->node) {
        PyErr_SetString(LibXML_Error, "node is not a child of this node");
        return NULL;
    }

    xmlUnlinkNode(child->node);
    child->owned = 1;  /* caller now owns it */

    Py_INCREF(child);
    return (PyObject *)child;
}

static PyObject *
node_replaceChild(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node, *old_node;
    if (!PyArg_ParseTuple(args, "O!O!",
            &LibXML_NodeType, &new_node,
            &LibXML_NodeType, &old_node))
        return NULL;

    if (old_node->node->parent != self->node) {
        PyErr_SetString(LibXML_Error, "old node is not a child of this node");
        return NULL;
    }

    xmlNodePtr newn = new_node->node;
    xmlDocPtr  our_doc = LibXML_DOC_FROM_NODE(self);

    xmlUnlinkNode(newn);
    if (newn->doc != our_doc) {
        xmlSetTreeDoc(newn, our_doc);
        LibXML_update_doc_refs(newn, node_doc_obj(self));
    }

    xmlNodePtr replaced = xmlReplaceNode(old_node->node, newn);
    if (replaced == NULL) {
        PyErr_SetString(LibXML_Error, "replaceChild failed");
        return NULL;
    }
    old_node->owned = 1;
    new_node->owned = 0;

    Py_INCREF(old_node);
    return (PyObject *)old_node;
}

static PyObject *
node_replaceNode(LibXML_NodeObject *self, PyObject *args)
{
    /* Replace self with new_node; return self */
    LibXML_NodeObject *new_node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &new_node)) return NULL;

    xmlNodePtr newn = new_node->node;
    xmlDocPtr  our_doc = self->node->doc;

    xmlUnlinkNode(newn);
    if (our_doc && newn->doc != our_doc) {
        xmlSetTreeDoc(newn, our_doc);
        LibXML_update_doc_refs(newn, node_doc_obj(self));
    }

    xmlNodePtr replaced = xmlReplaceNode(self->node, newn);
    if (replaced == NULL) {
        PyErr_SetString(LibXML_Error, "replaceNode failed");
        return NULL;
    }
    self->owned = 1;
    new_node->owned = 0;

    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
node_removeChildNodes(LibXML_NodeObject *self, PyObject *args)
{
    /* Remove and free all children */
    xmlNodePtr child = self->node->children;
    while (child) {
        xmlNodePtr next = child->next;
        /* Clear any Python wrappers pointing at these nodes */
        LibXML_clear_private_recursive(child);
        child = next;
    }
    xmlFreeNodeList(self->node->children);
    self->node->children = NULL;
    self->node->last = NULL;
    Py_RETURN_NONE;
}

static PyObject *
node_cloneNode(LibXML_NodeObject *self, PyObject *args)
{
    int deep = 0;
    if (!PyArg_ParseTuple(args, "|i", &deep)) return NULL;

    /* recursive=2 = shallow with properties (attrs); recursive=1 = deep */
    xmlNodePtr clone = xmlCopyNode(self->node, deep ? 1 : 2);
    if (clone == NULL) {
        PyErr_SetString(LibXML_Error, "cloneNode failed");
        return NULL;
    }

    /* Clone is a standalone owned tree */
    PyObject *doc_obj = node_doc_obj(self);
    PyObject *result = LibXML_Node_Wrap(clone, doc_obj);
    if (result) {
        ((LibXML_NodeObject *)result)->owned = 1;
    } else {
        xmlFreeNode(clone);
    }
    return result;
}

static PyObject *
node_unbindNode(LibXML_NodeObject *self, PyObject *args)
{
    xmlUnlinkNode(self->node);
    self->owned = 1;
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
node_addSibling(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &new_node)) return NULL;

    xmlNodePtr newn = new_node->node;
    xmlUnlinkNode(newn);
    if (self->node->doc && newn->doc != self->node->doc)
        xmlSetTreeDoc(newn, self->node->doc);

    xmlNodePtr result = xmlAddSibling(self->node, newn);
    if (result == NULL) {
        PyErr_SetString(LibXML_Error, "addSibling failed");
        return NULL;
    }
    new_node->owned = 0;
    return wrap_with_doc(result, self);
}

/* Recursive normalize helper */
static void
do_normalize(xmlNodePtr node)
{
    xmlNodePtr child = node->children;
    while (child) {
        xmlNodePtr next = child->next;
        /* Recurse into element children first */
        if (child->type == XML_ELEMENT_NODE)
            do_normalize(child);
        /* Merge this text node with the next if both are text */
        if (child->type == XML_TEXT_NODE && next &&
            next->type == XML_TEXT_NODE) {
            xmlNodeAddContent(child, next->content);
            xmlNodePtr to_free = next;
            next = to_free->next;
            if (to_free->_private)
                ((LibXML_NodeObject *)to_free->_private)->node = NULL;
            to_free->_private = NULL;
            xmlUnlinkNode(to_free);
            xmlFreeNode(to_free);
        } else {
            child = next;
        }
    }
}

static PyObject *
node_normalize(LibXML_NodeObject *self, PyObject *args)
{
    do_normalize(self->node);
    Py_RETURN_NONE;
}

static PyObject *
node_isSameNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *other;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &other)) return NULL;
    return PyBool_FromLong(self->node == other->node);
}

static PyObject *
node_nodePath(LibXML_NodeObject *self, PyObject *args)
{
    xmlChar *path = xmlGetNodePath(self->node);
    if (path == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)path);
    xmlFree(path);
    return ret;
}

static PyObject *
node_line_number(LibXML_NodeObject *self, PyObject *args)
{
    return PyLong_FromLong((long)xmlGetLineNo(self->node));
}

/* ------------------------------------------------------------------ */
/* toString / serialize                                                */
/* ------------------------------------------------------------------ */

static PyObject *
node_toString(LibXML_NodeObject *self, PyObject *args, PyObject *kwds)
{
    int format = 0;
    static char *kwlist[] = {"format", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &format))
        return NULL;

    xmlDocPtr doc = self->node->doc ? self->node->doc :
        (self->doc_obj ? (xmlDocPtr)LibXML_NODE(self->doc_obj) : NULL);

    xmlBufferPtr buf = xmlBufferCreate();
    if (buf == NULL) return PyErr_NoMemory();

    int bytes = xmlNodeDump(buf, doc, self->node, 0, format);
    if (bytes < 0) {
        xmlBufferFree(buf);
        PyErr_SetString(LibXML_Error, "serialization failed");
        return NULL;
    }

    PyObject *result = PyUnicode_FromStringAndSize(
        (const char *)xmlBufferContent(buf), xmlBufferLength(buf));
    xmlBufferFree(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* XPath: findnodes / find / findvalue / exists                        */
/* ------------------------------------------------------------------ */

/* Register in-scope namespace declarations from the context node.
 * This allows XPath expressions to use prefix:localname syntax. */
static void
register_inscope_ns(xmlXPathContextPtr ctx, xmlNodePtr node, xmlDocPtr doc)
{
    if (!node || node->type == XML_DOCUMENT_NODE ||
        node->type == XML_HTML_DOCUMENT_NODE)
        return;
    xmlNsPtr *nsList = xmlGetNsList(doc, node);
    if (!nsList) return;
    int count = 0;
    while (nsList[count]) count++;
    ctx->namespaces = nsList;  /* xmlXPathFreeContext will xmlFree() this */
    ctx->nsNr  = count;
}

static PyObject *
node_findnodes(LibXML_NodeObject *self, PyObject *args)
{
    const char *expr;
    if (!PyArg_ParseTuple(args, "s", &expr)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (doc == NULL) {
        PyErr_SetString(LibXML_Error, "node has no document");
        return NULL;
    }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (ctx == NULL) return PyErr_NoMemory();
    ctx->node = self->node;
    register_inscope_ns(ctx, self->node, doc);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    xmlXPathFreeContext(ctx);

    if (xobj == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }

    PyObject *result;
    if (xobj->type == XPATH_NODESET) {
        result = LibXML_NodeList_FromNodeSet(xobj->nodesetval, node_doc_obj(self));
    } else {
        result = LibXML_NodeList_FromNodeSet(NULL, node_doc_obj(self));
    }
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
node_find(LibXML_NodeObject *self, PyObject *args)
{
    const char *expr;
    if (!PyArg_ParseTuple(args, "s", &expr)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (doc == NULL) {
        PyErr_SetString(LibXML_Error, "node has no document");
        return NULL;
    }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (ctx == NULL) return PyErr_NoMemory();
    ctx->node = self->node;
    register_inscope_ns(ctx, self->node, doc);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    xmlXPathFreeContext(ctx);

    if (xobj == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }

    PyObject *result = LibXML_xpath_to_python(xobj, node_doc_obj(self));
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
node_findvalue(LibXML_NodeObject *self, PyObject *args)
{
    const char *expr;
    if (!PyArg_ParseTuple(args, "s", &expr)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (doc == NULL) {
        PyErr_SetString(LibXML_Error, "node has no document");
        return NULL;
    }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (ctx == NULL) return PyErr_NoMemory();
    ctx->node = self->node;
    register_inscope_ns(ctx, self->node, doc);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    xmlXPathFreeContext(ctx);

    if (xobj == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }

    /* For nodesets, concatenate all text (Perl to_literal behaviour).
     * For other types, cast to string via XPath rules. */
    xmlChar *str;
    if (xobj->type == XPATH_NODESET) {
        xmlNodeSetPtr ns = xobj->nodesetval;
        if (!ns || ns->nodeNr == 0) {
            xmlXPathFreeObject(xobj);
            return PyUnicode_FromString("");
        }
        str = xmlStrdup(BAD_CAST "");
        for (int i = 0; i < ns->nodeNr; i++) {
            xmlChar *content = xmlNodeGetContent(ns->nodeTab[i]);
            if (content) {
                xmlChar *tmp = xmlStrcat(str, content);
                xmlFree(content);
                str = tmp;
            }
        }
    } else {
        str = xmlXPathCastToString(xobj);
    }
    xmlXPathFreeObject(xobj);

    if (str == NULL) return PyUnicode_FromString("");
    PyObject *result = PyUnicode_FromString((const char *)str);
    xmlFree(str);
    return result;
}

static PyObject *
node_exists(LibXML_NodeObject *self, PyObject *args)
{
    const char *expr;
    if (!PyArg_ParseTuple(args, "s", &expr)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (doc == NULL) {
        PyErr_SetString(LibXML_Error, "node has no document");
        return NULL;
    }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (ctx == NULL) return PyErr_NoMemory();
    ctx->node = self->node;

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    xmlXPathFreeContext(ctx);

    if (xobj == NULL) {
        LibXML_raise_last_error();
        return NULL;
    }

    int val = xmlXPathCastToBoolean(xobj);
    xmlXPathFreeObject(xobj);
    return PyBool_FromLong(val);
}

/* ------------------------------------------------------------------ */
/* Namespace lookup                                                    */
/* ------------------------------------------------------------------ */

static PyObject *
node_lookupNamespaceURI(LibXML_NodeObject *self, PyObject *args)
{
    const char *prefix;
    if (!PyArg_ParseTuple(args, "z", &prefix)) return NULL;

    xmlNsPtr ns = xmlSearchNs(self->node->doc, self->node,
                              prefix ? BAD_CAST prefix : NULL);
    if (ns == NULL || ns->href == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)ns->href);
}

static PyObject *
node_lookupNamespacePrefix(LibXML_NodeObject *self, PyObject *args)
{
    const char *uri;
    if (!PyArg_ParseTuple(args, "s", &uri)) return NULL;

    xmlNsPtr ns = xmlSearchNsByHref(self->node->doc, self->node, BAD_CAST uri);
    if (ns == NULL || ns->prefix == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)ns->prefix);
}

/* ------------------------------------------------------------------ */
/* getNamespaces — returns list of (prefix, uri) tuples               */
/* ------------------------------------------------------------------ */

static PyObject *
node_getNamespaces(LibXML_NodeObject *self, PyObject *args)
{
    if (self->node->type != XML_ELEMENT_NODE)
        return PyList_New(0);

    xmlNsPtr *nsList = xmlGetNsList(self->node->doc, self->node);
    if (nsList == NULL) return PyList_New(0);

    PyObject *result = PyList_New(0);
    for (int i = 0; nsList[i] != NULL; i++) {
        const char *prefix = nsList[i]->prefix ?
            (const char *)nsList[i]->prefix : "";
        const char *href = nsList[i]->href ?
            (const char *)nsList[i]->href : "";
        PyObject *tup = PyTuple_Pack(2,
            PyUnicode_FromString(prefix),
            PyUnicode_FromString(href));
        PyList_Append(result, tup);
        Py_DECREF(tup);
    }
    xmlFree(nsList);
    return result;
}

/* ------------------------------------------------------------------ */
/* hasAttributes / attributes                                          */
/* ------------------------------------------------------------------ */

static PyObject *
node_hasAttributes(LibXML_NodeObject *self, PyObject *args)
{
    if (self->node->type == XML_ELEMENT_NODE)
        return PyBool_FromLong(self->node->properties != NULL);
    return PyBool_FromLong(0);
}

static PyObject *
node_attributes(LibXML_NodeObject *self, PyObject *args)
{
    /* Returns a list of Attr wrappers + namespace declaration wrappers */
    PyObject *list = PyList_New(0);
    if (self->node->type != XML_ELEMENT_NODE) return list;

    /* Namespace declarations (nsDef chain) — nodeType == XML_NAMESPACE_DECL (18) */
    xmlNsPtr ns = self->node->nsDef;
    while (ns) {
        PyObject *a = LibXML_Node_Wrap((xmlNodePtr)ns, node_doc_obj(self));
        PyList_Append(list, a);
        Py_DECREF(a);
        ns = ns->next;
    }

    /* Regular attributes */
    xmlAttrPtr attr = self->node->properties;
    while (attr) {
        PyObject *a = LibXML_Node_Wrap((xmlNodePtr)attr, node_doc_obj(self));
        PyList_Append(list, a);
        Py_DECREF(a);
        attr = attr->next;
    }
    return list;
}

static PyObject *
node_setOwnerDocument(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *doc_arg;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &doc_arg)) return NULL;

    /* This is essentially adoptNode on the document side */
    xmlDocPtr new_doc;
    PyObject *new_doc_obj;
    if (doc_arg->doc_obj == NULL) {
        /* doc_arg is itself the document */
        new_doc = (xmlDocPtr)doc_arg->node;
        new_doc_obj = (PyObject *)doc_arg;
    } else {
        new_doc = (xmlDocPtr)LibXML_NODE(doc_arg->doc_obj);
        new_doc_obj = doc_arg->doc_obj;
    }

    xmlUnlinkNode(self->node);
    xmlSetTreeDoc(self->node, new_doc);
    LibXML_update_doc_refs(self->node, new_doc_obj);
    self->owned = 1;
    Py_RETURN_NONE;
}

/* unique_key: return a unique integer identifying this node */
static PyObject *
node_unique_key(LibXML_NodeObject *self, PyObject *args)
{
    return PyLong_FromVoidPtr(self->node);
}

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_Node_methods[] = {
    {"childNodes",           (PyCFunction)node_childNodes,           METH_NOARGS,  "Return a NodeList of child nodes"},
    {"hasChildNodes",        (PyCFunction)node_hasChildNodes,         METH_NOARGS,  "Return True if node has children"},
    {"appendChild",          (PyCFunction)node_appendChild,           METH_VARARGS, "Append child node"},
    {"insertBefore",         (PyCFunction)node_insertBefore,          METH_VARARGS, "Insert new_node before ref_node"},
    {"insertAfter",          (PyCFunction)node_insertAfter,           METH_VARARGS, "Insert new_node after ref_node"},
    {"removeChild",          (PyCFunction)node_removeChild,           METH_VARARGS, "Remove and return child node"},
    {"replaceChild",         (PyCFunction)node_replaceChild,          METH_VARARGS, "Replace old_node with new_node, return old_node"},
    {"replaceNode",          (PyCFunction)node_replaceNode,           METH_VARARGS, "Replace self with new_node"},
    {"removeChildNodes",     (PyCFunction)node_removeChildNodes,      METH_NOARGS,  "Remove all child nodes"},
    {"cloneNode",            (PyCFunction)node_cloneNode,             METH_VARARGS, "Clone node (deep=False)"},
    {"unbindNode",           (PyCFunction)node_unbindNode,            METH_NOARGS,  "Detach node from its parent"},
    {"addSibling",           (PyCFunction)node_addSibling,            METH_VARARGS, "Add a sibling node"},
    {"normalize",            (PyCFunction)node_normalize,             METH_NOARGS,  "Merge adjacent text nodes"},
    {"isSameNode",           (PyCFunction)node_isSameNode,            METH_VARARGS, "Return True if same underlying node"},
    {"nodePath",             (PyCFunction)node_nodePath,              METH_NOARGS,  "Return XPath path to this node"},
    {"line_number",          (PyCFunction)node_line_number,           METH_NOARGS,  "Return source line number"},
    {"toString",             (PyCFunction)node_toString,              METH_VARARGS|METH_KEYWORDS, "Serialize to string"},
    {"serialize",            (PyCFunction)node_toString,              METH_VARARGS|METH_KEYWORDS, "Serialize to string (alias)"},
    {"findnodes",            (PyCFunction)node_findnodes,             METH_VARARGS, "Return NodeList for XPath expression"},
    {"find",                 (PyCFunction)node_find,                  METH_VARARGS, "Evaluate XPath, return typed result"},
    {"findvalue",            (PyCFunction)node_findvalue,             METH_VARARGS, "Evaluate XPath, return string value"},
    {"exists",               (PyCFunction)node_exists,                METH_VARARGS, "Return True if XPath matches"},
    {"lookupNamespaceURI",   (PyCFunction)node_lookupNamespaceURI,    METH_VARARGS, "Look up namespace URI for prefix"},
    {"lookupNamespacePrefix",(PyCFunction)node_lookupNamespacePrefix, METH_VARARGS, "Look up prefix for namespace URI"},
    {"getNamespaces",        (PyCFunction)node_getNamespaces,         METH_NOARGS,  "Return list of (prefix, uri) tuples"},
    {"hasAttributes",        (PyCFunction)node_hasAttributes,         METH_NOARGS,  "Return True if node has attributes"},
    {"attributes",           (PyCFunction)node_attributes,            METH_NOARGS,  "Return list of Attr nodes"},
    {"unique_key",           (PyCFunction)node_unique_key,            METH_NOARGS,  "Return unique integer key for this node"},
    {"setOwnerDocument",     (PyCFunction)node_setOwnerDocument,      METH_VARARGS, "Set owner document (adopt node)"},
    {"getNamespaceURI",      (PyCFunction)node_getNamespaceURI,       METH_NOARGS,  "Return namespace URI of this node"},
    {"getName",              (PyCFunction)node_getName,               METH_NOARGS,  "Return qualified node name"},
    /* Aliases */
    {"addChild",             (PyCFunction)node_appendChild,           METH_VARARGS, "Append child (alias for appendChild)"},
    {"string_value",         (PyCFunction)node_string_value,          METH_NOARGS,  "Return text content (alias for textContent)"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* Getset table                                                        */
/* ------------------------------------------------------------------ */

static PyGetSetDef LibXML_Node_getset[] = {
    {"nodeName",     (getter)node_get_nodeName,     NULL,                    "Node name",           NULL},
    {"localname",    (getter)node_get_localname,    NULL,                    "Local name",          NULL},
    {"localName",    (getter)node_get_localname,    NULL,                    "Local name (DOM alias)", NULL},
    {"prefix",       (getter)node_get_prefix,       NULL,                    "Namespace prefix",    NULL},
    {"namespaceURI", (getter)node_get_namespaceURI, NULL,                    "Namespace URI",       NULL},
    {"nodeType",     (getter)node_get_nodeType,     NULL,                    "Node type integer",   NULL},
    {"nodeValue",    (getter)node_get_nodeValue,    (setter)node_set_nodeValue,  "Node value",      NULL},
    {"textContent",  (getter)node_get_textContent,  (setter)node_set_textContent,"Text content",   NULL},
    {"parentNode",   (getter)node_get_parentNode,   NULL,                    "Parent node",         NULL},
    {"firstChild",   (getter)node_get_firstChild,   NULL,                    "First child node",    NULL},
    {"lastChild",    (getter)node_get_lastChild,    NULL,                    "Last child node",     NULL},
    {"nextSibling",  (getter)node_get_nextSibling,  NULL,                    "Next sibling node",   NULL},
    {"previousSibling",(getter)node_get_previousSibling, NULL,               "Previous sibling",    NULL},
    {"ownerDocument",(getter)node_get_ownerDocument,NULL,                    "Owner document",      NULL},
    {"baseURI",      (getter)node_get_baseURI,      NULL,                    "Base URI",            NULL},
    {NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Node",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_dealloc   = (destructor)LibXML_Node_dealloc,
    .tp_repr      = (reprfunc)LibXML_Node_repr,
    .tp_str       = (reprfunc)LibXML_Node_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_traverse  = (traverseproc)LibXML_Node_traverse,
    .tp_clear     = (inquiry)LibXML_Node_clear,
    .tp_methods   = LibXML_Node_methods,
    .tp_getset    = LibXML_Node_getset,
    .tp_doc       = "Base DOM node type",
};

/* ------------------------------------------------------------------ */
/* DocumentFragment type (thin subtype of Node)                        */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_DocumentFragment_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    xmlNodePtr node = xmlNewDocFragment(NULL);
    if (!node) { PyErr_NoMemory(); return NULL; }
    PyObject *result = LibXML_Node_Wrap(node, NULL);
    if (result) ((LibXML_NodeObject *)result)->owned = 1;
    return result;
}

PyTypeObject LibXML_DocumentFragmentType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.DocumentFragment",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_DocumentFragment_new,
    .tp_doc       = "Document fragment node",
};

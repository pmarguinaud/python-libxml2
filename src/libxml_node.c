#include <stdlib.h>
#include "libxml_node.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_namespace.h"
#include "libxml_document.h"
#include "libxml_nodelist.h"
#include "libxml_xpath.h"
#include <libxml/uri.h>
#include <libxml/tree.h>

/* ------------------------------------------------------------------ */
/* tp_dealloc                                                          */
/* ------------------------------------------------------------------ */

static void
LibXML_Node_dealloc(LibXML_NodeObject *self)
{
    PyObject_GC_UnTrack(self);
    if (self->node != NULL) {
        xmlNodePtr node = self->node;
        LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
        self->node = NULL;

        if (priv && priv->wrapper == (PyObject *)self) {
            priv->wrapper = NULL;
            if (priv->cargo == NULL) {
                free(priv);
                node->_private = NULL;
            }
        }

        /* A root is the last wrapper of its tree: see libxml_tree.h */
        if (self->owner == NULL)
            LibXML_free_tree(node);
    }
    Py_XDECREF(self->owner);
    Py_XDECREF(self->doc_obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ------------------------------------------------------------------ */
/* GC support                                                          */
/* ------------------------------------------------------------------ */

/* No tp_clear: wrappers only refer upwards to the root and the document, so
 * they form no cycle, and `owner` must stay set until the wrapper dies. */
int
LibXML_Node_traverse(LibXML_NodeObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->owner);
    Py_VISIT(self->doc_obj);
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
    return PyUnicode_FromFormat("<%s nodeName='%s'>",
        Py_TYPE(self)->tp_name,
        self->node->name ? (const char *)self->node->name : "(null)");
}

/* ------------------------------------------------------------------ */
/* Properties (getset)                                                 */
/* ------------------------------------------------------------------ */

/* Node types with a fixed DOM name, or NULL */
static const char *
fixed_node_name(xmlNodePtr node)
{
    switch (node->type) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE: return "#document";
    case XML_TEXT_NODE:          return "#text";
    case XML_COMMENT_NODE:       return "#comment";
    case XML_CDATA_SECTION_NODE: return "#cdata-section";
    case XML_DOCUMENT_FRAG_NODE: return "#document-fragment";
    default:                     return NULL;
    }
}

/* Only these node types have a namespace. */
static xmlNsPtr
node_ns(xmlNodePtr node)
{
    if (node->type == XML_ELEMENT_NODE || node->type == XML_ATTRIBUTE_NODE)
        return node->ns;
    return NULL;
}

static PyObject *
node_get_nodeName(LibXML_NodeObject *self, void *closure)
{
    const char *fixed = fixed_node_name(self->node);
    if (fixed) return PyUnicode_FromString(fixed);

    const xmlChar *name = self->node->name;
    if (name == NULL) Py_RETURN_NONE;

    xmlNsPtr ns = node_ns(self->node);
    if (ns && ns->prefix)
        return PyUnicode_FromFormat("%s:%s", (const char *)ns->prefix,
                                    (const char *)name);
    return PyUnicode_FromString((const char *)name);
}

int
LibXML_node_name_is(xmlNodePtr node, const char *name)
{
    const char *fixed = fixed_node_name(node);
    if (fixed) return strcmp(fixed, name) == 0;
    if (node->name == NULL) return 0;

    xmlNsPtr ns = node_ns(node);
    if (ns && ns->prefix) {
        size_t len = strlen((const char *)ns->prefix);
        return strncmp(name, (const char *)ns->prefix, len) == 0
            && name[len] == ':'
            && strcmp(name + len + 1, (const char *)node->name) == 0;
    }
    return strcmp(name, (const char *)node->name) == 0;
}

int
LibXML_set_node_name(xmlNodePtr node, const char *name)
{
    if (LibXML_check_name(name) < 0) return -1;

    xmlChar *prefix = NULL, *local = NULL;
    if (node_ns(node) != NULL)
        local = xmlSplitQName2(BAD_CAST name, &prefix);
    xmlNodeSetName(node, local ? local : BAD_CAST name);
    xmlFree(local);
    xmlFree(prefix);
    return 0;
}

static PyObject *
node_get_localname(LibXML_NodeObject *self, void *closure)
{
    switch (self->node->type) {
    case XML_ELEMENT_NODE:
    case XML_ATTRIBUTE_NODE:
    case XML_ELEMENT_DECL:
    case XML_ATTRIBUTE_DECL:
        return PyUnicode_FromString((const char *)self->node->name);
    default:
        Py_RETURN_NONE;
    }
}

static PyObject *
node_get_prefix(LibXML_NodeObject *self, void *closure)
{
    xmlNsPtr ns = node_ns(self->node);
    if (ns && ns->prefix)
        return PyUnicode_FromString((const char *)ns->prefix);
    Py_RETURN_NONE;
}

static PyObject *
node_get_namespaceURI(LibXML_NodeObject *self, void *closure)
{
    xmlNsPtr ns = node_ns(self->node);
    if (ns && ns->href && ns->href[0] != '\0')
        return PyUnicode_FromString((const char *)ns->href);
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
    switch (self->node->type) {
    case XML_ATTRIBUTE_NODE:
    case XML_TEXT_NODE:
    case XML_COMMENT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_ENTITY_REF_NODE:
        break;
    default:
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
    const char *s = "";
    if (value != NULL && value != Py_None) {
        s = PyUnicode_AsUTF8(value);
        if (s == NULL) return -1;
    }
    LibXML_set_text(self->node, s);
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
    return node_set_nodeValue(self, value, closure);
}

static PyObject *
node_get_parentNode(LibXML_NodeObject *self, void *closure)
{
    if (!self->node) Py_RETURN_NONE;
    xmlNodePtr parent = self->node->parent;
    if (parent == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(parent);
}

static PyObject *
node_get_firstChild(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr child = self->node->children;
    if (child == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(child);
}

static PyObject *
node_get_lastChild(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr child = self->node->last;
    if (child == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(child);
}

static PyObject *
node_get_nextSibling(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr sib = self->node->next;
    if (sib == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(sib);
}

static PyObject *
node_get_previousSibling(LibXML_NodeObject *self, void *closure)
{
    xmlNodePtr sib = self->node->prev;
    if (sib == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap(sib);
}

static PyObject *
node_get_ownerDocument(LibXML_NodeObject *self, void *closure)
{
    LibXML_NodeObject *doc = self->doc_obj ? (LibXML_NodeObject *)self->doc_obj : self;
    if (doc->hidden) Py_RETURN_NONE;
    Py_INCREF(doc);
    return (PyObject *)doc;
}

static PyObject *
node_get_baseURI(LibXML_NodeObject *self, void *closure)
{
    xmlChar *uri = xmlNodeGetBase(self->node->doc, self->node);
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
    return LibXML_NodeList_FromNodePtr(self->node->children);
}

static PyObject *
node_hasChildNodes(LibXML_NodeObject *self, PyObject *args)
{
    /* DOM: attribute nodes have no children in the DOM sense */
    if (self->node->type == XML_ATTRIBUTE_NODE)
        return PyBool_FromLong(0);
    return PyBool_FromLong(self->node->children != NULL);
}

/* The node a caller gets back: a fragment is replaced by its first child. */
static xmlNodePtr
inserted_node(xmlNodePtr node)
{
    return node->type == XML_DOCUMENT_FRAG_NODE ? node->children : node;
}

static PyObject *
node_appendChild(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *child;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &child)) return NULL;

    xmlNodePtr first = inserted_node(child->node);
    if (LibXML_insert_node(child->node, self->node, NULL) < 0) return NULL;
    return LibXML_Node_Wrap(first ? first : child->node);
}

/* Parse a reference node argument: a Node, or None for "at the end". */
static int
parse_ref_node(PyObject *obj, xmlNodePtr *ref)
{
    if (obj == Py_None) {
        *ref = NULL;
        return 0;
    }
    if (!PyObject_TypeCheck(obj, &LibXML_NodeType)) {
        PyErr_SetString(PyExc_TypeError, "ref_node must be a Node or None");
        return -1;
    }
    *ref = LibXML_NODE(obj);
    return 0;
}

static PyObject *
node_insertBefore(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node;
    PyObject *ref_obj;
    xmlNodePtr ref;
    if (!PyArg_ParseTuple(args, "O!O", &LibXML_NodeType, &new_node, &ref_obj))
        return NULL;
    if (parse_ref_node(ref_obj, &ref) < 0) return NULL;

    xmlNodePtr first = inserted_node(new_node->node);
    if (LibXML_insert_node(new_node->node, self->node, ref) < 0) return NULL;
    return LibXML_Node_Wrap(first);
}

static PyObject *
node_insertAfter(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node;
    PyObject *ref_obj;
    xmlNodePtr ref;
    if (!PyArg_ParseTuple(args, "O!O", &LibXML_NodeType, &new_node, &ref_obj))
        return NULL;
    if (parse_ref_node(ref_obj, &ref) < 0) return NULL;

    xmlNodePtr newn = new_node->node;
    xmlNodePtr first = inserted_node(newn);

    if (ref != newn) {
        xmlNodePtr before = NULL;
        if (ref != NULL) {
            if (ref->parent != self->node || ref->type == XML_ATTRIBUTE_NODE) {
                PyErr_SetString(LibXML_Error,
                    "HIERARCHY_REQUEST_ERR: the reference node is not a child of this node");
                return NULL;
            }
            before = (ref->next == newn) ? newn->next : ref->next;
        }
        if (LibXML_insert_node(newn, self->node, before) < 0) return NULL;
    }
    return LibXML_Node_Wrap(first);
}

static PyObject *
node_removeChild(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *child;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &child)) return NULL;

    if (child->node->parent != self->node) {
        PyErr_SetString(LibXML_Error, "node is not a child of this node");
        return NULL;
    }
    if (LibXML_check_detach(child->node) < 0) return NULL;

    LibXML_detach_node(child->node);
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

    if (LibXML_replace_node(old_node->node, new_node->node) < 0) return NULL;
    Py_INCREF(old_node);
    return (PyObject *)old_node;
}

static PyObject *
node_replaceNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &new_node)) return NULL;

    if (LibXML_replace_node(self->node, new_node->node) < 0) return NULL;
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
node_removeChildNodes(LibXML_NodeObject *self, PyObject *args)
{
    if (LibXML_check_children(self->node) < 0) return NULL;

    xmlNodePtr child = self->node->children;
    while (child) {
        xmlNodePtr next = child->next;
        LibXML_release_node(child);
        child = next;
    }
    Py_RETURN_NONE;
}

static PyObject *
node_cloneNode(LibXML_NodeObject *self, PyObject *args)
{
    int deep = 0;
    if (!PyArg_ParseTuple(args, "|i", &deep)) return NULL;

    /* extended: 1 = recursive; 2 = properties and namespaces only */
    xmlNodePtr clone = xmlDocCopyNode(self->node, self->node->doc, deep ? 1 : 2);
    if (clone == NULL) {
        PyErr_SetString(LibXML_Error, "cloneNode failed");
        return NULL;
    }

    PyObject *result = LibXML_Node_Wrap(clone);
    if (result == NULL) xmlFreeNode(clone);
    return result;
}

static PyObject *
node_unbindNode(LibXML_NodeObject *self, PyObject *args)
{
    if (LibXML_check_detach(self->node) < 0) return NULL;

    LibXML_detach_node(self->node);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
node_addSibling(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *new_node;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &new_node)) return NULL;

    xmlNodePtr newn = new_node->node;
    if (newn->type == XML_DOCUMENT_FRAG_NODE) {
        PyErr_SetString(LibXML_Error, "addSibling does not take a fragment");
        return NULL;
    }

    xmlNodePtr last = self->node;
    while (last->next) last = last->next;

    /* As in libxml2, adjacent text is merged. new_node survives, detached. */
    if (last->type == XML_TEXT_NODE && newn->type == XML_TEXT_NODE && last != newn) {
        /* Taken first: if newn is self, it may hold the last reference to the
         * tree that holds `last`. */
        PyObject *result = LibXML_Node_Wrap(last);
        if (result == NULL) return NULL;
        xmlNodeAddContent(last, newn->content);
        LibXML_detach_node(newn);
        return result;
    }

    if (self->node->parent == NULL) {
        PyErr_SetString(LibXML_Error, "addSibling needs a node with a parent");
        return NULL;
    }
    if (LibXML_insert_node(newn, self->node->parent, NULL) < 0) return NULL;
    return LibXML_Node_Wrap(newn);
}

/* Recursive normalize helper */
static void
do_normalize(xmlNodePtr node)
{
    xmlNodePtr child = node->children;
    while (child) {
        xmlNodePtr next = child->next;
        if (child->type == XML_ELEMENT_NODE)
            do_normalize(child);
        if (child->type == XML_TEXT_NODE && next && next->type == XML_TEXT_NODE) {
            xmlNodeAddContent(child, next->content);
            LibXML_release_node(next);
        } else {
            child = next;
        }
    }
}

static PyObject *
node_normalize(LibXML_NodeObject *self, PyObject *args)
{
    if (LibXML_check_children(self->node) < 0) return NULL;

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

    xmlDocPtr doc = self->node->doc;

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

/* Evaluate an XPath expression with self as context node. The prefixes in scope
 * there can be used, and those of the root element for a document. */
static xmlXPathObjectPtr
node_eval(LibXML_NodeObject *self, PyObject *args)
{
    const char *expr;
    if (!PyArg_ParseTuple(args, "s", &expr)) return NULL;

    xmlDocPtr doc = self->node->doc;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (ctx == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    ctx->node = self->node;
    LibXML_errors_begin();

    xmlNodePtr scope = self->node;
    if (scope->type == XML_DOCUMENT_NODE || scope->type == XML_HTML_DOCUMENT_NODE)
        scope = xmlDocGetRootElement(doc);
    if (scope != NULL)
        ctx->namespaces = xmlGetNsList(doc, scope);
    if (ctx->namespaces != NULL) {
        while (ctx->namespaces[ctx->nsNr] != NULL)
            ctx->nsNr++;
    }

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    xmlFree(ctx->namespaces);
    ctx->namespaces = NULL;
    xmlXPathFreeContext(ctx);

    if (xobj == NULL)
        LibXML_raise_last_error();
    return xobj;
}

static PyObject *
node_findnodes(LibXML_NodeObject *self, PyObject *args)
{
    xmlXPathObjectPtr xobj = node_eval(self, args);
    if (xobj == NULL) return NULL;

    PyObject *result = LibXML_NodeList_FromNodeSet(
        xobj->type == XPATH_NODESET ? xobj->nodesetval : NULL);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
node_find(LibXML_NodeObject *self, PyObject *args)
{
    xmlXPathObjectPtr xobj = node_eval(self, args);
    if (xobj == NULL) return NULL;

    PyObject *result = LibXML_xpath_to_python(xobj);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
node_findvalue(LibXML_NodeObject *self, PyObject *args)
{
    xmlXPathObjectPtr xobj = node_eval(self, args);
    if (xobj == NULL) return NULL;

    /* For nodesets, concatenate all text (Perl to_literal behaviour).
     * For other types, cast to string via XPath rules. */
    xmlChar *str;
    if (xobj->type == XPATH_NODESET) {
        xmlNodeSetPtr ns = xobj->nodesetval;
        str = xmlStrdup(BAD_CAST "");
        for (int i = 0; ns != NULL && i < ns->nodeNr; i++) {
            xmlChar *content = xmlNodeGetContent(ns->nodeTab[i]);
            if (content) {
                str = xmlStrcat(str, content);
                xmlFree(content);
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
    xmlXPathObjectPtr xobj = node_eval(self, args);
    if (xobj == NULL) return NULL;

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
    const char *prefix = NULL;
    if (!PyArg_ParseTuple(args, "|z", &prefix)) return NULL;
    if (prefix && prefix[0] == '\0') prefix = NULL;

    xmlNsPtr ns = xmlSearchNs(self->node->doc, self->node, BAD_CAST prefix);
    if (ns == NULL || ns->href == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)ns->href);
}

static PyObject *
node_lookupNamespacePrefix(LibXML_NodeObject *self, PyObject *args)
{
    const char *uri;
    if (!PyArg_ParseTuple(args, "z", &uri)) return NULL;
    if (uri == NULL || uri[0] == '\0') Py_RETURN_NONE;

    xmlNsPtr ns = xmlSearchNsByHref(self->node->doc, self->node, BAD_CAST uri);
    if (ns == NULL) Py_RETURN_NONE;
    return PyUnicode_FromString(ns->prefix ? (const char *)ns->prefix : "");
}

/* ------------------------------------------------------------------ */
/* Namespace declarations of this element                              */
/* ------------------------------------------------------------------ */

static PyObject *
node_getNamespaces(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *result = PyList_New(0);
    if (result == NULL || self->node->type != XML_ELEMENT_NODE) return result;

    for (xmlNsPtr ns = self->node->nsDef; ns; ns = ns->next) {
        if (ns->prefix == NULL && ns->href == NULL) continue;
        PyObject *item = LibXML_Namespace_Copy(ns);
        if (item == NULL || PyList_Append(result, item) < 0) {
            Py_XDECREF(item);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(item);
    }
    return result;
}

static PyObject *
node_getNamespace(LibXML_NodeObject *self, PyObject *args)
{
    xmlNsPtr ns = node_ns(self->node);
    if (ns == NULL) Py_RETURN_NONE;
    return LibXML_Namespace_Copy(ns);
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
    /* Attributes first, then namespace declarations, as in Perl */
    PyObject *list = PyList_New(0);
    if (list == NULL || self->node->type != XML_ELEMENT_NODE) return list;

    for (xmlAttrPtr attr = self->node->properties; attr; attr = attr->next) {
        PyObject *a = LibXML_Node_Wrap((xmlNodePtr)attr);
        if (a == NULL || PyList_Append(list, a) < 0) {
            Py_XDECREF(a);
            Py_DECREF(list);
            return NULL;
        }
        Py_DECREF(a);
    }

    for (xmlNsPtr ns = self->node->nsDef; ns; ns = ns->next) {
        if (ns->prefix == NULL && ns->href == NULL) continue;
        PyObject *a = LibXML_Namespace_Copy(ns);
        if (a == NULL || PyList_Append(list, a) < 0) {
            Py_XDECREF(a);
            Py_DECREF(list);
            return NULL;
        }
        Py_DECREF(a);
    }
    return list;
}

static PyObject *
node_setOwnerDocument(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *doc_arg;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_NodeType, &doc_arg)) return NULL;

    if (LibXML_adopt_node(self->node, doc_arg->node->doc) < 0) return NULL;
    Py_RETURN_NONE;
}

/* Set the base of this node, with xml:base on elements. */
static PyObject *
node_setBaseURI(LibXML_NodeObject *self, PyObject *args)
{
    const char *uri;
    if (!PyArg_ParseTuple(args, "s", &uri)) return NULL;
    xmlNodeSetBase(self->node, BAD_CAST uri);
    Py_RETURN_NONE;
}

static PyObject *
node_setNodeName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if (LibXML_set_node_name(self->node, name) < 0) return NULL;
    Py_RETURN_NONE;
}

/* unique_key: return a unique integer identifying this node */
static PyObject *
node_unique_key(LibXML_NodeObject *self, PyObject *args)
{
    return PyLong_FromVoidPtr(self->node);
}

/* ------------------------------------------------------------------ */
/* cargo: user-defined Python object attached to this node            */
/* ------------------------------------------------------------------ */

static PyObject *
node_getCargo(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)self->node->_private;
    if (!priv || !priv->cargo) Py_RETURN_NONE;
    Py_INCREF(priv->cargo);
    return priv->cargo;
}

static PyObject *
node_setCargo(LibXML_NodeObject *self, PyObject *args)
{
    PyObject *cargo;
    if (!PyArg_ParseTuple(args, "O", &cargo)) return NULL;

    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)self->node->_private;
    if (!priv) {
        priv = (LibXML_NodePrivate *)calloc(1, sizeof(LibXML_NodePrivate));
        if (!priv) return PyErr_NoMemory();
        priv->wrapper = (PyObject *)self;
        self->node->_private = priv;
    }

    PyObject *old = priv->cargo;
    if (cargo == Py_None) {
        priv->cargo = NULL;
    } else {
        Py_INCREF(cargo);
        priv->cargo = cargo;
    }
    Py_XDECREF(old);

    Py_RETURN_NONE;
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
    {"getNamespaces",        (PyCFunction)node_getNamespaces,         METH_NOARGS,  "Return the namespaces declared on this element"},
    {"namespaces",           (PyCFunction)node_getNamespaces,         METH_NOARGS,  "Return the namespaces declared on this element"},
    {"getNamespace",         (PyCFunction)node_getNamespace,          METH_NOARGS,  "Return the namespace of this node"},
    {"localNS",              (PyCFunction)node_getNamespace,          METH_NOARGS,  "Return the namespace of this node"},
    {"localNamespace",       (PyCFunction)node_getNamespace,          METH_NOARGS,  "Return the namespace of this node"},
    {"setNodeName",          (PyCFunction)node_setNodeName,           METH_VARARGS, "Rename this node"},
    {"setBaseURI",           (PyCFunction)node_setBaseURI,            METH_VARARGS, "Set the base URI of this node"},
    {"setName",              (PyCFunction)node_setNodeName,           METH_VARARGS, "Rename this node"},
    {"hasAttributes",        (PyCFunction)node_hasAttributes,         METH_NOARGS,  "Return True if node has attributes"},
    {"attributes",           (PyCFunction)node_attributes,            METH_NOARGS,  "Return list of Attr nodes"},
    {"unique_key",           (PyCFunction)node_unique_key,            METH_NOARGS,  "Return unique integer key for this node"},
    {"setOwnerDocument",     (PyCFunction)node_setOwnerDocument,      METH_VARARGS, "Set owner document (adopt node)"},
    {"getCargo",             (PyCFunction)node_getCargo,              METH_NOARGS,  "Return user cargo attached to this node, or None"},
    {"setCargo",             (PyCFunction)node_setCargo,              METH_VARARGS, "Attach a user Python object to this node (None to remove)"},
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
    PyObject *doc = LibXML_new_hidden_doc();
    if (doc == NULL) return NULL;

    xmlNodePtr node = xmlNewDocFragment((xmlDocPtr)LibXML_NODE(doc));
    PyObject *result = node ? LibXML_Node_Wrap(node) : PyErr_NoMemory();
    if (node && !result) xmlFreeNode(node);
    Py_DECREF(doc);
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

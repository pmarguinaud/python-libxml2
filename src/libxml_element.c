#include "libxml_element.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_parser.h"
#include "libxml_nodelist.h"
#include "libxml_attr.h"
#include "libxml_document.h"
#include <string.h>

#define XMLNS_URI "http://www.w3.org/2000/xmlns/"
#define XML_URI   "http://www.w3.org/XML/1998/namespace"

/* ------------------------------------------------------------------ */
/* Helpers for qualified-name attribute lookup (prefix:localname)     */
/* ------------------------------------------------------------------ */

/* Find an attribute by name, or by prefix:localname through the namespace bound
 * to the prefix. Defaults from a DTD do not count. */
static xmlAttrPtr
get_attr_node(xmlNodePtr node, const char *qname)
{
    xmlAttrPtr ret = xmlHasNsProp(node, BAD_CAST qname, NULL);

    if (ret == NULL) {
        xmlChar *prefix = NULL;
        xmlChar *local = xmlSplitQName2(BAD_CAST qname, &prefix);
        if (local != NULL) {
            xmlNsPtr ns = xmlSearchNs(node->doc, node, prefix);
            if (ns != NULL)
                ret = xmlHasNsProp(node, local, ns->href);
            xmlFree(prefix);
            xmlFree(local);
        }
    }
    return (ret && ret->type == XML_ATTRIBUTE_NODE) ? ret : NULL;
}

/* Name of a namespace declaration attribute: returns the prefix (NULL for the
 * default namespace) in *prefix, and 1 if name is one. */
static int
parse_xmlns_name(const char *name, const char **prefix)
{
    if (strncmp(name, "xmlns", 5) != 0) return 0;
    if (name[5] == '\0') { *prefix = NULL; return 1; }
    if (name[5] == ':')  { *prefix = name[6] ? name + 6 : NULL; return 1; }
    return 0;
}

static PyObject *
decl_uri(xmlNodePtr node, const char *prefix)
{
    for (xmlNsPtr ns = node->nsDef; ns; ns = ns->next) {
        if ((ns->prefix || ns->href) && xmlStrEqual(ns->prefix, BAD_CAST prefix)) {
            if (ns->href == NULL) Py_RETURN_NONE;
            return PyUnicode_FromString((const char *)ns->href);
        }
    }
    Py_RETURN_NONE;
}

static PyObject *
take_string(xmlChar *value)
{
    if (value == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)value);
    xmlFree(value);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Construction: Element(name) or Element(name, namespace_uri)        */
/* ------------------------------------------------------------------ */

static PyObject *
LibXML_Element_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *name;
    const char *ns_uri = NULL;
    static char *kwlist[] = {"name", "namespace_uri", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|z", kwlist, &name, &ns_uri))
        return NULL;

    if (LibXML_check_name(name) < 0) return NULL;

    PyObject *doc = LibXML_new_hidden_doc();
    if (doc == NULL) return NULL;

    xmlNodePtr node = xmlNewDocNode((xmlDocPtr)LibXML_NODE(doc), NULL, BAD_CAST name, NULL);
    if (node == NULL) {
        Py_DECREF(doc);
        PyErr_SetString(PyExc_MemoryError, "xmlNewDocNode failed");
        return NULL;
    }
    if (ns_uri)
        xmlSetNs(node, xmlNewNs(node, BAD_CAST ns_uri, NULL));

    PyObject *result = LibXML_Node_Wrap_As(node, type);
    if (result == NULL) xmlFreeNode(node);
    Py_DECREF(doc);
    return result;
}

/* ------------------------------------------------------------------ */
/* Attribute methods                                                   */
/* ------------------------------------------------------------------ */

/* Set an attribute. An existing one keeps its node, and libxml2 would free the
 * text under it, which a wrapper may refer to. */
static xmlAttrPtr
set_attr(xmlNodePtr node, xmlNsPtr ns, const char *name, const char *value)
{
    xmlAttrPtr attr = xmlHasNsProp(node, BAD_CAST name, ns ? ns->href : NULL);
    if (attr != NULL && attr->type == XML_ATTRIBUTE_NODE) {
        LibXML_set_text((xmlNodePtr)attr, value);
        return attr;
    }
    return xmlSetNsProp(node, ns, BAD_CAST name, BAD_CAST value);
}

/* Change the URI of a declaration on this element. Returns 1 if there is one. */
static int
set_decl_uri(xmlNodePtr node, const char *prefix, const char *uri)
{
    if (prefix && prefix[0] == '\0') prefix = NULL;
    if (uri && uri[0] == '\0') uri = NULL;

    for (xmlNsPtr ns = node->nsDef; ns; ns = ns->next) {
        if ((ns->prefix || ns->href) && xmlStrEqual(ns->prefix, BAD_CAST prefix)) {
            xmlFree((xmlChar *)ns->href);
            ns->href = uri ? xmlStrdup(BAD_CAST uri) : NULL;
            /* The declaration stays, unused, as it does in Perl. */
            if (uri == NULL)
                LibXML_forget_ns(node, ns);
            return 1;
        }
    }
    return 0;
}

/* Declare a namespace, and with activate put the element in it. */
static int
set_namespace(xmlNodePtr node, const char *uri, const char *prefix, int activate)
{
    xmlNsPtr ns = NULL;
    int done;
    char *name = activate ? strdup((const char *)node->name) : NULL;

    if (prefix && prefix[0] == '\0') prefix = NULL;
    if (uri && uri[0] == '\0') uri = NULL;

    if (prefix == NULL && uri == NULL) {
        ns = xmlSearchNs(node->doc, node, NULL);
        if (ns && ns->href && ns->href[0] != '\0') {
            done = 0;   /* an inherited default namespace cannot be left */
        } else if (activate) {
            xmlSetNs(node, NULL);
            done = 1;
        } else {
            done = 0;
        }
        ns = NULL;
    } else if (activate && (ns = xmlSearchNs(node->doc, node, BAD_CAST prefix)) != NULL
               && xmlStrEqual(ns->href, BAD_CAST uri)) {
        done = 1;       /* reuse the declaration */
    } else {
        ns = xmlNewNs(node, BAD_CAST uri, BAD_CAST prefix);
        done = (ns != NULL);
    }

    if (activate && ns != NULL)
        xmlSetNs(node, ns);

    /* The element may have had the prefix in its name. */
    if (done && activate && LibXML_set_node_name(node, name) < 0)
        PyErr_Clear();
    free(name);
    return done;
}

/* An xmlns or xmlns:prefix attribute changes a declaration. */
static PyObject *
set_xmlns_attr(xmlNodePtr node, const char *prefix, const char *value)
{
    int activate = 0;

    if (prefix != NULL && node->ns == NULL) {
        size_t len = strlen(prefix);
        activate = strncmp((const char *)node->name, prefix, len) == 0
                && node->name[len] == ':';
    } else if (prefix != NULL && node->ns->prefix != NULL) {
        activate = strcmp((const char *)node->ns->prefix, prefix) == 0;
    }

    int done = set_decl_uri(node, prefix, value)
            || set_namespace(node, value, prefix, activate);
    return PyBool_FromLong(done);
}

static PyObject *
elem_setAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *value, *decl_prefix;
    if (!PyArg_ParseTuple(args, "ss", &name, &value)) return NULL;

    if (parse_xmlns_name(name, &decl_prefix))
        return set_xmlns_attr(self->node, decl_prefix, value);

    if (LibXML_check_name(name) < 0) return NULL;

    /* A known prefix selects a namespace. An unknown one stays in the name. */
    xmlChar *prefix = NULL;
    xmlChar *local = xmlSplitQName2(BAD_CAST name, &prefix);
    xmlNsPtr ns = local ? xmlSearchNs(self->node->doc, self->node, prefix) : NULL;

    xmlAttrPtr attr = ns ? set_attr(self->node, ns, (const char *)local, value)
                         : set_attr(self->node, NULL, name, value);
    xmlFree(local);
    xmlFree(prefix);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *
namespace_error(const char *message)
{
    PyErr_Format(LibXML_Error, "NAMESPACE ERROR: %s", message);
    return NULL;
}

/* The namespace for an attribute: it needs a prefix, so a default namespace
 * does not do, and a prefixed declaration of the same URI is looked for. */
static xmlNsPtr
attr_ns_for(xmlNodePtr node, const char *uri, const xmlChar *prefix)
{
    xmlNsPtr ns = xmlSearchNsByHref(node->doc, node, BAD_CAST uri);

    if (ns && !ns->prefix) {
        xmlNsPtr *all = xmlGetNsList(node->doc, node);
        ns = NULL;
        for (int i = 0; all && all[i]; i++) {
            if (all[i]->prefix && xmlStrEqual(all[i]->href, BAD_CAST uri)) {
                ns = all[i];
                break;
            }
        }
        xmlFree(all);
    }

    if (ns == NULL && prefix != NULL && prefix[0] != '\0')
        ns = xmlNewNs(node, BAD_CAST uri, prefix);
    return ns;
}

static PyObject *
elem_setAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *qname, *value, *decl_prefix;
    if (!PyArg_ParseTuple(args, "zss", &ns_uri, &qname, &value)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;

    int is_xmlns = parse_xmlns_name(qname, &decl_prefix);

    if (ns_uri && strcmp(ns_uri, XMLNS_URI) == 0) {
        if (!is_xmlns)
            return namespace_error("Namespace declarations must have the prefix 'xmlns'");
        return set_xmlns_attr(self->node, decl_prefix, value);
    }
    if (is_xmlns)
        return namespace_error("'xmlns' prefix and qualified-name are reserved");
    if (strchr(qname, ':') && ns_uri == NULL)
        return namespace_error("Attribute without a prefix cannot be in a namespace");
    if (strncmp(qname, "xml:", 4) == 0 && strcmp(ns_uri, XML_URI) != 0)
        return namespace_error("'xml' prefix is reserved");

    if (LibXML_check_name(qname) < 0) return NULL;

    xmlChar *prefix = NULL;
    xmlChar *local = xmlSplitQName2(BAD_CAST qname, &prefix);

    xmlNsPtr ns = NULL;
    if (ns_uri != NULL) {
        ns = attr_ns_for(self->node, ns_uri, prefix);
        if (ns == NULL) {
            xmlFree(local);
            xmlFree(prefix);
            PyErr_SetString(LibXML_Error, "bad ns attribute");
            return NULL;
        }
    }

    xmlAttrPtr attr = set_attr(self->node, ns, local ? (const char *)local : qname, value);
    xmlFree(local);
    xmlFree(prefix);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *
elem_getAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *decl_prefix;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;

    if (parse_xmlns_name(name, &decl_prefix))
        return decl_uri(self->node, decl_prefix);

    xmlChar *value = xmlGetNoNsProp(self->node, BAD_CAST name);
    if (value == NULL) {
        xmlChar *prefix = NULL;
        xmlChar *local = xmlSplitQName2(BAD_CAST name, &prefix);
        if (local != NULL) {
            xmlNsPtr ns = xmlSearchNs(self->node->doc, self->node, prefix);
            if (ns != NULL)
                value = xmlGetNsProp(self->node, local, ns->href);
            xmlFree(prefix);
            xmlFree(local);
        }
    }
    return take_string(value);
}

static PyObject *
elem_getAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *name;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &name)) return NULL;

    if (ns_uri && strcmp(ns_uri, XMLNS_URI) == 0)
        return decl_uri(self->node, strcmp(name, "xmlns") == 0 ? NULL : name);

    if (ns_uri && ns_uri[0])
        return take_string(xmlGetNsProp(self->node, BAD_CAST name, BAD_CAST ns_uri));
    return take_string(xmlGetProp(self->node, BAD_CAST name));
}

static PyObject *
elem_getAttributeNode(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlAttrPtr attr = get_attr_node(self->node, name);
    if (attr == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap((xmlNodePtr)attr);
}

static PyObject *
elem_getAttributeNodeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    xmlAttrPtr attr = xmlHasNsProp(self->node, BAD_CAST localname, BAD_CAST ns_uri);
    if (attr == NULL || attr->type != XML_ATTRIBUTE_NODE) Py_RETURN_NONE;
    return LibXML_Node_Wrap((xmlNodePtr)attr);
}

static PyObject *
elem_removeAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlAttrPtr attr = get_attr_node(self->node, name);
    if (attr == NULL) Py_RETURN_FALSE;
    if (LibXML_check_attr_stays() < 0) return NULL;
    LibXML_release_node((xmlNodePtr)attr);
    Py_RETURN_TRUE;
}

static PyObject *
elem_removeAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;

    /* Iterate properties directly to find attr by localname + ns href.
     * Using xmlSearchNsByHref fails when elem is not in the document tree. */
    xmlAttrPtr prop = self->node->properties;
    while (prop) {
        xmlAttrPtr next = prop->next;
        if (xmlStrEqual(prop->name, BAD_CAST localname)) {
            const char *prop_ns = (prop->ns && prop->ns->href)
                                  ? (const char *)prop->ns->href : NULL;
            int match = (!ns_uri && !prop_ns) ||
                        (ns_uri && prop_ns && strcmp(ns_uri, prop_ns) == 0);
            if (match) {
                if (LibXML_check_attr_stays() < 0) return NULL;
                LibXML_release_node((xmlNodePtr)prop);
                Py_RETURN_TRUE;
            }
        }
        prop = next;
    }
    return PyBool_FromLong(0);
}

static PyObject *
elem_hasAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return PyBool_FromLong(get_attr_node(self->node, name) != NULL);
}

static PyObject *
elem_hasAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    xmlAttrPtr attr = xmlHasNsProp(self->node, BAD_CAST localname, BAD_CAST ns_uri);
    return PyBool_FromLong(attr != NULL && attr->type == XML_ATTRIBUTE_NODE);
}

/* Returns the attribute it replaced, if any. */
static PyObject *
elem_setAttributeNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *attr_wrap;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_AttrType, &attr_wrap)) return NULL;

    xmlAttrPtr attr = (xmlAttrPtr)attr_wrap->node;
    if (attr->parent == self->node) {
        Py_INCREF(attr_wrap);
        return (PyObject *)attr_wrap;
    }

    xmlAttrPtr same = LibXML_find_same_attr(self->node, attr);
    PyObject *replaced = LibXML_Node_Wrap((xmlNodePtr)same);
    if (replaced == NULL) return NULL;

    if (LibXML_insert_node((xmlNodePtr)attr, self->node, NULL) < 0) {
        Py_DECREF(replaced);
        return NULL;
    }
    return replaced;
}

/* ------------------------------------------------------------------ */
/* Child/descendant element search                                     */
/* ------------------------------------------------------------------ */

typedef enum { MATCH_QNAME, MATCH_LOCAL, MATCH_NS } MatchKind;

typedef struct {
    xmlNodePtr top;
    MatchKind kind;
    const char *name;
    const char *uri;
    xmlNodeSetPtr found;
} ElementSearch;

static int
matches_ns(xmlNodePtr node, const char *uri)
{
    if (strcmp(uri, "*") == 0) return 1;
    const char *href = (node->ns && node->ns->href) ? (const char *)node->ns->href : "";
    return strcmp(uri, href) == 0;
}

static int
matches(xmlNodePtr node, MatchKind kind, const char *name, const char *uri)
{
    int any_name = strcmp(name, "*") == 0;

    switch (kind) {
    case MATCH_QNAME:
        return any_name || LibXML_node_name_is(node, name);
    case MATCH_LOCAL:
        return any_name || strcmp((const char *)node->name, name) == 0;
    default:
        return (any_name || strcmp((const char *)node->name, name) == 0)
            && matches_ns(node, uri);
    }
}

static void
search_visit(xmlNodePtr node, void *arg)
{
    ElementSearch *search = (ElementSearch *)arg;
    if (node->type == XML_ELEMENT_NODE && node != search->top
        && matches(node, search->kind, search->name, search->uri))
        xmlXPathNodeSetAddUnique(search->found, node);
}

/* Elements below top, in document order, as a NodeList. */
PyObject *
LibXML_find_elements(xmlNodePtr top, int kind, const char *name, const char *uri)
{
    ElementSearch search = { top, (MatchKind)kind, name, uri, xmlXPathNodeSetCreate(NULL) };
    if (search.found == NULL) return PyErr_NoMemory();

    LibXML_walk_tree(top, search_visit, &search);

    PyObject *result = LibXML_NodeList_FromNodeSet(search.found);
    xmlXPathFreeNodeSet(search.found);
    return result;
}

/* Direct children of an element, as a NodeList. */
static PyObject *
find_children(xmlNodePtr parent, MatchKind kind, const char *name, const char *uri)
{
    xmlNodeSetPtr found = xmlXPathNodeSetCreate(NULL);
    if (found == NULL) return PyErr_NoMemory();

    for (xmlNodePtr c = parent->children; c; c = c->next) {
        /* By name, any node matches: getChildrenByTagName('#text') works. */
        int match;
        if (strcmp(name, "*") == 0)
            match = c->type == XML_ELEMENT_NODE;
        else if (kind == MATCH_QNAME)
            match = LibXML_node_name_is(c, name);
        else
            match = c->name && strcmp((const char *)c->name, name) == 0;

        if (match && kind == MATCH_NS)
            match = c->type == XML_ELEMENT_NODE ? matches_ns(c, uri)
                                                : strcmp(uri, "*") == 0 || uri[0] == '\0';
        if (match)
            xmlXPathNodeSetAddUnique(found, c);
    }

    PyObject *result = LibXML_NodeList_FromNodeSet(found);
    xmlXPathFreeNodeSet(found);
    return result;
}

static PyObject *
elem_getChildrenByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return find_children(self->node, MATCH_QNAME, name, NULL);
}

static PyObject *
elem_getChildrenByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    return find_children(self->node, MATCH_NS, localname, ns_uri ? ns_uri : "");
}

static PyObject *
elem_getChildrenByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    const char *localname;
    if (!PyArg_ParseTuple(args, "s", &localname)) return NULL;
    return find_children(self->node, MATCH_LOCAL, localname, NULL);
}

static PyObject *
elem_getElementsByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    return LibXML_find_elements(self->node, MATCH_QNAME, name, NULL);
}

static PyObject *
elem_getElementsByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    return LibXML_find_elements(self->node, MATCH_NS, localname, ns_uri ? ns_uri : "");
}

static PyObject *
elem_getElementsByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    const char *localname;
    if (!PyArg_ParseTuple(args, "s", &localname)) return NULL;
    return LibXML_find_elements(self->node, MATCH_LOCAL, localname, NULL);
}

/* ------------------------------------------------------------------ */
/* Text content methods                                                */
/* ------------------------------------------------------------------ */

static PyObject *
elem_appendText(LibXML_NodeObject *self, PyObject *args)
{
    const char *text;
    if (!PyArg_ParseTuple(args, "s", &text)) return NULL;
    xmlNodeAddContent(self->node, BAD_CAST text);
    Py_RETURN_NONE;
}

static PyObject *
elem_appendTextNode(LibXML_NodeObject *self, PyObject *args)
{
    return elem_appendText(self, args);
}

static PyObject *
elem_appendTextChild(LibXML_NodeObject *self, PyObject *args)
{
    const char *childname, *text = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &childname, &text)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    xmlNodePtr child = xmlNewDocNode(doc, NULL, BAD_CAST childname, NULL);
    if (!child) { LibXML_raise_last_error(); return NULL; }

    /* Append raw text child (without entity parsing) */
    if (text && text[0] != '\0') {
        xmlNodePtr txt = xmlNewDocText(doc, BAD_CAST text);
        if (txt) {
            txt->parent = child;
            child->children = child->last = txt;
        }
    }

    xmlAddChild(self->node, child);
    return LibXML_Node_Wrap(child);
}

static PyObject *
elem_appendWellBalancedChunk(LibXML_NodeObject *self, PyObject *args)
{
    const char *chunk;
    if (!PyArg_ParseTuple(args, "s", &chunk)) return NULL;

    PyObject *frag = LibXML_parse_chunk(chunk, 1);
    if (frag == NULL) return NULL;

    int rc = LibXML_insert_node(LibXML_NODE(frag), self->node, NULL);
    Py_DECREF(frag);
    if (rc < 0) return NULL;
    Py_RETURN_NONE;
}

static PyObject *
elem_addNewChild(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *name;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &name)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    if (LibXML_check_name(name) < 0) return NULL;

    xmlDocPtr doc = self->node->doc;
    xmlNodePtr child;

    if (ns_uri != NULL) {
        xmlChar *prefix = NULL;
        xmlChar *local = xmlSplitQName2(BAD_CAST name, &prefix);
        xmlNsPtr ns = xmlSearchNsByHref(doc, self->node, BAD_CAST ns_uri);

        child = xmlNewDocNode(doc, ns, local ? local : BAD_CAST name, NULL);
        if (child != NULL && ns == NULL)
            xmlSetNs(child, xmlNewNs(child, BAD_CAST ns_uri, prefix));
        xmlFree(local);
        xmlFree(prefix);
    } else {
        child = xmlNewDocNode(doc, NULL, BAD_CAST name, NULL);
    }
    if (child == NULL) return PyErr_NoMemory();

    if (LibXML_insert_node(child, self->node, NULL) < 0) {
        xmlFreeNode(child);
        return NULL;
    }
    return LibXML_Node_Wrap(child);
}

/* ------------------------------------------------------------------ */
/* Namespace management                                                */
/* ------------------------------------------------------------------ */

static PyObject *
elem_setNamespace(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *prefix = NULL;
    int activate = 1;
    if (!PyArg_ParseTuple(args, "z|zi", &ns_uri, &prefix, &activate)) return NULL;
    return PyBool_FromLong(set_namespace(self->node, ns_uri, prefix, activate));
}

static PyObject *
elem_setNamespaceDeclURI(LibXML_NodeObject *self, PyObject *args)
{
    const char *prefix, *uri;
    if (!PyArg_ParseTuple(args, "zz", &prefix, &uri)) return NULL;
    return PyBool_FromLong(set_decl_uri(self->node, prefix, uri));
}

static PyObject *
elem_setNamespaceDeclPrefix(LibXML_NodeObject *self, PyObject *args)
{
    const char *old_prefix, *new_prefix;
    if (!PyArg_ParseTuple(args, "zz", &old_prefix, &new_prefix)) return NULL;
    if (old_prefix && old_prefix[0] == '\0') old_prefix = NULL;
    if (new_prefix && new_prefix[0] == '\0') new_prefix = NULL;

    if (xmlStrEqual(BAD_CAST old_prefix, BAD_CAST new_prefix)) Py_RETURN_TRUE;

    xmlNsPtr used = xmlSearchNs(self->node->doc, self->node, BAD_CAST new_prefix);
    if (used != NULL) {
        PyErr_Format(LibXML_Error, "setNamespaceDeclPrefix: prefix '%s' is in use",
                     new_prefix ? new_prefix : "");
        return NULL;
    }

    for (xmlNsPtr ns = self->node->nsDef; ns; ns = ns->next) {
        if ((ns->prefix || ns->href) && xmlStrEqual(ns->prefix, BAD_CAST old_prefix)) {
            if (ns->href == NULL && new_prefix != NULL) {
                PyErr_SetString(LibXML_Error,
                    "setNamespaceDeclPrefix: cannot set non-empty prefix for empty namespace");
                return NULL;
            }
            xmlFree((xmlChar *)ns->prefix);
            ns->prefix = new_prefix ? xmlStrdup(BAD_CAST new_prefix) : NULL;
            Py_RETURN_TRUE;
        }
    }
    Py_RETURN_FALSE;
}

/* ------------------------------------------------------------------ */
/* tagName / setNodeName                                              */
/* ------------------------------------------------------------------ */

static PyObject *
elem_get_tagName(LibXML_NodeObject *self, void *closure)
{
    /* tagName = qualified nodeName */
    if (self->node->ns && self->node->ns->prefix)
        return PyUnicode_FromFormat("%s:%s",
            (const char *)self->node->ns->prefix,
            (const char *)self->node->name);
    if (self->node->name)
        return PyUnicode_FromString((const char *)self->node->name);
    Py_RETURN_NONE;
}

static PyObject *
elem_removeAttributeNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *attr;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_AttrType, &attr)) return NULL;
    if (attr->node->parent != self->node) {
        PyErr_SetString(LibXML_Error, "not an attribute of this element");
        return NULL;
    }
    if (LibXML_check_attr_stays() < 0) return NULL;
    LibXML_detach_node(attr->node);
    Py_INCREF(attr);
    return (PyObject *)attr;
}

static PyObject *
elem_setAttributeNodeNS(LibXML_NodeObject *self, PyObject *args)
{
    /* same as setAttributeNode but namespace-aware */
    return elem_setAttributeNode(self, args);
}

static PyObject *
elem_getAttributeHash(LibXML_NodeObject *self, PyObject *args)
{
    return LibXML_dom_call("AttributeHash", (PyObject *)self, NULL);
}

static PyGetSetDef LibXML_Element_getset[] = {
    {"tagName", (getter)elem_get_tagName, NULL, "Qualified tag name", NULL},
    {NULL}
};

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static PyMethodDef LibXML_Element_methods[] = {
    {"setAttribute",            (PyCFunction)elem_setAttribute,          METH_VARARGS, "Set attribute"},
    {"setAttributeNS",          (PyCFunction)elem_setAttributeNS,        METH_VARARGS, "Set namespaced attribute"},
    {"getAttribute",            (PyCFunction)elem_getAttribute,           METH_VARARGS, "Get attribute value"},
    {"getAttributeNS",          (PyCFunction)elem_getAttributeNS,         METH_VARARGS, "Get namespaced attribute value"},
    {"getAttributeNode",        (PyCFunction)elem_getAttributeNode,       METH_VARARGS, "Get Attr node"},
    {"getAttributeNodeNS",      (PyCFunction)elem_getAttributeNodeNS,     METH_VARARGS, "Get namespaced Attr node"},
    {"removeAttribute",         (PyCFunction)elem_removeAttribute,        METH_VARARGS, "Remove attribute"},
    {"removeAttributeNS",       (PyCFunction)elem_removeAttributeNS,      METH_VARARGS, "Remove namespaced attribute"},
    {"hasAttribute",            (PyCFunction)elem_hasAttribute,            METH_VARARGS, "Check if attribute exists"},
    {"hasAttributeNS",          (PyCFunction)elem_hasAttributeNS,          METH_VARARGS, "Check if namespaced attribute exists"},
    {"getAttributeHash",        (PyCFunction)elem_getAttributeHash,        METH_NOARGS,  "Attributes as a dict"},
    {"setAttributeNode",        (PyCFunction)elem_setAttributeNode,        METH_VARARGS, "Set attribute from Attr node"},
    {"setAttributeNodeNS",      (PyCFunction)elem_setAttributeNodeNS,      METH_VARARGS, "Set namespaced attribute from Attr node"},
    {"removeAttributeNode",     (PyCFunction)elem_removeAttributeNode,     METH_VARARGS, "Remove Attr node"},
    {"addNewChild",             (PyCFunction)elem_addNewChild,             METH_VARARGS, "Create a child element in a namespace"},
    {"getChildrenByTagName",    (PyCFunction)elem_getChildrenByTagName,    METH_VARARGS, "Get direct child elements by tag"},
    {"getChildrenByTagNameNS",  (PyCFunction)elem_getChildrenByTagNameNS,  METH_VARARGS, "Get direct child elements by NS tag"},
    {"getChildrenByLocalName",  (PyCFunction)elem_getChildrenByLocalName,  METH_VARARGS, "Get direct child elements by local name"},
    {"getElementsByTagName",    (PyCFunction)elem_getElementsByTagName,    METH_VARARGS, "Get descendant elements by tag"},
    {"getElementsByTagNameNS",  (PyCFunction)elem_getElementsByTagNameNS,  METH_VARARGS, "Get descendant elements by NS tag"},
    {"getElementsByLocalName",  (PyCFunction)elem_getElementsByLocalName,  METH_VARARGS, "Get descendant elements by local name"},
    {"appendText",              (PyCFunction)elem_appendText,              METH_VARARGS, "Append text content"},
    {"appendTextNode",          (PyCFunction)elem_appendTextNode,          METH_VARARGS, "Append text node"},
    {"appendTextChild",         (PyCFunction)elem_appendTextChild,         METH_VARARGS, "Append child element with text"},
    {"appendWellBalancedChunk", (PyCFunction)elem_appendWellBalancedChunk, METH_VARARGS, "Append well-balanced XML chunk"},
    {"setNamespace",            (PyCFunction)elem_setNamespace,            METH_VARARGS, "Set namespace on element"},
    {"setNamespaceDeclURI",     (PyCFunction)elem_setNamespaceDeclURI,     METH_VARARGS, "Change namespace declaration URI"},
    {"setNamespaceDeclPrefix",  (PyCFunction)elem_setNamespaceDeclPrefix,  METH_VARARGS, "Change namespace declaration prefix"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* PyTypeObject                                                        */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_ElementType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Element",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Element_new,
    .tp_methods   = LibXML_Element_methods,
    .tp_getset    = LibXML_Element_getset,
    .tp_doc       = "DOM Element node",
};

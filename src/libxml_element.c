#include "libxml_element.h"
#include "libxml_utils.h"
#include "libxml_tree.h"
#include "libxml_nodelist.h"
#include "libxml_attr.h"
#include "libxml_document.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers for qualified-name attribute lookup (prefix:localname)     */
/* ------------------------------------------------------------------ */

/* Resolve prefix to URI and look up attribute. Returns NULL if not found. */
static xmlAttrPtr
elem_ha_qname(xmlNodePtr node, const char *name)
{
    const char *colon = strchr(name, ':');
    if (!colon) {
        /* No prefix: match only no-namespace attributes (DOM Level 2 semantics) */
        return xmlHasNsProp(node, BAD_CAST name, NULL);
    }
    int plen = (int)(colon - name);
    char prefix[128];
    if (plen >= (int)sizeof(prefix) - 1) plen = (int)sizeof(prefix) - 2;
    memcpy(prefix, name, plen); prefix[plen] = '\0';
    const char *localname = colon + 1;
    xmlNsPtr ns = xmlSearchNs(node->doc, node, BAD_CAST prefix);
    const xmlChar *ns_uri = ns ? ns->href : NULL;
    return xmlHasNsProp(node, BAD_CAST localname, ns_uri);
}

static xmlChar *
elem_get_qname(xmlNodePtr node, const char *name)
{
    /* Handle xmlns: declarations: look in nsDef chain */
    if (strcmp(name, "xmlns") == 0) {
        xmlNsPtr ns = node->nsDef;
        while (ns) {
            if (!ns->prefix)  /* default namespace */
                return ns->href ? xmlStrdup(ns->href) : NULL;
            ns = ns->next;
        }
        return NULL;
    }
    if (strncmp(name, "xmlns:", 6) == 0 && name[6] != '\0') {
        const char *prefix = name + 6;
        xmlNsPtr ns = node->nsDef;
        while (ns) {
            if (ns->prefix && xmlStrEqual(ns->prefix, BAD_CAST prefix))
                return ns->href ? xmlStrdup(ns->href) : NULL;
            ns = ns->next;
        }
        return NULL;
    }

    const char *colon = strchr(name, ':');
    if (!colon) {
        /* No prefix: return value of no-namespace attribute */
        return xmlGetNsProp(node, BAD_CAST name, NULL);
    }
    int plen = (int)(colon - name);
    char prefix[128];
    if (plen >= (int)sizeof(prefix) - 1) plen = (int)sizeof(prefix) - 2;
    memcpy(prefix, name, plen); prefix[plen] = '\0';
    const char *localname = colon + 1;
    xmlNsPtr ns = xmlSearchNs(node->doc, node, BAD_CAST prefix);
    const xmlChar *ns_uri = ns ? ns->href : NULL;
    return xmlGetNsProp(node, BAD_CAST localname, ns_uri);
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

    PyObject *result = LibXML_Node_Wrap(node);
    if (result == NULL) xmlFreeNode(node);
    Py_DECREF(doc);
    return result;
}

/* ------------------------------------------------------------------ */
/* Helper: find namespace by URI on an element                        */
/* ------------------------------------------------------------------ */

static xmlNsPtr
find_or_create_ns(xmlNodePtr node, const char *ns_uri, const char *prefix)
{
    xmlNsPtr ns = xmlSearchNsByHref(node->doc, node, BAD_CAST ns_uri);
    if (ns) return ns;
    return xmlNewNs(node, BAD_CAST ns_uri, prefix ? BAD_CAST prefix : NULL);
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

static PyObject *
elem_setAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name, *value;
    if (!PyArg_ParseTuple(args, "ss", &name, &value)) return NULL;

    /* Intercept xmlns: and xmlns declarations to create real NS nodes */
    if (strcmp(name, "xmlns") == 0) {
        /* Default namespace declaration */
        xmlNsPtr existing = xmlSearchNs(self->node->doc, self->node, NULL);
        if (existing) {
            xmlFree((void *)existing->href);
            existing->href = value[0] ? xmlStrdup(BAD_CAST value) : NULL;
        } else {
            xmlNewNs(self->node, value[0] ? BAD_CAST value : NULL, NULL);
        }
        Py_RETURN_NONE;
    } else if (strncmp(name, "xmlns:", 6) == 0) {
        /* Prefixed namespace declaration */
        const char *prefix = name + 6;
        if (*prefix == '\0') { PyErr_SetString(PyExc_ValueError, "empty prefix"); return NULL; }
        xmlNsPtr existing = xmlSearchNs(self->node->doc, self->node, BAD_CAST prefix);
        if (existing && existing->prefix &&
            xmlStrEqual(existing->prefix, BAD_CAST prefix)) {
            xmlFree((void *)existing->href);
            existing->href = value[0] ? xmlStrdup(BAD_CAST value) : NULL;
        } else {
            xmlNewNs(self->node, BAD_CAST value, BAD_CAST prefix);
        }
        Py_RETURN_NONE;
    }

    /* For qualified names (prefix:local), resolve namespace and set NS prop */
    const char *colon = strchr(name, ':');
    if (colon) {
        int plen = (int)(colon - name);
        char prefix[128];
        if (plen >= (int)sizeof(prefix) - 1) plen = (int)sizeof(prefix) - 2;
        memcpy(prefix, name, plen); prefix[plen] = '\0';
        const char *localname = colon + 1;
        xmlNsPtr ns = xmlSearchNs(self->node->doc, self->node, BAD_CAST prefix);
        if (ns) {
            set_attr(self->node, ns, localname, value);
            Py_RETURN_NONE;
        }
        /* Unknown prefix: the name is kept as it is */
    }

    xmlAttrPtr attr = set_attr(self->node, NULL, name, value);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *
elem_setAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *qname, *value;
    if (!PyArg_ParseTuple(args, "zss", &ns_uri, &qname, &value)) return NULL;

    /* Treat NULL or empty ns_uri as no-namespace */
    if (!ns_uri || ns_uri[0] == '\0') {
        /* Use just the local part (after colon if any) */
        const char *colon = strchr(qname, ':');
        const char *localname = colon ? colon + 1 : qname;
        xmlAttrPtr attr = set_attr(self->node, NULL, localname, value);
        if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
        Py_RETURN_NONE;
    }

    /* xmlns namespace: create real namespace declaration instead of regular attr */
    if (strcmp(ns_uri, "http://www.w3.org/2000/xmlns/") == 0) {
        const char *colon = strchr(qname, ':');
        const char *prefix_for_decl = colon ? colon + 1 : NULL;
        if (!prefix_for_decl && strcmp(qname, "xmlns") == 0)
            prefix_for_decl = NULL; /* default namespace */
        /* Find existing nsDef with this prefix and update, or create new */
        xmlNsPtr ns = self->node->nsDef;
        while (ns) {
            int match = (prefix_for_decl == NULL && ns->prefix == NULL) ||
                        (prefix_for_decl && ns->prefix &&
                         xmlStrEqual(ns->prefix, BAD_CAST prefix_for_decl));
            if (match) {
                xmlFree((xmlChar *)ns->href);
                ns->href = xmlStrdup(BAD_CAST value);
                Py_RETURN_NONE;
            }
            ns = ns->next;
        }
        /* Create new namespace declaration */
        xmlNewNs(self->node, BAD_CAST value,
                 prefix_for_decl ? BAD_CAST prefix_for_decl : NULL);
        Py_RETURN_NONE;
    }

    const char *colon = strchr(qname, ':');
    xmlChar *prefix = NULL;
    const char *localname = colon ? colon + 1 : qname;
    if (colon) prefix = xmlStrndup(BAD_CAST qname, (int)(colon - qname));

    xmlNsPtr ns = find_or_create_ns(self->node, ns_uri, (const char *)prefix);
    xmlFree(prefix);

    xmlAttrPtr attr = set_attr(self->node, ns, localname, value);
    if (attr == NULL) { LibXML_raise_last_error(); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *
elem_getAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlChar *val = elem_get_qname(self->node, name);
    if (val == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)val);
    xmlFree(val);
    return ret;
}

static PyObject *
elem_getAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    xmlChar *val = xmlGetNsProp(self->node, BAD_CAST localname,
                                ns_uri ? BAD_CAST ns_uri : NULL);
    if (val == NULL) Py_RETURN_NONE;
    PyObject *ret = PyUnicode_FromString((const char *)val);
    xmlFree(val);
    return ret;
}

static PyObject *
elem_getAttributeNode(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlAttrPtr attr = elem_ha_qname(self->node, name);
    if (attr == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap((xmlNodePtr)attr);
}

static PyObject *
elem_getAttributeNodeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    xmlAttrPtr attr = xmlHasNsProp(self->node, BAD_CAST localname,
                                   ns_uri ? BAD_CAST ns_uri : NULL);
    if (attr == NULL) Py_RETURN_NONE;
    return LibXML_Node_Wrap((xmlNodePtr)attr);
}

static PyObject *
elem_removeAttribute(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlAttrPtr attr = elem_ha_qname(self->node, name);
    if (attr == NULL || attr->type != XML_ATTRIBUTE_NODE) Py_RETURN_FALSE;
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

    const char *colon = strchr(name, ':');
    if (colon) {
        /* Qualified name: match prefix:localname */
        int prefix_len = (int)(colon - name);
        const char *localname = colon + 1;
        xmlAttrPtr prop = self->node->properties;
        while (prop) {
            if (xmlStrEqual(prop->name, BAD_CAST localname) &&
                prop->ns && prop->ns->prefix &&
                xmlStrncmp(prop->ns->prefix, BAD_CAST name, prefix_len) == 0 &&
                prop->ns->prefix[prefix_len] == '\0')
                return PyBool_FromLong(1);
            prop = prop->next;
        }
        return PyBool_FromLong(0);
    } else {
        /* Unqualified name: match only no-namespace attributes */
        xmlAttrPtr prop = self->node->properties;
        while (prop) {
            if (xmlStrEqual(prop->name, BAD_CAST name) && prop->ns == NULL)
                return PyBool_FromLong(1);
            prop = prop->next;
        }
        return PyBool_FromLong(0);
    }
}

static PyObject *
elem_hasAttributeNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri = NULL, *localname;
    if (!PyArg_ParseTuple(args, "zs", &ns_uri, &localname)) return NULL;
    if (ns_uri && ns_uri[0] == '\0') ns_uri = NULL;
    return PyBool_FromLong(
        xmlHasNsProp(self->node, BAD_CAST localname,
                     ns_uri ? BAD_CAST ns_uri : NULL) != NULL);
}

static PyObject *
elem_setAttributeNode(LibXML_NodeObject *self, PyObject *args)
{
    LibXML_NodeObject *attr_wrap;
    if (!PyArg_ParseTuple(args, "O!", &LibXML_AttrType, &attr_wrap)) return NULL;

    xmlAttrPtr attr = (xmlAttrPtr)attr_wrap->node;
    /* If already attached to same element, return it unchanged */
    if (attr->parent == self->node) {
        Py_INCREF(attr_wrap);
        return (PyObject *)attr_wrap;
    }

    if (LibXML_insert_node((xmlNodePtr)attr, self->node, NULL) < 0) return NULL;

    Py_RETURN_NONE;
}

/* ------------------------------------------------------------------ */
/* Child/descendant element search                                     */
/* ------------------------------------------------------------------ */

static PyObject *
elem_getChildrenByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;

    PyObject *list = PyList_New(0);
    xmlNodePtr child = self->node->children;
    while (child) {
        if (child->type == XML_ELEMENT_NODE) {
            int match = (strcmp(name, "*") == 0) ||
                        (child->name && strcmp((const char *)child->name, name) == 0);
            if (match) {
                PyObject *w = LibXML_Node_Wrap(child);
                PyList_Append(list, w);
                Py_DECREF(w);
            }
        }
        child = child->next;
    }
    return list;
}

static PyObject *
elem_getChildrenByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "ss", &ns_uri, &localname)) return NULL;

    PyObject *list = PyList_New(0);
    xmlNodePtr child = self->node->children;
    while (child) {
        if (child->type == XML_ELEMENT_NODE) {
            int name_match = (strcmp(localname, "*") == 0) ||
                             (child->name && strcmp((const char *)child->name, localname) == 0);
            int ns_match = child->ns && child->ns->href &&
                           strcmp((const char *)child->ns->href, ns_uri) == 0;
            if (name_match && ns_match) {
                PyObject *w = LibXML_Node_Wrap(child);
                PyList_Append(list, w);
                Py_DECREF(w);
            }
        }
        child = child->next;
    }
    return list;
}

static PyObject *
elem_getChildrenByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    const char *localname;
    if (!PyArg_ParseTuple(args, "s", &localname)) return NULL;

    PyObject *list = PyList_New(0);
    xmlNodePtr child = self->node->children;
    while (child) {
        if (child->type == XML_ELEMENT_NODE) {
            int match = (strcmp(localname, "*") == 0) ||
                        (child->name && strcmp((const char *)child->name, localname) == 0);
            if (match) {
                PyObject *w = LibXML_Node_Wrap(child);
                PyList_Append(list, w);
                Py_DECREF(w);
            }
        }
        child = child->next;
    }
    return list;
}

static PyObject *
elem_getElementsByTagName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (!doc) { PyErr_SetString(LibXML_Error, "no document"); return NULL; }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) return PyErr_NoMemory();
    ctx->node = self->node;

    char xpath[512];
    if (strcmp(name, "*") == 0)
        snprintf(xpath, sizeof(xpath), "descendant::*");
    else
        snprintf(xpath, sizeof(xpath), "descendant::%s", name);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    xmlXPathFreeContext(ctx);
    if (!xobj) { LibXML_raise_last_error(); return NULL; }

    PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
elem_getElementsByTagNameNS(LibXML_NodeObject *self, PyObject *args)
{
    const char *ns_uri, *localname;
    if (!PyArg_ParseTuple(args, "ss", &ns_uri, &localname)) return NULL;

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (!doc) { PyErr_SetString(LibXML_Error, "no document"); return NULL; }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) return PyErr_NoMemory();
    ctx->node = self->node;

    xmlXPathRegisterNs(ctx, BAD_CAST "ns_", BAD_CAST ns_uri);

    char xpath[512];
    if (strcmp(localname, "*") == 0)
        snprintf(xpath, sizeof(xpath), "descendant::ns_:*");
    else
        snprintf(xpath, sizeof(xpath), "descendant::ns_:%s", localname);

    xmlXPathObjectPtr xobj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    xmlXPathFreeContext(ctx);
    if (!xobj) { LibXML_raise_last_error(); return NULL; }

    PyObject *result = LibXML_NodeList_FromNodeSet(xobj->nodesetval);
    xmlXPathFreeObject(xobj);
    return result;
}

static PyObject *
elem_getElementsByLocalName(LibXML_NodeObject *self, PyObject *args)
{
    /* Same as getElementsByTagName but ignoring namespace */
    return elem_getElementsByTagName(self, args);
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

    xmlDocPtr doc = LibXML_DOC_FROM_NODE(self);
    if (!doc) { PyErr_SetString(LibXML_Error, "no document"); return NULL; }

    xmlNodePtr frag = xmlStringGetNodeList(doc, BAD_CAST chunk);
    if (!frag) { LibXML_raise_last_error(); return NULL; }

    xmlNodePtr cur = frag;
    while (cur) {
        xmlNodePtr next = cur->next;
        xmlAddChild(self->node, cur);
        cur = next;
    }
    Py_RETURN_NONE;
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

    if (!ns_uri || ns_uri[0] == '\0') {
        /* Empty/NULL URI: remove namespace from element */
        xmlSetNs(self->node, NULL);
        Py_RETURN_TRUE;
    }

    xmlNsPtr ns;
    if (activate) {
        ns = find_or_create_ns(self->node, ns_uri, prefix);
        xmlSetNs(self->node, ns);
    } else {
        /* Just declare the namespace without activating */
        xmlNewNs(self->node, BAD_CAST ns_uri, prefix ? BAD_CAST prefix : NULL);
    }
    Py_RETURN_TRUE;
}

/* ------------------------------------------------------------------ */
/* setNamespaceDeclURI / setNamespaceDeclPrefix                       */
/* ------------------------------------------------------------------ */

static PyObject *
elem_setNamespaceDeclURI(LibXML_NodeObject *self, PyObject *args)
{
    const char *prefix;
    PyObject *uri_obj;
    if (!PyArg_ParseTuple(args, "zO", &prefix, &uri_obj)) return NULL;

    /* Find the ns decl on this element with matching prefix */
    xmlNsPtr ns = self->node->nsDef;
    while (ns) {
        int match = (prefix == NULL && ns->prefix == NULL) ||
                    (prefix && ns->prefix &&
                     xmlStrEqual(ns->prefix, BAD_CAST prefix));
        if (match) {
            xmlFree((void *)ns->href);
            if (uri_obj == Py_None || uri_obj == NULL) {
                ns->href = NULL;
                /* Remove the declaration entirely */
                /* Unlink from nsDef list */
                if (self->node->nsDef == ns) {
                    self->node->nsDef = ns->next;
                } else {
                    xmlNsPtr prev = self->node->nsDef;
                    while (prev && prev->next != ns) prev = prev->next;
                    if (prev) prev->next = ns->next;
                }
                ns->next = NULL;
                LibXML_forget_ns(self->node, ns);
                xmlFreeNs(ns);
            } else {
                const char *uri = PyUnicode_AsUTF8(uri_obj);
                if (!uri) return NULL;
                ns->href = xmlStrdup(BAD_CAST uri);
            }
            Py_RETURN_TRUE;
        }
        ns = ns->next;
    }
    Py_RETURN_FALSE;
}

static PyObject *
elem_setNamespaceDeclPrefix(LibXML_NodeObject *self, PyObject *args)
{
    const char *old_prefix;
    PyObject *new_prefix_obj;
    if (!PyArg_ParseTuple(args, "zO", &old_prefix, &new_prefix_obj)) return NULL;

    const char *new_prefix = NULL;
    if (new_prefix_obj != Py_None) {
        new_prefix = PyUnicode_AsUTF8(new_prefix_obj);
        if (!new_prefix) return NULL;
        if (new_prefix[0] == '\0') new_prefix = NULL;
    }

    /* Check new prefix not already in use */
    if (new_prefix) {
        xmlNsPtr existing = xmlSearchNs(self->node->doc, self->node,
                                        BAD_CAST new_prefix);
        if (existing) {
            PyErr_Format(LibXML_Error, "prefix '%s' is already in use", new_prefix);
            return NULL;
        }
    } else {
        /* Trying to set default ns: check if default ns already exists */
        xmlNsPtr existing = xmlSearchNs(self->node->doc, self->node, NULL);
        if (existing) {
            PyErr_SetString(LibXML_Error, "default namespace prefix already in use");
            return NULL;
        }
    }

    /* Find the ns decl with old_prefix */
    xmlNsPtr ns = self->node->nsDef;
    while (ns) {
        int match = (old_prefix == NULL && ns->prefix == NULL) ||
                    (old_prefix && ns->prefix &&
                     xmlStrEqual(ns->prefix, BAD_CAST old_prefix));
        if (match) {
            xmlFree((void *)ns->prefix);
            ns->prefix = new_prefix ? xmlStrdup(BAD_CAST new_prefix) : NULL;
            /* Update ns pointer on element if it used this ns */
            if (self->node->ns == ns) {
                /* element's own ns prefix changed */
            }
            Py_RETURN_TRUE;
        }
        ns = ns->next;
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
elem_setNodeName(LibXML_NodeObject *self, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    xmlNodeSetName(self->node, BAD_CAST name);
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
    {"setAttributeNode",        (PyCFunction)elem_setAttributeNode,        METH_VARARGS, "Set attribute from Attr node"},
    {"setAttributeNodeNS",      (PyCFunction)elem_setAttributeNodeNS,      METH_VARARGS, "Set namespaced attribute from Attr node"},
    {"removeAttributeNode",     (PyCFunction)elem_removeAttributeNode,     METH_VARARGS, "Remove Attr node"},
    {"setNodeName",             (PyCFunction)elem_setNodeName,             METH_VARARGS, "Rename element"},
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

#include <stdlib.h>
#include "libxml_tree.h"
#include "libxml_utils.h"

static int
hierarchy_error(const char *message)
{
    PyErr_Format(LibXML_Error, "HIERARCHY_REQUEST_ERR: %s", message);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Walking a tree                                                      */
/* ------------------------------------------------------------------ */

void
LibXML_walk_tree(xmlNodePtr top, LibXML_visit_fn visit, void *arg)
{
    xmlNodePtr cur = top;

    for (;;) {
        visit(cur, arg);

        if (cur->type == XML_ELEMENT_NODE) {
            for (xmlAttrPtr attr = cur->properties; attr; attr = attr->next) {
                visit((xmlNodePtr)attr, arg);
                for (xmlNodePtr c = attr->children; c; c = c->next)
                    visit(c, arg);
            }
        }

        /* The children of an entity reference belong to the entity. */
        if (cur->children && cur->type != XML_ENTITY_REF_NODE
            && cur->type != XML_ENTITY_DECL) {
            cur = cur->children;
            continue;
        }

        while (cur != top && cur->next == NULL)
            cur = cur->parent;
        if (cur == top)
            return;
        cur = cur->next;
    }
}

xmlNodePtr
LibXML_tree_top(xmlNodePtr node)
{
    /* An external subset has no parent but belongs to its document. */
    if (node->type == XML_DTD_NODE && node->doc
        && node->doc->extSubset == (xmlDtdPtr)node)
        return (xmlNodePtr)node->doc;
    while (node->parent)
        node = node->parent;
    return node;
}

static void
find_wrapper(xmlNodePtr node, void *arg)
{
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    if (priv && priv->wrapper)
        *(int *)arg = 1;
}

static void
clear_private_node(xmlNodePtr node, void *arg)
{
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;
    (void)arg;
    if (priv == NULL)
        return;
    node->_private = NULL;
    Py_XDECREF(priv->cargo);
    free(priv);
}

void
LibXML_clear_private(xmlNodePtr top)
{
    LibXML_walk_tree(top, clear_private_node, NULL);
}

void
LibXML_free_tree(xmlNodePtr top)
{
    LibXML_clear_private(top);

    if (top->type == XML_DTD_NODE && top->doc && top->doc->_private) {
        LibXML_NodePrivate *priv = (LibXML_NodePrivate *)top->doc->_private;
        /* A wrapper can come and go again through an entity reference. */
        for (xmlNodePtr d = priv->old_dtds; d; d = d->next) {
            if (d == top)
                return;
        }
        top->next = priv->old_dtds;
        priv->old_dtds = top;
        return;
    }
    xmlFreeNode(top);
}

void
LibXML_free_old_dtds(xmlNodePtr *list)
{
    xmlNodePtr dtd = *list;
    *list = NULL;
    while (dtd) {
        xmlNodePtr next = dtd->next;
        dtd->next = NULL;
        xmlFreeDtd((xmlDtdPtr)dtd);
        dtd = next;
    }
}

/* ------------------------------------------------------------------ */
/* Keeping namespaces valid                                            */
/* ------------------------------------------------------------------ */

/*
 * node->ns may point to a declaration on an ancestor. After a node moves, the
 * ancestor can be freed, or the prefix can be undeclared in the new place, so
 * each moved subtree must be reconciled. This follows domReconcileNs from Perl,
 * except that declarations are not duplicated on an element and that a detached
 * attribute keeps its namespace in the document.
 */

xmlNsPtr
LibXML_doc_ns(xmlDocPtr doc, const xmlChar *href, const xmlChar *prefix)
{
    /* The head of oldNs is reserved for the xml namespace. */
    xmlSearchNs(doc, (xmlNodePtr)doc, BAD_CAST "xml");
    if (doc->oldNs == NULL)
        return NULL;

    xmlNsPtr ns = doc->oldNs;
    for (;;) {
        if (xmlStrEqual(ns->href, href) && xmlStrEqual(ns->prefix, prefix))
            return ns;
        if (ns->next == NULL)
            break;
        ns = ns->next;
    }

    ns->next = xmlNewNs(NULL, href, prefix);
    return ns->next;
}

/* Find or add a declaration on element for the namespace of ns. */
static xmlNsPtr
declare_ns(xmlNodePtr element, xmlNsPtr ns)
{
    int conflict = 0;

    for (xmlNsPtr d = element->nsDef; d; d = d->next) {
        if (d == ns)
            return ns;
        if (xmlStrEqual(d->prefix, ns->prefix)) {
            if (xmlStrEqual(d->href, ns->href))
                return d;
            conflict = 1;
        }
    }

    if (!conflict) {
        xmlNsPtr copy = xmlNewNs(NULL, ns->href, ns->prefix);
        if (copy != NULL) {
            copy->next = element->nsDef;
            element->nsDef = copy;
        }
        return copy;
    }

    /* The prefix means something else here, so another one is needed. */
    for (int i = 1; i < 1000; i++) {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "%.50s%d",
                 ns->prefix ? (const char *)ns->prefix : "ns", i);
        if (xmlSearchNs(element->doc, element, BAD_CAST prefix) == NULL)
            return xmlNewNs(element, ns->href, BAD_CAST prefix);
    }
    return NULL;
}

/* Take ns out of the declarations of element. Returns 1 if it was there. */
static int
remove_ns_decl(xmlNodePtr element, xmlNsPtr ns)
{
    for (xmlNsPtr *link = &element->nsDef; *link; link = &(*link)->next) {
        if (*link == ns) {
            *link = ns->next;
            ns->next = NULL;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    xmlNodePtr top;
    xmlNsPtr unused;    /* declarations to free after the walk */
} Reconcile;

/* Drop a redundant declaration. Freed after the walk, because nodes further down
 * may still point to it. */
static void
drop_ns_decl(xmlNodePtr element, xmlNsPtr ns, Reconcile *rec)
{
    if (remove_ns_decl(element, ns)) {
        ns->next = rec->unused;
        rec->unused = ns;
    }
}

static void
reconcile_element(xmlNodePtr element, Reconcile *rec)
{
    xmlNsPtr ns = element->ns;
    if (ns == NULL)
        return;

    xmlNsPtr inherited = element->parent
        ? xmlSearchNs(element->doc, element->parent, ns->prefix) : NULL;

    if (inherited != NULL && xmlStrEqual(inherited->href, ns->href)) {
        drop_ns_decl(element, ns, rec);
        element->ns = inherited;
    } else {
        element->ns = declare_ns(element, ns);
    }
}

static void
reconcile_attr(xmlAttrPtr attr, Reconcile *rec)
{
    xmlNsPtr ns = attr->ns;
    xmlNodePtr element = attr->parent;

    if (ns == NULL)
        return;

    if (element == NULL) {
        attr->ns = LibXML_doc_ns(attr->doc, ns->href, ns->prefix);
        return;
    }

    xmlNsPtr visible = xmlSearchNs(attr->doc, element, ns->prefix);
    if (visible == NULL || !xmlStrEqual(visible->href, ns->href)) {
        attr->ns = declare_ns(element, ns);
        return;
    }

    /* A declaration can only go if the walk covers all the nodes that use it. */
    xmlNsPtr inherited = element->parent
        ? xmlSearchNs(attr->doc, element->parent, ns->prefix) : NULL;
    if (rec->top != (xmlNodePtr)attr && inherited != NULL
        && xmlStrEqual(inherited->href, ns->href)) {
        drop_ns_decl(element, visible, rec);
        attr->ns = inherited;
    } else {
        attr->ns = visible;
    }
}

static void
reconcile_node(xmlNodePtr node, void *arg)
{
    if (node->type == XML_ELEMENT_NODE)
        reconcile_element(node, (Reconcile *)arg);
    else if (node->type == XML_ATTRIBUTE_NODE)
        reconcile_attr((xmlAttrPtr)node, (Reconcile *)arg);
}

static void
reconcile_ns(xmlNodePtr node)
{
    Reconcile rec = { node, NULL };
    LibXML_walk_tree(node, reconcile_node, &rec);
    if (rec.unused != NULL)
        xmlFreeNsList(rec.unused);
}

static void
forget_ns_node(xmlNodePtr node, void *arg)
{
    /* Both node types have ns at the same offset. */
    if ((node->type == XML_ELEMENT_NODE || node->type == XML_ATTRIBUTE_NODE)
        && node->ns == (xmlNsPtr)arg)
        node->ns = NULL;
}

void
LibXML_forget_ns(xmlNodePtr top, xmlNsPtr ns)
{
    LibXML_walk_tree(top, forget_ns_node, ns);
}

/* ------------------------------------------------------------------ */
/* Keeping wrappers consistent                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    xmlNodePtr top;
    PyObject  *root;     /* wrapper of top, created on first use */
    PyObject  *doc_obj;  /* wrapper of the document, created on first use */
} FixOwner;

static void
fix_owner_node(xmlNodePtr node, void *arg)
{
    FixOwner *fix = (FixOwner *)arg;
    LibXML_NodePrivate *priv = (LibXML_NodePrivate *)node->_private;

    if (priv == NULL || priv->wrapper == NULL)
        return;

    LibXML_NodeObject *wrapper = (LibXML_NodeObject *)priv->wrapper;

    if (fix->doc_obj == NULL)
        fix->doc_obj = LibXML_Node_Wrap((xmlNodePtr)node->doc);
    if (fix->root == NULL)
        fix->root = LibXML_Node_Wrap(fix->top);

    PyObject *old_doc   = wrapper->doc_obj;
    PyObject *old_owner = wrapper->owner;

    Py_XINCREF(fix->doc_obj);
    wrapper->doc_obj = fix->doc_obj;

    if (node == fix->top) {
        wrapper->owner = NULL;
    } else {
        Py_XINCREF(fix->root);
        wrapper->owner = fix->root;
    }

    /* May free the tree the node came from, which no longer contains it. */
    Py_XDECREF(old_owner);
    Py_XDECREF(old_doc);
}

void
LibXML_fix_owner(xmlNodePtr node)
{
    FixOwner fix = { LibXML_tree_top(node), NULL, NULL };
    LibXML_walk_tree(node, fix_owner_node, &fix);
    Py_XDECREF(fix.root);
    Py_XDECREF(fix.doc_obj);
}

/* The content of an entity is shared by all its references, and a reference
 * placed inside it would make the entity contain itself. */
static int
inside_entity(xmlNodePtr node)
{
    for (; node; node = node->parent) {
        if (node->type == XML_ENTITY_DECL)
            return 1;
    }
    return 0;
}

int
LibXML_check_detach(xmlNodePtr node)
{
    if (inside_entity(node))
        return hierarchy_error("the content of an entity cannot be changed");

    switch (node->type) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        return hierarchy_error("a document cannot be detached");
    case XML_ELEMENT_DECL:
    case XML_ATTRIBUTE_DECL:
    case XML_ENTITY_DECL:
    case XML_NOTATION_NODE:
        /* The hash tables of the DTD keep pointing to the declaration. */
        return hierarchy_error("a declaration cannot leave its DTD");
    default:
        return 0;
    }
}

int
LibXML_check_children(xmlNodePtr node)
{
    switch (node->type) {
    case XML_ELEMENT_NODE:
    case XML_ATTRIBUTE_NODE:
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
    case XML_DOCUMENT_FRAG_NODE:
        return 0;
    default:
        /* Those of an entity reference or a DTD belong to the DTD. */
        return hierarchy_error("the children of this node cannot be changed");
    }
}

void
LibXML_detach_node(xmlNodePtr node)
{
    xmlUnlinkNode(node);
    reconcile_ns(node);
    LibXML_fix_owner(node);
}

void
LibXML_release_node(xmlNodePtr node)
{
    int has_wrapper = 0;

    xmlUnlinkNode(node);
    LibXML_walk_tree(node, find_wrapper, &has_wrapper);

    if (has_wrapper) {
        /* Give the subtree a root wrapper, held by the wrappers inside it. */
        PyObject *root = LibXML_Node_Wrap(node);
        reconcile_ns(node);
        LibXML_fix_owner(node);
        Py_XDECREF(root);
    } else {
        LibXML_free_tree(node);
    }
}

/* ------------------------------------------------------------------ */
/* Setting text                                                        */
/* ------------------------------------------------------------------ */

/* libxml2 is about to free the children of attr. It needs their value to update
 * the ID table, so a child that a wrapper refers to is swapped for a copy. */
static void
protect_attr_children(xmlAttrPtr attr)
{
    xmlNodePtr child = attr->children;

    while (child) {
        xmlNodePtr next = child->next;
        int has_wrapper = 0;

        LibXML_walk_tree(child, find_wrapper, &has_wrapper);
        if (has_wrapper) {
            xmlNodePtr copy = xmlDocCopyNode(child, child->doc, 1);
            if (copy != NULL && xmlReplaceNode(child, copy) != NULL) {
                reconcile_ns(child);
                LibXML_fix_owner(child);
            }
        } else {
            LibXML_clear_private(child);
        }
        child = next;
    }
}

void
LibXML_set_text(xmlNodePtr node, const char *text)
{
    switch (node->type) {
    case XML_ATTRIBUTE_NODE:
        if (node->parent != NULL) {
            xmlAttrPtr attr = (xmlAttrPtr)node;
            protect_attr_children(attr);
            xmlSetNsProp(node->parent, attr->ns, attr->name, BAD_CAST text);
            return;
        }
        break;
    case XML_ELEMENT_NODE:
    case XML_DOCUMENT_FRAG_NODE:
        break;
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
        xmlNodeSetContent(node, BAD_CAST text);
        return;
    default:
        /* No value in the DOM. The content of an entity belongs to the DTD. */
        return;
    }

    xmlNodePtr child = node->children;
    while (child) {
        xmlNodePtr next = child->next;
        LibXML_release_node(child);
        child = next;
    }

    /* xmlNodeSetContent parses entities in these node types. */
    xmlChar *escaped = xmlEncodeSpecialChars(node->doc, BAD_CAST text);
    if (escaped == NULL)
        return;
    xmlNodeSetContent(node, escaped);
    xmlFree(escaped);
}

/* ------------------------------------------------------------------ */
/* Moving nodes                                                        */
/* ------------------------------------------------------------------ */

/* Check that node can go under parent. */
static int
check_insert(xmlNodePtr node, xmlNodePtr parent, xmlNodePtr before, int new_root)
{
    switch (node->type) {
    case XML_ELEMENT_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
    case XML_ENTITY_REF_NODE:
        break;
    default:
        return hierarchy_error("this node type cannot be inserted");
    }

    switch (parent->type) {
    case XML_ELEMENT_NODE:
    case XML_DOCUMENT_FRAG_NODE:
        break;
    case XML_ATTRIBUTE_NODE:
        if (node->type != XML_TEXT_NODE && node->type != XML_ENTITY_REF_NODE)
            return hierarchy_error("an attribute can only hold text");
        break;
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE
            || node->type == XML_ENTITY_REF_NODE)
            return hierarchy_error("a document cannot hold text");
        if (node->type == XML_ELEMENT_NODE && !new_root) {
            xmlNodePtr root = xmlDocGetRootElement((xmlDocPtr)parent);
            if (root != NULL && root != node)
                return hierarchy_error("a document has a single root element");
        }
        break;
    default:
        return hierarchy_error("this node type cannot have children");
    }

    for (xmlNodePtr p = parent; p; p = p->parent) {
        if (p == node)
            return hierarchy_error("a node cannot be inserted into itself");
        if (p->type == XML_ENTITY_DECL)
            return hierarchy_error("the content of an entity cannot be changed");
    }

    if (inside_entity(node))
        return hierarchy_error("the content of an entity cannot be changed");

    if (before != NULL
        && (before->parent != parent || before->type == XML_ATTRIBUTE_NODE))
        return hierarchy_error("the reference node is not a child of this node");

    return 0;
}

/* Link node under parent. libxml2's functions merge adjacent text nodes,
 * which frees one of them. */
static void
link_node(xmlNodePtr node, xmlNodePtr parent, xmlNodePtr before)
{
    node->parent = parent;
    node->next = before;

    if (before != NULL) {
        node->prev = before->prev;
        if (before->prev)
            before->prev->next = node;
        else
            parent->children = node;
        before->prev = node;
    } else {
        node->prev = parent->last;
        if (parent->last)
            parent->last->next = node;
        else
            parent->children = node;
        parent->last = node;
    }
}

/* Move an unlinked node to another document, and restore owners on failure. */
static int
adopt_unlinked(xmlNodePtr node, xmlDocPtr doc, xmlNodePtr parent)
{
    if (node->doc == doc)
        return 0;

    /* A document cannot be used to look up namespaces. */
    if (parent != NULL && parent->type != XML_ELEMENT_NODE)
        parent = NULL;

    if (xmlDOMWrapAdoptNode(NULL, node->doc, node, doc, parent, 0) != 0) {
        LibXML_fix_owner(node);
        PyErr_SetString(LibXML_Error, "cannot move this node to another document");
        return -1;
    }
    return 0;
}

static int
move_node(xmlNodePtr node, xmlNodePtr parent, xmlNodePtr before)
{
    if (before == node)
        return 0;

    xmlNodePtr old_top = LibXML_tree_top(node);

    xmlUnlinkNode(node);
    if (adopt_unlinked(node, parent->doc, parent) < 0)
        return -1;
    link_node(node, parent, before);
    reconcile_ns(node);

    /* Inside one tree no owner changes, and the walk would be wasted. */
    if (old_top == node || old_top != LibXML_tree_top(node))
        LibXML_fix_owner(node);
    return 0;
}

xmlAttrPtr
LibXML_find_same_attr(xmlNodePtr element, xmlAttrPtr attr)
{
    const xmlChar *href = attr->ns ? attr->ns->href : NULL;

    for (xmlAttrPtr a = element->properties; a; a = a->next) {
        if (a != attr && xmlStrEqual(a->name, attr->name)
            && xmlStrEqual(a->ns ? a->ns->href : NULL, href))
            return a;
    }
    return NULL;
}

static int
attach_attr(xmlAttrPtr attr, xmlNodePtr element)
{
    if (element->type != XML_ELEMENT_NODE)
        return hierarchy_error("only an element can hold an attribute");
    if (inside_entity(element) || inside_entity((xmlNodePtr)attr))
        return hierarchy_error("the content of an entity cannot be changed");

    if (attr->parent == element)
        return 0;

    xmlUnlinkNode((xmlNodePtr)attr);
    if (adopt_unlinked((xmlNodePtr)attr, element->doc, element) < 0)
        return -1;

    xmlAttrPtr same = LibXML_find_same_attr(element, attr);
    if (same != NULL)
        LibXML_release_node((xmlNodePtr)same);

    if (xmlAddChild(element, (xmlNodePtr)attr) == NULL) {
        LibXML_fix_owner((xmlNodePtr)attr);
        PyErr_SetString(LibXML_Error, "cannot attach attribute");
        return -1;
    }
    reconcile_ns((xmlNodePtr)attr);
    LibXML_fix_owner((xmlNodePtr)attr);
    return 0;
}

static int
insert_fragment(xmlNodePtr fragment, xmlNodePtr parent, xmlNodePtr before)
{
    if (fragment == parent)
        return hierarchy_error("a node cannot be inserted into itself");

    /* Refuse before anything moves. */
    for (xmlNodePtr c = fragment->children; c; c = c->next) {
        if (check_insert(c, parent, before, 0) < 0)
            return -1;
    }

    xmlNodePtr child = fragment->children;
    while (child) {
        xmlNodePtr next = child->next;
        if (move_node(child, parent, before) < 0)
            return -1;
        child = next;
    }
    return 0;
}

int
LibXML_insert_node(xmlNodePtr node, xmlNodePtr parent, xmlNodePtr before)
{
    if (node->type == XML_ATTRIBUTE_NODE)
        return attach_attr((xmlAttrPtr)node, parent);
    if (node->type == XML_DOCUMENT_FRAG_NODE)
        return insert_fragment(node, parent, before);
    if (check_insert(node, parent, before, 0) < 0)
        return -1;
    return move_node(node, parent, before);
}

static int
replace_attr(xmlAttrPtr old_attr, xmlAttrPtr new_attr)
{
    xmlNodePtr element = old_attr->parent;

    if (new_attr == old_attr)
        return 0;

    xmlUnlinkNode((xmlNodePtr)new_attr);
    if (adopt_unlinked((xmlNodePtr)new_attr, element->doc, element) < 0)
        return -1;

    if (xmlReplaceNode((xmlNodePtr)old_attr, (xmlNodePtr)new_attr) == NULL) {
        LibXML_fix_owner((xmlNodePtr)new_attr);
        PyErr_SetString(LibXML_Error, "cannot replace attribute");
        return -1;
    }
    reconcile_ns((xmlNodePtr)new_attr);
    reconcile_ns((xmlNodePtr)old_attr);
    LibXML_fix_owner((xmlNodePtr)new_attr);
    LibXML_fix_owner((xmlNodePtr)old_attr);
    return 0;
}

/* A DTD belongs to its document through doc->intSubset and hash tables shared
 * with nothing else, so the new one is a copy and new_dtd stays where it is. */
static int
replace_dtd(xmlDtdPtr old_dtd, xmlDtdPtr new_dtd)
{
    xmlDocPtr doc = old_dtd->doc;

    if (doc->intSubset != old_dtd)
        return hierarchy_error("only an internal subset can be replaced");

    xmlDtdPtr copy = xmlCopyDtd(new_dtd);
    if (copy == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    xmlSetTreeDoc((xmlNodePtr)copy, doc);

    if (xmlReplaceNode((xmlNodePtr)old_dtd, (xmlNodePtr)copy) == NULL) {
        xmlFreeDtd(copy);
        PyErr_SetString(LibXML_Error, "cannot replace DTD");
        return -1;
    }
    doc->intSubset = copy;
    LibXML_fix_owner((xmlNodePtr)old_dtd);
    return 0;
}

int
LibXML_replace_node(xmlNodePtr old_node, xmlNodePtr new_node)
{
    xmlNodePtr parent = old_node->parent;

    if (LibXML_check_detach(old_node) < 0)
        return -1;
    if (parent == NULL)
        return hierarchy_error("the node to replace has no parent");
    if (old_node == new_node)
        return 0;

    if (old_node->type == XML_DTD_NODE) {
        if (new_node->type != XML_DTD_NODE)
            return hierarchy_error("a DTD can only be replaced by a DTD");
        return replace_dtd((xmlDtdPtr)old_node, (xmlDtdPtr)new_node);
    }

    if (old_node->type == XML_ATTRIBUTE_NODE) {
        if (new_node->type != XML_ATTRIBUTE_NODE)
            return hierarchy_error("an attribute can only be replaced by an attribute");
        return replace_attr((xmlAttrPtr)old_node, (xmlAttrPtr)new_node);
    }
    if (new_node->type == XML_ATTRIBUTE_NODE)
        return hierarchy_error("this node cannot be replaced by an attribute");

    if (new_node->type == XML_DOCUMENT_FRAG_NODE) {
        if (insert_fragment(new_node, parent, old_node) < 0)
            return -1;
    } else {
        int new_root = (old_node->type == XML_ELEMENT_NODE);
        if (check_insert(new_node, parent, old_node, new_root) < 0)
            return -1;
        if (move_node(new_node, parent, old_node) < 0)
            return -1;
    }

    LibXML_detach_node(old_node);
    return 0;
}

int
LibXML_set_root(xmlDocPtr doc, xmlNodePtr node)
{
    xmlNodePtr parent = (xmlNodePtr)doc;
    xmlNodePtr old_root = xmlDocGetRootElement(doc);

    if (node == old_root)
        return 0;
    if (node->type != XML_ELEMENT_NODE)
        return hierarchy_error("the root of a document must be an element");
    if (check_insert(node, parent, old_root, 1) < 0)
        return -1;
    if (move_node(node, parent, old_root) < 0)
        return -1;
    if (old_root != NULL)
        LibXML_release_node(old_root);
    return 0;
}

int
LibXML_adopt_node(xmlNodePtr node, xmlDocPtr doc)
{
    switch (node->type) {
    case XML_ELEMENT_NODE:
    case XML_ATTRIBUTE_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
    case XML_ENTITY_REF_NODE:
    case XML_DOCUMENT_FRAG_NODE:
        break;
    default:
        return hierarchy_error("this node type cannot be adopted");
    }
    if (LibXML_check_detach(node) < 0)
        return -1;

    xmlUnlinkNode(node);

    if (node->type == XML_DOCUMENT_FRAG_NODE) {
        /* Children are adopted one by one, and adoption wants them unlinked. */
        xmlNodePtr child = node->children;
        node->children = node->last = NULL;
        while (child) {
            xmlNodePtr next = child->next;
            child->parent = child->next = child->prev = NULL;
            int rc = adopt_unlinked(child, doc, NULL);
            link_node(child, node, NULL);
            if (rc < 0) {
                /* Keep the rest in the fragment, in its old document. */
                while (next) {
                    xmlNodePtr rest = next;
                    next = next->next;
                    rest->parent = rest->next = rest->prev = NULL;
                    link_node(rest, node, NULL);
                }
                LibXML_fix_owner(node);
                return -1;
            }
            child = next;
        }
        node->doc = doc;
    } else if (adopt_unlinked(node, doc, NULL) < 0) {
        return -1;
    }

    reconcile_ns(node);
    LibXML_fix_owner(node);
    return 0;
}

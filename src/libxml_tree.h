#ifndef LIBXML_TREE_H
#define LIBXML_TREE_H

#include "libxml_module.h"

/*
 * Tree ownership.
 *
 * Every wrapped node has a document (node->doc is never NULL): nodes created
 * without one get a private hidden document. Each tree is either a document or
 * a detached subtree, and the wrapper at the top of it is the tree's root:
 *
 *   wrapper->doc_obj  strong ref to the Document wrapper; NULL for a Document
 *   wrapper->owner    strong ref to the root wrapper; NULL for a root
 *
 * Every other wrapper of the tree keeps the root alive through `owner`, so no
 * node is freed while a wrapper can reach it. The root frees the tree when it
 * is deallocated.
 *
 * Any code that moves a node out of a tree or between trees must restore this
 * with LibXML_fix_owner, and any code that would free nodes must go through
 * LibXML_release_node instead.
 */

typedef void (*LibXML_visit_fn)(xmlNodePtr node, void *arg);

/* Visit node and its descendants, attributes and attribute children. */
void LibXML_walk_tree(xmlNodePtr top, LibXML_visit_fn visit, void *arg);

/* The node at the top of the tree holding node. */
xmlNodePtr LibXML_tree_top(xmlNodePtr node);

/* Free the private struct, and the cargo, of each node of a dead tree. */
void LibXML_clear_private(xmlNodePtr top);

/* Update the wrappers of the subtree at node after it moved. */
void LibXML_fix_owner(xmlNodePtr node);

/* Remove node from its tree. Freed unless a wrapper still refers to it. */
void LibXML_release_node(xmlNodePtr node);

/* Set the content of a node to raw text: `&` and `<` have no special meaning. */
void LibXML_set_text(xmlNodePtr node, const char *text);

/* Insert node under parent, before `before`, or last if `before` is NULL.
 * Fragments are inlined and attributes attached. Returns 0, or -1 with a
 * Python exception set. */
int LibXML_insert_node(xmlNodePtr node, xmlNodePtr parent, xmlNodePtr before);

/* Put new_node in the place of old_node, which is detached but stays alive. */
int LibXML_replace_node(xmlNodePtr old_node, xmlNodePtr new_node);

/* Make node the root element of doc. The previous root is released. */
int LibXML_set_root(xmlDocPtr doc, xmlNodePtr node);

/* Detach node and make it belong to doc, without a parent. */
int LibXML_adopt_node(xmlNodePtr node, xmlDocPtr doc);

/* Detach node, which stays alive as the root of its own tree. */
void LibXML_detach_node(xmlNodePtr node);

/* Check that node can leave its tree. Returns 0, or -1 with an exception set. */
int LibXML_check_detach(xmlNodePtr node);

/* Check that node holds a list of children that can be changed. */
int LibXML_check_children(xmlNodePtr node);

/* A namespace owned by doc, for attributes without an element to declare it. */
xmlNsPtr LibXML_doc_ns(xmlDocPtr doc, const xmlChar *href, const xmlChar *prefix);

/* Clear every pointer to ns in the subtree, before ns is freed. */
void LibXML_forget_ns(xmlNodePtr top, xmlNsPtr ns);

#endif /* LIBXML_TREE_H */

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

/* Cargo is reached from the wrapper at the root of a tree, for the garbage
 * collector: a wrapper alone cannot tell if its node is still in use. */
extern long LibXML_cargo_count;
int LibXML_traverse_cargo(xmlNodePtr top, visitproc visit, void *arg);
void LibXML_clear_cargo(xmlNodePtr top);

/* Free a tree nothing refers to any more. A DTD waits for its document: entity
 * references keep pointing to its declarations. */
void LibXML_free_tree(xmlNodePtr top);

/* A callback can change the tree while libxml2 walks a node set. So while an
 * evaluation runs, trees are not freed, and the last one to end frees them. */
void LibXML_eval_begin(void);
void LibXML_eval_end(void);
int  LibXML_eval_running(void);

/* Free the DTDs that have left doc. */
void LibXML_free_old_dtds(xmlNodePtr *list);

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

/* The internal subset and the external subset of a document. A DTD from another
 * document moves by a copy: its wrapper takes the copy, and the original stays
 * with its document, as entity references point into it. */
int LibXML_set_int_subset(xmlDocPtr doc, xmlNodePtr dtd, xmlNodePtr before);
int LibXML_set_ext_subset(xmlDocPtr doc, xmlNodePtr dtd);

/* Before XInclude processing, which frees xi:include elements and what they hold:
 * those that wrappers point into leave the tree, and a copy takes their place.
 * Returns 0, or -1 with an exception. */
int LibXML_xinclude_prepare(xmlDocPtr doc);

/* After it: the end markers XInclude adds share the namespace of the start ones,
 * which may be declared on the start marker. They hold nothing, so they lose it. */
void LibXML_xinclude_finish(xmlDocPtr doc);

/* Make node the root element of doc. The previous root is released. */
int LibXML_set_root(xmlDocPtr doc, xmlNodePtr node);

/* Detach node and make it belong to doc, without a parent. */
int LibXML_adopt_node(xmlNodePtr node, xmlDocPtr doc);

/* Detach node, which stays alive as the root of its own tree. */
void LibXML_detach_node(xmlNodePtr node);

/* Check that node can leave its tree. Returns 0, or -1 with an exception set. */
int LibXML_check_detach(xmlNodePtr node);

/* libxml2 reads the parent of every attribute when it sorts a node set, so an
 * attribute cannot lose its element during an evaluation. */
int LibXML_check_attr_stays(void);

/* Check that node holds a list of children that can be changed. */
int LibXML_check_children(xmlNodePtr node);

/* The attribute of element with the same name and namespace as attr, if any. */
xmlAttrPtr LibXML_find_same_attr(xmlNodePtr element, xmlAttrPtr attr);

/* A namespace owned by doc, for attributes without an element to declare it. */
xmlNsPtr LibXML_doc_ns(xmlDocPtr doc, const xmlChar *href, const xmlChar *prefix);

/* Clear every pointer to ns in the subtree, before ns is freed. */
void LibXML_forget_ns(xmlNodePtr top, xmlNsPtr ns);

#endif /* LIBXML_TREE_H */

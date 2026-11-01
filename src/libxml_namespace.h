#ifndef LIBXML_NAMESPACE_H
#define LIBXML_NAMESPACE_H

#include "libxml_module.h"

/*
 * A namespace declaration. As in Perl it is not a node: libxml2's xmlNs has
 * another layout than xmlNode. The object owns a private copy of the
 * declaration, so it stays valid after the element it came from is freed.
 */
typedef struct {
    PyObject_HEAD
    xmlNsPtr ns;
} LibXML_NamespaceObject;

extern PyTypeObject LibXML_NamespaceType;

/* Make a Namespace holding a copy of orig. */
PyObject *LibXML_Namespace_Copy(xmlNsPtr orig);

#endif /* LIBXML_NAMESPACE_H */

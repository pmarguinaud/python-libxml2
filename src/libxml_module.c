#include "libxml_module.h"
#include "libxml_utils.h"
#include "libxml_node.h"
#include "libxml_document.h"
#include "libxml_element.h"
#include "libxml_attr.h"
#include "libxml_text.h"
#include "libxml_nodelist.h"
#include "libxml_xpath.h"
#include "libxml_parser.h"

PyObject *LibXML_Error = NULL;

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

PyMODINIT_FUNC
PyInit__libxml(void)
{
    /* Initialise libxml2 */
    xmlInitParser();

    /* Install structured error handler */
    xmlSetStructuredErrorFunc(NULL, LibXML_error_handler);

    /* Install node deregister callback to manage LibXML_NodePrivate lifetime */
    xmlDeregisterNodeDefault(LibXML_node_deregister);

    /* Ready all types (dependency order: Node first, then subtypes) */
    if (PyType_Ready(&LibXML_NodeType) < 0) return NULL;
    if (PyType_Ready(&LibXML_DocumentFragmentType) < 0) return NULL;
    if (PyType_Ready(&LibXML_DocumentType) < 0) return NULL;
    if (PyType_Ready(&LibXML_ElementType) < 0) return NULL;
    if (PyType_Ready(&LibXML_TextType) < 0) return NULL;
    if (PyType_Ready(&LibXML_CommentType) < 0) return NULL;
    if (PyType_Ready(&LibXML_CDATASectionType) < 0) return NULL;
    if (PyType_Ready(&LibXML_PIType) < 0) return NULL;
    if (PyType_Ready(&LibXML_AttrType) < 0) return NULL;
    if (PyType_Ready(&LibXML_NodeListType) < 0) return NULL;
    if (LibXML_NodeList_Init() < 0) return NULL;
    if (PyType_Ready(&LibXML_XPathContextType) < 0) return NULL;
    if (PyType_Ready(&LibXML_ParserType) < 0) return NULL;

    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_libxml",
        "Python C extension wrapping libxml2, interface compatible with Perl XML::LibXML",
        -1,
        NULL
    };

    PyObject *m = PyModule_Create(&moduledef);
    if (m == NULL) return NULL;

    /* Error exception */
    LibXML_Error = PyErr_NewException("xml_libxml.Error", PyExc_Exception, NULL);
    if (LibXML_Error == NULL) { Py_DECREF(m); return NULL; }
    Py_INCREF(LibXML_Error);
    if (PyModule_AddObject(m, "Error", LibXML_Error) < 0) {
        Py_DECREF(LibXML_Error); Py_DECREF(m); return NULL;
    }

    /* Add types */
#define ADD_TYPE(name, type) \
    Py_INCREF(&(type)); \
    if (PyModule_AddObject(m, name, (PyObject *)&(type)) < 0) { \
        Py_DECREF(&(type)); Py_DECREF(m); return NULL; \
    }

    ADD_TYPE("Node",             LibXML_NodeType)
    ADD_TYPE("DocumentFragment", LibXML_DocumentFragmentType)
    ADD_TYPE("Document",         LibXML_DocumentType)
    ADD_TYPE("Element",          LibXML_ElementType)
    ADD_TYPE("Text",             LibXML_TextType)
    ADD_TYPE("Comment",          LibXML_CommentType)
    ADD_TYPE("CDATASection",     LibXML_CDATASectionType)
    ADD_TYPE("PI",               LibXML_PIType)
    ADD_TYPE("Attr",             LibXML_AttrType)
    ADD_TYPE("NodeList",         LibXML_NodeListType)
    ADD_TYPE("XPathContext",     LibXML_XPathContextType)
    ADD_TYPE("Parser",           LibXML_ParserType)

#undef ADD_TYPE

    /* Node type constants */
#define ADD_INT(name, val) \
    if (PyModule_AddIntConstant(m, name, val) < 0) { Py_DECREF(m); return NULL; }

    ADD_INT("XML_ELEMENT_NODE",       XML_ELEMENT_NODE)
    ADD_INT("XML_ATTRIBUTE_NODE",     XML_ATTRIBUTE_NODE)
    ADD_INT("XML_TEXT_NODE",          XML_TEXT_NODE)
    ADD_INT("XML_CDATA_SECTION_NODE", XML_CDATA_SECTION_NODE)
    ADD_INT("XML_ENTITY_REF_NODE",    XML_ENTITY_REF_NODE)
    ADD_INT("XML_ENTITY_NODE",        XML_ENTITY_NODE)
    ADD_INT("XML_PI_NODE",            XML_PI_NODE)
    ADD_INT("XML_COMMENT_NODE",       XML_COMMENT_NODE)
    ADD_INT("XML_DOCUMENT_NODE",      XML_DOCUMENT_NODE)
    ADD_INT("XML_DOCUMENT_TYPE_NODE", XML_DOCUMENT_TYPE_NODE)
    ADD_INT("XML_DOCUMENT_FRAG_NODE", XML_DOCUMENT_FRAG_NODE)
    ADD_INT("XML_NOTATION_NODE",      XML_NOTATION_NODE)
    ADD_INT("XML_HTML_DOCUMENT_NODE", XML_HTML_DOCUMENT_NODE)
    ADD_INT("XML_DTD_NODE",           XML_DTD_NODE)
    ADD_INT("XML_ELEMENT_DECL",       XML_ELEMENT_DECL)
    ADD_INT("XML_ATTRIBUTE_DECL",     XML_ATTRIBUTE_DECL)
    ADD_INT("XML_ENTITY_DECL",        XML_ENTITY_DECL)
    ADD_INT("XML_NAMESPACE_DECL",     XML_NAMESPACE_DECL)
    ADD_INT("XML_XINCLUDE_START",     XML_XINCLUDE_START)
    ADD_INT("XML_XINCLUDE_END",       XML_XINCLUDE_END)

#undef ADD_INT

    /* Namespace constants */
    if (PyModule_AddStringConstant(m, "XML_XML_NS",
            "http://www.w3.org/XML/1998/namespace") < 0) {
        Py_DECREF(m); return NULL;
    }
    if (PyModule_AddStringConstant(m, "XML_XMLNS_NS",
            "http://www.w3.org/2000/xmlns/") < 0) {
        Py_DECREF(m); return NULL;
    }

    /* Version info */
    if (PyModule_AddIntConstant(m, "LIBXML_VERSION", LIBXML_VERSION) < 0) {
        Py_DECREF(m); return NULL;
    }
    if (PyModule_AddStringConstant(m, "LIBXML_DOTTED_VERSION",
            LIBXML_DOTTED_VERSION) < 0) {
        Py_DECREF(m); return NULL;
    }

    return m;
}

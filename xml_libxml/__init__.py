from xml_libxml._libxml import (
    # Main classes
    Parser,
    Document,
    Node,
    Element,
    Text,
    Comment,
    CDATASection,
    PI,
    Attr,
    Namespace,
    Dtd,
    RelaxNG,
    Schema,
    Pattern,
    RegExp,
    NodeList,
    XPathContext,
    XPathExpression,
    Literal,
    Number,
    Boolean,
    DocumentFragment,

    # Exception
    Error,

    # Node type constants
    XML_ELEMENT_NODE,
    XML_ATTRIBUTE_NODE,
    XML_TEXT_NODE,
    XML_CDATA_SECTION_NODE,
    XML_ENTITY_REF_NODE,
    XML_ENTITY_NODE,
    XML_PI_NODE,
    XML_COMMENT_NODE,
    XML_DOCUMENT_NODE,
    XML_DOCUMENT_TYPE_NODE,
    XML_DOCUMENT_FRAG_NODE,
    XML_NOTATION_NODE,
    XML_HTML_DOCUMENT_NODE,
    XML_DTD_NODE,
    XML_ELEMENT_DECL,
    XML_ATTRIBUTE_DECL,
    XML_ENTITY_DECL,
    XML_NAMESPACE_DECL,
    XML_XINCLUDE_START,
    XML_XINCLUDE_END,

    # Namespace constants
    XML_XML_NS,
    XML_XMLNS_NS,

    # Version info
    LIBXML_VERSION,
    LIBXML_DOTTED_VERSION,
    LIBXML_RUNTIME_VERSION,
)

from xml_libxml import Common, ErrNo
from xml_libxml._callbacks import (InputCallback, callbacks, close_callback,
                                   match_callback, open_callback, read_callback)
from xml_libxml._libxml import externalEntityLoader
from xml_libxml._reader import Reader
from xml_libxml._dom import AttributeHash, NamedNodeMap
from xml_libxml.Common import decodeFromUTF8, encodeToUTF8
from xml_libxml import SAX


createDocument = Document.createDocument
load_catalog = Parser.load_catalog


def load_xml(*args, **options):
    """Parse XML with the given options: string=, location= or IO=."""
    return Parser().load_xml(*args, **options)


def load_html(*args, **options):
    """Parse HTML with the given options: string=, location= or IO=."""
    return Parser().load_html(*args, **options)


__version__ = '0.1.0'
__all__ = [
    'Parser', 'Document', 'Node', 'Element', 'Text', 'Comment',
    'CDATASection', 'PI', 'Attr', 'Namespace', 'Dtd', 'RelaxNG', 'Schema', 'Pattern', 'RegExp', 'NodeList', 'XPathContext', 'XPathExpression',
    'Literal', 'Number', 'Boolean', 'DocumentFragment',
    'Error', 'ErrNo', 'Common', 'SAX', 'load_xml', 'load_html', 'createDocument', 'load_catalog',
    'InputCallback', 'externalEntityLoader', 'Reader', 'callbacks', 'match_callback',
    'open_callback', 'read_callback', 'close_callback',
    'AttributeHash', 'NamedNodeMap', 'encodeToUTF8', 'decodeFromUTF8',
    'XML_ELEMENT_NODE', 'XML_ATTRIBUTE_NODE', 'XML_TEXT_NODE',
    'XML_CDATA_SECTION_NODE', 'XML_ENTITY_REF_NODE', 'XML_ENTITY_NODE',
    'XML_PI_NODE', 'XML_COMMENT_NODE', 'XML_DOCUMENT_NODE',
    'XML_DOCUMENT_TYPE_NODE', 'XML_DOCUMENT_FRAG_NODE', 'XML_NOTATION_NODE',
    'XML_HTML_DOCUMENT_NODE', 'XML_DTD_NODE', 'XML_ELEMENT_DECL',
    'XML_ATTRIBUTE_DECL', 'XML_ENTITY_DECL', 'XML_NAMESPACE_DECL',
    'XML_XINCLUDE_START', 'XML_XINCLUDE_END',
    'XML_XML_NS', 'XML_XMLNS_NS',
    'LIBXML_VERSION', 'LIBXML_DOTTED_VERSION', 'LIBXML_RUNTIME_VERSION',
]

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


# The parser flags of libxml2, as Perl exports them (Perl itself sets them from options).
XML_PARSE_RECOVER = 1 << 0
XML_PARSE_NOENT = 1 << 1
XML_PARSE_DTDLOAD = 1 << 2
XML_PARSE_DTDATTR = 1 << 3
XML_PARSE_DTDVALID = 1 << 4
XML_PARSE_NOERROR = 1 << 5
XML_PARSE_NOWARNING = 1 << 6
XML_PARSE_PEDANTIC = 1 << 7
XML_PARSE_NOBLANKS = 1 << 8
XML_PARSE_SAX1 = 1 << 9
XML_PARSE_XINCLUDE = 1 << 10
XML_PARSE_NONET = 1 << 11
XML_PARSE_NODICT = 1 << 12
XML_PARSE_NSCLEAN = 1 << 13
XML_PARSE_NOCDATA = 1 << 14
XML_PARSE_NOXINCNODE = 1 << 15
XML_PARSE_COMPACT = 1 << 16
XML_PARSE_OLD10 = 1 << 17
XML_PARSE_NOBASEFIX = 1 << 18
XML_PARSE_HUGE = 1 << 19
XML_PARSE_OLDSAX = 1 << 20
HTML_PARSE_RECOVER = 1 << 0
HTML_PARSE_NOERROR = 1 << 5


def HAVE_READER():
    """True: the Reader is always built, as in Perl."""
    return True


def HAVE_SCHEMAS():
    """True: RelaxNG and Schema are always built, as in Perl."""
    return True


def HAVE_STRUCT_ERRORS():
    """True: errors are always Error objects, as in Perl."""
    return True


def load_xml(*args, **options):
    """Parse XML with the given options: string=, location= or IO=."""
    return Parser().load_xml(*args, **options)


def load_html(*args, **options):
    """Parse HTML with the given options: string=, location= or IO=."""
    return Parser().load_html(*args, **options)


__version__ = '0.2.0'
__all__ = [
    'Parser', 'Document', 'Node', 'Element', 'Text', 'Comment',
    'CDATASection', 'PI', 'Attr', 'Namespace', 'Dtd', 'RelaxNG', 'Schema', 'Pattern', 'RegExp', 'NodeList', 'XPathContext', 'XPathExpression',
    'Literal', 'Number', 'Boolean', 'DocumentFragment',
    'Error', 'ErrNo', 'Common', 'SAX', 'load_xml', 'load_html', 'createDocument', 'load_catalog',
    'InputCallback', 'externalEntityLoader', 'Reader', 'callbacks', 'match_callback',
    'open_callback', 'read_callback', 'close_callback',
    'AttributeHash', 'NamedNodeMap', 'encodeToUTF8', 'decodeFromUTF8',
    'HAVE_READER', 'HAVE_SCHEMAS', 'HAVE_STRUCT_ERRORS',
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

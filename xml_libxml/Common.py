"""
XML::LibXML::Common: names for node types, and encoding helpers.

encodeToUTF8 takes bytes in an encoding and gives text. decodeFromUTF8 takes text
and gives bytes in an encoding. As in Perl, None gives None.
"""
import codecs

from xml_libxml._error import Error
from xml_libxml._libxml import (
    XML_ATTRIBUTE_DECL, XML_ATTRIBUTE_NODE, XML_CDATA_SECTION_NODE, XML_COMMENT_NODE,
    XML_DOCUMENT_FRAG_NODE, XML_DOCUMENT_NODE, XML_DOCUMENT_TYPE_NODE, XML_DTD_NODE,
    XML_ELEMENT_DECL, XML_ELEMENT_NODE, XML_ENTITY_DECL, XML_ENTITY_NODE,
    XML_ENTITY_REF_NODE, XML_HTML_DOCUMENT_NODE, XML_NAMESPACE_DECL, XML_NOTATION_NODE,
    XML_PI_NODE, XML_TEXT_NODE, XML_XINCLUDE_END, XML_XINCLUDE_START,
)

ELEMENT_NODE = XML_ELEMENT_NODE
ATTRIBUTE_NODE = XML_ATTRIBUTE_NODE
TEXT_NODE = XML_TEXT_NODE
CDATA_SECTION_NODE = XML_CDATA_SECTION_NODE
ENTITY_REFERENCE_NODE = XML_ENTITY_REF_NODE
ENTITY_NODE = XML_ENTITY_NODE
PI_NODE = XML_PI_NODE
PROCESSING_INSTRUCTION_NODE = XML_PI_NODE
COMMENT_NODE = XML_COMMENT_NODE
DOCUMENT_NODE = XML_DOCUMENT_NODE
DOCUMENT_TYPE_NODE = XML_DOCUMENT_TYPE_NODE
DOCUMENT_FRAG_NODE = XML_DOCUMENT_FRAG_NODE
DOCUMENT_FRAGMENT_NODE = XML_DOCUMENT_FRAG_NODE
NOTATION_NODE = XML_NOTATION_NODE
HTML_DOCUMENT_NODE = XML_HTML_DOCUMENT_NODE
DTD_NODE = XML_DTD_NODE
ELEMENT_DECLARATION = XML_ELEMENT_DECL
ATTRIBUTE_DECLARATION = XML_ATTRIBUTE_DECL
ENTITY_DECLARATION = XML_ENTITY_DECL
NAMESPACE_DECLARATION = XML_NAMESPACE_DECL
XINCLUDE_START = XML_XINCLUDE_START
XINCLUDE_END = XML_XINCLUDE_END


def _codec(encoding):
    try:
        codecs.lookup(encoding)
    except LookupError:
        raise Error('unknown encoding: %s' % encoding) from None
    # libxml2 writes UTF-16 without a byte order mark here.
    return 'utf-16-le' if encoding.upper() == 'UTF-16' else encoding


def encodeToUTF8(encoding, data):
    """Text from bytes in this encoding."""
    if data is None:
        return None
    codec = _codec(encoding)
    return data if isinstance(data, str) else bytes(data).decode(codec)


def decodeFromUTF8(encoding, text):
    """Bytes in this encoding from text."""
    if text is None:
        return None
    return text.encode(_codec(encoding))

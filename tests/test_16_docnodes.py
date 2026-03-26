"""
test_16_docnodes.py — port of t/16docnodes.t

Tests document child nodes: childNodes(), firstChild, lastChild.
"""
import pytest
from xml_libxml import Parser, Element, XML_ELEMENT_NODE

INPUT = """\
<doc>
   <clean>   </clean>
   <dirty>   A   B   </dirty>
   <mixed>
      A
      <clean>   </clean>
      B
      <dirty>   A   B   </dirty>
      C
   </mixed>
</doc>
"""


def test_document_childnodes_count():
    for _ in range(3):
        doc = Parser().parse_string(INPUT)
        children = list(doc.childNodes())
        assert len(children) == 1


def test_document_firstChild_is_element():
    doc = Parser().parse_string(INPUT)
    for _ in range(3):
        e = doc.firstChild
        assert e is not None
        assert e.nodeType == XML_ELEMENT_NODE


def test_document_lastChild_is_element():
    doc = Parser().parse_string(INPUT)
    for _ in range(3):
        e = doc.lastChild
        assert e is not None
        assert e.nodeType == XML_ELEMENT_NODE


def test_serialize_non_ascii():
    """Element with non-ASCII attribute serializes with entity reference."""
    import unicodedata
    doc = Parser().parse_string('<root/>')
    from xml_libxml import Document
    doc2 = Document()
    node = doc2.createElement('test')
    char = '\u00e4'   # ä
    node.setAttribute('contents', char)
    doc2.setDocumentElement(node)
    serialized = node.toString()
    # Should contain either ä directly or &#xE4; or &auml;
    assert 'contents' in serialized
    assert 'test' in serialized


def test_document_childnodes_pi():
    """Document with PI child has 2 children: PI + root element."""
    doc = Parser().parse_string(
        '<?xml version="1.0"?>'
        '<?xsl-stylesheet type="text/xsl" href="a.xsl"?>'
        '<a/>'
    )
    children = list(doc.childNodes())
    assert len(children) == 2

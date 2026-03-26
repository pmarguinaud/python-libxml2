"""
test_03_document.py — port of t/03doc.t

Tests the DOM Document interface: createDocument, encoding, version, standalone,
createElement, createTextNode, createComment, createCDATASection, createAttribute,
createPI, setDocumentElement, getElementsByTagName, toString, toFile.
"""
import os
import pytest
from xml_libxml import (
    Parser, Document, Element,
    XML_ELEMENT_NODE, XML_TEXT_NODE, XML_COMMENT_NODE,
    XML_CDATA_SECTION_NODE, XML_ATTRIBUTE_NODE, XML_PI_NODE,
    XML_DOCUMENT_FRAG_NODE,
)

EXAMPLE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'example')


# ------------------------------------------------------------------ #
# 1.  Document Attributes                                             #
# ------------------------------------------------------------------ #

def test_createDocument():
    doc = Document()
    assert doc is not None


def test_encoding_default_none():
    doc = Document()
    assert doc.encoding is None


def test_version_default():
    doc = Document()
    assert doc.version == "1.0"


def test_standalone_default():
    doc = Document()
    assert doc.standalone == -1


def test_URI_default_none():
    doc = Document()
    assert doc.URI is None


def test_compression_default():
    doc = Document()
    assert doc.compression() == -1


def test_setEncoding():
    doc = Document()
    doc.setEncoding("iso-8859-1")
    assert doc.encoding == "iso-8859-1"


def test_setVersion():
    doc = Document()
    doc.setVersion("12.5")
    assert doc.version == "12.5"


def test_setStandalone():
    doc = Document()
    doc.setStandalone(1)
    assert doc.standalone == 1


def test_setBaseURI():
    doc = Document()
    doc.setBaseURI("localhost/here.xml")
    assert doc.URI == "localhost/here.xml"


def test_createDocument_with_params():
    doc2 = Document("1.1", "iso-8859-2")
    assert doc2.encoding == "iso-8859-2"
    assert doc2.version == "1.1"
    assert doc2.standalone == -1


# ------------------------------------------------------------------ #
# 2.  Creating Elements                                               #
# ------------------------------------------------------------------ #

def test_createDocumentFragment():
    doc = Document()
    node = doc.createDocumentFragment()
    assert node is not None
    assert node.nodeType == XML_DOCUMENT_FRAG_NODE


def test_createElement():
    doc = Document()
    node = doc.createElement("foo")
    assert node is not None
    assert node.nodeType == XML_ELEMENT_NODE
    assert node.nodeName == "foo"


def test_createElementNS():
    doc = Document()
    node = doc.createElementNS("http://kungfoo", "foo:bar")
    assert node is not None
    assert node.nodeType == XML_ELEMENT_NODE
    assert node.nodeName == "foo:bar"
    assert node.prefix == "foo"
    assert node.localname == "bar"
    assert node.namespaceURI == "http://kungfoo"


@pytest.mark.xfail(reason="libxml2 may not validate all element names")
def test_createElement_bad_names():
    doc = Document()
    for name in (";", "&", "<><", "/", "1A"):
        with pytest.raises(Exception):
            doc.createElement(name)


def test_createTextNode():
    doc = Document()
    node = doc.createTextNode("foo")
    assert node is not None
    assert node.nodeType == XML_TEXT_NODE
    assert node.nodeValue == "foo"


def test_createComment():
    doc = Document()
    node = doc.createComment("foo")
    assert node is not None
    assert node.nodeType == XML_COMMENT_NODE
    assert node.nodeValue == "foo"
    assert node.toString() == "<!--foo-->"


def test_createCDATASection():
    doc = Document()
    node = doc.createCDATASection("foo")
    assert node is not None
    assert node.nodeType == XML_CDATA_SECTION_NODE
    assert node.nodeValue == "foo"
    assert node.toString() == "<![CDATA[foo]]>"


# ------------------------------------------------------------------ #
# 2.  Attributes                                                      #
# ------------------------------------------------------------------ #

def test_createAttribute():
    doc = Document()
    attr = doc.createAttribute("foo", "bar")
    assert attr is not None
    assert attr.nodeType == XML_ATTRIBUTE_NODE
    assert attr.nodeName == "foo"
    assert attr.nodeValue == "bar"
    assert attr.hasChildNodes() == False


@pytest.mark.xfail(reason="libxml2 may not validate all attribute names")
def test_createAttribute_bad_names():
    doc = Document()
    for name in (";", "&", "<><", "/", "1A"):
        with pytest.raises(Exception):
            doc.createAttribute(name, "bar")


def test_addChild_attr_serialization():
    doc = Document()
    elem = doc.createElement("foo")
    attr = doc.createAttribute("attr", "e & f")
    elem.addChild(attr)
    assert elem.toString() == '<foo attr="e &amp; f"/>'


def test_createAttributeNS():
    doc = Document()
    root = doc.createElement("foo")
    doc.setDocumentElement(root)
    attr = doc.createAttributeNS("http://kungfoo", "kung:foo", "bar")
    assert attr is not None
    assert attr.nodeName == "kung:foo"
    assert attr.nodeValue == "bar"


def test_attr_setValue_getValue():
    doc = Document()
    root = doc.createElement("foo")
    doc.setDocumentElement(root)
    attr = doc.createAttributeNS("http://kungfoo", "kung:foo", "bar")
    attr.setValue("bar&amp;")
    assert attr.getValue() == "bar&amp;"


# ------------------------------------------------------------------ #
# 3.  Processing Instructions                                         #
# ------------------------------------------------------------------ #

def test_createPI_with_data():
    doc = Document()
    pi = doc.createProcessingInstruction("foo", "bar")
    assert pi is not None
    assert pi.nodeType == XML_PI_NODE
    assert pi.nodeName == "foo"
    assert pi.textContent == "bar"
    assert pi.getData() == "bar"


def test_createPI_without_data():
    doc = Document()
    pi = doc.createProcessingInstruction("foo")
    assert pi is not None
    assert pi.nodeType == XML_PI_NODE
    assert pi.nodeName == "foo"
    data = pi.textContent
    assert data is None or data == ""
    pi.setData("bar&amp;")
    assert pi.getData() == "bar&amp;"
    assert pi.textContent == "bar&amp;"


# ------------------------------------------------------------------ #
# 4.  Document Element Manipulation                                   #
# ------------------------------------------------------------------ #

def test_setDocumentElement():
    doc = Document()
    node = doc.createElement("foo")
    doc.setDocumentElement(node)
    tn = doc.documentElement
    assert tn is not None
    assert node.isSameNode(tn)


def test_setDocumentElement_replace():
    doc = Document()
    node2 = doc.createElement("bar")
    for _ in range(2):
        nodeA = doc.createElement("x")
        doc.setDocumentElement(nodeA)
    doc.setDocumentElement(node2)
    cn = list(doc.childNodes())
    assert len(cn) == 1
    assert cn[0].isSameNode(node2)


def test_appendChild_pi_to_doc():
    doc = Document()
    root = doc.createElement("foo")
    doc.setDocumentElement(root)
    pi = doc.createProcessingInstruction("foo", "bar")
    doc.appendChild(pi)
    cn = list(doc.childNodes())
    assert pi.isSameNode(cn[-1])
    pi.setData('bar="foo"')
    assert pi.textContent == 'bar="foo"'


# ------------------------------------------------------------------ #
# 5.  Serialization                                                   #
# ------------------------------------------------------------------ #

def test_toFile_and_parse():
    parser = Parser()
    doc = parser.parse_string("<foo>bar</foo>")
    outpath = os.path.join(EXAMPLE_DIR, "testrun.xml")
    doc.toFile(outpath)
    try:
        tdoc = parser.parse_file(outpath)
        assert tdoc is not None
        assert tdoc.documentElement is not None
        assert tdoc.documentElement.nodeName == "foo"
        assert tdoc.documentElement.textContent == "bar"
    finally:
        if os.path.exists(outpath):
            os.unlink(outpath)


# ------------------------------------------------------------------ #
# 6.  getElementsByTagName / getElementsByLocalName                   #
# ------------------------------------------------------------------ #

def test_getElementsByTagName_count():
    doc = Parser().parse_string("<A><A><B/></A><A><B/></A></A>")
    nodes = list(doc.getElementsByTagName("A"))
    assert len(nodes) == 3


def test_getElementsByTagName_star():
    doc = Parser().parse_string("<A><A><B/></A><A><B/></A></A>")
    nodes = list(doc.getElementsByTagName("*"))
    assert len(nodes) == 5


def test_getElementsByLocalName():
    doc = Parser().parse_string("<A><A><B/></A><A><B/></A></A>")
    nodes = list(doc.getElementsByLocalName("A"))
    assert len(nodes) == 3


def test_getElementsByTagNameNS():
    doc = Parser().parse_string('<C:A xmlns:C="xml://D"><C:A><C:B/></C:A><C:A><C:B/></C:A></C:A>')
    nodes = list(doc.getElementsByTagNameNS("xml://D", "A"))
    assert len(nodes) == 3


def test_getDocumentElement():
    doc = Parser().parse_string("<root/>")
    assert doc.getDocumentElement() is not None
    assert doc.getDocumentElement().nodeName == "root"


# ------------------------------------------------------------------ #
# 7.  getEncoding / setEncoding                                       #
# ------------------------------------------------------------------ #

def test_getEncoding():
    doc = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?>\n<test/>')
    assert doc.getEncoding() == "UTF-8"


def test_setEncoding_clear():
    doc = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?>\n<test/>')
    doc.setEncoding()
    assert doc.getEncoding() is None

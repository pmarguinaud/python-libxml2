"""
test_06_elements.py — port of t/06elements.t

Tests the DOM Element and Attr interface:
getAttribute/NS, setAttribute/NS, getAttributeNode/NS,
removeAttribute/NS, hasAttribute/NS, setAttributeNode/NS,
getChildrenByTagName/NS, normalization, appendText/TextChild, string_value.
"""
import pytest
from xml_libxml import (
    Parser, Document, Element,
    XML_ELEMENT_NODE, XML_ATTRIBUTE_NODE,
)

FOO = "foo"
BAR = "bar"
NSURI = "http://foo"
PREFIX = "x"
A1 = "A"
V1 = "a"
A2 = "B"
V2 = "b"
A3 = "C"


# ------------------------------------------------------------------ #
# 1.  Bound element (in document)                                     #
# ------------------------------------------------------------------ #

def test_createElement_tagName():
    doc = Document()
    elem = doc.createElement(FOO)
    assert elem is not None
    assert elem.tagName == FOO


def test_setAttribute_hasAttribute():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttribute(A1, V1)
    assert elem.hasAttribute(A1)
    assert elem.getAttribute(A1) == V1


def test_getAttributeNode():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttribute(A1, V1)
    attr = elem.getAttributeNode(A1)
    assert attr is not None
    assert attr.nodeName == A1
    assert attr.nodeValue == V1


def test_setAttribute_update_attr():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttribute(A1, V1)
    attr = elem.getAttributeNode(A1)
    elem.setAttribute(A1, V2)
    assert elem.getAttribute(A1) == V2
    # The attr wrapper reflects the new value
    assert attr.nodeValue == V2


def test_setAttributeNode():
    doc = Document()
    elem = doc.createElement(FOO)
    attr2 = doc.createAttribute(A2, V1)
    elem.setAttributeNode(attr2)
    assert elem.hasAttribute(A2)
    assert elem.getAttribute(A2) == V1
    tattr = elem.getAttributeNode(A2)
    assert tattr.isSameNode(attr2)


def test_setAttribute_empty():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttribute(A2, "")
    assert elem.hasAttribute(A2)
    assert elem.getAttribute(A2) == ""


def test_setAttributeNS_hasAttributeNS():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS(NSURI, PREFIX + ":" + FOO, V2)
    assert elem.hasAttributeNS(NSURI, FOO)
    assert not elem.hasAttribute(FOO)
    assert elem.hasAttribute(PREFIX + ":" + FOO)


def test_getAttributeNodeNS():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS(NSURI, PREFIX + ":" + FOO, V2)
    tattr = elem.getAttributeNodeNS(NSURI, FOO)
    assert tattr is not None
    assert tattr.nodeName == PREFIX + ":" + FOO
    assert tattr.nodeValue == V2


def test_removeAttributeNode():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS(NSURI, PREFIX + ":" + FOO, V2)
    tattr = elem.getAttributeNodeNS(NSURI, FOO)
    elem.removeAttributeNode(tattr)
    assert not elem.hasAttributeNS(NSURI, FOO)


def test_setAttributeNS_empty_ns():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS("", FOO, V2)
    assert elem.hasAttribute(FOO)
    tattr = elem.getAttributeNode(FOO)
    assert tattr is not None
    assert tattr.nodeName == FOO
    assert tattr.namespaceURI is None
    assert tattr.nodeValue == V2


def test_hasAttributeNS_equivalence():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS("", FOO, V2)
    assert elem.hasAttribute(FOO) == 1
    assert elem.hasAttributeNS(None, FOO) == 1
    assert elem.hasAttributeNS("", FOO) == 1


def test_removeAttributeNode_empty_ns():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS("", FOO, V2)
    tattr = elem.getAttributeNode(FOO)
    elem.removeAttributeNode(tattr)
    assert not elem.hasAttributeNS("", FOO)
    assert not elem.hasAttributeNS(None, FOO)


def test_setAttributeNodeNS():
    doc = Document()
    e2 = doc.createElement(FOO)
    doc.setDocumentElement(e2)
    elem = doc.createElement("bar")
    ns_attr = doc.createAttributeNS(NSURI + ".x", PREFIX + ":" + FOO, BAR)
    assert ns_attr is not None
    elem.setAttributeNodeNS(ns_attr)
    assert elem.hasAttributeNS(NSURI + ".x", FOO)
    elem.removeAttributeNS(NSURI + ".x", FOO)
    assert not elem.hasAttributeNS(NSURI + ".x", FOO)


def test_removeAttributeNS_vs_setAttributeNS():
    doc = Document()
    elem = doc.createElement(FOO)
    elem.setAttributeNS(NSURI, PREFIX + ":" + A1, V2)
    elem.removeAttributeNS("", A1)
    # removeAttributeNS("", A1) removes no-NS attr; NS attr remains
    assert not elem.hasAttribute(A1)
    assert elem.hasAttributeNS(NSURI, A1)


# ------------------------------------------------------------------ #
# 2.  Unbound element (standalone)                                    #
# ------------------------------------------------------------------ #

def test_standalone_element():
    elem = Element(FOO)
    assert elem is not None
    assert elem.tagName == FOO


def test_standalone_setAttribute():
    elem = Element(FOO)
    elem.setAttribute(A1, V1)
    assert elem.hasAttribute(A1)
    assert elem.getAttribute(A1) == V1
    attr = elem.getAttributeNode(A1)
    assert attr is not None
    assert attr.nodeName == A1
    assert attr.nodeValue == V1


def test_standalone_setAttributeNS():
    elem = Element(FOO)
    elem.setAttributeNS(NSURI, PREFIX + ":" + FOO, V2)
    assert elem.hasAttributeNS(NSURI, FOO)
    tattr = elem.getAttributeNodeNS(NSURI, FOO)
    assert tattr is not None
    assert tattr.nodeName == PREFIX + ":" + FOO
    assert tattr.nodeValue == V2
    elem.removeAttributeNode(tattr)
    assert not elem.hasAttributeNS(NSURI, FOO)


# ------------------------------------------------------------------ #
# 3.  Namespace / setNamespace                                        #
# ------------------------------------------------------------------ #

def test_setNamespace():
    doc = Document()
    elem = doc.createElementNS("foo", "root")
    doc.setDocumentElement(elem)
    elem.setNamespace("foo", "bar")
    elem.setAttributeNS("foo", "x:attr", "test")
    elem.setAttributeNS(None, "attr2", "test")
    assert elem.getAttributeNS("foo", "attr") == "test"
    assert elem.getAttributeNS("", "attr2") == "test"

    elem.setAttributeNS("foo", "attr2", "bar")
    assert elem.getAttributeNS("foo", "attr2") == "bar"


# ------------------------------------------------------------------ #
# 4.  Text Append and Normalization                                   #
# ------------------------------------------------------------------ #

def test_normalize_element():
    doc = Document()
    t1 = doc.createTextNode("bar1")
    t2 = doc.createTextNode("bar2")
    t3 = doc.createTextNode("bar3")
    e = doc.createElement("foo")
    e2 = doc.createElement("bar")
    e.appendChild(e2)
    e.appendChild(t1)
    e.appendChild(t2)
    e.appendChild(t3)

    cn = list(e.childNodes())
    assert len(cn) == 4

    e.normalize()

    cn = list(e.childNodes())
    assert len(cn) == 2
    # t2 and t3 have been merged into t1; their wrappers' node is NULL
    assert t2.parentNode is None
    assert t3.parentNode is None


def test_normalize_document():
    doc = Document()
    t1 = doc.createTextNode("bar1")
    t2 = doc.createTextNode("bar2")
    t3 = doc.createTextNode("bar3")
    e = doc.createElement("foo")
    e2 = doc.createElement("bar")
    doc.setDocumentElement(e)
    e.appendChild(e2)
    e.appendChild(t1)
    e.appendChild(t2)
    e.appendChild(t3)

    cn = list(e.childNodes())
    assert len(cn) == 4

    doc.normalize()

    cn = list(e.childNodes())
    assert len(cn) == 2
    assert t2.parentNode is None
    assert t3.parentNode is None


# ------------------------------------------------------------------ #
# 5.  appendText / appendTextChild / string_value                     #
# ------------------------------------------------------------------ #

def test_appendText():
    doc = Document()
    elem = doc.createElement(FOO)
    doc.setDocumentElement(elem)
    plain = "foo"
    stdent = "foo & this"
    elem.appendText(plain)
    assert elem.string_value() == plain
    elem.appendText(stdent)
    assert elem.string_value() == plain + stdent


def test_appendTextChild():
    doc = Document()
    elem = doc.createElement(FOO)
    doc.setDocumentElement(elem)
    elem.appendText("foo")
    elem.appendTextChild("foo")
    elem.appendTextChild("foo", "foo&bar")
    cn = list(elem.childNodes())
    assert len(cn) == 3
    assert not cn[1].hasChildNodes()
    assert cn[2].hasChildNodes()

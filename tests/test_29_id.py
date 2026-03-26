"""
test_29_id.py — port of t/29id.t

Tests getElementById, isId, xml:id.
"""
import pytest
from xml_libxml import Parser


XML1 = """\
<!DOCTYPE root [
<!ELEMENT root (root?)>
<!ATTLIST root id ID #REQUIRED
               notid CDATA #IMPLIED
>
]>
<root id="foo" notid="x"/>
"""

XML2 = '<root2 xml:id="foo"/>'


@pytest.fixture
def doc1():
    return Parser().parse_string(XML1)


@pytest.fixture
def doc2():
    return Parser().parse_string(XML2)


# ------------------------------------------------------------------ #
# 1.  getElementById with DTD-declared ID attribute                   #
# ------------------------------------------------------------------ #

def test_getElementById_found(doc1):
    root = doc1.getDocumentElement()
    n = doc1.getElementById('foo')
    assert n is not None
    assert root.isSameNode(n)


def test_getElementsById_alias(doc1):
    root = doc1.getDocumentElement()
    n = doc1.getElementsById('foo')
    assert root.isSameNode(n)


def test_isId_true(doc1):
    n = doc1.getElementById('foo')
    at = n.getAttributeNode('id')
    assert at is not None
    assert at.isId()


def test_isId_false(doc1):
    root = doc1.getDocumentElement()
    at = root.getAttributeNode('notid')
    assert not at.isId()


def test_getElementById_after_setAttribute(doc1):
    root = doc1.getDocumentElement()
    root.setAttribute('id', 'bar')
    n = doc1.getElementById('bar')
    assert root.isSameNode(n)
    assert doc1.getElementById('foo') is None


def test_getElementById_new_child(doc1):
    root = doc1.getDocumentElement()
    child = doc1.createElement('root')
    root.appendChild(child)
    child.setAttribute('id', 'new')
    n = doc1.getElementById('new')
    assert n is not None
    assert child.isSameNode(n)
    at = n.getAttributeNode('id')
    assert at is not None
    assert at.isId()


# ------------------------------------------------------------------ #
# 2.  xml:id attribute                                                #
# ------------------------------------------------------------------ #

def test_getElementById_xml_id(doc2):
    root = doc2.getDocumentElement()
    n = doc2.getElementById('foo')
    assert n is not None
    assert root.isSameNode(n)


def test_getElementById_xml_id_after_change(doc2):
    root = doc2.getDocumentElement()
    root.setAttribute('xml:id', 'bar')
    assert doc2.getElementById('foo') is None
    n = doc2.getElementById('bar')
    assert root.isSameNode(n)


def test_isId_xml_id(doc2):
    root = doc2.getDocumentElement()
    root.setAttribute('xml:id', 'bar')
    n = doc2.getElementById('bar')
    at = n.getAttributeNode('xml:id')
    assert at is not None
    assert at.isId()


def test_setAttribute_non_id(doc2):
    root = doc2.getDocumentElement()
    root.setAttribute('xml:id', 'bar')
    root.setAttribute('id', 'FOO')
    at = root.getAttributeNode('id')
    assert at is not None
    assert not at.isId()


def test_getAttributeNodeNS_xml_id(doc2):
    root = doc2.getDocumentElement()
    root.setAttribute('xml:id', 'bar')
    at = root.getAttributeNodeNS('http://www.w3.org/XML/1998/namespace', 'id')
    assert at is not None
    assert at.isId()


def test_setAttributeNS_xml_id(doc2):
    XML_NS = 'http://www.w3.org/XML/1998/namespace'
    root = doc2.getDocumentElement()
    root.setAttributeNS(XML_NS, 'id', 'baz')
    assert doc2.getElementById('foo') is None  # old id gone
    n = doc2.getElementById('baz')
    assert root.isSameNode(n)
    at = n.getAttributeNodeNS(XML_NS, 'id')
    assert at is not None
    assert at.isId()


def test_removeAttribute_id(doc2):
    root = doc2.getDocumentElement()
    root.removeAttribute('id')
    assert root.toString() == '<root2 xml:id="foo"/>'

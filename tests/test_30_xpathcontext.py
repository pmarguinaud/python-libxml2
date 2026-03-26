"""
test_30_xpathcontext.py — port of t/30xpathcontext.t

Tests XPathContext: findnodes, findvalue, find, registerNs, setContextNode.
"""
import pytest
from xml_libxml import Parser, Document, XPathContext, NodeList

DOC_STR = '<foo><bar a="b"></bar></foo>'
DOC_NS_STR = '<foo xmlns="http://example.com/foobar"><bar a="b"></bar></foo>'


@pytest.fixture(scope="module")
def doc():
    return Parser().parse_string(DOC_STR)


@pytest.fixture(scope="module")
def doc_ns():
    return Parser().parse_string(DOC_NS_STR)


# ------------------------------------------------------------------ #
# 1.  findnodes() in list context                                     #
# ------------------------------------------------------------------ #

def test_findnodes_list(doc):
    nodes = list(XPathContext(doc).findnodes('/*'))
    assert len(nodes) == 1
    assert nodes[0].nodeName == 'foo'


def test_findnodes_from_element(doc):
    nodes = list(XPathContext(doc).findnodes('/*'))
    bar_nodes = list(XPathContext(nodes[0]).findnodes('bar'))
    assert bar_nodes[0].nodeName() == 'bar' if callable(bar_nodes[0].nodeName) else bar_nodes[0].nodeName == 'bar'


def test_findnodes_from_element_context(doc):
    xc = XPathContext(doc)
    root_nodes = xc.findnodes('/*')
    root = root_nodes.pop()
    bar_nodes = list(XPathContext(root).findnodes('bar'))
    assert bar_nodes[0].nodeName == 'bar'


# ------------------------------------------------------------------ #
# 2.  findnodes() in scalar context (NodeList)                        #
# ------------------------------------------------------------------ #

def test_findnodes_nodelist_pop(doc):
    nl = XPathContext(doc).findnodes('/*')
    node = nl.pop()
    assert node.nodeName == 'foo'
    assert nl.pop() is None


# ------------------------------------------------------------------ #
# 3.  findvalue()                                                     #
# ------------------------------------------------------------------ #

def test_findvalue_arithmetic(doc):
    assert XPathContext(doc).findvalue('1+1') == '2'


def test_findvalue_bool_false(doc):
    assert XPathContext(doc).findvalue('1=2') == 'false'


# ------------------------------------------------------------------ #
# 4.  find()                                                          #
# ------------------------------------------------------------------ #

def test_find_nodelist_pop(doc):
    nl = XPathContext(doc).find('/foo/bar')
    assert isinstance(nl, NodeList)
    assert nl.pop().nodeName == 'bar'


def test_find_number(doc):
    result = XPathContext(doc).find('1*3')
    assert result == 3.0


def test_find_boolean(doc):
    result = XPathContext(doc).find('1=1')
    assert result == True


# ------------------------------------------------------------------ #
# 5.  registerNs() / lookupNs() / unregisterNs()                     #
# ------------------------------------------------------------------ #

def test_registerNs(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    nl = xc.findnodes('/xxx:foo')
    assert nl.pop().nodeName == 'foo'


def test_lookupNs(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    assert xc.lookupNs('xxx') == 'http://example.com/foobar'


def test_exists(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    assert xc.exists('//xxx:bar/@a')
    assert xc.exists('//xxx:bar/@b') == 0 or not xc.exists('//xxx:bar/@b')


def test_exists_with_context_node(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    root = doc_ns.getDocumentElement()
    assert xc.exists('xxx:bar', root)


def test_unregisterNs(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    xc.unregisterNs('xxx')
    with pytest.raises(Exception):
        xc.findnodes('/xxx:foo')
    assert xc.lookupNs('xxx') is None


# ------------------------------------------------------------------ #
# 6.  getContextNode() / setContextNode()                             #
# ------------------------------------------------------------------ #

def test_getContextNode(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    assert xc.getContextNode().isSameNode(doc_ns)


def test_setContextNode(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    root = doc_ns.getDocumentElement()
    xc.setContextNode(root)
    assert xc.getContextNode().isSameNode(root)


def test_findnodes_after_setContextNode(doc_ns):
    xc = XPathContext(doc_ns)
    xc.registerNs('xxx', 'http://example.com/foobar')
    root = doc_ns.getDocumentElement()
    xc.setContextNode(root)
    nl = xc.findnodes('.')
    assert nl.pop().isSameNode(root)


def test_context_preserved_after_find():
    doc = Parser().parse_string('<foo><bar/></foo>')
    xc = XPathContext(doc.getDocumentElement())
    xc.find('/')
    assert xc.getContextNode().toString() == '<foo><bar/></foo>'


def test_empty_context():
    xc = XPathContext()
    assert xc.getContextNode() is None
    with pytest.raises(Exception):
        xc.find('/')


def test_setContextNode_then_find():
    doc = Parser().parse_string('<foo><bar/></foo>')
    xc = XPathContext()
    root = doc.getDocumentElement()
    xc.setContextNode(root)
    assert xc.find('/')
    assert xc.getContextNode().isSameNode(root)


def test_findnodes_with_temp_context():
    doc = Parser().parse_string('<foo><bar/></foo>')
    xc = XPathContext(doc.getDocumentElement())
    # Pass a different context node to findnodes temporarily
    bar_list = xc.findnodes('foo/bar', doc)
    assert bar_list.pop().nodeName == 'bar'
    # Original context node unchanged
    assert xc.getContextNode().isSameNode(doc.getDocumentElement())


def test_findnodes_parent_axis():
    doc = Parser().parse_string('<foo><bar/></foo>')
    xc = XPathContext(doc.getDocumentElement())
    bar_list = xc.findnodes('foo/bar', doc)
    bar = bar_list.pop()
    parent_list = xc.findnodes('parent::*', bar)
    assert parent_list.pop().nodeName == 'foo'
    # Context node still original
    assert xc.getContextNode().isSameNode(doc.getDocumentElement())


def test_no_segfault_setContextNode_none():
    """Setting context node to None then back to a doc should not crash."""
    xc = XPathContext()
    xc.registerNs('pfx', 'http://www.foo.com')
    doc = Parser().parse_string('<foo xmlns="http://www.foo.com" />')
    xc.setContextNode(doc)
    xc.findnodes('/')
    xc.setContextNode(None)
    xc.getContextNode()
    xc.setContextNode(doc)
    xc.findnodes('/')
    assert True  # No crash

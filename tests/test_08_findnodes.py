"""
test_08_findnodes.py — port of t/08findnodes.t

Tests findnodes(), find(), findvalue() called directly on any node.
"""
import os
import pytest
from xml_libxml import Parser, Document, Element, Text, XPathContext

EXAMPLE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'example')


# ------------------------------------------------------------------ #
# 1.  findnodes from document and elements (dromeds.xml)              #
# ------------------------------------------------------------------ #

@pytest.fixture(scope="module")
def dromedary_doc():
    return Parser().parse_file(os.path.join(EXAMPLE_DIR, "dromeds.xml"))


def test_findnodes_from_root(dromedary_doc):
    elem = dromedary_doc.getDocumentElement()
    lst = list(elem.findnodes("species"))
    assert len(lst) == 3


def test_findnodes_from_child(dromedary_doc):
    elem = dromedary_doc.getDocumentElement()
    lst = list(elem.findnodes("species"))
    node = lst[0]
    slist = list(node.findnodes("humps"))
    assert len(slist) == 1


def test_findnodes_with_predicate(dromedary_doc):
    elem = dromedary_doc.getDocumentElement()
    lst = list(elem.findnodes("species[@name='Llama']"))
    assert len(lst) == 1


def test_findnodes_not_predicate(dromedary_doc):
    elem = dromedary_doc.getDocumentElement()
    lst = list(elem.findnodes("species[@name!='Llama']/disposition"))
    assert len(lst) == 2


def test_findnodes_attribute_axis(dromedary_doc):
    elem = dromedary_doc.getDocumentElement()
    lst = list(elem.findnodes("species/@name"))
    assert len(lst) > 0
    # First attr node toString contains ' name="Camel"'
    assert 'Camel' in lst[0].toString()


def test_text_node_getData():
    x = Text("1234")
    assert x.getData() == "1234"


def test_finddoc_root(dromedary_doc):
    # findnodes("/") from document root should not crash
    rn = dromedary_doc.documentElement
    result = rn.findnodes("/")
    assert result is not None


# ------------------------------------------------------------------ #
# 2.  findnodes with namespace prefixes                               #
# ------------------------------------------------------------------ #

def test_findnodes_namespace_prefix():
    doc = Parser().parse_string(
        '<a:foo xmlns:a="http://foo.com" xmlns:b="http://bar.com">'
        ' <b:bar>'
        '  <a:foo xmlns:a="http://other.com"/>'
        ' </b:bar>'
        '</a:foo>'
    )
    root = doc.getDocumentElement()
    a_nodes = list(root.findnodes('//a:foo'))
    assert len(a_nodes) == 1

    b_nodes = list(root.findnodes('//b:bar'))
    assert len(b_nodes) == 1

    none_nodes = list(root.findnodes('//b:foo'))
    none_nodes += list(root.findnodes('//foo'))
    assert len(none_nodes) == 0


def test_findnodes_empty_result():
    doc = Parser().parse_string('<root/>')
    root = doc.documentElement
    nodes = list(root.findnodes("/humpty/dumpty"))
    assert len(nodes) == 0


def test_findnodes_namespace_axis():
    doc = Parser().parse_string(
        '<foo xmlns="http://kungfoo" xmlns:bar="http://foo"/>'
    )
    root = doc.documentElement
    ns_nodes = list(root.findnodes('namespace::*'))
    # libxml2 includes the built-in xml: namespace, so >= 2
    assert len(ns_nodes) >= 2


# ------------------------------------------------------------------ #
# 3.  Bad XPath expressions raise exceptions                          #
# ------------------------------------------------------------------ #

@pytest.mark.parametrize("bad_xpath", [
    'abc:::def',
    'foo///bar',
    '...',
    '/-',
])
def test_bad_xpath_findnodes(bad_xpath):
    doc = Parser().parse_string('<foo/>')
    root = doc.documentElement
    with pytest.raises(Exception):
        root.findnodes(bad_xpath)


@pytest.mark.parametrize("bad_xpath", [
    'abc:::def',
    'foo///bar',
    '...',
    '/-',
])
def test_bad_xpath_find(bad_xpath):
    doc = Parser().parse_string('<foo/>')
    root = doc.documentElement
    with pytest.raises(Exception):
        root.find(bad_xpath)


@pytest.mark.parametrize("bad_xpath", [
    'abc:::def',
    'foo///bar',
    '...',
    '/-',
])
def test_bad_xpath_findvalue(bad_xpath):
    doc = Parser().parse_string('<foo/>')
    root = doc.documentElement
    with pytest.raises(Exception):
        root.findvalue(bad_xpath)


# ------------------------------------------------------------------ #
# 4.  findnodes on dynamically built document                         #
# ------------------------------------------------------------------ #

def test_findnodes_dynamic_doc():
    doc = Document()
    root = doc.createElement("A")
    doc.setDocumentElement(root)
    b = doc.createElement("B")
    root.appendChild(b)

    lst = list(doc.findnodes('//A'))
    assert len(lst) > 0
    assert lst[0].isSameNode(root)

    lst = list(doc.findnodes('//B'))
    assert len(lst) > 0
    assert lst[0].isSameNode(b)


def test_getElementsByTagName_dynamic():
    doc = Document()
    root = doc.createElement("A")
    doc.setDocumentElement(root)
    b = doc.createElement("B")
    root.appendChild(b)

    lst = list(root.getElementsByTagName('B'))
    assert len(lst) > 0
    assert lst[0].isSameNode(b)


# ------------------------------------------------------------------ #
# 5.  Unbind nodes while iterating                                    #
# ------------------------------------------------------------------ #

def test_unbind_while_iterating():
    doc = Document()
    root = doc.createElement("A")
    doc.setDocumentElement(root)
    for _ in range(2):
        b = doc.createElement("B")
        root.appendChild(b)
        c = doc.createElement("C")
        b.appendChild(c)

    lst = list(root.findnodes("B"))
    assert len(lst) == 2
    for node in lst:
        subnodes = list(node.findnodes("C"))
        if subnodes:
            node.unbindNode()


def test_findnodes_after_removeChild():
    xmlstr = "<a><b><c>1</c><c>2</c></b></a>"
    doc = Parser().parse_string(xmlstr)
    root = doc.documentElement
    lastc_list = list(root.findnodes('b/c[last()]'))
    assert len(lastc_list) == 1
    lastc = lastc_list[0]
    # lastc is a child of b, not root — remove from correct parent
    lastc.parentNode.removeChild(lastc)
    assert root.toString() == "<a><b><c>1</c></b></a>"

"""
test_09_xpath.py — port of t/09xpath.t

Tests XPath: findnodes, findvalue, find, types returned.
"""
import pytest
from xml_libxml import Parser, Document, NodeList

XML_STR = """\
<foo>
    <bar>
        test 1
    </bar>
    <bar>
        test 2
    </bar>
</foo>
"""


@pytest.fixture(scope="module")
def doc():
    return Parser().parse_string(XML_STR)


# ------------------------------------------------------------------ #
# 1.  findnodes() on document                                         #
# ------------------------------------------------------------------ #

def test_findnodes_two_bars(doc):
    nodes = list(doc.findnodes("/foo/bar"))
    assert len(nodes) == 2


def test_findnodes_owner_document(doc):
    nodes = list(doc.findnodes("/foo/bar"))
    assert doc.isSameNode(nodes[0].ownerDocument)


def test_insertBefore_then_childNodes(doc):
    nodes = list(doc.findnodes("/foo/bar"))
    n = doc.createElement("foobar")
    p = nodes[1].parentNode
    p.insertBefore(n, nodes[1])
    assert p.isSameNode(doc.documentElement)
    cn = list(p.childNodes())
    # original 2 bars + foobar + whitespace text nodes
    assert len(cn) >= 3


# ------------------------------------------------------------------ #
# 2.  find() returns typed results                                    #
# ------------------------------------------------------------------ #

def test_find_nodelist(doc):
    result = doc.find("/foo/bar")
    assert isinstance(result, NodeList)
    assert result.size() >= 2


def test_find_numeric():
    doc = Parser().parse_string("<root/>")
    result = doc.find("32 + 13")
    assert result == 45.0


def test_find_string():
    doc = Parser().parse_string("<root/>")
    result = doc.find("'Hello World'")
    assert result == "Hello World"


def test_find_boolean():
    doc = Parser().parse_string("<root/>")
    result = doc.find("1 and 2")
    assert result == True


def test_find_number_value():
    doc = Parser().parse_string("<root/>")
    assert doc.find("1*3") == 3.0


def test_find_boolean_true():
    doc = Parser().parse_string("<foo><bar>t</bar></foo>")
    result = doc.find("contains(/foo/bar, 't')")
    assert result == True


# ------------------------------------------------------------------ #
# 3.  findvalue() returns string                                      #
# ------------------------------------------------------------------ #

def test_findvalue_arithmetic():
    doc = Parser().parse_string("<root/>")
    assert doc.findvalue("1+1") == "2"


def test_findvalue_boolean_false():
    doc = Parser().parse_string("<root/>")
    assert doc.findvalue("1=2") == "false"


def test_findvalue_string_content():
    doc = Parser().parse_string("<foo><bar>test 1</bar><bar>test 2</bar></foo>")
    val = doc.findvalue("//bar")
    assert "test 1" in val


def test_find_nodelist_owner_doc(doc):
    result = doc.find("/foo/bar")
    assert isinstance(result, NodeList)
    node = result.pop()
    assert doc.isSameNode(node.ownerDocument)

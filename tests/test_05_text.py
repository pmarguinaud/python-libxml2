"""
test_05_text.py — port of t/05text.t

Tests the DOM CharacterData interface: Text, Comment, CDATASection.
"""
import pytest
from xml_libxml import (
    Document, Text, Comment, CDATASection, DocumentFragment,
    XML_TEXT_NODE, XML_COMMENT_NODE, XML_CDATA_SECTION_NODE,
    XML_DOCUMENT_FRAG_NODE,
)

doc = Document()

FOO = "foobar"


# ------------------------------------------------------------------ #
# 1.  Creation                                                        #
# ------------------------------------------------------------------ #

def test_textNode_create():
    tn = doc.createTextNode(FOO)
    assert tn is not None


def test_textNode_nodeName():
    tn = doc.createTextNode(FOO)
    assert tn.nodeName == "#text"


def test_textNode_nodeValue():
    tn = doc.createTextNode(FOO)
    assert tn.nodeValue == FOO


def test_textNode_attributes_empty():
    tn = doc.createTextNode(FOO)
    assert tn.attributes() == []


# ------------------------------------------------------------------ #
# 2.  substringData                                                   #
# ------------------------------------------------------------------ #

def test_substringData_1():
    tn = doc.createTextNode(FOO)
    assert tn.substringData(1, 2) == "oo"


def test_substringData_2():
    tn = doc.createTextNode(FOO)
    assert tn.substringData(0, 3) == "foo"


def test_substringData_unchanged():
    tn = doc.createTextNode(FOO)
    tn.substringData(1, 2)
    assert tn.nodeValue == FOO


# ------------------------------------------------------------------ #
# 3.  appendData / insertData                                         #
# ------------------------------------------------------------------ #

def test_appendData():
    tn = doc.createTextNode(FOO)
    tn.appendData(FOO)
    assert tn.nodeValue == FOO + FOO


def test_insertData_middle():
    # After appendData(FOO), value is FOO+FOO = "foobarfoobar"
    # insertData(6, "FOO") inserts at position 6 -> "foobarFOOfoobar"
    tn = doc.createTextNode(FOO)
    tn.appendData(FOO)
    tn.insertData(6, "FOO")
    assert tn.nodeValue == FOO + "FOO" + FOO


def test_insertData_at_end():
    tn = doc.createTextNode(FOO)
    tn.insertData(6, "FOO")
    assert tn.nodeValue == FOO + "FOO"


def test_insertData_empty():
    tn = doc.createTextNode(FOO)
    tn.insertData(3, "")
    assert tn.nodeValue == FOO


# ------------------------------------------------------------------ #
# 4.  deleteData                                                      #
# ------------------------------------------------------------------ #

def test_deleteData_1():
    tn = doc.createTextNode(FOO)
    tn.deleteData(1, 2)
    assert tn.nodeValue == "fbar"


def test_deleteData_2():
    tn = doc.createTextNode(FOO)
    tn.deleteData(1, 10)
    assert tn.nodeValue == "f"


def test_deleteData_beyond():
    tn = doc.createTextNode(FOO)
    tn.deleteData(10, 1)
    assert tn.nodeValue == FOO


def test_deleteData_zero_len():
    tn = doc.createTextNode(FOO)
    tn.deleteData(1, 0)
    assert tn.nodeValue == FOO


def test_deleteData_zero_offset():
    tn = doc.createTextNode(FOO)
    tn.deleteData(0, 0)
    assert tn.nodeValue == FOO


def test_deleteData_from_start():
    tn = doc.createTextNode(FOO)
    tn.deleteData(0, 2)
    assert tn.nodeValue == "obar"


# ------------------------------------------------------------------ #
# 5.  replaceData                                                     #
# ------------------------------------------------------------------ #

def test_replaceData_1():
    tn = doc.createTextNode("test")
    tn.replaceData(1, 2, "phish")
    assert tn.nodeValue == "tphisht"


def test_replaceData_2():
    tn = doc.createTextNode("test")
    tn.replaceData(1, 4, "phish")
    assert tn.nodeValue == "tphish"


def test_replaceData_3():
    tn = doc.createTextNode("test")
    tn.replaceData(1, 0, "phish")
    assert tn.nodeValue == "tphishest"


# ------------------------------------------------------------------ #
# 6.  replaceDataString / deleteDataString                            #
# ------------------------------------------------------------------ #

def test_replaceDataString_1():
    tn = doc.createTextNode("test")
    tn.replaceDataString("es", "new")
    assert tn.nodeValue == "tnewt"


def test_deleteDataString_not_matching_dot():
    # '.' is literal, not regex
    tn = doc.createTextNode("hitpit")
    tn.deleteDataString("h.t")
    assert tn.nodeValue == "hitpit"


def test_deleteDataString_first():
    tn = doc.createTextNode("hitpithit")
    tn.deleteDataString("hit")
    assert tn.nodeValue == "pithit"


def test_deleteDataString_all():
    tn = doc.createTextNode("hitpithit")
    tn.deleteDataString("hit", 1)
    assert tn.nodeValue == "pit"


def test_getData():
    tn = doc.createTextNode("foo&amp;bar")
    assert tn.getData() == "foo&amp;bar"


# ------------------------------------------------------------------ #
# 7.  Standalone nodes                                                #
# ------------------------------------------------------------------ #

def test_standalone_text():
    node = Text("foo")
    assert node is not None
    assert node.nodeValue == "foo"


def test_cdata_string_value():
    node = CDATASection("test")
    assert node.string_value() == "test"


def test_cdata_nodeName():
    node = CDATASection("test")
    assert node.nodeName == "#cdata-section"


def test_comment_string_value():
    node = Comment("test")
    assert node.string_value() == "test"


def test_comment_nodeName():
    node = Comment("test")
    assert node.nodeName == "#comment"


def test_document_nodeName():
    node = Document()
    assert node.nodeName == "#document"


def test_documentFragment_nodeName():
    node = DocumentFragment()
    assert node.nodeName == "#document-fragment"

"""
test_20_extras.py — port of t/20extras.t

Tests serialization options and cloneNode.
"""
import pytest
from xml_libxml import Parser, Document


STRING = "<foo><bar/></foo>"


@pytest.fixture
def doc():
    return Parser().parse_string(STRING)


# ------------------------------------------------------------------ #
# 1.  skipXMLDeclaration                                              #
# ------------------------------------------------------------------ #

def test_toString_skip_xml_declaration(doc):
    result = doc.toString(skip_xml_declaration=True)
    assert result == STRING or result == STRING + "\n" or result.rstrip("\n") == STRING


def test_toString_default_has_declaration(doc):
    result = doc.toString()
    assert result.startswith("<?xml")


# ------------------------------------------------------------------ #
# 2.  setTagCompression (no empty tags)                              #
# ------------------------------------------------------------------ #

def test_toString_tag_compression(doc):
    result = doc.toString(skip_xml_declaration=True, tag_compression=True)
    assert "<bar></bar>" in result
    assert "<foo>" in result


# ------------------------------------------------------------------ #
# 3.  cloneNode                                                       #
# ------------------------------------------------------------------ #

def test_cloneNode_deep(doc):
    clone = doc.cloneNode(True)
    assert not clone.isSameNode(doc)
    assert clone.getDocumentElement() is not None
    assert doc.toString() == clone.toString()


def test_cloneNode_shallow(doc):
    clone = doc.cloneNode(False)
    assert not clone.isSameNode(doc)
    # Shallow clone of document has no document element
    assert clone.getDocumentElement() is None


def test_cloneNode_shallow_matches_after_unbind(doc):
    clone = doc.cloneNode(False)
    root = doc.getDocumentElement()
    root.unbindNode()
    # After unbinding the root, doc and clone should both lack a document element
    assert doc.toString() == clone.toString()

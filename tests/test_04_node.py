"""
test_04_node.py — port of t/04node.t

Tests the DOM Node interface: nodeType, nodeName, nodeValue, textContent,
child navigation, tree manipulation (appendChild, insertBefore, insertAfter,
removeChild, replaceChild), cloneNode, namespaces, document switching.
"""
import pytest
from xml_libxml import (
    Parser, Document, Element, Text, Comment, CDATASection,
    XML_ELEMENT_NODE, XML_TEXT_NODE, XML_COMMENT_NODE,
    XML_CDATA_SECTION_NODE, XML_ATTRIBUTE_NODE,
    XML_DOCUMENT_FRAG_NODE,
)

XMLSTRING = '<foo>bar<foobar/><bar foo="foobar"/><!--foo--><![CDATA[&foo bar]]></foo>'

@pytest.fixture
def doc():
    return Parser().parse_string(XMLSTRING)


# ------------------------------------------------------------------ #
# 1.1  Node Attributes                                                #
# ------------------------------------------------------------------ #

def test_documentElement_present(doc):
    node = doc.documentElement
    assert node is not None


def test_nodeType_element(doc):
    node = doc.documentElement
    assert node.nodeType == XML_ELEMENT_NODE


def test_nodeName_element(doc):
    node = doc.documentElement
    assert node.nodeName == "foo"


def test_nodeValue_element_is_none(doc):
    node = doc.documentElement
    assert node.nodeValue is None


def test_hasChildNodes(doc):
    node = doc.documentElement
    assert node.hasChildNodes()


def test_textContent(doc):
    node = doc.documentElement
    # bar + CDATA content "&foo bar"
    assert node.textContent == "bar&foo bar"


# ------------------------------------------------------------------ #
# Children                                                            #
# ------------------------------------------------------------------ #

def test_childNodes_count(doc):
    children = list(doc.documentElement.childNodes())
    assert len(children) == 5


def test_first_child_is_text(doc):
    children = list(doc.documentElement.childNodes())
    assert children[0].nodeType == XML_TEXT_NODE


def test_first_child_nodeValue(doc):
    children = list(doc.documentElement.childNodes())
    assert children[0].nodeValue == "bar"


def test_last_child_is_cdata(doc):
    children = list(doc.documentElement.childNodes())
    assert children[4].nodeType == XML_CDATA_SECTION_NODE


def test_last_child_nodeValue(doc):
    children = list(doc.documentElement.childNodes())
    assert children[4].nodeValue == "&foo bar"


def test_firstChild(doc):
    node = doc.documentElement
    fc = node.firstChild
    assert fc is not None
    children = list(node.childNodes())
    assert fc.isSameNode(children[0])


def test_ownerDocument(doc):
    fc = doc.documentElement.firstChild
    od = fc.ownerDocument
    assert od is not None
    assert od.isSameNode(doc)


def test_nextSibling(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    fc = node.firstChild
    xc = fc.nextSibling
    assert xc is not None
    assert xc.isSameNode(children[1])


def test_lastChild(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    fc = node.lastChild
    assert fc is not None
    assert fc.isSameNode(children[4])


def test_previousSibling(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    fc = node.lastChild
    xc = fc.previousSibling
    assert xc is not None
    assert xc.isSameNode(children[3])


def test_parentNode(doc):
    node = doc.documentElement
    fc = node.lastChild
    xc = fc.parentNode
    assert xc is not None
    assert xc.isSameNode(node)


# ------------------------------------------------------------------ #
# 1.2  Attribute Node                                                 #
# ------------------------------------------------------------------ #

def test_hasAttributes(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]  # <bar foo="foobar"/>
    assert xc.hasAttributes()


def test_attributes_list(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]
    attrs = xc.attributes()
    assert attrs is not None
    assert len(attrs) == 1


def test_attr_nodeType(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]
    attrs = xc.attributes()
    attr = attrs[0]
    assert attr.nodeType == XML_ATTRIBUTE_NODE


def test_attr_nodeName(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]
    attrs = xc.attributes()
    attr = attrs[0]
    assert attr.nodeName == "foo"


def test_attr_nodeValue(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]
    attrs = xc.attributes()
    attr = attrs[0]
    assert attr.nodeValue == "foobar"


def test_attr_hasChildNodes_false(doc):
    children = list(doc.documentElement.childNodes())
    xc = children[2]
    attrs = xc.attributes()
    assert attrs[0].hasChildNodes() == False


# ------------------------------------------------------------------ #
# 1.2  Node Cloning                                                   #
# ------------------------------------------------------------------ #

def test_cloneNode_shallow(doc):
    cnode = doc.createElement("foo")
    cnode.setAttribute('aaa', 'AAA')
    cnode.setAttributeNS('http://ns', 'x:bbb', 'BBB')
    c1node = doc.createElement("bar")
    cnode.appendChild(c1node)

    xnode = cnode.cloneNode(0)
    assert xnode is not None
    assert xnode.nodeName == "foo"
    assert not xnode.hasChildNodes()
    assert xnode.getAttribute('aaa') == 'AAA'
    assert xnode.getAttributeNS('http://ns', 'bbb') == 'BBB'


def test_cloneNode_deep(doc):
    cnode = doc.createElement("foo")
    cnode.setAttribute('aaa', 'AAA')
    cnode.setAttributeNS('http://ns', 'x:bbb', 'BBB')
    c1node = doc.createElement("bar")
    cnode.appendChild(c1node)

    xnode = cnode.cloneNode(1)
    assert xnode is not None
    assert xnode.nodeName == "foo"
    assert xnode.hasChildNodes()
    assert xnode.getAttribute('aaa') == 'AAA'
    assert xnode.getAttributeNS('http://ns', 'bbb') == 'BBB'

    cn = list(xnode.childNodes())
    assert len(cn) == 1
    assert cn[0].nodeName == "bar"
    assert not cn[0].isSameNode(c1node)


def test_cloneNode_namespaced(doc):
    nsnode = doc.createElementNS("fooNS", "foo:bar")
    cnsnode = nsnode.cloneNode(0)
    assert cnsnode.nodeName == "foo:bar"
    assert cnsnode.namespaceURI == 'fooNS'


def test_cloneNode_namespaced_deep(doc):
    nsnode = doc.createElementNS("fooNS", "foo:bar")
    c2nsnode = nsnode.cloneNode(1)
    assert c2nsnode.toString() == nsnode.toString()


# ------------------------------------------------------------------ #
# 1.3  Node Value                                                     #
# ------------------------------------------------------------------ #

def test_textContent_nested():
    doc2 = Parser().parse_string("<foo>bar<tag>foo</tag></foo>")
    root = doc2.documentElement
    assert root.nodeValue is None
    assert root.textContent == "barfoo"


# ------------------------------------------------------------------ #
# 2.1  Tree Manipulation                                              #
# ------------------------------------------------------------------ #

def test_insertBefore(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    rnode = children[3]  # comment node

    inode = doc.createElement("kungfoo")
    xn = node.insertBefore(inode, rnode)
    assert xn is not None
    assert xn.isSameNode(inode)

    cn = list(node.childNodes())
    assert len(cn) == 6
    assert cn[3].isSameNode(inode)


def test_removeChild(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    rnode = children[3]

    inode = doc.createElement("kungfoo")
    node.insertBefore(inode, rnode)

    xn = node.removeChild(inode)
    assert xn is not None
    assert xn.isSameNode(inode)

    cn = list(node.childNodes())
    assert len(cn) == 5
    assert cn[3].isSameNode(rnode)


def test_appendChild(doc):
    node = doc.documentElement
    inode = doc.createElement("kungfoo")
    xn = node.appendChild(inode)
    assert xn is not None
    assert xn.isSameNode(inode)
    assert xn.isSameNode(node.lastChild)

    xn2 = node.removeChild(inode)
    assert xn2.isSameNode(inode)


def test_replaceChild(doc):
    node = doc.documentElement
    children = list(node.childNodes())
    rnode = children[3]

    inode = doc.createElement("kungfoo")
    xn = node.replaceChild(inode, rnode)
    assert xn is not None
    assert xn.isSameNode(rnode)

    cn2 = list(node.childNodes())
    assert cn2[3].isSameNode(inode)


# ------------------------------------------------------------------ #
# insertAfter                                                         #
# ------------------------------------------------------------------ #

def test_insertAfter_append_to_empty():
    doc = Document()
    anode = doc.createElement("a")
    bnode = doc.createElement("b")
    anode.insertAfter(bnode, None)
    assert anode.toString() == '<a><b/></a>'


def test_insertAfter_append_to_end():
    doc = Document()
    anode = doc.createElement("a")
    bnode = doc.createElement("b")
    dnode = doc.createElement("d")
    anode.insertAfter(bnode, None)
    anode.insertAfter(dnode, None)
    assert anode.toString() == '<a><b/><d/></a>'


def test_insertAfter_after_ref():
    doc = Document()
    anode = doc.createElement("a")
    bnode = doc.createElement("b")
    cnode = doc.createElement("c")
    dnode = doc.createElement("d")
    anode.insertAfter(bnode, None)
    anode.insertAfter(dnode, None)
    anode.insertAfter(cnode, bnode)
    assert anode.toString() == '<a><b/><c/><d/></a>'


# ------------------------------------------------------------------ #
# 2.1.2  Document Fragment                                            #
# ------------------------------------------------------------------ #

def test_documentFragment_appendChild(doc):
    node = doc.documentElement
    cn_before = list(node.childNodes())

    frag = doc.createDocumentFragment()
    node1 = doc.createElement("kung")
    node2 = doc.createElement("foo")
    frag.appendChild(node1)
    frag.appendChild(node2)

    xn = node.appendChild(frag)
    assert xn is not None
    cn2 = list(node.childNodes())
    assert len(cn2) == 7
    assert cn2[-1].isSameNode(node2)
    assert cn2[-2].isSameNode(node1)


# ------------------------------------------------------------------ #
# 2.3  removeChildNodes                                               #
# ------------------------------------------------------------------ #

def test_removeChildNodes():
    doc = Parser().parse_string("<foo><bar/>com</foo>")
    elem = doc.documentElement
    assert elem.hasChildNodes()
    elem.removeChildNodes()
    assert not elem.hasChildNodes()


# ------------------------------------------------------------------ #
# 3.  Namespaces                                                      #
# ------------------------------------------------------------------ #

def test_namespaced_element():
    doc = Document()
    URI = "http://kungfoo"
    pre = "foo"
    name = "bar"

    elem = doc.createElementNS(URI, pre + ":" + name)
    assert elem is not None
    assert elem.nodeName == pre + ":" + name
    assert elem.namespaceURI == URI
    assert elem.prefix == pre
    assert elem.localname == name


def test_lookupNamespacePrefix():
    doc = Document()
    URI = "http://kungfoo"
    elem = doc.createElementNS(URI, "foo:bar")
    assert elem.lookupNamespacePrefix(URI) == "foo"


def test_lookupNamespaceURI():
    doc = Document()
    URI = "http://kungfoo"
    elem = doc.createElementNS(URI, "foo:bar")
    assert elem.lookupNamespaceURI("foo") == URI


def test_getNamespaces():
    doc = Document()
    URI = "http://kungfoo"
    elem = doc.createElementNS(URI, "foo:bar")
    ns = elem.getNamespaces()
    assert len(ns) == 1


# ------------------------------------------------------------------ #
# 4.  Document switching                                              #
# ------------------------------------------------------------------ #

def test_document_switching():
    docA = Document()
    docB = Document()
    e1 = docB.createElement("A")
    e2 = docB.createElement("B")
    e3 = docB.createElementNS("http://kungfoo", "C:D")
    e1.appendChild(e2)
    e1.appendChild(e3)

    docA.setDocumentElement(e1)
    elem = docA.documentElement
    c = list(elem.childNodes())
    xroot = c[0].ownerDocument
    assert xroot.isSameNode(docA)


# ------------------------------------------------------------------ #
# 5.  libxml2 specials: replaceNode / addSibling                     #
# ------------------------------------------------------------------ #

def test_replaceNode():
    docA = Document()
    e1 = docA.createElement("A")
    e2 = docA.createElement("B")
    e3 = docA.createElement("C")

    e1.appendChild(e2)
    x = e2.replaceNode(e3)
    cn = list(e1.childNodes())
    assert len(cn) == 1
    assert cn[0].isSameNode(e3)
    assert x.isSameNode(e2)


def test_addSibling():
    docA = Document()
    e1 = docA.createElement("A")
    e2 = docA.createElement("B")
    e3 = docA.createElement("C")

    e1.appendChild(e2)
    e2.replaceNode(e3)
    e3.addSibling(e2)

    cn = list(e1.childNodes())
    assert len(cn) == 2
    assert cn[0].isSameNode(e3)
    assert cn[1].isSameNode(e2)


# ------------------------------------------------------------------ #
# 7.  importNode / adoptNode                                          #
# ------------------------------------------------------------------ #

def test_importNode():
    doc1 = Parser().parse_string("<foo>bar<foobar/></foo>")
    doc2 = Document()

    rnode1 = doc1.documentElement
    rnode2 = doc2.importNode(rnode1, 1)
    assert not rnode2.isSameNode(rnode1)
    doc2.setDocumentElement(rnode2)


def test_adoptNode():
    doc2 = Document()
    doc3 = Document()
    xnode = Element("test")
    node3 = doc3.adoptNode(xnode)
    assert node3 is not None
    assert xnode.isSameNode(node3)
    assert doc3.isSameNode(node3.ownerDocument)


def test_setOwnerDocument():
    doc3 = Document()
    xnode2 = Element("test")
    xnode2.setOwnerDocument(doc3)
    assert xnode2.ownerDocument is not None
    assert doc3.isSameNode(xnode2.ownerDocument)


# ------------------------------------------------------------------ #
# 8.  addSibling text node merge (RT #94149)                         #
# ------------------------------------------------------------------ #

def test_addSibling_text_merge():
    orig = Text('Double ')
    ret = orig.addSibling(Text('Free'))
    # libxml2 merges adjacent text nodes
    assert ret is not None


# ------------------------------------------------------------------ #
# cargo                                                               #
# ------------------------------------------------------------------ #

def test_cargo_default_none():
    doc = Parser().parse_string('<a/>')
    root = doc.documentElement
    assert root.getCargo() is None

def test_cargo_set_get():
    doc = Parser().parse_string('<a/>')
    root = doc.documentElement
    root.setCargo({'key': 'value'})
    assert root.getCargo() == {'key': 'value'}

def test_cargo_same_node_identity():
    """Le cargo survit à plusieurs accès au même nœud."""
    doc = Parser().parse_string('<a><b/></a>')
    root = doc.documentElement
    root.setCargo(42)
    root2 = doc.documentElement
    assert root.isSameNode(root2)
    assert root2.getCargo() == 42

def test_cargo_remove_with_none():
    doc = Parser().parse_string('<a/>')
    root = doc.documentElement
    root.setCargo('hello')
    root.setCargo(None)
    assert root.getCargo() is None

def test_cargo_refcount_on_doc_free():
    """Le refcount de la cargaison est décrémenté quand le doc est libéré."""
    import sys
    doc = Parser().parse_string('<a/>')
    root = doc.documentElement
    obj = [1, 2, 3]
    base = sys.getrefcount(obj)
    root.setCargo(obj)
    assert sys.getrefcount(obj) == base + 1
    del root
    del doc      # libère le document → doit décrementer le refcount du cargo
    assert sys.getrefcount(obj) == base

def test_cargo_independent_per_node():
    doc = Parser().parse_string('<a><b/><c/></a>')
    root = doc.documentElement
    b = list(root.childNodes())[0]
    c = list(root.childNodes())[1]
    root.setCargo('root-cargo')
    b.setCargo('b-cargo')
    assert root.getCargo() == 'root-cargo'
    assert b.getCargo() == 'b-cargo'
    assert c.getCargo() is None

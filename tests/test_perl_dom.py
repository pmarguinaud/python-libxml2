"""
The parts of t/04node.t, t/05text.t, t/06elements.t, t/10ns.t and the rt tests of
the same area that test_04, test_05, test_06 and test_10 do not cover, in the
order of the Perl files.

NamedNodeMap, nonBlank navigation and replaceDataRegEx are in test_perl_doc.py.
"""
import pytest

from xml_libxml import (Attr, CDATASection, Comment, Document, DocumentFragment,
                        Element, Error, Namespace, Parser, Text, XML_ELEMENT_NODE,
                        XML_XMLNS_NS)

BAD_NAMES = ("1A", "<><", "&", "-:")
XMLNS = 'http://www.w3.org/2000/xmlns/'


def create_document(uri, qname):
    doc = Document()
    doc.setDocumentElement(doc.createElementNS(uri, qname))
    return doc


# ------------------------------------------------------------------ t/04node.t

NODE_XML = '<foo>bar<foobar/><bar foo="foobar"/><!--foo--><![CDATA[&foo bar]]></foo>'


@pytest.fixture
def node_doc():
    return Parser().parse_string(NODE_XML)


def test_node_values(node_doc):
    node = node_doc.documentElement
    assert node.nodeValue is None
    assert node.textContent == 'bar&foo bar'
    children = list(node.childNodes())
    assert children[0].nodeValue == 'bar'
    assert children[4].nodeValue == '&foo bar'


def test_clone_keeps_attributes():
    doc = Document()
    cnode = doc.createElement('foo')
    cnode.setAttribute('aaa', 'AAA')
    cnode.setAttributeNS('http://ns', 'x:bbb', 'BBB')
    c1node = doc.createElement('bar')
    cnode.appendChild(c1node)

    shallow = cnode.cloneNode(0)
    assert shallow.nodeName == 'foo'
    assert not shallow.hasChildNodes()
    assert shallow.getAttribute('aaa') == 'AAA'
    assert shallow.getAttributeNS('http://ns', 'bbb') == 'BBB'

    deep = cnode.cloneNode(1)
    assert deep.hasChildNodes()
    assert deep.getAttribute('aaa') == 'AAA'
    assert deep.getAttributeNS('http://ns', 'bbb') == 'BBB'
    children = list(deep.childNodes())
    assert len(children) == 1
    assert children[0].nodeName == 'bar'
    assert not children[0].isSameNode(c1node)


def test_clone_keeps_namespace():
    nsnode = Document().createElementNS('fooNS', 'foo:bar')
    copy = nsnode.cloneNode(0)
    assert copy.nodeName == 'foo:bar'
    assert copy.localNS() is not None
    assert copy.namespaceURI == 'fooNS'
    assert nsnode.cloneNode(1).toString() == nsnode.toString()


def test_insert_remove_replace(node_doc):
    doc = node_doc
    node = doc.documentElement
    rnode = list(node.childNodes())[3]
    inode = doc.createElement('kungfoo')
    jnode = doc.createElement('kungfoo')

    xn = node.insertBefore(inode, rnode)
    assert xn.isSameNode(inode)
    node.insertBefore(jnode, None)
    assert list(node.childNodes())[-1].isSameNode(jnode)
    jnode.unbindNode()

    cn = list(node.childNodes())
    assert len(cn) == 6
    assert cn[3].isSameNode(inode)

    assert node.removeChild(inode).isSameNode(inode)
    cn = list(node.childNodes())
    assert len(cn) == 5
    assert cn[3].isSameNode(rnode)

    xn = node.appendChild(inode)
    assert xn.isSameNode(inode)
    assert xn.isSameNode(node.lastChild)

    assert node.removeChild(inode).isSameNode(inode)
    assert cn[-1].isSameNode(node.lastChild)

    assert node.replaceChild(inode, rnode).isSameNode(rnode)
    assert list(node.childNodes())[3].isSameNode(inode)


def test_insertAfter():
    doc = Document()
    a, b, c, d = [doc.createElement(n) for n in 'abcd']
    a.insertAfter(b, None)
    assert a.toString() == '<a><b/></a>'
    a.insertAfter(d, None)
    assert a.toString() == '<a><b/><d/></a>'
    a.insertAfter(c, b)
    assert a.toString() == '<a><b/><c/><d/></a>'


def test_insertBefore_on_detached_element():
    doc = Document()
    inode, jnode = doc.createElement('kungfoo'), doc.createElement('foobar')
    assert inode.insertBefore(jnode, None).isSameNode(jnode)


def test_fragments(node_doc):
    doc = node_doc
    node = doc.documentElement
    cn = list(node.childNodes())
    frag = doc.createDocumentFragment()
    node1, node2 = doc.createElement('kung'), doc.createElement('foo')
    frag.appendChild(node1)
    frag.appendChild(node2)

    assert node.appendChild(frag) is not None
    children = list(node.childNodes())
    assert len(children) == 7
    assert children[-1].isSameNode(node2)
    assert children[-2].isSameNode(node1)

    frag.appendChild(node1)
    frag.appendChild(node2)
    assert len(list(node.childNodes())) == 5

    assert node.replaceChild(frag, cn[3]).isSameNode(cn[3])
    assert len(list(node.childNodes())) == 6

    frag.appendChild(node1)
    frag.appendChild(node2)
    assert node.insertBefore(frag, cn[0]) is not None
    assert node1.isSameNode(node.firstChild)
    assert len(list(node.childNodes())) == 6


def test_removeChildNodes():
    elem = Parser().parse_string('<foo><bar/>com</foo>').documentElement
    assert elem.hasChildNodes()
    elem.removeChildNodes()
    assert not elem.hasChildNodes()
    elem.toString()


def test_element_ns_basics():
    uri = 'http://kungfoo'
    elem = Document().createElementNS(uri, 'foo:bar')
    assert elem.nodeName == 'foo:bar'
    assert elem.namespaceURI == uri
    assert elem.prefix == 'foo'
    assert elem.localname == 'bar'
    assert elem.lookupNamespacePrefix(uri) == 'foo'
    assert elem.lookupNamespaceURI('foo') == uri
    assert len(elem.getNamespaces()) == 1


def test_root_from_other_document():
    doc_a = Document()
    doc_b = Document()
    e1 = doc_b.createElement('A')
    e1.appendChild(doc_b.createElement('B'))
    e1.appendChild(doc_b.createElementNS('http://kungfoo', 'C:D'))
    doc_a.setDocumentElement(e1)
    del doc_b, e1
    children = list(doc_a.documentElement.childNodes())
    assert children[0].ownerDocument.isSameNode(doc_a)


def test_import_and_adopt():
    doc1 = Parser().parse_string('<foo>bar<foobar/></foo>')
    doc2 = Document()
    rnode1 = doc1.documentElement
    rnode2 = doc2.importNode(rnode1)
    assert not rnode2.isSameNode(rnode1)
    doc2.setDocumentElement(rnode2)
    assert rnode2.cloneNode(0).ownerDocument.isSameNode(doc2)

    xnode = Element('test')
    assert doc2.importNode(xnode).ownerDocument.isSameNode(doc2)

    doc3 = Document()
    assert doc3.adoptNode(xnode).isSameNode(xnode)
    assert doc3.isSameNode(xnode.ownerDocument)


def test_append_empty_fragment():
    doc = Document()
    assert doc.createElement('foo').appendChild(doc.createDocumentFragment()) is not None


def test_append_to_itself():
    doc = Document('1.0', 'UTF-8')
    schema = doc.createElement('sphinx:schema')
    with pytest.raises(Error, match='HIERARCHY_REQUEST_ERR'):
        schema.appendChild(schema)


def test_attribute_with_entity_reference():
    doc = Document('1.0', 'UTF-8')
    attr = doc.createAttribute('test', 'bar')
    attr.appendChild(doc.createEntityReference('foo'))
    attr.appendChild(doc.createTextNode('baz'))
    assert attr.toString() == ' test="bar&foo;baz"'


def test_addSibling_merges_text():
    assert Text('Double ').addSibling(Text('Free')).textContent == 'Double Free'


# ------------------------------------------------------------------ t/05text.t

@pytest.fixture
def text():
    return Document().createTextNode('foobar')


FOO = 'foobar'


def test_text_substring(text):
    assert text.nodeName == '#text'
    assert text.attributes() == []
    assert text.substringData(1, 2) == 'oo'
    assert text.substringData(0, 3) == 'foo'
    assert text.nodeValue == FOO


def test_text_insert(text):
    text.appendData(FOO)
    assert text.nodeValue == FOO + FOO
    text.insertData(6, 'FOO')
    assert text.nodeValue == FOO + 'FOO' + FOO
    text.setData(FOO)
    text.insertData(6, 'FOO')
    assert text.nodeValue == FOO + 'FOO'
    text.setData(FOO)
    text.insertData(3, '')
    assert text.nodeValue == FOO


def test_text_delete(text):
    text.deleteData(1, 2)
    assert text.nodeValue == 'fbar'
    for args, value in (((1, 10), 'f'), ((10, 1), FOO), ((1, 0), FOO), ((0, 0), FOO),
                        ((0, 2), 'obar')):
        text.setData(FOO)
        text.deleteData(*args)
        assert text.nodeValue == value


def test_text_replace(text):
    for args, value in (((1, 2), 'tphisht'), ((1, 4), 'tphish'), ((1, 0), 'tphishest')):
        text.setData('test')
        text.replaceData(args[0], args[1], 'phish')
        assert text.nodeValue == value


def test_text_replace_string(text):
    text.setData('test')
    text.replaceDataString('es', 'new')
    assert text.nodeValue == 'tnewt'
    text.setData('te?st')
    text.replaceDataString('e?s', 'ne\\w')
    assert text.nodeValue == 'tne\\wt'
    text.setData('h.thrt')
    text.replaceDataString('h.t', 'new', 1)
    assert text.nodeValue == 'newhrt'


def test_text_delete_string(text):
    text.setData('hitpit')
    text.deleteDataString('h.t')
    assert text.nodeValue == 'hitpit'
    text.setData('hitpithit')
    text.deleteDataString('hit')
    assert text.nodeValue == 'pithit'
    text.setData('hitpithit')
    text.deleteDataString('hit', 1)
    assert text.nodeValue == 'pit'


def test_text_data_is_literal(text):
    text.setData('foo&amp;bar')
    assert text.getData() == 'foo&amp;bar'


FOO_UTF8 = 'фooбar'
TEST_UTF8 = 'te\xdft'
PHISH = 'phⅠsh'


def test_utf8_text():
    doc = Document()
    assert doc.createTextNode(TEST_UTF8).nodeValue == TEST_UTF8

    node = doc.createTextNode(FOO_UTF8)
    assert node.substringData(1, 2) == 'oo'
    assert node.substringData(0, 3) == 'фoo'

    node.appendData(FOO_UTF8)
    assert node.nodeValue == FOO_UTF8 + FOO_UTF8
    ins = 'ФOO'
    node.insertData(6, ins)
    assert node.nodeValue == FOO_UTF8 + ins + FOO_UTF8
    node.setData(FOO_UTF8)
    node.insertData(6, ins)
    assert node.nodeValue == FOO_UTF8 + ins

    for args, value in (((1, 3), 'фar'), ((1, 10), 'ф'), ((6, 100), FOO_UTF8)):
        node.setData(FOO_UTF8)
        node.deleteData(*args)
        assert node.nodeValue == value

    for args, value in (((1, 2), 't' + PHISH + 't'), ((1, 4), 't' + PHISH),
                        ((1, 0), 't' + PHISH + 'e\xdft')):
        node.setData(TEST_UTF8)
        node.replaceData(args[0], args[1], PHISH)
        assert node.nodeValue == value

    new = 'n\U0001d522w'
    node.setData(TEST_UTF8)
    node.replaceDataString('e\xdf', new)
    assert node.nodeValue == 't' + new + 't'

    hit, pit = 'hi\U0001d54b', 'ℙit'
    node.setData(hit + pit + hit)
    node.deleteDataString(hit)
    assert node.nodeValue == pit + hit
    node.setData(hit + pit + hit)
    node.deleteDataString(hit, 1)
    assert node.nodeValue == pit


def test_text_constructors():
    assert Text('foo').nodeValue == 'foo'
    assert CDATASection('test').string_value() == 'test'
    assert CDATASection('test').nodeName == '#cdata-section'
    assert Comment('test').string_value() == 'test'
    assert Comment('test').nodeName == '#comment'
    assert Document().nodeName == '#document'
    assert DocumentFragment().nodeName == '#document-fragment'


# -------------------------------------------------------------- t/06elements.t

FOO_NS = 'http://foo'


@pytest.mark.parametrize('name', BAD_NAMES)
def test_bad_names(name):
    doc = Document()
    elem = doc.createElement('foo')
    with pytest.raises(Error):
        elem.setNodeName(name)
    with pytest.raises(Error):
        elem.setAttribute(name, 'X')
    with pytest.raises(Error):
        elem.setAttributeNS(None, name, 'X')


def test_attributes_on_document_element():
    doc = Document()
    elem = doc.createElement('foo')
    assert elem.tagName == 'foo'

    elem.setAttribute('A', 'a')
    assert elem.hasAttribute('A')
    assert elem.getAttribute('A') == 'a'
    attr = elem.getAttributeNode('A')
    assert attr.name == 'A'
    assert attr.value == 'a'

    elem.setAttribute('A', 'b')
    assert elem.getAttribute('A') == 'b'
    assert attr.value == 'b'

    attr2 = doc.createAttribute('B', 'a')
    elem.setAttributeNode(attr2)
    assert elem.hasAttribute('B')
    assert elem.getAttribute('B') == 'a'
    assert elem.getAttributeNode('B').isSameNode(attr2)

    elem.setAttribute('B', '')
    assert elem.hasAttribute('B')
    assert elem.getAttribute('B') == ''
    elem.setAttribute('C', '')
    assert elem.hasAttribute('C')
    assert elem.getAttribute('C') == ''

    elem.setAttributeNS(FOO_NS, 'x:foo', 'b')
    assert elem.hasAttributeNS(FOO_NS, 'foo')
    assert not elem.hasAttribute('foo')
    assert elem.hasAttribute('x:foo')

    tattr = elem.getAttributeNodeNS(FOO_NS, 'foo')
    assert tattr.name == 'foo'
    assert tattr.nodeName == 'x:foo'
    assert tattr.value == 'b'
    elem.removeAttributeNode(tattr)
    assert not elem.hasAttributeNS(FOO_NS, 'foo')

    elem.setAttributeNS('', 'foo', 'b')
    assert elem.hasAttribute('foo')
    tattr = elem.getAttributeNode('foo')
    assert tattr.name == 'foo'
    assert tattr.nodeName == 'foo'
    assert tattr.namespaceURI is None
    assert tattr.value == 'b'
    assert elem.hasAttributeNS(None, 'foo')
    assert elem.hasAttributeNS('', 'foo')
    elem.removeAttributeNode(tattr)
    assert not elem.hasAttributeNS('', 'foo')
    assert not elem.hasAttributeNS(None, 'foo')

    doc.setDocumentElement(doc.createElement('foo'))
    ns_attr = doc.createAttributeNS(FOO_NS + '.x', 'x:foo', 'bar')
    elem.setAttributeNodeNS(ns_attr)
    assert elem.hasAttributeNS(FOO_NS + '.x', 'foo')
    elem.removeAttributeNS(FOO_NS + '.x', 'foo')
    assert not elem.hasAttributeNS(FOO_NS + '.x', 'foo')

    elem.setAttributeNS(FOO_NS, 'x:A', 'b')
    elem.removeAttributeNS('', 'A')
    assert not elem.hasAttribute('A')
    assert elem.hasAttributeNS(FOO_NS, 'A')


def test_attribute_moves_to_documentless_element():
    elem = Element('foo')
    doc = Document()
    doc.setDocumentElement(doc.createElement('foo'))
    ns_attr = doc.createAttributeNS(FOO_NS, 'x:foo', 'bar')
    elem.setAttributeNodeNS(ns_attr)
    assert elem.hasAttributeNS(FOO_NS, 'foo')
    assert ns_attr.ownerDocument is None


def test_setAttributeNS_uses_existing_prefix():
    doc = Document()
    elem = doc.createElementNS('foo', 'root')
    doc.setDocumentElement(elem)
    elem.setNamespace('foo', 'bar')
    elem.setAttributeNS('foo', 'x:attr', 'test')
    elem.setAttributeNS(None, 'attr2', 'test')
    assert elem.getAttributeNS('foo', 'attr') == 'test'
    assert elem.getAttributeNS('', 'attr2') == 'test'
    elem.setAttributeNS('foo', 'attr2', 'bar')
    assert elem.getAttributeNS('foo', 'attr2') == 'bar'


@pytest.mark.parametrize('qname,uri', [
    ('q:x', ''), ('xmlns', 'urn:a'), ('xmlns:p', 'urn:a'), ('xml:a', 'urn:a'), ('a', XMLNS),
])
def test_setAttributeNS_namespace_errors(qname, uri):
    elem = Element('foo')
    with pytest.raises(Error, match='NAMESPACE ERROR'):
        elem.setAttributeNS(uri, qname, 'v')


@pytest.mark.parametrize('on_document', [False, True])
def test_normalize(on_document):
    doc = Document()
    texts = [doc.createTextNode('bar%d' % i) for i in (1, 2, 3)]
    elem = doc.createElement('foo')
    if on_document:
        doc.setDocumentElement(elem)
    elem.appendChild(doc.createElement('bar'))
    for t in texts:
        elem.appendChild(t)
    assert len(elem.childNodes()) == 4

    (doc if on_document else elem).normalize()
    assert len(elem.childNodes()) == 2
    assert texts[1].parentNode is None
    assert texts[2].parentNode is None


def test_appendText():
    doc = Document()
    elem = doc.createElement('foo')
    doc.setDocumentElement(elem)
    elem.appendText('foo')
    assert elem.string_value() == 'foo'
    elem.appendText('foo & this')
    assert elem.string_value() == 'foofoo & this'
    elem.appendTextChild('foo')
    elem.appendTextChild('foo', 'foo&bar')
    children = list(elem.childNodes())
    assert len(children) == 3
    assert not children[1].hasChildNodes()
    assert children[2].hasChildNodes()


DTD = """<!DOCTYPE root [
<!ELEMENT root EMPTY>
<!ATTLIST root fixed CDATA  #FIXED "foo">
<!ATTLIST root a:ns_fixed CDATA  #FIXED "ns_foo">
<!ATTLIST root name NMTOKEN #IMPLIED>
<!ENTITY ent "ENT">
]>
"""
URN = 'urn:xx'
XML_NONS = '<root foo="&quot;bar&ent;&quot;" xmlns:a="%s"/>' % URN
XML_NS = '<root xmlns="%s" xmlns:a="%s" foo="&quot;bar&ent;&quot;"/>' % (URN, URN)


@pytest.mark.parametrize('xml', [XML_NONS, XML_NS])
def test_dtd_attributes_without_defaults(xml):
    root = Parser(expand_entities=0).parse_string(DTD + xml).documentElement

    attr = root.getAttributeNode('foo')
    assert isinstance(attr, Attr)
    assert root.isSameNode(attr.ownerElement)
    assert attr.value == '"barENT"'
    assert attr.serializeContent() == '&quot;bar&ent;&quot;'
    assert attr.toString() == ' foo="&quot;bar&ent;&quot;"'

    attr = root.getAttributeNodeNS(None, 'foo')
    assert root.isSameNode(attr.ownerElement)
    assert attr.value == '"barENT"'

    assert root.getAttribute('fixed') == 'foo'
    assert root.getAttributeNS(URN, 'ns_fixed') == 'ns_foo'
    assert root.getAttribute('a:ns_fixed') == 'ns_foo'
    assert not root.hasAttribute('fixed')
    assert not root.hasAttributeNS(URN, 'ns_fixed')
    assert not root.hasAttribute('a:ns_fixed')

    for name in ('a:ns_fixed', 'fixed', 'name', 'baz'):
        assert root.getAttributeNode(name) is None
    for uri, name in ((URN, 'foo'), (URN, 'fixed'), (URN, 'ns_fixed'),
                      (None, 'fixed'), (None, 'name'), (None, 'baz')):
        assert root.getAttributeNodeNS(uri, name) is None


# --------------------------------------------------------------------- t/10ns.t

XML1 = """<a xmlns:b="http://whatever"
><x b:href="out.xml"
/><b:c/></a>
"""


def test_post_creation_namespace_setting():
    e1, e2, e3 = Element('foo'), Element('bar:foo'), Element('foo')
    e3.setAttribute('kung', 'foo')
    a = e3.getAttributeNode('kung')
    e1.appendChild(e2)
    e2.appendChild(e3)
    assert e2.setNamespace('http://kungfoo', 'bar')
    assert e2.nodeName == 'bar:foo'
    assert a.setNamespace('http://kungfoo', 'bar')
    assert a.nodeName == 'bar:kung'


def test_lossless_setting_with_setAttribute():
    doc = Document()
    root = doc.createElementNS('http://example.com', 'document')
    root.setAttribute('xmlns:xxx', 'http://example.com')
    root.setAttribute('xmlns:yyy', 'http://yonder.com')
    doc.setDocumentElement(root)
    out = root.toString()
    assert 'xmlns:xxx' in out and 'xmlns=' in out


@pytest.mark.parametrize('encoding', [None, 'UTF-8'])
def test_namespaced_attributes(encoding):
    doc = Parser().parse_string('<test xmlns:xxx="http://example.com"/>')
    if encoding:
        doc.setEncoding(encoding)
    root = doc.documentElement
    root.setAttribute('xxx:attr', 'value')
    assert root.getAttributeNode('xxx:attr') is not None
    assert root.getAttribute('xxx:attr') == 'value'
    assert root.getAttributeNodeNS('http://example.com', 'attr') is not None
    assert root.getAttributeNS('http://example.com', 'attr') == 'value'
    assert root.getAttributeNode('xxx:attr').getNamespaceURI() == 'http://example.com'


def test_changing_namespace_declarations():
    doc = Document()
    root = doc.createElementNS('http://example.com', 'document')
    root.setAttributeNS(XMLNS, 'xmlns:xxx', 'http://example.com')
    root.setAttribute('xmlns:yyy', 'http://yonder.com')
    doc.setDocumentElement(root)

    assert root.getAttribute('xmlns:xxx') == 'http://example.com'
    assert root.getAttributeNS(XMLNS, 'xmlns') == 'http://example.com'
    assert root.getAttribute('xmlns:yyy') == 'http://yonder.com'
    assert root.lookupNamespacePrefix('http://yonder.com') == 'yyy'
    assert root.lookupNamespaceURI('yyy') == 'http://yonder.com'

    assert root.setAttribute('xmlns:yyy', 'http://newyonder.com')
    assert root.getAttribute('xmlns:yyy') == 'http://newyonder.com'
    assert root.lookupNamespacePrefix('http://newyonder.com') == 'yyy'
    assert root.lookupNamespaceURI('yyy') == 'http://newyonder.com'

    root.setAttribute('xmlns', 'http://other.com')
    assert root.getAttribute('xmlns') == 'http://other.com'
    assert root.lookupNamespacePrefix('http://other.com') == ''
    assert root.lookupNamespaceURI('') == 'http://other.com'
    assert root.lookupNamespaceURI('foo') is None
    assert root.lookupNamespacePrefix('foo') is None
    assert root.getAttribute('xmlns:foo') is None

    assert root.setNamespaceDeclURI('yyy', 'http://changed.com')
    assert root.getAttribute('xmlns:yyy') == 'http://changed.com'
    assert root.lookupNamespaceURI('yyy') == 'http://changed.com'

    with pytest.raises(Error):
        root.setNamespaceDeclPrefix('yyy', 'xxx')   # prefix occupied
    with pytest.raises(Error):
        root.setNamespaceDeclPrefix('yyy', '')      # prefix occupied

    assert root.setNamespaceDeclPrefix('yyy', 'zzz')
    assert root.lookupNamespaceURI('yyy') is None
    assert root.lookupNamespaceURI('zzz') == 'http://changed.com'

    assert root.setNamespaceDeclURI('zzz', None)
    assert root.lookupNamespaceURI('zzz') is None
    assert 'xmlns:zzz' not in root.toString()

    assert root.setNamespaceDeclURI('', 'http://test')
    assert root.lookupNamespaceURI('') == 'http://test'
    assert root.getNamespaceURI() == 'http://test'

    assert root.setNamespaceDeclPrefix('', 'foo')
    assert root.lookupNamespaceURI('') is None
    assert root.lookupNamespaceURI('foo') == 'http://test'
    assert root.getNamespaceURI() == 'http://test'
    assert root.prefix == 'foo'

    assert root.setNamespaceDeclPrefix('foo', '')
    assert root.lookupNamespaceURI('foo') is None
    assert root.lookupNamespaceURI('') == 'http://test'
    assert root.lookupNamespaceURI(None) == 'http://test'
    assert root.getNamespaceURI() == 'http://test'
    assert root.prefix is None

    assert root.setNamespaceDeclURI('', None)
    assert root.lookupNamespaceURI('') is None
    assert root.getNamespaceURI() is None
    assert 'xmlns=' not in root.toString()

    root.setAttribute('xxx:attr', 'value')
    assert root.getAttributeNode('xxx:attr') is not None
    assert root.getAttribute('xxx:attr') == 'value'
    assert root.getAttributeNodeNS('http://example.com', 'attr') is not None
    assert root.getAttributeNS('http://example.com', 'attr') == 'value'
    assert root.getAttributeNode('xxx:attr').getNamespaceURI() == 'http://example.com'

    root.addNewChild('http://example.com', 'xxx:foo')
    assert root.setNamespaceDeclURI('xxx', None)
    assert root.lookupNamespaceURI('xxx') is None
    assert root.getNamespaceURI() is None
    assert root.firstChild.getNamespaceURI() is None
    assert root.prefix is None
    assert root.firstChild.prefix is None

    assert root.getAttributeNode('xxx:attr') is None
    assert root.getAttributeNodeNS('http://example.com', 'attr') is None
    assert root.getAttributeNode('attr') is not None
    assert root.getAttribute('attr') == 'value'
    assert root.getAttributeNodeNS(None, 'attr') is not None
    assert root.getAttributeNS(None, 'attr') == 'value'
    assert root.getAttributeNode('attr').getNamespaceURI() is None

    out = root.toString()
    assert 'xmlns=' not in out
    assert 'xmlns:xxx=' not in out
    assert '<foo' in out

    assert root.setNamespaceDeclPrefix('xxx', None)
    assert len(doc.findnodes('/document/foo')) == 1
    assert len(doc.findnodes('/document[foo]')) == 1
    assert len(doc.findnodes('/document[*]')) == 1
    assert len(doc.findnodes('/document[@attr and foo]')) == 1
    assert doc.findvalue('/document/@attr') == 'value'


def test_namespace_reconciliation():
    doc = create_document('http://default', 'root')
    root = doc.documentElement
    root.setNamespace('http://children', 'child', 0)

    n = doc.createElementNS('http://default', 'branch')
    root.appendChild(n)
    assert n.getAttribute('xmlns') is None

    a, b, c = [doc.createElementNS('http://children', 'child:' + x) for x in 'abc']
    n.appendChild(a)
    n.appendChild(b)
    n.appendChild(c)
    assert c.getAttribute('xmlns:child') is None
    c.setAttribute('xmlns:foo', 'http://children')
    assert c.getAttribute('xmlns:foo') == 'http://children'

    d = doc.createElementNS('http://other', 'branch')
    n.appendChild(d)
    assert d.getAttribute('xmlns') == 'http://other'

    doca = create_document('http://default/', 'root')
    doca.adoptNode(a)
    doca.adoptNode(b)
    doca.documentElement.appendChild(a)
    doca.documentElement.appendChild(b)
    assert a.getAttribute('xmlns:child') == 'http://children'
    assert b.getAttribute('xmlns:child') == 'http://children'

    doca = create_document('http://children', 'child:root')
    doca.adoptNode(a)
    doca.documentElement.appendChild(a)
    assert a.getAttribute('xmlns:child') is None
    doca.documentElement.removeChild(a)
    assert a.getAttribute('xmlns:child') == 'http://children'
    doca.documentElement.appendChild(a)
    assert a.getAttribute('xmlns:child') is None

    doc = Document()
    n = doc.createElement('didl')
    xsi = 'http://www.w3.org/2001/XMLSchema-instance'
    n.setAttribute('xmlns:xsi', xsi)
    a = doc.createElement('dc')
    a.setAttribute('xmlns:xsi', xsi)
    a.setAttribute('xsi:schemaLocation', 'loc')
    n.appendChild(a)
    assert a.getAttribute('xmlns:xsi') is None
    n.removeChild(a)
    assert a.getAttribute('xmlns:xsi') == xsi

    b = doc.createElement('foo')
    b.setAttribute('xsi:bar', 'bar')
    n.appendChild(b)
    n.removeChild(b)
    assert b.getAttribute('xmlns:xsi') is None

    attr = doca.createAttributeNS('http://children', 'child:attr', 'value')
    child = doca.documentElement.firstChild
    child.setAttributeNodeNS(attr)
    assert child.getAttribute('xmlns:child') is None

    attr = doca.createAttributeNS('http://other', 'other:attr', 'value')
    child.setAttributeNodeNS(attr)
    assert child.getAttribute('xmlns:other') is None
    assert doca.documentElement.getAttribute('xmlns:other') is not None


def test_empty_namespace():
    doc = Parser().load_xml(string=XML1)
    node = doc.find('/a/b:c')[0]
    assert node.setNamespace('')
    assert node.prefix is None
    assert node.namespaceURI is None
    assert node.getName() == 'c'

    attr = doc.find('/a/x/@b:href')[0]
    assert attr.setNamespace('', '')
    assert attr.prefix is None
    assert attr.namespaceURI is None
    assert attr.getName() == 'href'


def test_reconciliation_during_cross_document_adoption():
    src = Parser().load_xml(string='<root xmlns:ns1="http://ns1.example.com">'
                                   '<ns1:child ns1:attr="val"/></root>')
    dst = create_document('http://other.example.com', 'other:root')
    node = src.documentElement.firstChild
    dst.adoptNode(node)
    dst.documentElement.appendChild(node)
    assert node.namespaceURI == 'http://ns1.example.com'
    assert node.prefix == 'ns1'
    assert node.getAttributeNS('http://ns1.example.com', 'attr') == 'val'


def test_namespace_type():
    ns = Namespace('http://u', 'p')
    assert ns.declaredURI == 'http://u'
    assert ns.declaredPrefix == 'p'
    assert ns.nodeName == 'xmlns:p'
    assert ns.prefix == 'xmlns'
    assert ns.getNamespaceURI() == XML_XMLNS_NS
    assert ns.isEqualNode(Namespace('http://u', 'p'))
    assert not ns.isEqualNode(Namespace('http://u'))
    assert Namespace('http://u').nodeName == 'xmlns'
    assert Namespace('http://u').declaredPrefix is None


# ---------------------------------------------------------------- lookups in Perl

def test_lookups_follow_xpath_order():
    doc = Parser().parse_string(
        '<r xmlns:p="u" xmlns="d"><p:c/><c><p:c/></c>text<c/></r>')
    root = doc.documentElement
    assert len(doc.getElementsByTagName('c')) == 2
    assert len(doc.getElementsByTagName('p:c')) == 2
    assert len(doc.getElementsByTagName('*')) == 5
    assert len(root.getElementsByTagName('*')) == 4
    assert len(root.getElementsByLocalName('c')) == 4
    assert len(root.getElementsByTagNameNS('u', 'c')) == 2
    assert len(root.getElementsByTagNameNS('d', '*')) == 2
    assert len(root.getElementsByTagNameNS('*', 'c')) == 4
    assert len(doc.getElementsByTagNameNS('*', '*')) == 5
    assert len(root.getChildrenByTagName('c')) == 2
    assert len(root.getChildrenByTagName('*')) == 3
    assert len(root.getChildrenByTagName('#text')) == 1
    assert len(root.getChildrenByLocalName('c')) == 3
    assert len(root.getChildrenByTagNameNS('u', 'c')) == 1
    assert len(root.getChildrenByTagNameNS('*', 'c')) == 3


def test_lookup_names_are_not_xpath():
    doc = Parser().parse_string('<r><c/><e/></r>')
    assert len(doc.getElementsByTagName('c|e')) == 0
    assert len(doc.getElementsByTagName("c'")) == 0
    assert len(doc.documentElement.getElementsByLocalName("x']")) == 0


# ------------------------------------------------------------------- rt tests

def test_rt123379_setNamespace():
    doc = Document()
    elem = doc.createElement('D:element')
    assert elem.setAttribute('xmlns:D', 'attribute')
    doc.setDocumentElement(elem)
    out = doc.toString()
    Parser().parse_string(out)
    assert '<D:element xmlns:D="attribute"/>' in out


def test_rt69520_importing_nodes_ids():
    doc = Parser().load_xml(string='<root>\n<item xml:id="id1">item1</item>\n</root>')
    assert doc.getElementById('id1').textContent == 'item1'

    doc2 = Document('1.0', 'UTF-8')
    doc2.setDocumentElement(doc2.importNode(doc.documentElement))
    assert doc2.getElementById('id1').textContent == 'item1'


def test_well_balanced_chunk():
    root = Parser().parse_string('<a/>').documentElement
    root.appendWellBalancedChunk('<b>t</b>a &amp; b<c/>')
    assert root.toString() == '<a><b>t</b>a &amp; b<c/></a>'
    with pytest.raises(Error):
        root.appendWellBalancedChunk('<a>')


def test_children_of_document_are_elements_only_once():
    doc = Parser().parse_string('<a/>')
    assert doc.documentElement.nodeType == XML_ELEMENT_NODE
    assert doc.prefix is None
    assert doc.namespaceURI is None
    assert doc.localname is None
    assert doc.nodeValue is None


# ------------------------------------------------------------- xmlns="" (Phase 10)

def test_set_attribute_xmlns_empty_undeclares_the_default():
    # Perl ignores it: its text then puts a in u.
    doc = Document()
    root = doc.createElementNS('u', 'r')
    doc.setDocumentElement(root)
    a = doc.createElement('a')
    root.appendChild(a)
    assert root.toString() == '<r xmlns="u"><a/></r>'
    assert a.setAttribute('xmlns', '')
    assert root.toString() == '<r xmlns="u"><a xmlns=""/></r>'
    assert a.namespaceURI is None
    a.appendChild(doc.createElement('b'))
    reparsed = Parser().parse_string(root.toString()).documentElement
    assert [(e.nodeName, e.namespaceURI) for e in reparsed.findnodes('//*')] == [
        ('r', 'u'), ('a', None), ('b', None)]


def test_set_attribute_xmlns_empty_is_ignored_where_it_means_nothing():
    doc = Document()
    root = doc.createElementNS('u', 'r')
    doc.setDocumentElement(root)
    in_u = doc.createElementNS('u', 'x')
    root.appendChild(in_u)
    assert not in_u.setAttribute('xmlns', '')       # x is in the default namespace
    alone = Element('a')
    assert not alone.setAttribute('xmlns', '')      # no default namespace in scope
    assert root.toString() == '<r xmlns="u"><x/></r>'
    assert alone.toString() == '<a/>'


def test_xmlns_empty_moves_with_its_element():
    doc = Parser().parse_string('<r xmlns="u"><a xmlns=""><b/></a></r>')
    a = doc.documentElement.firstChild
    other = Parser().parse_string('<s xmlns="v"/>')
    other.documentElement.appendChild(other.adoptNode(a))
    assert other.documentElement.toString() == '<s xmlns="v"><a xmlns=""><b/></a></s>'
    assert [e.namespaceURI for e in other.findnodes('//*')] == ['v', None, None]

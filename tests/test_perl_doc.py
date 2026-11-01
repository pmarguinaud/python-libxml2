"""
t/01basic.t, t/03doc.t, t/07dtd.t, t/13dtd.t, t/24c14n.t, t/42common.t and
t/72destruction.t, in that order, then what Phase 5 took from t/04node.t, t/05text.t,
t/62overload.t and t/71overloads.t.

Left out of t/03doc.t: getAttribute(name, 1) and names given as encoded bytes, as
a Python str is text and has no encoding.
"""
import io
import os
import re

import pytest

import xml_libxml
from xml_libxml import (Attr, Common, Document, Dtd, Element, Error, Parser, Text,
                        XPathContext, createDocument, decodeFromUTF8, encodeToUTF8)
from xml_libxml.Common import (ELEMENT_NODE, XML_ATTRIBUTE_NODE, XML_CDATA_SECTION_NODE,
                               XML_COMMENT_NODE, XML_DOCUMENT_FRAG_NODE, XML_ELEMENT_DECL,
                               XML_ELEMENT_NODE, XML_ENTITY_DECL, XML_ENTITY_REF_NODE,
                               XML_PI_NODE, XML_TEXT_NODE)

EXAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'example')
BAD_NAMES = (";", "&", "<><", "/", "1A")
HTML_PUBLIC = "-//W3C//DTD XHTML 1.0 Transitional//EN"
HTML_SYSTEM = "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"


def example(name):
    return os.path.join(EXAMPLE, name)


def slurp(name):
    with open(example(name)) as fh:
        return fh.read()


def names(nodes):
    return ','.join(n.nodeName for n in nodes)


# ----------------------------------------------------------------- t/01basic.t

def test_runtime_version():
    runtime = re.match(r'\d+', xml_libxml.LIBXML_RUNTIME_VERSION).group()
    assert int(runtime) == xml_libxml.LIBXML_VERSION


# ------------------------------------------------------------------- t/03doc.t

def test_document_attributes():
    doc = Document.createDocument()
    assert doc.encoding is None
    assert doc.version == "1.0"
    assert doc.standalone == -1
    assert doc.URI is None
    assert doc.compression() == -1

    doc.setEncoding("iso-8859-1")
    assert doc.encoding == "iso-8859-1"
    doc.setVersion(12.5)
    assert doc.version == "12.5"
    doc.setStandalone(1)
    assert doc.standalone == 1
    doc.setBaseURI("localhost/here.xml")
    assert doc.URI == "localhost/here.xml"

    doc2 = Document.createDocument("1.1", "iso-8859-2")
    assert doc2.encoding == "iso-8859-2"
    assert doc2.version == "1.1"
    assert doc2.standalone == -1


def test_create_nodes():
    doc = Document()
    assert doc.createDocumentFragment().nodeType == XML_DOCUMENT_FRAG_NODE

    node = doc.createElementNS("http://kungfoo", "foo:bar")
    assert node.nodeType == XML_ELEMENT_NODE
    assert (node.nodeName, node.prefix, node.localname, node.namespaceURI) == \
        ("foo:bar", "foo", "bar", "http://kungfoo")

    for name in BAD_NAMES:
        with pytest.raises(Error):
            doc.createElement(name)
        with pytest.raises(Error):
            doc.createAttribute(name, "bar")
        with pytest.raises(Error):
            doc.createAttributeNS(None, name, "bar")

    assert doc.createTextNode("foo").nodeType == XML_TEXT_NODE
    comment = doc.createComment("foo")
    assert (comment.nodeType, comment.nodeValue, comment.toString()) == \
        (XML_COMMENT_NODE, "foo", "<!--foo-->")
    cdata = doc.createCDATASection("foo")
    assert (cdata.nodeType, cdata.nodeValue, cdata.toString()) == \
        (XML_CDATA_SECTION_NODE, "foo", "<![CDATA[foo]]>")


def test_create_attributes():
    doc = Document()
    attr = doc.createAttribute("foo", "bar")
    assert (attr.nodeType, attr.name, attr.value) == (XML_ATTRIBUTE_NODE, "foo", "bar")
    assert not attr.hasChildNodes()
    assert attr.firstChild.nodeType == XML_TEXT_NODE

    elem = doc.createElement('foo')
    elem.addChild(doc.createAttribute('attr', 'e & f'))
    assert elem.toString() == '<foo attr="e &amp; f"/>'
    elem.removeAttribute('attr')
    elem.addChild(doc.createAttributeNS(None, 'attr2', 'a & b'))
    assert elem.toString() == '<foo attr2="a &amp; b"/>'

    doc.setDocumentElement(doc.createElement("foo"))
    attr = doc.createAttributeNS("http://kungfoo", "kung:foo", "bar")
    assert (attr.nodeName, attr.name, attr.value) == ("kung:foo", "foo", "bar")
    attr.setValue('bar&amp;')
    assert attr.getValue() == 'bar&amp;'


def test_create_pi():
    doc = Document()
    pi = doc.createProcessingInstruction("foo", "bar")
    assert (pi.nodeType, pi.nodeName, pi.textContent, pi.getData()) == \
        (XML_PI_NODE, "foo", "bar", "bar")

    pi = doc.createProcessingInstruction("foo")
    assert not pi.textContent
    assert not pi.getData()
    pi.setData('bar&amp;')
    assert pi.getData() == 'bar&amp;'
    assert pi.textContent == 'bar&amp;'


def test_document_elements():
    doc = Document()
    node = doc.createElement("foo")
    doc.setDocumentElement(node)
    assert node.isSameNode(doc.documentElement)

    node2 = doc.createElement("bar")
    with pytest.raises(Error):
        doc.appendChild(node2)
    with pytest.raises(Error):
        doc.insertBefore(node2, node)
    assert len(doc.childNodes()) == 1

    doc.removeChild(node)
    assert len(doc.childNodes()) == 0
    for _ in range(2):
        doc.setDocumentElement(doc.createElement("x"))

    doc.setDocumentElement(node2)
    children = doc.childNodes()
    assert len(children) == 1 and children[0].isSameNode(node2)

    pi = doc.createProcessingInstruction("foo", "bar")
    doc.appendChild(pi)
    assert pi.isSameNode(doc.childNodes()[-1])
    pi.setData('bar="foo"')
    assert pi.textContent == 'bar="foo"'
    pi.setData(foo="foo")
    assert pi.textContent == 'foo="foo"'
    pi.setData('a?>b')
    assert pi.textContent == 'foo="foo"'


def test_insert_processing_instruction():
    doc = Parser().parse_string('<a/>')
    doc.insertProcessingInstruction('x', 'y')
    doc.insertPI('z')
    assert names(doc.childNodes()) == 'x,z,a'


def test_create_raw():
    doc = Document()
    assert doc.createRawElement('1A').nodeName == '1A'
    node = doc.createRawElementNS('urn:x', 'p:1A')
    assert (node.nodeName, node.namespaceURI) == ('p:1A', 'urn:x')


def test_create_document_dom_form():
    doc = createDocument('urn:x', 'p:r', Dtd.parse_string('<!ELEMENT r EMPTY>'))
    assert doc.documentElement.nodeName == 'p:r'
    assert doc.externalSubset is not None
    with pytest.raises(TypeError):
        createDocument('urn:x', 'p:r', 'no dtd')


def check_foo(doc):
    assert doc.documentElement.nodeName == "foo"
    assert doc.documentElement.textContent == "bar"


def test_to_fh(tmp_path):
    doc = Parser().parse_string("<foo>bar</foo>")
    path = str(tmp_path / 'testrun.xml')

    with open(path, 'wb') as fh:
        doc.toFH(fh)
    check_foo(Parser().parse_file(path))

    with open(path, 'w') as fh:
        doc.toFH(fh)
    check_foo(Parser().parse_file(path))

    text = io.StringIO()
    doc.toFH(text, 1)
    assert '<foo>bar</foo>' in text.getvalue()


def test_to_file(tmp_path):
    doc = Parser().parse_string("<foo>bar</foo>")
    doc.toFile(str(tmp_path / 'a.xml'))
    check_foo(Parser().parse_file(str(tmp_path / 'a.xml')))
    doc.toFile(tmp_path / 'b.xml')
    check_foo(Parser().parse_file(str(tmp_path / 'b.xml')))


def test_compression(tmp_path):
    doc = Parser().parse_string("<foo>" + "bar" * 1000 + "</foo>")
    doc.setCompression(9)
    assert doc.compression() == 9
    doc.toFile(str(tmp_path / 'a.xml'))
    assert os.path.getsize(str(tmp_path / 'a.xml')) < 1000


def count(nodes, want):
    assert len(nodes) == want


def test_element_like_functions():
    doc = Parser().parse_string("<A><A><B/></A><A><B/></A></A>")
    count(doc.getElementsByTagName('A'), 3)
    count(doc.getElementsByTagName('*'), 5)
    count(doc.getElementsByTagNameNS('*', 'B'), 2)
    count(doc.getElementsByLocalName('A'), 3)
    count(doc.getElementsByLocalName('*'), 5)

    doc = Parser().parse_string(
        '<C:A xmlns:C="xml://D"><C:A><C:B/></C:A><C:A><C:B/></C:A></C:A>')
    count(doc.getElementsByTagName('C:A'), 3)
    count(doc.getElementsByTagNameNS('xml://D', 'A'), 3)
    count(doc.getElementsByTagNameNS('*', 'A'), 3)
    count(doc.getElementsByLocalName('A'), 3)

    doc = Parser().parse_string('<A xmlns="xml://D"><A><B/></A><A><B/></A></A>')
    count(doc.getElementsByTagNameNS('xml://D', 'A'), 3)
    count(doc.getElementsByLocalName('A'), 3)

    doc = Parser().parse_string(
        '<A xmlns:C="xml://D"><C:A>foo<A/>bar</C:A><A><C:B/>X</A>baz</A>')
    count(doc.getElementsByTagName('C:A'), 1)
    count(doc.getElementsByTagName('A'), 3)
    count(doc.getElementsByTagNameNS('*', 'A'), 4)
    count(doc.getElementsByTagNameNS('*', '*'), 5)
    count(doc.getElementsByTagNameNS('xml://D', '*'), 2)

    root = doc.getDocumentElement()
    count(root.getChildrenByTagName('A'), 1)
    count(root.getChildrenByTagName('C:A'), 1)
    count(root.getChildrenByTagName('C:B'), 0)
    count(root.getChildrenByTagName('*'), 2)
    count(root.getChildrenByTagNameNS('*', 'A'), 2)
    count(root.getChildrenByTagNameNS('xml://D', '*'), 1)
    count(root.getChildrenByTagNameNS('*', '*'), 2)
    count(root.getChildrenByLocalName('A'), 2)


def test_nodes_outlive_document():
    doc = createDocument()
    pi = doc.createPI("foo", "bar")
    del doc
    assert pi.nodeName == 'foo'

    doc = createDocument()
    attr = doc.createAttribute("foo", "bar")
    del doc
    assert attr.value == 'bar'

    doc = Parser().parse_string('<foo xmlns:x="http://foo.bar"/>')
    attr = doc.createAttributeNS('http://foo.bar', 'x:foo', "bar")
    del doc
    assert attr.value == 'bar'


def test_rt_30610_no_owner_document():
    obj = Element('object')
    doc = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?>\n<lom/>')
    obj.appendChild(doc.getDocumentElement())
    assert obj.firstChild.ownerDocument is None


def test_set_encoding_to_none():
    dom = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?>\n<test/>\n')
    assert dom.getEncoding() == "UTF-8"
    dom.setEncoding()
    assert dom.getEncoding() is None
    assert dom.toString() == '<?xml version="1.0"?>\n<test/>\n'


@pytest.mark.parametrize('enc, codec', [('UTF-16', 'utf-16'), ('UTF-16LE', 'utf-16-le'),
                                        ('UTF-16BE', 'utf-16-be')])
def test_rt_33810_utf16(enc, codec):
    xml = '<?xml version="1.0" encoding="%s"?>\n<test foo="bar"/>\n' % enc
    dom = Parser().parse_string(xml.encode(codec))
    assert dom.getEncoding() == enc
    assert dom.actualEncoding == enc
    assert dom.getDocumentElement().getAttribute('foo') == 'bar'


def test_dom_names_of_document_properties():
    doc = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?><a/>')
    assert (doc.xmlVersion, doc.xmlEncoding, doc.xmlStandalone, doc.getVersion()) == \
        ('1.0', 'UTF-8', -2, '1.0')
    assert Document().actualEncoding == 'UTF-8'


# ------------------------------------------------------------------- t/07dtd.t

def test_external_subset():
    doc = Document()
    dtd = doc.createExternalSubset("html", HTML_PUBLIC, HTML_SYSTEM)
    assert dtd.isSameNode(doc.externalSubset)
    assert dtd.publicId == HTML_PUBLIC
    assert dtd.systemId == HTML_SYSTEM
    assert dtd.getName() == 'html'


def test_internal_subset():
    doc = Document()
    dtd = doc.createInternalSubset("html", HTML_PUBLIC, HTML_SYSTEM)
    assert dtd.isSameNode(doc.internalSubset)

    doc.setExternalSubset(dtd)
    assert doc.internalSubset is None
    assert dtd.isSameNode(doc.externalSubset)
    assert dtd.getPublicId() == HTML_PUBLIC
    assert dtd.getSystemId() == HTML_SYSTEM

    doc.setInternalSubset(dtd)
    assert doc.externalSubset is None
    assert dtd.isSameNode(doc.internalSubset)

    dtd2 = doc.createDTD("huhu", HTML_PUBLIC, HTML_SYSTEM)
    doc.setInternalSubset(dtd2)
    assert dtd.parentNode is None
    assert dtd2.isSameNode(doc.internalSubset)

    dtd3 = doc.removeInternalSubset()
    assert dtd3.isSameNode(dtd2)
    assert doc.internalSubset is None

    doc.setExternalSubset(dtd2)
    dtd3 = doc.removeExternalSubset()
    assert dtd3.isSameNode(dtd2)
    assert doc.externalSubset is None


def test_entity_reference_of_dtd():
    doc = Parser().parse_file(example('dtd.xml'))
    dtd = doc.internalSubset
    assert dtd.getName() == 'doc'
    assert dtd.publicId is None
    assert dtd.systemId is None

    entity = doc.createEntityReference("foo")
    assert entity.nodeType == XML_ENTITY_REF_NODE
    assert entity.hasChildNodes()
    assert entity.firstChild.nodeType == XML_ENTITY_DECL
    assert entity.firstChild.nodeValue == " test "
    assert entity.firstChild.previousSibling.nodeType == XML_ELEMENT_DECL

    assert isinstance(doc.internalSubset.cloneNode(1), Dtd)


def test_validate_with_parser():
    doc = Parser(validation=1, keep_blanks=1).parse_string("""<?xml version='1.0'?>
<!DOCTYPE test [
 <!ELEMENT test (#PCDATA)>
]>
<test>
</test>
""")
    assert doc.validate()
    assert doc.is_valid()


def test_no_load_of_external_dtd():
    xml = ('<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" '
           '"http://localhost/does_not_exist.dtd">\n'
           '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>foo</title></head>'
           '<body><p>bar</p></body></html>')
    assert Parser(validation=0, load_ext_dtd=0).parse_string(xml)


def test_bad_dtd():
    with pytest.raises(Error):
        Dtd("-//Foo//Test DTD 1.0//EN", example('bad.dtd'))
    with pytest.raises(Error):
        Dtd.parse_string(slurp('bad.dtd'))

    xml = '<!DOCTYPE test SYSTEM "%s">\n<test/>' % example('bad.dtd')
    assert Parser(load_ext_dtd=0, validation=0).parse_string(xml)
    with pytest.raises(Error):
        Parser(load_ext_dtd=1, validation=0).parse_string(xml)


def test_rt_71076_dtd_has_no_attributes():
    doc = Parser().parse_string("""<!DOCTYPE test [
 <!ELEMENT test (#PCDATA)>
 <!ATTLIST test
  attr CDATA #IMPLIED
 >
]>
<test>
</test>
""")
    assert not doc.internalSubset.hasAttributes()
    assert list(doc.internalSubset.attributes()) == []


@pytest.mark.parametrize('remove', [
    lambda doc, dtd: dtd.unbindNode(),
    lambda doc, dtd: doc.removeChild(dtd),
    lambda doc, dtd: doc.removeChildNodes(),
], ids=['unbindNode', 'removeChild', 'removeChildNodes'])
def test_remove_dtd(remove):
    doc = Parser().parse_file(example('dtd.xml'))
    remove(doc, doc.internalSubset)
    assert not doc.internalSubset


@pytest.mark.parametrize('insert', [
    lambda doc, dtd: doc.insertBefore(dtd, doc.internalSubset),
    lambda doc, dtd: doc.insertBefore(dtd, doc.documentElement),
    lambda doc, dtd: doc.insertAfter(dtd, doc.internalSubset),
    lambda doc, dtd: doc.insertAfter(dtd, doc.documentElement),
    lambda doc, dtd: doc.replaceChild(dtd, doc.internalSubset),
    lambda doc, dtd: doc.replaceChild(dtd, doc.documentElement),
    lambda doc, dtd: doc.internalSubset.replaceNode(dtd),
    lambda doc, dtd: doc.documentElement.replaceNode(dtd),
    lambda doc, dtd: doc.appendChild(dtd),
    lambda doc, dtd: doc.internalSubset.addSibling(dtd),
    lambda doc, dtd: doc.documentElement.addSibling(dtd),
], ids=['insertBefore_dtd', 'insertBefore_root', 'insertAfter_dtd', 'insertAfter_root',
        'replaceChild_dtd', 'replaceChild_root', 'replaceNode_dtd', 'replaceNode_root',
        'appendChild', 'addSibling_dtd', 'addSibling_root'])
def test_insert_dtd(insert):
    src_doc = Parser().parse_file(example('dtd.xml'))
    dtd = src_doc.internalSubset
    doc = Parser().parse_file(example('dtd.xml'))
    insert(doc, dtd)
    assert doc.internalSubset.isSameNode(dtd)
    assert dtd.ownerDocument.isSameNode(doc)
    assert src_doc.internalSubset is None
    assert '<!DOCTYPE' in doc.toString()


# ------------------------------------------------------------------- t/13dtd.t

def dtd_string():
    return slurp('test.dtd').replace('\r', '').rstrip('\r\n')


def test_dtd_from_system_id():
    dtd = Dtd('ignore', example('test.dtd'))
    text = dtd.toString().replace('\r', '')
    text = text.split('\n', 1)[1]
    text = re.sub(r'\n[^\n]*\Z', '', text)
    assert text == dtd_string()


def test_validate_with_dtd():
    dtd = Dtd.parse_string(dtd_string())
    xml = Parser().parse_file(example('article.xml'))
    assert xml.is_valid(dtd)
    assert xml.validate(dtd)


def test_validate_bad_document():
    dtd = Dtd.parse_string(dtd_string())
    xml = Parser().parse_file(example('article_bad.xml'))
    assert not xml.is_valid(dtd)
    with pytest.raises(Error):
        xml.validate(dtd)

    parser = Parser(load_ext_dtd=1, validation=1)
    with pytest.raises(Error):
        parser.parse_file(example('article_bad.xml'))
    with pytest.raises(Error):
        parser.parse_file(example('article_internal_bad.xml'))


def test_two_child_nodes():
    assert len(Parser().parse_file(example('dtd.xml')).getChildnodes()) == 2


def test_dtd_of_empty_names():
    with pytest.raises(Error):
        Dtd("", "")
    assert Dtd('', example('test.dtd'))


# ------------------------------------------------------------------ t/24c14n.t

@pytest.mark.parametrize('xml, plain, comments', [
    ("<a><b/> <c/> <!-- d --> </a>",
     "<a><b></b> <c></c>  </a>", "<a><b></b> <c></c> <!-- d --> </a>"),
    ('<a><b/><![CDATA[ >e&f<]]><!-- d --> </a>',
     '<a><b></b> &gt;e&amp;f&lt; </a>', '<a><b></b> &gt;e&amp;f&lt;<!-- d --> </a>'),
    ('<a a="foo"/>', '<a a="foo"></a>', None),
    ('<b:a xmlns:b="http://foo"/>', '<b:a xmlns:b="http://foo"></b:a>', None),
    ('<b:a xmlns:b="http://foo" xmlns:a="xml://bar"/>',
     '<b:a xmlns:a="xml://bar" xmlns:b="http://foo"></b:a>', None),
    ('<b:a xmlns:b="http://foo"><b:b xmlns:b="http://foo"/></b:a>',
     '<b:a xmlns:b="http://foo"><b:b></b:b></b:a>', None),
    ('<a xmlns="xml://foo"/>', '<a xmlns="xml://foo"></a>', None),
    ('<?xml version="1.0" encoding="iso-8859-1"?>\n<a><b/></a>\n', '<a><b></b></a>', None),
])
def test_c14n(xml, plain, comments):
    doc = Parser().parse_string(xml)
    assert doc.toStringC14N() == plain
    assert doc.toStringC14N(0) == plain
    if comments:
        assert doc.toStringC14N(1) == comments


def test_c14n_with_xpath():
    doc = Parser().parse_string('<a><b><c/><d/></b></a>')
    assert doc.toStringC14N(0, "//d") == '<d></d>'

    doc = Parser().parse_string('<a xmlns="http://foo/test#"><b><c/><d><e/></d></b></a>')
    root = doc.documentElement
    assert root.toStringC14N(0, "//*[local-name()='d']") == '<d></d>'
    d = doc.findnodes("//*[local-name()='d']")[0]
    assert d.toStringC14N() == '<d xmlns="http://foo/test#"><e></e></d>'
    assert doc.documentElement.firstChild.toStringC14N(0) == \
        '<b xmlns="http://foo/test#"><c></c><d><e></e></d></b>'


EC14N_1 = """<n0:local xmlns:n0="http://something.org" xmlns:n3="ftp://example.org">
  <n1:elem2 xmlns:n1="http://example.net" xml:lang="en">
     <n3:stuff xmlns:n3="ftp://example.org"/>
  </n1:elem2>
</n0:local>
"""
EC14N_2 = """<n2:pdu xmlns:n1="http://example.com"
           xmlns:n2="http://foo.example"
           xml:lang="fr"
           xml:space="preserve">
  <n1:elem2 xmlns:n1="http://example.net" xml:lang="en">
     <n3:stuff xmlns:n3="ftp://example.org"/>
  </n1:elem2>
</n2:pdu>
"""
EC14N_XPATH = "(//. | //@* | //namespace::*)[ancestor-or-self::*[name()='n1:elem2']]"
EC14N_RESULT = ('<n1:elem2 xmlns:n1="http://example.net" xml:lang="en">\n'
                '     <n3:stuff xmlns:n3="ftp://example.org"></n3:stuff>\n  </n1:elem2>')
EC14N_RESULT_N2 = ('<n1:elem2 xmlns:n1="http://example.net" xmlns:n2="http://foo.example" '
                   'xml:lang="en">\n'
                   '     <n3:stuff xmlns:n3="ftp://example.org"></n3:stuff>\n  </n1:elem2>')


def test_exclusive_c14n():
    doc1 = Parser().parse_string(EC14N_1)
    doc2 = Parser().parse_string(EC14N_2)
    assert doc1.toStringEC14N(0, EC14N_XPATH) == EC14N_RESULT
    assert doc2.toStringEC14N(0, EC14N_XPATH) == EC14N_RESULT
    assert doc1.toStringEC14N(0, EC14N_XPATH, []) == EC14N_RESULT
    assert doc2.toStringEC14N(0, EC14N_XPATH, []) == EC14N_RESULT
    assert doc2.toStringEC14N(0, EC14N_XPATH, ['n1', 'n3']) == EC14N_RESULT
    assert doc2.toStringEC14N(0, EC14N_XPATH, ['n0', 'n2']) == EC14N_RESULT_N2


def test_exclusive_c14n_with_context():
    doc = Parser().parse_string(
        '<e:e xmlns:e="urn:e" xmlns:soap="urn:soap" xmlns:w="urn:w" xmlns:u="urn:u">'
        '<w:id u:i="1">x</w:id></e:e>')
    xpc = XPathContext(doc)
    xpc.registerNs('x', 'urn:w')
    xpath = '(//. | //@* | //namespace::*)[ancestor-or-self::x:id]'
    xpath2 = ("(//. | //@* | //namespace::*)[ancestor-or-self::*"
              "[local-name()='id' and namespace-uri()='urn:w']]")
    expect = ('<w:id xmlns:soap="urn:soap" xmlns:u="urn:u" xmlns:w="urn:w" u:i="1">'
              'x</w:id>')
    assert doc.toStringEC14N(0, xpath2, ['soap']) == expect
    assert doc.toStringEC14N(0, xpath, xpc, ['soap']) == expect
    assert doc.toStringEC14N(0, xpath2, xpc, ['soap']) == expect
    with pytest.raises(Error):
        doc.toStringEC14N(0, xpath, ['a'], ['b'])


def test_c14n_details():
    doc = Parser().parse_string('<a><b x="1"/><!-- c --></a>')
    assert doc.toStringC14N_v1_1() == '<a><b x="1"></b></a>'
    assert doc.serialize_c14n(1) == '<a><b x="1"></b><!-- c --></a>'
    assert doc.serialize_exc_c14n() == '<a><b x="1"></b></a>'
    assert doc.documentElement.toStringC14N(xpath='') == '<a><b x="1"></b></a>'
    with pytest.raises(Error):
        doc.toStringC14N(0, 'string(/a)')
    with pytest.raises(Error):
        doc.toStringC14N(0, '///')


# ----------------------------------------------------------------- t/42common.t

def test_common():
    assert XML_ELEMENT_NODE == 1 and ELEMENT_NODE == 1
    assert decodeFromUTF8('iso-8859-1', encodeToUTF8('iso-8859-1', b't\xe4st')) == b't\xe4st'
    assert decodeFromUTF8('UTF-8', encodeToUTF8('UTF-8', b'test')) == b'test'
    for encoding in ('UTF-16', 'UTF-16BE', 'UTF-16LE'):
        assert len(decodeFromUTF8(encoding, encodeToUTF8('UTF-8', b'test'))) == 8
    assert Common.decodeFromUTF8 is decodeFromUTF8

    with pytest.raises(Error):
        encodeToUTF8("foo", b't\xe4st')

    assert encodeToUTF8('UTF-16', '') == ''
    assert encodeToUTF8('UTF-16', None) is None
    assert decodeFromUTF8('UTF-16', '') == b''
    assert decodeFromUTF8('UTF-16', None) is None


# ------------------------------------------------------------ t/04node.t, others

def test_named_node_map():
    doc = Parser().parse_string('<foo bar="foo"/>')
    root = doc.documentElement
    attributes = root.attributes()
    assert attributes.length() == 1
    assert isinstance(attributes, xml_libxml.NamedNodeMap)
    attr = attributes.getNamedItem("bar")
    assert (attr.nodeName, attr.nodeValue) == ("bar", "foo")
    assert attributes.getNamedItem("nope") is None
    assert attributes.item(0).isSameNode(attr)
    assert attributes.item(5) is None

    attributes.setNamedItem(doc.createAttribute("kung", "foo"))
    assert len(root.attributes()) == 2

    attributes.setNamedItem(doc.createAttributeNS("http://kungfoo", "x:kung", "foo"))
    assert len(root.attributes()) == 4       # with the namespace

    new_attr = doc.createAttributeNS("http://kungfoo", "x:kung", "bar")
    attributes.setNamedItem(new_attr)
    att = root.attributes()
    assert len(att) == 4
    assert att[2].isSameNode(new_attr)

    attributes.removeNamedItem("x:kung")
    assert len(root.attributes()) == 3
    assert attributes.length() == 3


NON_BLANK = """<r>
  <a/>
  <b/>
  <![CDATA[   ]]>
  <!-- foo -->
  <![CDATA[x]]>
  <foo/>
  <c/>
  text
</r>
"""


def test_non_blank():
    root = Parser().parse_string(NON_BLANK).documentElement
    assert names(root.nonBlankChildNodes()) == 'a,b,#comment,#cdata-section,foo,c,#text'
    first = root.firstNonBlankChild()
    assert first.nodeName == 'a'
    assert first.nextSibling.nodeName == '#text'
    assert first.previousNonBlankSibling() is None

    seen = [first]
    while seen[-1].nextNonBlankSibling() is not None:
        following = seen[-1].nextNonBlankSibling()
        assert following.previousNonBlankSibling().isSameNode(seen[-1])
        seen.append(following)
    assert names(seen) == 'a,b,#comment,#cdata-section,foo,c,#text'


def test_replace_data_regex():
    text = Text('tnewt')
    text.replaceDataRegEx('n(.)w', '$1s')
    assert text.nodeValue == 'tesst'[:1] + 'es' + 't'
    text.setData("blue phish, white phish, no phish")
    text.replaceDataRegEx('phish', 'test')
    assert text.nodeValue == "blue test, white phish, no phish"
    text.replaceDataRegEx('phish', 'test', 'g')
    assert text.nodeValue == "blue test, white test, no test"


def test_replace_data_regex_utf8():
    text = Text("tn\U0001D522wt")
    text.replaceDataRegEx('n(.)w', '$1s')
    assert text.nodeValue == "t\U0001D522st"


def test_attribute_hash():
    root = Parser().parse_string(
        '<root attr1="foo" xmlns:x="http://localhost/" x:attr2="bar"/>').documentElement
    attrs = root.getAttributeHash()
    assert 'attr1' in attrs and attrs['attr1'] == 'foo'
    attrs['attr1'] = 'bar'
    assert root.getAttribute('attr1') == 'bar'
    attrs['attr11'] = 'baz'
    assert root.getAttribute('attr11') == 'baz'
    del attrs['attr11']
    assert not root.hasAttribute('attr11')

    key = '{http://localhost/}attr2'
    assert key in attrs and attrs[key] == 'bar'
    attrs[key] = 'quux'
    assert root.getAttributeNS('http://localhost/', 'attr2') == 'quux'
    attrs['{http://localhost/}attr22'] = 'quuux'
    assert root.getAttributeNS('http://localhost/', 'attr22') == 'quuux'
    attrs['{http://localhost/another}attr22'] = 'xyzzy'
    assert root.getAttributeNS('http://localhost/another', 'attr22') == 'xyzzy'
    del attrs['{http://localhost/another}attr22']
    assert not root.hasAttributeNS('http://localhost/another', 'attr22')

    assert sorted(attrs) == ['attr1', '{http://localhost/}attr2', '{http://localhost/}attr22']
    assert len(attrs) == 3
    with pytest.raises(KeyError):
        attrs['nope']
    with pytest.raises(KeyError):
        del attrs['nope']
    assert root.toStringEC14N() == ('<root xmlns:x="http://localhost/" attr1="bar" '
                                    'x:attr2="quux" x:attr22="quuux"></root>')


def test_overloads():
    e1 = Element('test1')
    e1.setAttribute('attr', 'value1')
    e2 = Element('test2')
    e2.setAttribute('attr', 'value2')
    assert e1.getAttributeHash()['attr'] == 'value1'
    assert e2.getAttributeHash()['attr'] == 'value2'
    assert str(e1) == '<test1 attr="value1"/>'
    assert int(e1) > 1 and int(e1) != int(e2)


def test_aliases():
    doc = Parser().parse_string('<a x="1"><p:b xmlns:p="urn:p">t</p:b><c/></a>')
    root = doc.documentElement
    b = root.getFirstChild()
    assert b.nodeName == 'p:b' and b.isSameNode(root.firstChild)
    assert root.getLastChild().isSameNode(root.lastChild)
    assert b.getNextSibling().isSameNode(b.nextSibling)
    assert root.getLastChild().getPreviousSibling().isSameNode(b)
    assert b.getParentNode().isSameNode(root)
    assert b.getOwnerDocument().isSameNode(doc)
    assert b.getOwner().isSameNode(doc)
    assert b.ownerNode.isSameNode(doc)
    assert (b.getPrefix(), b.getLocalName(), b.getType()) == ('p', 'b', XML_ELEMENT_NODE)
    assert b.firstChild.getValue() == 't' and b.firstChild.getData() == 't'
    assert len(root.getChildNodes()) == 2 and len(root.getChildnodes()) == 2
    assert len(root.getAttributes()) == 1
    attr = root.getAttributeNode('x')
    assert attr.getOwnerElement().isSameNode(root)
    assert b.isEqual(root.firstChild) and not b.isEqual(root)
    assert root.isSupported('getFirstChild') and not root.isSupported('nothing')
    root.lastChild.unlink()
    b.unlinkNode()
    assert not root.hasChildNodes()
    b.setRawName('1b')
    assert b.nodeName == "p:1b"


def test_add_new_child_on_fragment():
    frag = Document().createDocumentFragment()
    child = frag.addNewChild('urn:x', 'p:c')
    assert child.namespaceURI == 'urn:x'
    assert frag.firstChild.isSameNode(child)


def test_entity_value():
    doc = Parser().parse_string('<!DOCTYPE d [<!ENTITY e "v">]><d/>')
    entity = doc.internalSubset.firstChild
    assert entity.nodeType == XML_ENTITY_DECL
    assert entity.nodeValue == 'v'

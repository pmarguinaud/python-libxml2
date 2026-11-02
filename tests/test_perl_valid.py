"""
t/21catalog.t, t/25relaxng.t, t/26schema.t, t/41xinclude.t and t/45regex.t, in that
order. Perl has no test file for Pattern outside the Reader: its tests are here too.
"""
import os

import pytest

from xml_libxml import (Document, Element, Error, Parser, Pattern, RegExp, RelaxNG,
                        Schema, XPathContext, load_catalog)

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')
EXAMPLE = os.path.join(HERE, '..', 'example')

RNG = os.path.join(DATA, 'relaxng', 'schema.rng')
BAD_RNG = os.path.join(DATA, 'relaxng', 'badschema.rng')
XSD = os.path.join(DATA, 'schema', 'schema.xsd')
BAD_XSD = os.path.join(DATA, 'schema', 'badschema.xsd')
XINCLUDE = os.path.join(DATA, 'xinclude', 'test.xml')


def slurp(path):
    with open(path) as fh:
        return fh.read()


# --------------------------------------------------------------- t/21catalog.t

CATALOG_DOC = """<!DOCTYPE article
  PUBLIC "-//Perl//XML LibXML V4.1.2//EN"
  "http://axkit.org/xml-libxml/test.dtd">
<article>
<pubData>Something here</pubData>
<pubArticleID>12345</pubArticleID>
<pubDate>2001-04-01</pubDate>
<pubName>XML.com</pubName>
<section>Foo</section>
<lead>Here's some leading text</lead>
<rest>And here is the rest...</rest>
</article>
"""


def test_catalog(monkeypatch):
    monkeypatch.chdir(EXAMPLE)
    # Loading the DTD needs the catalog, as the network is off.
    with pytest.raises(Error):
        Parser(load_ext_dtd=1).parse_string(CATALOG_DOC)
    doc = Parser(catalog='catalog.xml', load_ext_dtd=1, validation=1).parse_string(CATALOG_DOC)
    assert doc.documentElement.nodeName == 'article'
    assert doc.externalSubset is not None


def test_catalog_errors():
    with pytest.raises(Error):
        load_catalog('')
    with pytest.raises(Error):
        Parser(catalog='')


# --------------------------------------------------------------- t/25relaxng.t

def test_relaxng_from_file():
    assert RelaxNG(location=RNG)
    with pytest.raises(Error):
        RelaxNG(location=BAD_RNG)


def test_relaxng_from_string():
    assert RelaxNG(string=slurp(RNG))
    with pytest.raises(Error):
        RelaxNG(string=slurp(BAD_RNG))


def test_relaxng_from_document():
    doc = Parser().parse_file(RNG)
    before = doc.toString()
    assert RelaxNG(DOM=doc)
    assert doc.toString() == before
    with pytest.raises(Error):
        RelaxNG(DOM=Parser().parse_file(BAD_RNG))


def test_relaxng_validate():
    rng = RelaxNG(location=RNG)
    assert rng.validate(Parser().parse_file(os.path.join(DATA, 'relaxng', 'demo.xml'))) == 0
    with pytest.raises(Error):
        rng.validate(Parser().parse_file(os.path.join(DATA, 'relaxng', 'invaliddemo.xml')))


def test_relaxng_revalidate_modified_document():
    rng = RelaxNG(location=os.path.join(DATA, 'relaxng', 'demo4.rng'))
    doc = Parser().parse_string('<?xml version="1.0" encoding="UTF-8"?>\n<root/>\n')
    root = doc.documentElement
    bogus = doc.createElement('bogus-element')

    with pytest.raises(Error):
        rng.validate(doc)
    root.setAttribute('name', 'rootElem')
    rng.validate(doc)
    root.appendChild(bogus)
    with pytest.raises(Error):
        rng.validate(doc)
    bogus.unlinkNode()
    rng.validate(doc)
    root.removeAttribute('name')
    with pytest.raises(Error):
        rng.validate(doc)


def test_relaxng_no_network():
    with pytest.raises(Error, match='Attempt to load network entity'):
        RelaxNG(location=os.path.join(DATA, 'relaxng', 'net.rng'), no_network=1)
    with pytest.raises(Error, match='Attempt to load network entity'):
        RelaxNG(location=os.path.join(DATA, 'relaxng', 'net.rng'))
    with pytest.raises(Error, match='Attempt to load network entity'):
        RelaxNG(string="""<?xml version="1.0" encoding="iso-8859-1"?>
<grammar xmlns="http://relaxng.org/ns/structure/1.0" datatypeLibrary="http://www.w3.org/2001/XMLSchema-datatypes">
  <include href="http://example.com/xml.rng"/>
  <start>
    <ref name="include"/>
  </start>
  <define name="include">
    <element name="include">
      <text/>
    </element>
  </define>
</grammar>
""", no_network=1)


def test_relaxng_arguments():
    for kwds in ({}, {'location': RNG, 'string': 'x'}, {'DOM': 'x'}, {'nothing': 1}):
        with pytest.raises(TypeError):
            RelaxNG(**kwds)
    with pytest.raises(TypeError):
        RelaxNG(location=RNG).validate(Element('a'))


# ---------------------------------------------------------------- t/26schema.t

def test_schema_from_file():
    assert Schema(location=XSD)
    with pytest.raises(Error):
        Schema(location=BAD_XSD)


def test_schema_from_string():
    assert Schema(string=slurp(XSD))
    with pytest.raises(Error):
        Schema(string=slurp(BAD_XSD))


def test_schema_from_document():
    doc = Parser().parse_file(XSD)
    schema = Schema(DOM=doc)
    del doc
    assert schema.validate(Parser().parse_file(os.path.join(DATA, 'schema', 'demo.xml'))) == 0
    with pytest.raises(Error):
        Schema(DOM=Parser().parse_file(BAD_XSD))


def test_schema_validate():
    schema = Schema(location=XSD)
    assert schema.validate(Parser().parse_file(os.path.join(DATA, 'schema', 'demo.xml'))) == 0
    with pytest.raises(Error):
        schema.validate(Parser().parse_file(os.path.join(DATA, 'schema', 'invaliddemo.xml')))


SHIP_XSD = """<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
  <xs:element name="shiporder">
    <xs:complexType>
      <xs:sequence>
        <xs:element name="orderperson" type="xs:string"/>
        <xs:element ref="shipto"/>
      </xs:sequence>
      <xs:attribute name="orderid" type="xs:string" use="required"/>
    </xs:complexType>
  </xs:element>
  <xs:element name="shipto">
    <xs:complexType>
      <xs:sequence>
        <xs:element name="name" type="xs:string"/>
      </xs:sequence>
    </xs:complexType>
  </xs:element>
</xs:schema>
"""


def test_schema_validate_node():
    doc = Parser().load_xml(string="""<shiporder orderid="889923">
  <orderperson>John Smith</orderperson>
  <shipto>
    <name>Ola Nordmann</name>
  </shipto>
</shiporder>
""")
    schema = Schema(string=SHIP_XSD)
    shipto = doc.findnodes('/shiporder/shipto')[0]
    assert schema.validate(shipto) == 0

    shipto.unbindNode()
    assert schema.validate(shipto) == 0
    with pytest.raises(TypeError):
        schema.validate(doc.createTextNode('x'))
    with pytest.raises(TypeError):
        schema.validate('x')


def test_schema_no_network():
    with pytest.raises(Error, match='Attempt to load network entity'):
        Schema(location=os.path.join(DATA, 'schema', 'net.xsd'), no_network=1)
    with pytest.raises(Error, match='Attempt to load network entity'):
        Schema(string="""<?xml version="1.0" encoding="UTF-8"?>
<xsd:schema xmlns:xsd="http://www.w3.org/2001/XMLSchema">
  <xsd:import namespace="http://example.com/namespace" schemaLocation="http://example.com/xml.xsd"/>
</xsd:schema>
""", no_network=1)


# -------------------------------------------------------------- t/41xinclude.t

def test_no_expansion():
    doc = Parser(expand_xinclude=0, expand_entities=1).parse_file(XINCLUDE)
    assert 'IT WORKS' not in doc.toString()


def test_expansion_without_entities():
    doc = Parser(expand_xinclude=1, expand_entities=0).parse_file(XINCLUDE)
    assert 'IT WORKS' not in doc.toString()


def test_expansion_with_entities():
    doc = Parser(expand_xinclude=1, expand_entities=1).parse_file(XINCLUDE)
    assert 'IT WORKS' in doc.toString()


def test_process_xinclude_later():
    parser = Parser(expand_xinclude=0, expand_entities=1)
    doc = parser.parse_file(XINCLUDE)
    assert doc.process_xinclude({'expand_entities': 0})
    assert 'IT WORKS' not in doc.toString()

    doc = parser.parse_file(XINCLUDE)
    assert doc.process_xinclude({'expand_entities': 1})
    assert 'IT WORKS' in doc.toString()


def test_process_xincludes_of_parser():
    parser = Parser()
    doc = parser.parse_file(XINCLUDE)
    assert parser.process_xincludes(doc, {'expand_entities': 1})
    assert 'IT WORKS' in doc.toString()

    doc = parser.parse_file(XINCLUDE)
    assert parser.processXIncludes(doc, expand_entities=1)
    assert 'IT WORKS' in doc.toString()

    doc = parser.parse_string('<a/>')
    assert parser.process_xincludes(doc) == 1
    with pytest.raises(TypeError):
        parser.process_xincludes(Element('a'))
    with pytest.raises(TypeError):
        parser.process_xincludes(doc, 'options')


def test_xinclude_nodes_kept_or_not():
    names = lambda doc: [n.nodeType for n in doc.documentElement.childNodes()]
    doc = Parser().parse_file(XINCLUDE)
    doc.process_xinclude({'no_xinclude_nodes': 1})
    assert 19 not in names(doc)
    doc = Parser().parse_file(XINCLUDE)
    doc.process_xinclude()
    assert 19 in names(doc) and 20 in names(doc)


def test_xinclude_failure():
    xml = ('<a xmlns:xi="http://www.w3.org/2001/XInclude">'
           '<xi:include href="does-not-exist.xml"/></a>')
    with pytest.raises(Error):
        Parser(expand_xinclude=1).parse_string(xml)
    doc = Parser().parse_string(xml)
    with pytest.raises(Error):
        doc.process_xinclude()


def test_xinclude_fallback():
    doc = Parser(expand_xinclude=1, no_xinclude_nodes=1).parse_string(
        '<a xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="does-not-exist.xml">'
        '<xi:fallback><b/></xi:fallback></xi:include></a>')
    assert doc.documentElement.firstChild.nodeName == 'b'


def test_tree_with_xinclude_markers_moves():
    doc = Parser().parse_file(XINCLUDE)
    doc.process_xinclude()
    root = doc.documentElement
    before = root.toString()
    assert 19 in [n.nodeType for n in root.childNodes()]

    other = Parser().parse_string('<z/>')
    other.documentElement.appendChild(root)
    assert root.ownerDocument.isSameNode(other)
    assert root.toString() == before
    assert 19 not in [n.nodeType for n in root.childNodes()]


def test_wrapped_xinclude_survives():
    doc = Parser().parse_string(
        '<a xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="does-not-exist.xml">'
        '<xi:fallback><b x="1"/></xi:fallback></xi:include></a>')
    include = doc.documentElement.firstChild
    fallback = include.firstChild
    inner = fallback.firstChild
    attr = inner.getAttributeNode('x')
    doc.process_xinclude({'no_xinclude_nodes': 1})

    assert doc.documentElement.firstChild.nodeName == 'b'
    assert not doc.documentElement.firstChild.isSameNode(inner)
    assert include.parentNode is None
    assert include.namespaceURI == 'http://www.w3.org/2001/XInclude'
    assert fallback.parentNode.isSameNode(include)
    assert inner.toString() == '<b x="1"/>'
    assert attr.value == '1'


def test_xinclude_with_ext_ent_handler():
    seen = []

    def handler(url, ident):
        seen.append(url)
        return '<c/>'

    doc = Parser(ext_ent_handler=handler, expand_xinclude=1, no_xinclude_nodes=1).parse_string(
        '<a xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="urn:virtual"/></a>')
    assert doc.documentElement.firstChild.nodeName == 'c'
    assert seen == ['urn:virtual']


def test_xinclude_refused_in_xpath_callback():
    doc = Parser().parse_file(XINCLUDE)
    errors = []

    def callback():
        try:
            doc.process_xinclude()
        except Error as error:
            errors.append(error)
        return True

    xpc = XPathContext(doc)
    xpc.registerFunction('run', callback)
    xpc.findnodes('//*[run()]')
    assert errors


# ----------------------------------------------------------------- t/45regex.t

def test_regexp():
    regexp = RegExp('[0-9]{5}(-[0-9]{4})?')
    for text in ('00', '00-', '123456', ' 12345-1234', ' 12345-12345', '12345-1234 '):
        assert not regexp.matches(text)
    assert regexp.matches('12345')
    assert regexp.matches('12345-1234')
    assert regexp.isDeterministic()


def test_regexp_not_deterministic():
    assert not RegExp('(bc)|(bd)').isDeterministic()


def test_regexp_bad():
    with pytest.raises(Error):
        RegExp('[0-9]{5}(-[0-9]{4}?')


# ------------------------------------------------------------------- Pattern

def test_pattern():
    doc = Parser().parse_string(
        '<html xmlns="http://www.w3.org/1999/xhtml"><body><p/><div><div/></div></body></html>')
    xhtml = {'x': 'http://www.w3.org/1999/xhtml'}
    pattern = Pattern('/x:html/x:body//x:div', xhtml)
    divs = doc.getElementsByLocalName('div')
    assert len(divs) == 2 and all(pattern.matchesNode(div) for div in divs)
    assert not pattern.matchesNode(doc.getElementsByLocalName('p')[0])
    assert not pattern.matchesNode(doc)

    assert not Pattern('html').matchesNode(doc.documentElement)
    assert Pattern('x:html | x:p', xhtml).matchesNode(doc.documentElement)
    assert Pattern('.//x:*', xhtml).matchesNode(doc.documentElement)


def test_pattern_on_other_nodes():
    doc = Parser().parse_string('<a b="1">t</a>')
    root = doc.documentElement
    assert Pattern('a').matchesNode(root)
    assert not Pattern('a').matchesNode(root.firstChild)
    assert not Pattern('a').matchesNode(root.getAttributeNode('b'))
    assert Pattern('a').matchesNode(Element('a'))


def test_pattern_bad():
    for text in ('/a[1]', '', '//@'):
        with pytest.raises(Error):
            Pattern(text)
    with pytest.raises(TypeError):
        Pattern('a', 'x')
    with pytest.raises(TypeError):
        Pattern('a', {'p': 1})
    with pytest.raises(TypeError):
        Pattern('a').matchesNode('x')

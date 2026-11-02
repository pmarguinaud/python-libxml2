"""
t/40reader.t, t/40reader_mem_error.t, t/48_RH5_double_free_rt83779.t and
t/48_reader_undef_warning_on_empty_str_rt106830.t, then what makes the Reader safe:
the nodes it hands out, and the document that cannot change while it is read.
"""
import gc
import io
import os

import pytest

from xml_libxml import (Document, Error, Parser, Pattern, Reader, RelaxNG, Schema,
                        XPathContext)
from xml_libxml._reader import (XML_READER_TYPE_ELEMENT, XML_READER_TYPE_END_ELEMENT,
                                XML_READER_TYPE_SIGNIFICANT_WHITESPACE,
                                XML_READER_TYPE_TEXT)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'tests', 'data')
COUNTRIES = os.path.join(DATA, 'textReader', 'countries.xml')


@pytest.fixture(autouse=True)
def in_root(monkeypatch):
    monkeypatch.chdir(ROOT)


# ----------------------------------------------------------------- t/40reader.t

def test_reader_methods():
    reader = Reader(location=COUNTRIES, expand_entities=1)
    assert isinstance(reader, Reader)
    assert reader.read() == 1
    assert reader.byteConsumed() == os.path.getsize(COUNTRIES)
    assert reader.attributeCount() == 0
    assert reader.baseURI() == COUNTRIES
    assert reader.encoding() == 'UTF-8'
    assert reader.localName() == 'countries'
    assert reader.name() == 'countries'
    assert reader.prefix() is None
    assert reader.value() is None
    assert reader.xmlLang() is None
    assert reader.xmlVersion() == '1.0'
    for _ in range(3):
        reader.read()
    assert reader.name() == 'country'
    assert reader.depth() == 1
    assert reader.getAttribute("acronym") == "AL"
    assert reader.getAttributeNo(0) == "AL"
    assert reader.getAttributeNs("acronym", None) == "AL"
    assert reader.lineNumber() == 20
    assert reader.columnNumber() == 1
    assert reader.hasAttributes()
    assert not reader.hasValue()
    assert not reader.isDefault()
    assert not reader.isEmptyElement()
    assert not reader.isNamespaceDecl()
    assert not reader.isValid()
    assert reader.localName() == "country"
    assert reader.lookupNamespace(None) is None

    assert reader.moveToAttribute("acronym")
    assert reader.moveToAttributeNo(0)
    assert reader.moveToAttributeNs("acronym", None)
    assert reader.moveToElement()
    assert reader.moveToFirstAttribute()
    assert reader.moveToNextAttribute()
    assert reader.readAttributeValue()

    reader.moveToElement()
    assert reader.name() == "country"
    assert reader.namespaceURI() is None

    assert reader.nextSibling()
    assert reader.nodeType() == XML_READER_TYPE_SIGNIFICANT_WHITESPACE
    assert reader.prefix() is None
    assert reader.readInnerXml() == ""
    assert reader.readOuterXml() == "\n"
    assert reader.readState()
    assert reader.getParserProp('expand_entities') == 1
    assert reader.standalone()
    assert reader.value() == "\n"
    assert reader.xmlLang() is None
    assert reader.close()


@pytest.mark.parametrize('how', ['FD', 'IO'])
def test_fd_and_io(how):
    with open(COUNTRIES, 'rb') as fh:
        reader = Reader(**{how: fh, 'URI': COUNTRIES})
        reader.read()
        reader.read()
        assert reader.name() == "countries"
        for _ in range(3):
            reader.read()


def test_io_text_file():
    with open(COUNTRIES) as fh:
        reader = Reader(IO=fh)
        assert reader.finish()


def test_string():
    with open(COUNTRIES) as fh:
        text = fh.read()
    reader = Reader(string=text, URI=COUNTRIES)
    reader.read()
    reader.read()
    assert reader.name() == "countries"
    reader = Reader(string=text.encode('utf-8'))
    assert reader.finish()


def test_dom():
    dom = Parser().parse_file(COUNTRIES)
    reader = Reader(DOM=dom)
    reader.read()
    reader.read()
    assert reader.name() == "countries"
    assert reader.document().isSameNode(dom)


EXPAND = """<root>
  <AA foo="FOO"> text1 <inner/> </AA>
  <DD/><BB bar="BAR">text2<CC> xx </CC>foo<FF/> </BB>x
  <EE baz="BAZ"> xx <PP>preserved</PP> yy <XX>FOO</XX></EE>
  <a/>
  <b/>
  <x:ZZ xmlns:x="foo"/>
  <QQ/>
  <YY/>
</root>
"""


def test_expand_and_preserve():
    reader = Reader(string=EXPAND)
    reader.preservePattern('//PP')
    reader.preservePattern('//x:ZZ', {'x': "foo"})

    reader.nextElement()
    assert reader.name() == "root"
    reader.nextElement()
    node1 = reader.copyCurrentNode(1)
    assert node1.nodeName == "AA"
    reader.next()
    assert reader.nextElement("DD")
    assert reader.name() == "DD"
    assert reader.readOuterXml() == "<DD/>"
    assert reader.read()
    assert reader.name() == "BB"
    node2 = reader.copyCurrentNode(0)
    assert node2.nodeName == "BB"
    reader.nextElement()
    assert reader.name() == "CC"
    reader.nextSibling()
    assert reader.nodeType() == XML_READER_TYPE_TEXT
    assert reader.value() == "foo"
    reader.skipSiblings()
    assert reader.nodeType() == XML_READER_TYPE_END_ELEMENT
    reader.nextElement()
    assert reader.name() == "EE"
    assert reader.nextSiblingElement("ZZ", "foo")
    assert reader.namespaceURI() == "foo"
    reader.nextElement()
    node3 = reader.preserveNode()
    assert reader.readOuterXml() == node3.toString()
    reader.finish()
    doc = reader.document()
    assert doc.documentElement.toString() == \
        '<root><EE baz="BAZ"><PP>preserved</PP></EE><x:ZZ xmlns:x="foo"/><QQ/></root>'
    del reader
    gc.collect()

    assert node1.hasChildNodes()
    assert node1.toString() == '<AA foo="FOO"> text1 <inner/> </AA>'
    assert node2.firstChild is None
    assert node2.toString() == '<BB bar="BAR"/>'
    assert node3.toString() == '<QQ/>'
    assert node3.ownerDocument.isSameNode(doc)


BAD = """<root>
  <foo/>
  <x>
     foo
  </u>
  <x>
    foo
  </x>
</root>
"""


def test_error():
    reader = Reader(string=BAD, URI="mystring.xml")
    with pytest.raises(Error, match='mystring.xml:5:'):
        reader.finish()


@pytest.mark.parametrize('schema', ['path', 'object'])
def test_relaxng(schema):
    path = os.path.join(DATA, 'relaxng', 'demo.rng')
    rng = path if schema == 'path' else RelaxNG(location=path)
    assert Reader(location=os.path.join(DATA, 'relaxng', 'demo.xml'), RelaxNG=rng).finish()
    with pytest.raises(Error):
        Reader(location=os.path.join(DATA, 'relaxng', 'invaliddemo.xml'), RelaxNG=rng).finish()


@pytest.mark.parametrize('schema', ['path', 'object'])
def test_schema(schema):
    path = os.path.join(DATA, 'schema', 'schema.xsd')
    xsd = path if schema == 'path' else Schema(location=path)
    assert Reader(location=os.path.join(DATA, 'schema', 'demo.xml'), Schema=xsd).finish()
    with pytest.raises(Error):
        Reader(location=os.path.join(DATA, 'schema', 'invaliddemo.xml'), Schema=xsd).finish()


def test_schema_object_outlives_its_name():
    reader = Reader(location=os.path.join(DATA, 'schema', 'demo.xml'),
                    Schema=Schema(location=os.path.join(DATA, 'schema', 'schema.xsd')))
    gc.collect()
    assert reader.finish()


def test_patterns():
    # Perl checks these with ok(), which does not compare. The reader also stops at
    # the end of CC, and nodePath() writes the prefix of ZZ.
    pattern = Pattern('//inner|CC|/root/y:ZZ', {'y': 'foo'})
    expected = '/root/AA/inner,/root/BB/CC,/root/BB/CC,/root/x:ZZ,'

    reader = Reader(string=EXPAND)
    matches = ''
    while reader.read():
        if reader.matchesPattern(pattern):
            matches += reader.nodePath() + ','
    assert matches == expected

    reader = Reader(string=EXPAND)
    matches = ''
    while reader.nextPatternMatch(pattern):
        matches += reader.nodePath() + ','
    assert matches == expected

    dom = Parser().parse_string(EXPAND)
    matches = ''.join(node.nodePath() + ',' for node in dom.findnodes('//node()|@*')
                      if pattern.matchesNode(node))
    assert matches == '/root/AA/inner,/root/BB/CC,/root/x:ZZ,'


# ------------------------------------------------------- t/40reader_mem_error.t

def test_two_readers_side_by_side():
    path = os.path.join(DATA, 'textReader', 'goliath.xhtml')
    common = dict(validation=0, load_ext_dtd=0, no_network=1)
    got, expected = Reader(location=path, **common), Reader(location=path, **common)
    while got.read() > 0 and expected.read() > 0:
        assert got.nodeType() == expected.nodeType()
        if got.nodeType() == XML_READER_TYPE_ELEMENT:
            assert (got.name(), got.namespaceURI()) == (expected.name(), expected.namespaceURI())
        elif got.nodeType() == XML_READER_TYPE_TEXT:
            assert got.value() == expected.value()


# ----------------------------------------- t/48_RH5_double_free, t/48_reader_undef

HTML = """<html>
  <head>
    <title>David vs. Goliath - Part I</title>
  </head>
  <body>
  </body>
</html>
"""


def test_names():
    reader = Reader(string=HTML)
    names = [r.name() for r in reader]
    assert ','.join(names) == \
        'html,#text,head,#text,title,#text,title,#text,head,#text,body,#text,body,#text,html'


def test_preserve_every_node():
    reader = Reader(string=HTML)
    while reader.read():
        reader.preserveNode()
    assert reader.document().toString() == '<?xml version="1.0"?>\n' + HTML


def test_copy_of_body():
    reader = Reader(string=HTML)
    copy = None
    while reader.read():
        if reader.name() == 'body':
            copy = reader.copyCurrentNode()
    assert copy.toString() == '<body/>'
    assert copy.ownerDocument is None


def test_empty_string():
    reader = Reader(string='')
    with pytest.raises(Error):
        reader.read()


# ------------------------------------------------------------------ the Python side

def test_arguments():
    for kwds in ({}, {'string': '<a/>', 'location': 'x'}, {'DOM': 'x'}, {'nothing': 1}):
        with pytest.raises(TypeError):
            Reader(**kwds)
    with pytest.raises(Error):
        Reader(location=os.path.join(DATA, 'missing.xml')).read()
    assert Reader({'string': '<a/>'}).finish()


def test_parser_props():
    reader = Reader(string='<a/>')
    assert reader.getParserProp('nothing') is None
    reader.setParserProp(expand_entities=1, load_ext_dtd=0)
    assert reader.getParserProp('expand_entities') == 1
    with pytest.raises(TypeError):
        reader.setParserProp(nothing=1)


def test_attribute_hash_and_quote():
    reader = Reader(string='<a x="1" y=\'2\' xmlns:p="urn:p"/>')
    reader.read()
    assert reader.getAttributeHash() == {'x': '1', 'y': '2', 'xmlns:p': 'urn:p'}
    assert reader.name() == 'a'
    reader.moveToAttribute('y')
    assert reader.quoteChar() == '"'      # always, in recent libxml2


def test_copy_of_attribute_and_namespace():
    reader = Reader(string='<a x="1" xmlns:p="urn:p"/>')
    reader.read()
    reader.moveToAttribute('x')
    attr = reader.copyCurrentNode()
    assert (attr.nodeName, attr.value) == ('x', '1')
    reader.moveToAttribute('xmlns:p')
    assert reader.isNamespaceDecl()
    ns = reader.copyCurrentNode()
    assert ns.declaredURI == 'urn:p'
    assert not reader.matchesPattern(Pattern('a'))
    reader.readAttributeValue()
    text = reader.copyCurrentNode()
    del reader
    gc.collect()
    assert attr.toString() == ' x="1"'
    assert text.nodeValue == 'urn:p'


def test_iteration_errors():
    reader = Reader(string=BAD)
    with pytest.raises(Error):
        for _ in reader:
            pass


def test_close_then_use():
    reader = Reader(string=EXPAND)
    reader.read()
    assert reader.close()
    assert reader.read() == -1
    assert reader.name() is None
    assert reader.copyCurrentNode() is None


def test_io_raises():
    class Broken:
        def read(self, length):
            raise ValueError('broken')

    with pytest.raises(ValueError):
        Reader(IO=Broken()).read()


def test_io_returns_more():
    class Big:
        sent = False

        def read(self, length):
            if self.sent:
                return b''
            self.sent = True
            return b'<a>' + b'x' * 300000 + b'</a>'

    reader = Reader(IO=Big())
    reader.read()
    reader.read()
    assert len(reader.value()) == 300000


# ---------------------------------------------------- the document while it is read

def test_document_cannot_change_while_read():
    reader = Reader(string=EXPAND)
    reader.nextElement('BB')
    doc = reader.document()
    node = reader.preserveNode()
    other = Parser().parse_string('<z/>')
    for change in (lambda: node.setAttribute('q', '1'),
                   lambda: node.appendChild(other.createElement('n')),
                   lambda: other.documentElement.appendChild(node),
                   lambda: node.unbindNode(),
                   lambda: doc.documentElement.removeChild(node),
                   lambda: setattr(doc, 'encoding', 'latin1'),
                   lambda: setattr(node, 'textContent', 'x'),
                   lambda: doc.adoptNode(other.documentElement),
                   lambda: other.adoptNode(node),
                   lambda: doc.setDocumentElement(other.createElement('y')),
                   lambda: doc.process_xinclude(),
                   lambda: doc.validate(),
                   lambda: doc.createInternalSubset('r'),
                   lambda: node.firstChild.setData('t')):
        with pytest.raises(Error, match='Reader'):
            change()
    # Reading is fine.
    assert node.nodeName == 'BB' and doc.findnodes('//BB')
    assert other.importNode(node, 1).nodeName == 'BB'

    reader.finish()
    node.setAttribute('q', '1')
    doc.documentElement.appendChild(other.createElement('n'))
    assert doc.findnodes('//n')


def test_nodes_python_holds_are_kept():
    reader = Reader(string=EXPAND)
    reader.nextElement('AA')
    doc = reader.document()
    seen = []
    while reader.read():
        if reader.nodeType() == XML_READER_TYPE_ELEMENT and reader.depth() == 1:
            seen.extend(doc.findnodes('//*[last()]'))
        gc.collect()
    for node in seen:
        assert node.nodeName
        assert node.toString()
    doc.documentElement.appendChild(doc.createElement('end'))


def test_document_outlives_the_reader():
    reader = Reader(string=EXPAND)
    reader.nextElement('BB')
    node = reader.preserveNode()
    doc = reader.document()
    del reader
    gc.collect()
    assert node.toString().startswith('<BB')
    node.setAttribute('after', '1')
    assert doc.toString()


def test_node_outlives_reader_and_document():
    reader = Reader(string=EXPAND)
    reader.nextElement('CC')
    node = reader.preserveNode()
    del reader
    gc.collect()
    assert node.ownerDocument.findnodes('//CC')[0].isSameNode(node)


def test_dom_is_locked_while_walked():
    dom = Parser().parse_string(EXPAND)
    reader = Reader(DOM=dom)
    reader.nextElement('BB')
    with pytest.raises(Error):
        dom.documentElement.removeChild(dom.findnodes('//BB')[0])
    reader.finish()
    # The walker stays on the last node.
    with pytest.raises(Error):
        dom.documentElement.removeChild(dom.findnodes('//BB')[0])
    reader.close()
    dom.documentElement.removeChild(dom.findnodes('//BB')[0])

    reader = Reader(DOM=dom)
    reader.read()
    del reader
    dom.documentElement.appendChild(dom.createElement('free'))


def test_two_readers_on_one_dom():
    dom = Parser().parse_string(EXPAND)
    first, second = Reader(DOM=dom), Reader(DOM=dom)
    first.close()
    with pytest.raises(Error):
        dom.documentElement.appendChild(dom.createElement('n'))
    second.close()
    dom.documentElement.appendChild(dom.createElement('n'))


def test_walker_after_its_document_changed():
    dom = Parser().parse_string('<a><b x="1" xmlns:p="urn:p"/></a>')
    reader = Reader(DOM=dom)
    reader.nextElement('b')
    reader.moveToFirstAttribute()
    reader.readAttributeValue()
    other = Parser().parse_string('<z/>')
    with pytest.raises(Error):
        other.documentElement.appendChild(dom.findnodes('//b')[0])
    reader.close()
    other.documentElement.appendChild(dom.findnodes('//b')[0])
    assert reader.name() is None
    del reader, dom
    gc.collect()
    assert other.toString().count('<b') == 1


def test_reader_in_an_xpath_callback():
    reader = Reader(string=EXPAND)
    reader.nextElement('AA')
    doc = reader.document()
    errors = []

    def move():
        try:
            reader.read()
        except Error as error:
            errors.append(error)
        return True

    xpc = XPathContext(doc)
    xpc.registerFunction('move', move)
    assert xpc.findnodes('//*[move()]')
    assert errors and 'XPath' in str(errors[0])


def test_reader_in_its_own_callbacks():
    errors = []
    holder = []

    class Feed:
        def __init__(self):
            self.parts = [b'<a>', b'<b/>', b'</a>']

        def read(self, length):
            for reader in holder:
                try:
                    reader.name()
                except Error as error:
                    errors.append(error)
            return self.parts.pop(0) if self.parts else b''

    feed = Feed()
    holder.append(Reader(IO=feed))
    assert holder[0].finish()
    assert errors and 'middle of a read' in str(errors[0])


def test_ext_ent_handler():
    xml = '<!DOCTYPE a [<!ENTITY e SYSTEM "urn:e">]><a>&e;</a>'
    reader = Reader(string=xml, expand_entities=1,
                    ext_ent_handler=lambda url, ident: '<from>%s</from>' % url)
    names = [r.name() for r in reader]
    assert 'from' in names

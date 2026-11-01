"""
Ports of the Perl tests of the parser: t/02parse.t without SAX, XInclude and push
(later phases), 12html, 19encoding, 19die_on_invalid_utf8, 20extras, 30keep_blanks,
35huge_mode, 43options, 44extent, 46err_column, 48_rt118032_xxe, 48_rt55000,
48_rt93429, 48_security_oob_utf8, 49_load_html, 51_parse_html_string, 60error_prev_chain,
60struct_error and 61error, plus the complete_attributes block of 06elements.

Parser options are attributes here: `$p->keep_blanks(0)` is `p.keep_blanks = 0`.
"""
import io
import os
import threading
import warnings

import pytest

from xml_libxml import (Attr, Document, ErrNo, Error, Parser, XML_ENTITY_REF_NODE,
                        load_html, load_xml)

EXAMPLE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'example')
XML_DECL = '<?xml version="1.0"?>\n'


def example(name):
    return os.path.join(EXAMPLE, name)


# ------------------------------------------------------------------ 02parse

GOOD_WF = [
    '<foobar/>',
    '<foobar></foobar>',
    XML_DECL + '<foobar></foobar>',
    '<?xml version="1.0" encoding="UTF-8"?>\n<foobar></foobar>',
    '<?xml version="1.0" encoding="ISO-8859-1"?>\n<foobar></foobar>',
    XML_DECL + '<foobar> </foobar>\n',
    XML_DECL + '<foobar><foo/></foobar> ',
    XML_DECL + '<foobar> <foo/> </foobar> ',
    XML_DECL + '<foobar><![CDATA[<>&"\']]></foobar>',
    XML_DECL + '<foobar>&lt;&gt;&amp;&quot;&apos;</foobar>',
    XML_DECL + '<foobar>&#x20;&#160;</foobar>',
    XML_DECL + '<!--comment--><foobar>foo</foobar>',
    XML_DECL + '<foobar>foo</foobar><!--comment-->',
    XML_DECL + '<foobar>foo<!----></foobar>',
    XML_DECL + '<foobar foo="bar"/>',
    XML_DECL + '<foobar foo="\'bar>"/>',
    XML_DECL + '<foobar xmlns:bar="xml://foo" bar:foo="bar"/>\n',
    XML_DECL + '<foobar xmlns="xml://foo" foo="bar"><foo/></foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" foo="bar"><foo/></bar:foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" foo="bar"><bar:foo/></bar:foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" bar:foo="bar"><bar:foo/></bar:foobar>\n',
    XML_DECL + '<!DOCTYPE foobar [\n<!ENTITY foo " test ">\n]>\n<foobar>&foo;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;&gt;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar=&quot;foo&quot;">]><foobar>&foo;&gt;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar foo="&foo;"/>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar foo="&gt;&foo;"/>',
]

BAD_WF = [
    XML_DECL,
    '<!--ouch-->',
    '<!DOCTYPE ouch [<!ENTITY foo "bar">]>',
    '<ouch>',
    '<ouch/>foo',
    'foo<ouch/>',
    '<ouch foo=bar/>',
    '<ouch foo="bar/>',
    '<ouch>&</ouch>',
    '<ouch>&#0x20;</ouch>',
    b'<foob\xe4r/>',
    '<ouch>&foo;</ouch>',
    '<ouch>&gt</ouch>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar &foo;="ouch"/>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar=&quot;foo&quot;">]><foobar &foo;/>',
    '<ouch><!---></ouch>',
    '<ouch><!-----></ouch>',
]

SETTINGS = [{}, {'keep_blanks': 0}, {'expand_entities': 0},
            {'expand_entities': 1, 'pedantic_parser': 1}]


@pytest.mark.parametrize('options', SETTINGS)
@pytest.mark.parametrize('string', GOOD_WF)
def test_good_strings(options, string):
    assert isinstance(Parser(**options).parse_string(string), Document)


@pytest.mark.parametrize('options', SETTINGS)
@pytest.mark.parametrize('string', BAD_WF)
def test_bad_strings(options, string):
    with pytest.raises(Error):
        Parser(**options).parse_string(string)


@pytest.mark.parametrize('string', ['', b''])
def test_empty_string(string):
    with pytest.raises(Error, match='Empty String'):
        Parser().parse_string(string)


def test_files():
    parser = Parser()
    assert isinstance(parser.parse_file(example('dromeds.xml')), Document)
    with pytest.raises(Error) as exc:
        parser.parse_file(example('bad.xml'))
    assert str(exc.value).startswith(example('bad.xml') + ':3: parser error : '
                                     'Extra content at the end of the document')
    with pytest.raises(Error):
        parser.parse_file('does_not_exist.xml')


def test_blanks():
    parser = Parser()
    parser.keep_blanks = 0
    str_doc = parser.parse_string('<a>    <b/> </a>')
    file_doc = parser.parse_file(example('test3.xml'))
    assert str_doc.toString(skip_xml_declaration=1).strip() == '<a><b/></a>'
    assert file_doc.toString(skip_xml_declaration=1).strip() == '<a><b/></a>'


def test_parse_fh():
    parser = Parser()
    with open(example('dromeds.xml'), 'rb') as fh:
        assert isinstance(parser.parse_fh(fh), Document)
    with open(example('bad.xml'), 'rb') as fh:
        with pytest.raises(Error) as exc:
            parser.parse_fh(fh)
    assert str(exc.value).startswith('Entity: line 3: parser error : '
                                     'Extra content at the end of the document')
    with pytest.raises(AttributeError):
        parser.parse_fh(None)


def test_expand_entities_in_files():
    parser = Parser()
    parser.expand_entities = 1
    assert len(parser.parse_file(example('dtd.xml')).documentElement.childNodes()) == 1
    doc = parser.parse_file(example('complex/complex2.xml'))
    assert len(doc.documentElement.childNodes()) == 1
    parser.expand_entities = 0
    assert len(parser.parse_file(example('dtd.xml')).documentElement.childNodes()) == 3


def test_validation_and_line_numbers():
    bad = XML_DECL + '<!DOCTYPE foo [<!ELEMENT foo EMPTY>]>\n<bar/>\n'
    good = XML_DECL + '<foo>\n<bar/>\n</foo>'

    parser = Parser(expand_entities=1)
    parser.validation = 1
    with pytest.raises(Error) as exc:
        parser.parse_string(bad)
    assert exc.value.line == 3 or exc.value._prev.line == 3

    parser.validation = 0
    root = parser.parse_string('<!-- -->\n<foo>\n<bar/>\n</foo>').documentElement
    assert root.line_number() == 2
    kids = list(root.childNodes())
    assert kids[1].line_number() == 3
    assert root.appendChild(root.ownerDocument.createElement('bar')).line_number() == 0
    assert parser.parse_string(good) is not None


def test_clean_namespaces():
    parser = Parser()
    parser.clean_namespaces = 1
    doc1 = '<A:B xmlns:A="http://D"><A:C xmlns:A="http://D"></A:C></A:B>'
    doc2 = '<A:B xmlns:A="http://D"><A:C xmlns:A="http://E"/></A:B>'
    clean = '<A:B xmlns:A="http://D"><A:C/></A:B>'
    assert parser.parse_string(doc1).documentElement.toString() == clean
    assert parser.parse_string(doc2).documentElement.toString() == doc2
    assert parser.parse_file(example('xmlns/goodguy.xml')).documentElement.toString() == clean
    assert parser.parse_file(example('xmlns/badguy.xml')).documentElement.toString() == doc2
    with open(example('xmlns/goodguy.xml'), 'rb') as fh:
        assert parser.parse_fh(fh).documentElement.toString() == clean


def test_load_ext_dtd(monkeypatch):
    monkeypatch.chdir(os.path.dirname(EXAMPLE))
    xml = '<!DOCTYPE X SYSTEM "example/ext_ent.dtd">\n<X>&foo;</X>\n'

    parser = Parser()
    parser.load_ext_dtd = 1
    parser.expand_entities = 1
    assert parser.parse_string(xml).documentElement.string_value() == ' test '
    assert parser.parse_string(xml).documentElement.string_value() == ' test '

    parser.load_ext_dtd = 0
    assert parser.parse_string(xml).documentElement.firstChild.nodeName == 'foo'

    parser.validation = 1
    assert isinstance(parser.parse_file('example/article_external_bad.xml'), Document)
    parser.load_ext_dtd = 1
    with pytest.raises(Error):
        parser.parse_file('example/article_external_bad.xml')


def test_base_uri():
    doc = Parser().parse_string('<foo xml:base="foo.xml"/>', 'bar.xml')
    el = doc.documentElement
    assert doc.URI == 'bar.xml'
    assert doc.baseURI == 'bar.xml'
    assert el.baseURI == 'foo.xml'

    doc.setURI('baz.xml')
    assert doc.URI == 'baz.xml'
    assert doc.baseURI == 'baz.xml'
    assert el.baseURI == 'foo.xml'

    doc.setBaseURI('bag.xml')
    assert doc.URI == 'bag.xml'
    assert doc.baseURI == 'bag.xml'
    assert el.baseURI == 'foo.xml'

    el.setBaseURI('bam.xml')
    assert doc.URI == 'bag.xml'
    assert doc.baseURI == 'bag.xml'
    assert el.baseURI == 'bam.xml'


def test_base_uri_html():
    doc = Parser().parse_html_string(
        '<html><head><base href="foo.html"></head><body></body></html>', URI='bar.html')
    el = doc.documentElement
    assert doc.URI == 'bar.html'
    assert doc.baseURI == 'foo.html'
    assert el.baseURI == 'foo.html'
    doc.setURI('baz.html')
    assert doc.URI == 'baz.html'
    assert doc.baseURI == 'foo.html'


def test_unknown_base_uri():
    doc = Parser().parse_string('<a>t</a>')
    assert doc.documentElement.firstChild.baseURI.startswith('unknown-')
    assert Parser().parse_html_string('<p>x').URI.startswith('unknown-')
    assert Parser(base_uri='http://x/').parse_string('<a/>').URI == 'http://x/'


# ------------------------------------------------------------------ 06elements

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
def test_complete_attributes(xml):
    parser = Parser()
    parser.complete_attributes = 1
    parser.expand_entities = 1
    root = parser.parse_string(DTD + xml).documentElement

    attr = root.getAttributeNode('foo')
    assert isinstance(attr, Attr)
    assert root.isSameNode(attr.ownerElement)
    assert attr.value == '"barENT"'
    assert attr.serializeContent() == '&quot;barENT&quot;'
    assert attr.toString() == ' foo="&quot;barENT&quot;"'

    assert root.getAttribute('fixed') == 'foo'
    assert root.getAttributeNS(URN, 'ns_fixed') == 'ns_foo'
    assert root.getAttribute('a:ns_fixed') == 'ns_foo'

    attr = root.getAttributeNode('fixed')
    assert attr.value == 'foo'
    assert attr.toString() == ' fixed="foo"'
    assert root.getAttributeNode('a:ns_fixed').value == 'ns_foo'
    attr = root.getAttributeNodeNS(URN, 'ns_fixed')
    assert attr.value == 'ns_foo'
    assert attr.toString() == ' a:ns_fixed="ns_foo"'

    for name in ('ns_fixed', 'name', 'baz'):
        assert root.getAttributeNode(name) is None
    for uri, name in ((URN, 'foo'), (URN, 'fixed'), (None, 'name'), (None, 'baz')):
        assert root.getAttributeNodeNS(uri, name) is None


# ------------------------------------------------------------------ 12html

CZECH = 'ěščř'


def test_html_sources():
    parser = Parser()
    path = example('test.html')
    assert parser.parse_html_file(path) is not None
    with open(path, 'rb') as fh:
        content = fh.read()
    assert parser.parse_html_string(content) is not None
    assert parser.parse_html_fh(io.BytesIO(content)) is not None


def test_html_recover():
    html = '<html>\n<body>\n<a href="http:/foo.bar/foobar.pl?foo=bar&bar=foo">\nfoo\n</a>' \
           '\n<p>test\n</body>\n</html>\n'
    parser = Parser()
    parser.recover = 1
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        assert parser.parse_html_string(html) is not None


def test_html_encodings():
    parser = Parser()
    parser.recover = 1
    html = '<html>\n  <body>\n    <p>%s</p>\n  </body>\n</html>\n' % CZECH

    doc = parser.parse_html_string(html)
    assert doc.findvalue('//p/text()') == CZECH
    doc = parser.parse_html_string(html, encoding='UTF-8')
    assert doc.findvalue('//p/text()') == CZECH
    doc = parser.parse_html_string(html.encode('iso-8859-2'), encoding='iso-8859-2')
    assert doc.findvalue('//p/text()') == CZECH


def test_html_meta_encoding():
    parser = Parser()
    html = """<html>
  <head>
    <meta http-equiv="Content-Type" content="text/html;
      charset=iso-8859-2">
  </head>
  <body>
    <p>%s</p>
  </body>
</html>
""" % CZECH
    assert parser.parse_html_string(html, encoding='UTF-8').findvalue('//p/text()') == CZECH

    latin2 = html.encode('iso-8859-2')
    assert parser.parse_html_string(latin2).findvalue('//p/text()') == CZECH
    doc = parser.parse_html_string(latin2, encoding='iso-8859-2', URI='foo')
    assert doc.findvalue('//p/text()') == CZECH
    assert doc.URI == 'foo'


def test_html_files_with_encoding():
    parser = Parser()
    path = example('enc_latin2.html')
    assert parser.parse_html_file(path).findvalue('//p/text()') == CZECH
    doc = parser.parse_html_file(path, encoding='iso-8859-2', URI='foo')
    assert doc.findvalue('//p/text()') == CZECH
    assert doc.URI == 'foo'

    with open(path, 'rb') as fh:
        assert parser.parse_html_fh(fh).findvalue('//p/text()') == CZECH
    with open(path, 'rb') as fh:
        doc = parser.parse_html_fh(fh, encoding='iso-8859-2', URI='foo')
    assert doc.URI == 'foo'
    assert doc.findvalue('//p/text()') == CZECH

    path = example('enc2_latin2.html')
    doc = parser.parse_html_file(path, encoding='iso-8859-2')
    assert doc.findvalue('//p/text()') == CZECH
    with open(path, 'rb') as fh:
        doc = parser.parse_html_fh(fh, encoding='iso-8859-2')
    assert doc.findvalue('//p/text()') == CZECH


def test_html_xhtml_with_errors():
    html = """<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"
"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>Test &amp; Test some more</title>
</head>
<body>
<p>Meet you at the caf&eacute;?</p>
<p>How about <a href="http://example.com?mode=cafe&id=1&ref=foo">this one</a>?
</p>
<input class="wibble" id="foo" value="working" />
</body>
</html>
"""
    doc = Parser().parse_html_string(html, recover=1, suppress_errors=1)
    assert doc.documentElement.findvalue('//input[@id="foo"]/@value') == 'working'


def test_html_default_dtd():
    html = '<body bgcolor="#ffffff" leftmargin=0><p>x</p></body>'
    parser = Parser()
    out = parser.parse_html_string(html, recover=2, no_defdtd=1, encoding='UTF-8')
    assert out.toStringHTML().startswith('<html>')
    out = parser.parse_html_string(html, recover=2, encoding='UTF-8')
    assert out.toStringHTML().startswith('<!DOCTYPE html')


# ------------------------------------------------------------------ 19encoding

@pytest.mark.parametrize('encoding,text', [
    ('iso-8859-1', 't\xe4st'),
    ('EUC-JP', 'テスト'),
    ('KOI8-R', 'тест'),
])
def test_document_encodings(encoding, text):
    doc = Document('1.0', encoding)
    elem = doc.createElement(text)
    doc.setDocumentElement(elem)
    assert elem.nodeName == text
    assert elem.toString() == '<%s/>' % text
    elem.appendText(text)
    assert elem.string_value() == text
    expected = '<?xml version="1.0" encoding="%s"?>\n<%s>%s</%s>\n' % (encoding, text, text, text)
    assert doc.toString() == expected
    assert doc.toBytes() == expected.encode(encoding)


def test_invalid_utf8_in_file():
    with pytest.raises(Error, match='not proper UTF-8|Invalid bytes in character encoding'):
        Parser().parse_file(example('thedieline.rss'))


def test_no_declared_encoding_uses_references():
    doc = Parser().parse_string('<a>\xe9</a>')
    assert doc.toString() == '<?xml version="1.0"?>\n<a>&#xE9;</a>\n'
    assert doc.documentElement.textContent == '\xe9'


def test_str_output_can_be_parsed_again():
    doc = Parser().parse_string(b'<?xml version="1.0" encoding="iso-8859-1"?><a>\xe9</a>')
    text = doc.toString()
    assert 'encoding="iso-8859-1"' in text
    again = Parser().parse_string(text)
    assert again.documentElement.textContent == '\xe9'
    assert again.encoding == 'iso-8859-1'


@pytest.mark.parametrize('method', ['createElement', 'createAttribute'])
def test_surrogates_are_refused(method):
    with pytest.raises(UnicodeError):
        getattr(Document(), method)('a\udc80')


def test_surrogates_in_names():
    # 48_security_oob_utf8_gh146: bad characters must not reach libxml2
    doc = Document()
    node = doc.createElement('tmp')
    for call in (lambda: node.setNodeName('a\ud800'),
                 lambda: doc.createElementNS('http://example.com/ns', 'a\ud800'),
                 lambda: doc.createAttributeNS('http://example.com/ns', 'a\ud800', 'v')):
        with pytest.raises(UnicodeError):
            call()


# ------------------------------------------------------------------ 20extras

def test_output_options():
    string = '<foo><bar/></foo>'
    doc = Parser().parse_string(string)
    assert doc.toString(skip_xml_declaration=1).strip() == string
    assert doc.toString(skip_xml_declaration=1, tag_compression=1).strip() == \
        '<foo><bar></bar></foo>'


def test_skip_dtd():
    parser = Parser()
    parser.expand_entities = 0
    doc = parser.parse_file(example('dtd.xml'))
    expected = '<?xml version="1.0"?>\n<doc>This is a valid document &foo; !</doc>\n'
    assert doc.toString(skip_dtd=1) == expected
    assert '<!DOCTYPE' in doc.toString()
    assert doc.firstChild.nodeName == 'doc' or doc.firstChild.nodeType == 14


def test_document_clones():
    string = '<foo><bar/></foo>'
    doc = Parser().parse_string(string)
    deep = doc.cloneNode(1)
    assert not deep.isSameNode(doc)
    assert deep.documentElement is not None
    assert doc.toString() == deep.toString()

    shallow = doc.cloneNode()
    assert not shallow.isSameNode(doc)
    assert shallow.documentElement is None
    doc.documentElement.unbindNode()
    assert doc.toString() == shallow.toString()


# ------------------------------------------------------------------ small files

def test_keep_blanks_in_chunk():
    parser = Parser()
    parser.keep_blanks = 0
    frag = parser.parse_balanced_chunk('<bla> <foo/> </bla>\n')
    assert frag.toString() == '<bla><foo/></bla>\n'


def test_huge_mode():
    evil = '<!DOCTYPE root [\n  <!ENTITY ha "Ha !">\n  <!ENTITY ha2 "&ha; &ha;">\n'
    for i in range(2, 48):
        evil += '  <!ENTITY ha%d "&ha%d; &ha%d;">\n' % (i + 1, i, i)
    evil += ']>\n<root>&ha48;</root>\n'

    parser = Parser(expand_entities=1)
    assert not parser.get_option('huge')
    with pytest.raises(Error, match='(?si)entity'):
        parser.parse_string(evil)

    benign = XML_DECL + '<!DOCTYPE lolz [\n  <!ENTITY lol "haha">\n]>\n<lolz>&lol;</lolz>\n'
    assert Parser(expand_entities=1).parse_string(benign).findvalue('/lolz') == 'haha'


ALL_OPTIONS = """recover expand_entities load_ext_dtd complete_attributes validation
    suppress_errors suppress_warnings pedantic_parser no_blanks expand_xinclude xinclude
    clean_namespaces no_cdata no_xinclude_nodes old10 no_base_fix huge oldsax
    line_numbers URI base_uri gdome""".split()


def test_options_default_off():
    parser = Parser()
    for option in ALL_OPTIONS:
        assert not parser.get_option(option), option
    assert not parser.option_exists('foo')


def test_no_network_is_on_by_default():
    # Differs from Perl on purpose: the safe side.
    assert Parser().get_option('no_network') == 1
    assert Parser(no_network=0).get_option('no_network') == 0


def test_option_access():
    p = Parser()
    assert p.keep_blanks == 1
    assert p.set_option('no_blanks', 1) == 1
    assert not p.keep_blanks
    p.keep_blanks = 1
    assert not p.get_option('no_blanks')

    uri = 'http://foo/bar'
    assert p.set_option('URI', uri) == uri
    assert p.base_uri == uri
    p.base_uri = uri + '2'
    assert p.get_option('URI') == uri + '2'
    assert p.get_option('base_uri') == uri + '2'

    assert not p.recover_silently
    p.set_option('recover', 1)
    assert not p.recover_silently
    p.set_option('recover', 2)
    assert p.recover_silently
    p.recover_silently = 0
    assert p.get_option('recover') == 0
    p.recover_silently = 1
    assert p.get_option('recover') == 2


def test_expand_entities_needs_ext_dtd():
    p = Parser()
    assert p.expand_entities == 0
    assert p.load_ext_dtd == 0
    p.load_ext_dtd = 1
    assert p.load_ext_dtd == 1
    p.load_ext_dtd = 0
    p.expand_entities = 1
    assert p.expand_entities == 1
    assert p.load_ext_dtd == 1
    p.expand_entities = 0
    assert p.expand_entities == 0


@pytest.mark.parametrize('use_dict', [False, True])
@pytest.mark.parametrize('value', [0, 1])
def test_constructor_options(use_dict, value):
    options = {name: value for name in ALL_OPTIONS for name in [name]}
    parser = Parser(options) if use_dict else Parser(**options)
    for name in ALL_OPTIONS:
        assert parser.get_option(name) == value, name


def test_set_get_all_options():
    p = Parser()
    for name in ALL_OPTIONS:
        assert p.option_exists(name)
        assert p.set_option(name, 0) == 0
        assert p.get_option(name) == 0
        assert p.set_option(name, 1) == 1
        assert p.get_option(name) == 1


def test_set_options():
    p = Parser()
    p.set_options({'no_blanks': 1}, huge=1)
    assert p.no_blanks and p.huge


def test_unknown_options():
    with pytest.raises(TypeError):
        Parser(nonsense=1)
    with pytest.raises(TypeError):
        Parser().load_xml(string='<a/>', nonsense=1)
    with pytest.raises(ValueError):
        Parser().get_option('nonsense')


def test_options_kept_by_load_xml():
    xml = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE title [ <!ELEMENT title ANY >
<!ENTITY xxe SYSTEM "file:///etc/passwd" >]>
<rss version="2.0"><channel><item><title>&xxe;</title></item></channel></rss>
"""
    parser = Parser(expand_entities=0, load_ext_dtd=0, no_network=1, expand_xinclude=0)
    assert '<title>&xxe;</title>' in parser.load_xml(string=xml).toString()


def test_per_call_options_leave_the_parser_alone():
    parser = Parser()
    assert parser.load_xml(string='<a> <b/> </a>', no_blanks=1).documentElement.toString() \
        == '<a><b/></a>'
    assert parser.load_xml(string='<a> <b/> </a>').documentElement.toString() \
        == '<a> <b/> </a>'
    assert parser.no_blanks == 0


def test_module_functions():
    assert load_xml(string='<a/>').documentElement.nodeName == 'a'
    assert load_html(string='<p>x').findvalue('//p') == 'x'
    with pytest.raises(ValueError):
        load_xml()


def test_external_entity_handler():
    xml = """<?xml version="1.0"?>
<!DOCTYPE foo [
<!ENTITY a PUBLIC "//foo/bar/b" "file:/dev/null">
<!ENTITY b SYSTEM "file:///dev/null">
]>
<root>
  <a>&a;</a>
  <b>&b;</b>
</root>
"""
    parser = Parser(expand_entities=1, ext_ent_handler=lambda url, id: url + ',' + id)
    expected = xml.replace('&a;', 'file:/dev/null,//foo/bar/b') \
                  .replace('&b;', 'file:///dev/null,')
    assert parser.parse_string(xml).toString() == expected

    parser.set_option('ext_ent_handler', lambda url, id: '<!-- -->')
    expected = xml.replace('&a;', '<!-- -->').replace('&b;', '<!-- -->')
    assert parser.parse_string(xml).toString() == expected

    parser.set_option('ext_ent_handler', lambda url, id: '')
    parser.set_options({'expand_entities': 0, 'recover': 2})
    doc = parser.parse_string(xml)
    assert doc.toString() == xml
    for el in doc.findnodes('/root/*'):
        assert el.hasChildNodes()
        assert el.firstChild.nodeType == XML_ENTITY_REF_NODE


def test_external_entity_handler_exception():
    def handler(url, id):
        raise KeyError('no entity')

    xml = '<!DOCTYPE a [<!ENTITY e SYSTEM "x.txt">]><a>&e;</a>'
    with pytest.raises(KeyError):
        Parser(expand_entities=1, ext_ent_handler=handler).parse_string(xml)


def test_error_column():
    with pytest.raises(Error) as exc:
        Parser().parse_string(
            '<foo attr1="value1" attr2="value2" attr3="value2" attr4="value2"'
            ' attr5="value2" attr6="value2" attr7="value2" attr8="value2"'
            ' attr9="value2" attr10="value2" attr11="value2" attr12="value2"'
            ' attr13="value2"attr14="value2" attr15="value2" />')
    first = exc.value
    while first._prev is not None:
        first = first._prev
    assert first.column == 203


XXE = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE title [ <!ELEMENT title ANY >
<!ENTITY xxe SYSTEM "file:///etc/nonexistent" >]>
<rss version="2.0"><channel><title>&xxe;</title></channel></rss>
"""


def test_xxe_defaults():
    xml = '<?xml version="1.0" encoding="UTF-8"?>\n<EXAMPLE>&apos;&quot;&#38;</EXAMPLE>\n'
    assert '<EXAMPLE>\'"&amp;</EXAMPLE>' in Parser().load_xml(string=xml).toString()

    xml = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE EXAMPLE SYSTEM "example.dtd" [
<!ENTITY xml "Extensible Markup Language">
]>
<EXAMPLE>&xml;</EXAMPLE>
"""
    assert '<EXAMPLE>&xml;</EXAMPLE>' in Parser().load_xml(string=xml).toString()

    xml = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE m PUBLIC "-//B/A/EN" "http://example.com">
<rss version="2.0"><channel><link>example.com</link></channel></rss>
"""
    assert Parser().load_xml(string=xml).toString()

    assert '<title>&xxe;</title>' in Parser().load_xml(string=XXE).toString()


def test_rt55000_attribute_in_default_namespace():
    xml = """<?xml version="1.0" encoding="UTF-8"?>
<element xmlns="uri"
xmlns:wrong="other">
</element>
"""
    doc = Parser().parse_string(xml)
    root = doc.documentElement
    root.setAttributeNS('uri', 'prefix:attribute', 'text')
    root.setAttributeNS('uri', 'second', 'text')
    out = doc.toString(1)
    assert 'prefix:attribute="text"' in out
    assert 'wrong:attribute' not in out
    assert 'prefix:second="text"' in out
    assert 'wrong:second' not in out


def test_rt93429_recover_2_is_silent(capfd):
    with warnings.catch_warnings():
        warnings.simplefilter('error')
        Parser().load_html(string='<html><body><lkj/></body></html>', recover=2)
    out, err = capfd.readouterr()
    assert (out, err) == ('', '')


def test_load_html_suppress_errors(capfd):
    with open(example('yahoo-finance-html-with-errors.html'), encoding='utf-8') as fh:
        content = fh.read()
    parser = Parser()
    parser.set_option('recover', 1)
    parser.set_option('suppress_errors', 1)
    with warnings.catch_warnings():
        warnings.simplefilter('error')
        parser.load_html(string=content)
    assert capfd.readouterr() == ('', '')


def test_load_html_recover_2_is_silent(capfd):
    txt = '<div>\n<a href="milu?a=eins&b=zwei"> ampersand not URL-encoded </a>\n</div>\n'
    with warnings.catch_warnings():
        warnings.simplefilter('error')
        Parser(recover=2).load_html(string=txt)
    assert capfd.readouterr() == ('', '')


def test_parse_html_string_doctype():
    dom = Parser().load_html(string='<!DOCTYPE html><html>')
    assert dom.toStringHTML() == '<!DOCTYPE html>\n<html></html>\n'


def test_error_chain():
    with pytest.raises(Error) as exc:
        Parser().parse_string('<doc>&ldquo;&nbsp;&rdquo;</doc>')
    err, count = exc.value, 0
    while err is not None and count < 200:
        err = err._prev
        count += 1
    assert err is None
    assert count == 3


def test_struct_error():
    with pytest.raises(Error) as exc:
        Parser().parse_string('<X></Y>')
    err = exc.value
    assert err.domain == 'parser'
    assert err.line == 1
    assert err.code == ErrNo.ERR_TAG_NAME_MISMATCH or \
        err._prev.code == ErrNo.ERR_TAG_NAME_MISMATCH


def test_fake_error():
    fake = Error('fake error')
    assert fake.message == 'fake error'
    fake._domain = 1000
    assert fake.domain == 'domain_1000'
    assert 'fake error' in str(fake)


def test_error_format():
    with pytest.raises(Error) as exc:
        Parser().parse_string('<a:b/>')
    assert 'namespace error : Namespace prefix a on b is not defined' in str(exc.value)
    assert '<a:b/>\n' in str(exc.value)
    assert str(exc.value).endswith('^\n')

    with pytest.raises(Error) as exc:
        Parser().parse_string('<a/>').findnodes('///')
    assert exc.value.domain == 'XPath'
    assert '///\n' in str(exc.value)


def test_namespace_error_with_recover():
    with pytest.warns(RuntimeWarning, match='Namespace prefix'):
        doc = Parser(recover=1).parse_string('<a:b/>')
    assert doc is not None
    with warnings.catch_warnings():
        warnings.simplefilter('error')
        assert Parser(recover=2).parse_string('<a:b/>') is not None


# ------------------------------------------------------------------ threads

def test_threads():
    failures = []

    def work(n):
        try:
            for i in range(200):
                parser = Parser()
                doc = parser.parse_string('<r n="%d"><a>t</a></r>' % n)
                root = doc.documentElement
                kid = root.firstChild
                root.textContent = 'x'
                assert kid.nodeName == 'a'
                assert root.getAttribute('n') == str(n)
                try:
                    parser.parse_string('<r>')
                except Error as err:
                    assert 'Premature end' in str(err) or 'expected' in str(err)
                else:
                    raise AssertionError('no error')
        except BaseException as exc:
            failures.append(exc)

    threads = [threading.Thread(target=work, args=(n,)) for n in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert failures == []

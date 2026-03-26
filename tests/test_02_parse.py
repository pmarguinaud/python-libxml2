"""
test_02_parse.py — port of t/02parse.t

Tests parsing capabilities: parse_string, parse_file, parser options
(no_blanks, expand_entities, recover), bad XML, namespaces, HTML parsing.
"""
import os
import pytest
from xml_libxml import Parser, Document

EXAMPLE = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'example')


XML_DECL = '<?xml version="1.0"?>\n'

GOOD_WF_STRINGS = [
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
]

GOOD_WF_NS_STRINGS = [
    XML_DECL + '<foobar xmlns:bar="xml://foo" bar:foo="bar"/>\n',
    XML_DECL + '<foobar xmlns="xml://foo" foo="bar"><foo/></foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" foo="bar"><foo/></bar:foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" foo="bar"><bar:foo/></bar:foobar>\n',
    XML_DECL + '<bar:foobar xmlns:bar="xml://foo" bar:foo="bar"><bar:foo/></bar:foobar>\n',
]

GOOD_WF_DTD_STRINGS = [
    XML_DECL + '<!DOCTYPE foobar [\n<!ENTITY foo " test ">\n]>\n<foobar>&foo;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;&gt;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar=&quot;foo&quot;">]><foobar>&foo;&gt;</foobar>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar foo="&foo;"/>',
    XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar foo="&gt;&foo;"/>',
]

BAD_WF_STRINGS = [
    "",                             # totally empty document
    XML_DECL,                       # only XML Declaration
    "<!--ouch-->",                  # comment only
    "<ouch>",                       # single tag (mismatch)
    "<ouch/>foo",                   # trailing junk
    "foo<ouch/>",                   # leading junk
    "<ouch foo=bar/>",              # bad attribute
    '<ouch foo="bar/>',             # bad attribute
    "<ouch>&</ouch>",              # bad char
    "<ouch>&foo;</ouch>",           # undefined entity
    "<ouch>&gt</ouch>",             # unterminated entity
    "<ouch><!---></ouch>",          # bad comment
]


# ------------------------------------------------------------------ #
# 1.1  parse_string — well-formed strings                            #
# ------------------------------------------------------------------ #

@pytest.mark.parametrize("xml", GOOD_WF_STRINGS, ids=range(len(GOOD_WF_STRINGS)))
def test_parse_string_good_wf(xml):
    doc = Parser().parse_string(xml)
    assert isinstance(doc, Document)


@pytest.mark.parametrize("xml", GOOD_WF_NS_STRINGS, ids=range(len(GOOD_WF_NS_STRINGS)))
def test_parse_string_good_ns(xml):
    doc = Parser().parse_string(xml)
    assert isinstance(doc, Document)


@pytest.mark.parametrize("xml", GOOD_WF_DTD_STRINGS, ids=range(len(GOOD_WF_DTD_STRINGS)))
def test_parse_string_good_dtd(xml):
    doc = Parser().parse_string(xml)
    assert isinstance(doc, Document)


@pytest.mark.parametrize("xml", BAD_WF_STRINGS, ids=range(len(BAD_WF_STRINGS)))
def test_parse_string_bad_raises(xml):
    with pytest.raises(Exception):
        Parser().parse_string(xml)


def test_parse_string_none_raises():
    with pytest.raises((TypeError, Exception)):
        Parser().parse_string(None)


# ------------------------------------------------------------------ #
# 1.1.2  no_blanks option                                            #
# ------------------------------------------------------------------ #

def test_no_blanks_strips_whitespace():
    xml = '<a>    <b/> </a>'
    doc_with = Parser().parse_string(xml)
    doc_without = Parser(no_blanks=True).parse_string(xml)
    # with blanks: a has text + b + text children
    cn_with = list(doc_with.documentElement.childNodes())
    # without blanks: only b
    cn_without = list(doc_without.documentElement.childNodes())
    assert len(cn_without) < len(cn_with)
    assert len(cn_without) == 1  # just <b/>


def test_no_blanks_file():
    p = Parser(no_blanks=True)
    doc = p.parse_file(os.path.join(EXAMPLE, 'test3.xml'))
    assert isinstance(doc, Document)
    root = doc.documentElement
    cn = list(root.childNodes())
    assert len(cn) == 1  # test3.xml is '<a>    <b/> </a>'


# ------------------------------------------------------------------ #
# 1.1.3  expand_entities option                                      #
# ------------------------------------------------------------------ #

def test_expand_entities_off():
    xml = XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;</foobar>'
    doc = Parser(expand_entities=False).parse_string(xml)
    assert isinstance(doc, Document)
    # with expand_entities=False, the entity reference is kept as a node
    cn = list(doc.documentElement.childNodes())
    # should have at least one child (entity ref or text)
    assert len(cn) >= 1


def test_expand_entities_on():
    xml = XML_DECL + '<!DOCTYPE foobar [<!ENTITY foo "bar">]><foobar>&foo;</foobar>'
    doc = Parser(expand_entities=True).parse_string(xml)
    assert doc.documentElement.textContent == 'bar'


# ------------------------------------------------------------------ #
# 1.2  parse_file                                                    #
# ------------------------------------------------------------------ #

def test_parse_file_good():
    doc = Parser().parse_file(os.path.join(EXAMPLE, 'dromeds.xml'))
    assert isinstance(doc, Document)


def test_parse_file_bad_raises():
    with pytest.raises(Exception):
        Parser().parse_file(os.path.join(EXAMPLE, 'bad.xml'))


def test_parse_file_nonexistent_raises():
    with pytest.raises(Exception):
        Parser().parse_file('does_not_exist.xml')


# ------------------------------------------------------------------ #
# 1.3  Namespaces after parsing                                      #
# ------------------------------------------------------------------ #

def test_ns_attributes_included():
    """Namespace declarations appear in attributes() as nodeType==18"""
    from xml_libxml import XML_NAMESPACE_DECL
    xml = '<foo xmlns:bar="http://foo.bar">bar<bar:bi/></foo>'
    doc = Parser().parse_string(xml)
    root = doc.documentElement
    attrs = list(root.attributes())
    assert len(attrs) == 1
    assert attrs[0].nodeType == XML_NAMESPACE_DECL


def test_ns_round_trip():
    xml = XML_DECL + '<foobar xmlns:bar="xml://foo" bar:foo="bar"/>\n'
    doc = Parser().parse_string(xml)
    assert isinstance(doc, Document)


def test_ns_element_namespace():
    xml = '<bar:foobar xmlns:bar="xml://foo"/>'
    doc = Parser().parse_string(xml)
    root = doc.documentElement
    assert root.namespaceURI == 'xml://foo'
    assert root.prefix == 'bar'


# ------------------------------------------------------------------ #
# 1.4  recover option                                                #
# ------------------------------------------------------------------ #

def test_recover_bad_xml():
    """recover=True should parse broken XML without raising"""
    xml = '<root><unclosed>'
    doc = Parser(recover=True).parse_string(xml)
    assert isinstance(doc, Document)


# ------------------------------------------------------------------ #
# 2  HTML parsing                                                    #
# ------------------------------------------------------------------ #

def test_parse_html_string():
    html = '<html><head><title>Test</title></head><body><p>Hello</p></body></html>'
    doc = Parser().parse_html_string(html)
    assert isinstance(doc, Document)


def test_parse_html_file():
    doc = Parser().parse_html_file(os.path.join(EXAMPLE, 'test.html'))
    assert isinstance(doc, Document)


def test_parse_html_string_malformed():
    """HTML parser should recover from malformed HTML"""
    html = '<html><body><p>unclosed'
    doc = Parser().parse_html_string(html)
    assert isinstance(doc, Document)


# ------------------------------------------------------------------ #
# 3  DTD entity expansion                                            #
# ------------------------------------------------------------------ #

def test_dtd_expand_entities_file():
    doc = Parser(expand_entities=True).parse_file(
        os.path.join(EXAMPLE, 'dtd.xml'))
    cn = list(doc.documentElement.childNodes())
    assert len(cn) == 1


def test_dtd_no_expand_entities_file():
    doc = Parser(expand_entities=False).parse_file(
        os.path.join(EXAMPLE, 'dtd.xml'))
    cn = list(doc.documentElement.childNodes())
    assert len(cn) >= 1  # entity refs kept


# ------------------------------------------------------------------ #
# 4  Document properties after parsing                               #
# ------------------------------------------------------------------ #

def test_parsed_doc_encoding():
    xml = '<?xml version="1.0" encoding="UTF-8"?>\n<root/>'
    doc = Parser().parse_string(xml)
    assert doc.encoding in ('UTF-8', 'utf-8', None)


def test_parsed_doc_version():
    doc = Parser().parse_string('<root/>')
    assert doc.version in ('1.0', None)


def test_parsed_doc_element():
    doc = Parser().parse_string('<root attr="val"><child/></root>')
    root = doc.documentElement
    assert root.nodeName == 'root'
    assert root.getAttribute('attr') == 'val'

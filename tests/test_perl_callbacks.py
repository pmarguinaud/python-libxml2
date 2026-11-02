"""
t/17callbacks.t, t/27new_callbacks_simple.t, t/28new_callbacks_multiple.t,
t/47load_xml_callbacks.t, t/49callbacks_returning_undef.t and the t/49global_extent*.t
files, then the push parser, then what Perl does not test: callbacks that raise,
reenter or run in threads.
"""
import gc
import os
import threading
import time

import pytest

import xml_libxml
from xml_libxml import (Document, Error, InputCallback, Parser, RelaxNG, Schema,
                        externalEntityLoader)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'tests', 'data')
XINCLUDE = 'http://www.w3.org/2001/XInclude'


@pytest.fixture(autouse=True)
def clean_state(monkeypatch):
    monkeypatch.chdir(ROOT)
    yield
    externalEntityLoader(None)
    xml_libxml.callbacks(None, None, None, None)


class Count:
    """Wraps a function and counts the calls."""

    def __init__(self, func):
        self.func = func
        self.calls = []

    def __call__(self, *args):
        self.calls.append(args)
        return self.func(*args)

    def take(self):
        count = len(self.calls)
        del self.calls[:]
        return count


def file_set(pattern='/example/'):
    """Callbacks for files, under /example/ if that is the pattern."""
    return (Count(lambda uri: uri.startswith(pattern)),
            Count(lambda uri: open('.' + uri)),
            Count(lambda fh, length: fh.read(length)),
            Count(lambda fh: fh.close()))


def plain_file_set():
    return (Count(lambda uri: True),
            Count(lambda uri: open(uri)),
            Count(lambda fh, length: fh.read(length)),
            Count(lambda fh: fh.close()))


def lines_set(pattern):
    def read(handle, length):
        line = handle['line']
        handle['line'] += 1
        lines = handle['lines']
        return lines[line] if line < len(lines) else ''

    return (Count(lambda uri: uri.startswith(pattern)),
            Count(lambda uri: {'line': 0,
                               'lines': ['<foo>', 'bar', '<xsl/>', '..', '</foo>']}),
            Count(read),
            Count(lambda handle: None))


def dom_set(pattern='/xmldom/', source='<?xml version="1.0"?><foo><tmp/>barbar</foo>'):
    results = []

    def read(dom, length):
        tmp = dom.documentElement.findnodes('tmp')
        text = dom.toString() if tmp else ''
        if tmp:
            tmp[0].unbindNode()
        results.append(text)
        return text

    return (Count(lambda uri: uri.startswith(pattern)),
            Count(lambda uri: Parser().parse_string(source)),
            read,
            Count(lambda dom: 1)), results


def counts(cbset):
    return [c.take() for c in cbset]


# -------------------------------------------------------------- t/17callbacks.t

def test_parser_callbacks():
    cbset = plain_file_set()
    parser = Parser(expand_xinclude=1)
    parser.match_callback(cbset[0])
    parser.read_callback(cbset[2])
    parser.open_callback(cbset[1])
    parser.close_callback(cbset[3])

    dom = parser.parse_file('example/test.xml')
    match, opened, read, closed = counts(cbset)
    assert (match, opened, closed) == (2, 2, 2)
    assert read >= 2
    assert dom.getDocumentElement().findnodes('xml/xsl')


def test_callbacks_are_per_parser():
    first, second = plain_file_set(), plain_file_set()
    parser, parser2 = Parser(expand_xinclude=1), Parser(expand_xinclude=1)
    parser.callbacks(*first)
    parser2.callbacks(*second)
    assert parser.callbacks() == first

    dom1 = parser.parse_file('example/test.xml')
    assert first[1].take() == 2 and second[1].take() == 0
    dom2 = parser2.parse_file('example/test.xml')
    assert first[1].take() == 0 and second[1].take() == 2
    assert dom1.findnodes('/x/xml/text()')[0].string_value().strip() == 'test'
    assert dom2.findnodes('/x/xml/text()')[0].string_value().strip() == 'test'


def test_callbacks_within_dtds(monkeypatch):
    monkeypatch.chdir(os.path.join(ROOT, 'example', 'complex'))
    with open('complex.xml') as fh:
        text = fh.read()
    assert Parser(expand_xinclude=1).parse_string(text)


def test_global_callbacks(monkeypatch):
    monkeypatch.chdir(os.path.join(ROOT, 'example', 'complex'))
    with open('complex.xml') as fh:
        text = fh.read()

    cbset = plain_file_set()
    xml_libxml.match_callback(cbset[0])
    xml_libxml.open_callback(cbset[1])
    xml_libxml.read_callback(cbset[2])
    xml_libxml.close_callback(cbset[3])
    assert xml_libxml.callbacks() == cbset

    assert Parser(load_ext_dtd=1).parse_string(text)
    match, opened, read, closed = counts(cbset)
    assert (match, opened, closed) == (3, 3, 3)
    assert read >= 3

    xml_libxml.callbacks(None, None, None, None)
    assert Parser(load_ext_dtd=1).parse_string(text)
    assert counts(cbset) == [0, 0, 0, 0]


def test_setting_a_callback_drops_the_input_callback():
    parser = Parser()
    icb = InputCallback()
    parser.input_callbacks(icb)
    assert parser.input_callbacks() is icb
    parser.match_callback(lambda uri: False)
    assert parser.input_callbacks() is None


# ------------------------------------------------------ t/27new_callbacks_simple.t

SIMPLE = ('<x xmlns:xinclude="%s"><xml>test<xinclude:include href="/example/test2.xml"/>'
          '</xml></x>\n' % XINCLUDE)


def test_simple_file_callbacks():
    cbset = file_set()
    icb = InputCallback()
    icb.register_callbacks(cbset)
    parser = Parser(expand_xinclude=1)
    parser.input_callbacks(icb)

    doc = parser.parse_string(SIMPLE)
    # Recent libxml2 asks match twice for a file. Perl counts once with older ones.
    match, opened, read, closed = counts(cbset)
    assert match >= 1 and (opened, read, closed) == (1, 2, 1)
    assert doc.string_value() == 'test..'


def test_simple_hash_callbacks():
    cbset = lines_set('/example/')
    icb = InputCallback()
    icb.register_callbacks(cbset)
    parser = Parser(expand_xinclude=1)
    parser.input_callbacks(icb)

    doc = parser.parse_string(SIMPLE)
    match, opened, read, closed = counts(cbset)
    assert match >= 1 and (opened, read, closed) == (1, 6, 1)
    assert doc.string_value() == 'testbar..'


# ---------------------------------------------------- t/28new_callbacks_multiple.t

MULTIPLE = """<x xmlns:xinclude="%s">
<xml>test
<xinclude:include href="/example/test2.xml"/>
<xinclude:include href="/libxml/test2.xml"/>
<xinclude:include href="/xmldom/test2.xml"/></xml>
</x>
""" % XINCLUDE


def test_multiple_callbacks():
    files, hashes = file_set(), lines_set('/libxml/')
    doms, results = dom_set()
    icb = InputCallback()
    icb.register_callbacks(files)
    icb.register_callbacks(hashes)
    icb.register_callbacks(doms)
    parser = Parser(expand_xinclude=1)
    parser.input_callbacks(icb)

    for _ in range(2):      # https://rt.cpan.org/Ticket/Display.html?id=51086
        doc = parser.parse_string(MULTIPLE)
        assert hashes[2].take() == 6
        assert files[2].take() == 2
        assert files[3].take() == 1
        assert [c[0] for c in files[1].calls] == ['/example/test2.xml']
        files[1].take()
        assert results == ['<?xml version="1.0"?>\n<foo><tmp/>barbar</foo>\n', '']
        del results[:]
        assert hashes[1].take() == 1 and hashes[3].take() == 1
        assert doms[1].take() == 1 and doms[3].take() == 1
        assert doc.string_value() == "\ntest\n..\nbar..\nbarbar\n"


def test_latest_registered_wins_and_unregister():
    string = """<x xmlns:xinclude="%s">
<xml>test
<xinclude:include href="/example/test2.xml"/>
<xinclude:include href="/example/test3.xml"/></xml>
</x>
""" % XINCLUDE
    files, hashes = file_set(), lines_set('/example/')
    icb = InputCallback()
    icb.register_callbacks(files)
    icb.register_callbacks(hashes)
    parser = Parser(expand_xinclude=1)
    parser.input_callbacks(icb)

    doc = parser.parse_string(string)
    assert hashes[2].take() == 12
    assert files[1].take() == 0
    assert doc.string_value() == "\ntest\nbar..\nbar..\n"
    assert hashes[1].take() == 2 and hashes[3].take() == 2

    icb.unregister_callbacks(hashes)
    doc = parser.parse_string(string)
    assert files[2].take() == 4 and files[3].take() == 2
    assert files[1].take() == 2
    assert doc.string_value() == "\ntest\n..\n\n         \n   \n"

    icb.unregister_callbacks()
    assert icb._callbacks == []
    icb.unregister_callbacks()


def test_nested_parser_in_open_callback():
    string = """<x xmlns:xinclude="%s">
<xml>test
<xinclude:include href="/example/test2.xml"/>
<xinclude:include href="/xmldom/test2.xml"/></xml>
</x>
""" % XINCLUDE
    string2 = """<x xmlns:xinclude="%s">
<tmp/><xml>foo..<xinclude:include href="/example/test2.xml"/>bar</xml>
</x>
""" % XINCLUDE

    icb = InputCallback()
    parsed = []

    def open_xml2(uri):
        parser = Parser(expand_xinclude=1)
        parser.input_callbacks(icb)
        dom = parser.parse_string(string2)
        parsed.append(dom)
        return dom

    doms, results = dom_set()
    hashes = lines_set('/example/')
    icb.register_callbacks((doms[0], open_xml2, doms[2], doms[3]))
    icb.register_callbacks(hashes)

    files = file_set()
    parser = Parser(expand_xinclude=1)
    parser.callbacks(*files)
    parser.input_callbacks(icb)

    doc = parser.parse_string(string)
    assert len(parsed) == 1
    assert hashes[2].take() == 6
    assert hashes[1].take() == 1 and hashes[3].take() == 1
    assert doms[3].take() == 1
    assert files[2].take() == 2 and files[3].take() == 1
    assert results == [
        '<?xml version="1.0"?>\n<x xmlns:xinclude="%s">\n<tmp/><xml>foo..<foo '
        'xml:base="/example/test2.xml">bar<xsl/>..</foo>bar</xml>\n</x>\n' % XINCLUDE, '']
    assert doc.string_value() == "\ntest\n..\n\nfoo..bar..bar\n\n"


# ------------------------------------------- t/47load_xml, t/49callbacks_returning_undef

def test_load_xml_uses_callbacks():
    cbset = plain_file_set()
    icb = InputCallback()
    icb.register_callbacks(cbset)
    parser = Parser()
    parser.input_callbacks(icb)
    parser.load_xml(location='example/dromeds.xml')
    match, opened, read, closed = counts(cbset)
    assert (opened, closed) == (1, 1) and read > 0


def test_callbacks_returning_none():
    path = os.path.join(DATA, 'callbacks_returning_undef.xml')
    string = """<?xml version="1.0" encoding="us-ascii"?>
<!DOCTYPE foo [
    <!ENTITY foo SYSTEM "file://%s">
]>
<methodCall><value><string>Entity test: &foo;</string></value></methodCall>
""" % path
    matches = [True]

    def match(uri):
        return matches.pop() if matches else False

    icb = InputCallback()
    icb.register_callbacks([match, lambda uri: None, None, None])
    parser = Parser(expand_entities=1)
    parser.input_callbacks(icb)
    assert parser.parse_string(string).findvalue('//string').startswith('Entity test: ')


# ------------------------------------------------------- t/49global_extent*.t

ENTITY = """<?xml version="1.0"?>
<!DOCTYPE foo [
<!ENTITY a PUBLIC "//foo/bar/b" "http:///invalid-url">
]>
<root>
  <a>&a;</a>
</root>
"""


def handler_global(url, ident):
    return 'global,%s,%s' % (url, ident)


def handler_private(url, ident):
    return 'private,%s,%s' % (url, ident)


def expanding():
    return Parser(expand_entities=1)


def test_global_loader():
    with pytest.raises(Error):
        expanding().parse_string(ENTITY)
    assert externalEntityLoader(handler_global) is None
    doc = expanding().parse_string(ENTITY)
    assert doc.findvalue('//a') == 'global,http:///invalid-url,//foo/bar/b'


def test_global_loader_goes_back():
    second = lambda url, ident: 'second'
    externalEntityLoader(handler_global)
    assert externalEntityLoader(second) is handler_global
    assert expanding().parse_string(ENTITY).findvalue('//a') == 'second'
    assert externalEntityLoader(None) is second
    assert externalEntityLoader(None) is None
    with pytest.raises(Error):
        expanding().parse_string(ENTITY)
    with pytest.raises(TypeError):
        externalEntityLoader('no')


def test_private_handler_and_global_loader():
    parser = Parser(expand_entities=1, ext_ent_handler=handler_private)
    assert parser.parse_string(ENTITY).findvalue('//a') == \
        'private,http:///invalid-url,//foo/bar/b'
    externalEntityLoader(handler_global)
    assert parser.parse_string(ENTITY).findvalue('//a') == \
        'global,http:///invalid-url,//foo/bar/b'
    externalEntityLoader(None)
    assert parser.parse_string(ENTITY).findvalue('//a') == \
        'private,http:///invalid-url,//foo/bar/b'


@pytest.mark.parametrize('scheme', ['http', 'https', 'ftp', 'gopher'])
def test_global_loader_is_above_no_network(scheme):
    xml = ('<?xml version="1.0"?>\n<!DOCTYPE foo [\n<!ENTITY a SYSTEM "%s:///u">\n]>\n'
           '<root>&a;</root>\n' % scheme)
    seen = []

    def loader(url, ident):
        seen.append(url)
        return 'x'

    externalEntityLoader(loader)
    doc = Parser(expand_entities=1, no_network=1).parse_string(xml)
    assert doc.findvalue('/root') == 'x'
    assert seen == ['%s:///u' % scheme]


def test_global_loader_and_schemas():
    rng = """<grammar xmlns="http://relaxng.org/ns/structure/1.0">
  <include href="http://example.invalid/x.rng"/>
  <start><ref name="i"/></start><define name="i"><element name="i"><text/></element></define>
</grammar>"""
    xsd = """<xsd:schema xmlns:xsd="http://www.w3.org/2001/XMLSchema">
  <xsd:import namespace="urn:x" schemaLocation="http://example.invalid/x.xsd"/>
</xsd:schema>"""
    with pytest.raises(Error, match='Attempt to load network entity'):
        RelaxNG(string=rng, no_network=1)
    with pytest.raises(Error, match='Attempt to load network entity'):
        Schema(string=xsd, no_network=1)

    seen = []

    def loader(url, ident):
        seen.append(url)
        if url.endswith('.rng'):
            return '<grammar xmlns="http://relaxng.org/ns/structure/1.0"/>'
        return '<xsd:schema xmlns:xsd="http://www.w3.org/2001/XMLSchema" targetNamespace="urn:x"/>'

    externalEntityLoader(loader)
    assert RelaxNG(string=rng, no_network=1)
    assert Schema(string=xsd, no_network=1)
    assert seen == ['http://example.invalid/x.rng', 'http://example.invalid/x.xsd']


def test_no_network_after_schema_with_loader_failure():
    """The loader has no state to restore when a schema fails."""
    with pytest.raises(Error):
        RelaxNG(location=os.path.join(DATA, 'relaxng', 'net.rng'))
    assert Parser(load_ext_dtd=1).parse_file('example/dtd.xml')


# --------------------------------------------------------------- push parser

def test_push():
    parser = Parser()
    parser.push('<a>', '<b/>', '</a>')
    assert parser.finish_push().toString() == '<?xml version="1.0"?>\n<a><b/></a>\n'
    assert parser.finish_push() is None


def test_push_bad_xml():
    parser = Parser()
    with pytest.raises(Error, match='mismatch'):
        parser.push('<a></b>')
    with pytest.raises(Error):
        parser.finish_push()

    parser.push('<a/>')
    assert parser.finish_push().documentElement.nodeName == 'a'


def test_finish_push_restore():
    parser = Parser()
    with pytest.raises(Error):
        parser.push('<a></b>')
    assert parser.finish_push(1).documentElement.nodeName == 'a'

    parser.push('<a><b>')
    with pytest.raises(Error):
        parser.finish_push()

    parser.push('<a><b>')
    with pytest.warns(RuntimeWarning):
        doc = parser.finish_push(restore=True)
    assert doc.documentElement.firstChild.nodeName == 'b'


def test_push_in_recover_mode_still_raises():
    parser = Parser(recover=2)
    with pytest.raises(Error, match='not well-formed'):
        parser.push('<a></b>')
    with pytest.raises(Error):
        parser.finish_push()


def test_parse_chunk():
    parser = Parser()
    assert parser.parse_chunk('<a>') is None
    parser.parse_chunk('</a>')
    assert parser.parse_chunk('', 1).documentElement.nodeName == 'a'
    assert parser.parse_chunk('<a/>', True).documentElement.nodeName == 'a'
    assert parser.parse_chunk(b'<a/>', terminate=True).documentElement.nodeName == 'a'
    with pytest.raises(Error):
        parser.parse_chunk('', 1)


def test_init_push_starts_again():
    parser = Parser()
    parser.push('<a>')
    parser.init_push()
    parser.push('<b/>')
    assert parser.finish_push().documentElement.nodeName == 'b'
    parser.init_push()
    with pytest.raises(Error):
        parser.finish_push()


def test_push_skips_empty_chunks():
    parser = Parser()
    parser.push('', None, b'')
    assert parser.finish_push() is None


def test_push_type_error():
    parser = Parser()
    with pytest.raises(TypeError):
        parser.push(12)


def test_push_splits_multibyte_characters():
    data = '<a>\u00e9\u20ac\U0001d522</a>'.encode('utf-8')
    parser = Parser()
    for i in range(len(data)):
        parser.push(data[i:i + 1])
    assert parser.finish_push().documentElement.textContent == '\u00e9\u20ac\U0001d522'


def test_push_str_with_a_declared_encoding():
    parser = Parser()
    parser.push('<?xml version="1.0" encoding="iso-8859-1"?>\n<a>\u00e9\u20ac')
    parser.push('</a>')
    doc = parser.finish_push()
    assert doc.documentElement.textContent == '\u00e9\u20ac'
    assert doc.encoding == 'iso-8859-1'


def test_push_bytes_with_a_declared_encoding():
    parser = Parser()
    parser.push('<?xml version="1.0" encoding="iso-8859-1"?>\n<a>\u00e9</a>'.encode('latin-1'))
    assert parser.finish_push().documentElement.textContent == '\u00e9'


def test_push_uses_options():
    parser = Parser(no_blanks=1, URI='urn:base')
    parser.push('<a> <b/> </a>')
    doc = parser.finish_push()
    assert doc.documentElement.toString() == '<a><b/></a>'
    assert doc.URI == 'urn:base'


def test_push_uses_callbacks():
    seen = []

    def handler(url, ident):
        seen.append(url)
        return 'text'

    parser = Parser(expand_entities=1, ext_ent_handler=handler)
    parser.push('<!DOCTYPE a [<!ENTITY e SYSTEM "urn:e">]>', '<a>&e;</a>')
    assert parser.finish_push().findvalue('/a') == 'text'
    assert seen == ['urn:e']


def test_push_xinclude():
    parser = Parser(expand_xinclude=1, no_xinclude_nodes=1)
    parser.push('<x xmlns:xi="%s"><xi:include href="example/test2.xml"/></x>' % XINCLUDE)
    assert parser.finish_push().documentElement.firstChild.nodeName == 'xsl'


def test_parser_dropped_during_push():
    parser = Parser()
    parser.push('<a><b>')
    del parser
    gc.collect()


def test_parse_while_pushing():
    parser = Parser()
    parser.push('<a>')
    assert parser.parse_string('<z/>').documentElement.nodeName == 'z'
    parser.push('</a>')
    assert parser.finish_push().documentElement.nodeName == 'a'


def test_parse_xml_chunk():
    frag = Parser().parse_xml_chunk('<a/><b/>')
    assert len(frag.childNodes()) == 2


# ---------------------------------------------------------- callbacks that raise

class Boom(Exception):
    pass


def boom(*args):
    raise Boom('callback')


def in_example(uri):
    # Not all: the first parse of the process also loads the XML catalog.
    return uri.startswith('/example/')


def parse_with(cbset, expect=Boom):
    icb = InputCallback()
    icb.register_callbacks(cbset)
    parser = Parser(expand_xinclude=1)
    parser.input_callbacks(icb)
    with pytest.raises(expect):
        parser.parse_string(SIMPLE)
    assert Parser(expand_xinclude=1).parse_string(SIMPLE.replace('/example', 'example'))


def test_match_raises():
    parse_with((boom, boom, boom, boom))


def test_open_raises():
    parse_with((in_example, boom, boom, boom))


def test_read_raises_and_close_still_runs():
    closed = Count(lambda fh: fh.close())
    parse_with((in_example, lambda uri: open('.' + uri), boom, closed))
    assert closed.take() == 1


def test_read_raises_and_close_raises_too():
    def close(fh):
        fh.close()
        raise ValueError('second')

    parse_with((in_example, lambda uri: open('.' + uri), boom, close))


def test_close_raises():
    def close(fh):
        fh.close()
        raise Boom('close')

    parse_with((in_example, lambda uri: open('.' + uri),
                lambda fh, length: fh.read(length), close))


def test_read_returns_a_wrong_type():
    parse_with((in_example, lambda uri: open('.' + uri), lambda fh, n: 12,
                lambda fh: fh.close()), TypeError)


def test_incomplete_parser_callbacks_are_not_used():
    parser = Parser(expand_xinclude=1)
    parser.callbacks(boom, boom, None, boom)
    with pytest.raises(Error, match='could not load'):
        parser.parse_string(SIMPLE)


def test_read_is_not_callable():
    parse_with((in_example, lambda uri: open('.' + uri), None,
                lambda fh: fh.close()), TypeError)


def test_handler_raises():
    with pytest.raises(Boom):
        Parser(expand_entities=1, ext_ent_handler=boom).parse_string(ENTITY)
    with pytest.raises(TypeError):
        Parser(expand_entities=1,
               ext_ent_handler=lambda url, ident: 12).parse_string(ENTITY)


def test_global_loader_raises():
    externalEntityLoader(boom)
    with pytest.raises(Boom):
        expanding().parse_string(ENTITY)
    with pytest.raises(Boom):
        RelaxNG(location=os.path.join(DATA, 'relaxng', 'net.rng'))
    with pytest.raises(Boom):
        Schema(location=os.path.join(DATA, 'schema', 'net.xsd'))


def test_callback_raises_in_push():
    parser = Parser(expand_entities=1, ext_ent_handler=boom)
    with pytest.raises(Boom):
        parser.push(ENTITY)
    parser.init_push()
    parser.ext_ent_handler = None
    parser.push('<a/>')
    assert parser.finish_push().documentElement.nodeName == 'a'


def test_read_gives_more_than_asked():
    text = '<foo>' + 'x' * 500000 + '</foo>'
    sent = []

    def read(handle, length):
        if sent:
            return ''
        sent.append(1)
        return text

    parser = Parser(expand_xinclude=1, huge=1)
    parser.callbacks(in_example, lambda uri: 1, read, lambda handle: None)
    doc = parser.parse_string(SIMPLE)
    assert len(doc.findvalue('//foo')) == 500000


def test_read_gives_bytes_and_none():
    chunks = [b'<foo>', bytearray(b'b'), b'</foo>', None]
    parser = Parser(expand_xinclude=1)
    parser.callbacks(in_example, lambda uri: 1, lambda h, n: chunks.pop(0),
                     lambda handle: None)
    assert parser.parse_string(SIMPLE).findvalue('//foo') == 'b'


# ------------------------------------------------------------- reentrancy, threads

def test_nested_ext_ent_handler():
    """A handler that starts a parse with a handler leaves the loader as it was."""
    inner = Parser(expand_entities=1, ext_ent_handler=lambda url, ident: 'inner')

    def handler(url, ident):
        return inner.parse_string(ENTITY).findvalue('//a').strip() + '-outer'

    outer = Parser(expand_entities=1, ext_ent_handler=handler)
    assert outer.parse_string(ENTITY).findvalue('//a') == 'inner-outer'
    assert outer.parse_string(ENTITY).findvalue('//a') == 'inner-outer'
    assert Parser(load_ext_dtd=1).parse_file('example/dtd.xml')
    with pytest.raises(Error):
        expanding().parse_string(ENTITY)


def test_nested_failure_stays_in_the_callback():
    """The errors of a parse in a callback are not those of the outer parse."""
    def handler(url, ident):
        with pytest.raises(Error):
            Parser().parse_string('<bad')
        return '<broken'

    with pytest.raises(Error) as info:
        Parser(expand_entities=1, ext_ent_handler=handler).parse_string(ENTITY)
    assert '<bad' not in str(info.value)


def test_handler_replaces_itself():
    def handler(url, ident):
        externalEntityLoader(None)
        return 'once'

    externalEntityLoader(handler)
    del_handler = handler
    del handler
    assert expanding().parse_string(ENTITY).findvalue('//a') == 'once'
    assert externalEntityLoader(None) is None
    del del_handler


def test_callback_changes_the_parser():
    parser = Parser(expand_xinclude=1)
    icb = InputCallback()
    cbset = file_set()

    def match(uri):
        parser.input_callbacks(None)
        icb.unregister_callbacks()
        return cbset[0](uri)

    icb.register_callbacks((match, cbset[1], cbset[2], cbset[3]))
    parser.input_callbacks(icb)
    assert parser.parse_string(SIMPLE).string_value() == 'test..'
    assert parser.input_callbacks() is None


def test_push_from_a_callback_is_refused():
    errors = []

    def handler(url, ident):
        for call in (lambda: parser.push('<x/>'), parser.finish_push, parser.init_push,
                     lambda: parser.parse_chunk('', 1)):
            with pytest.raises(Error, match='in the middle of a push') as info:
                call()
            errors.append(info.value)
        return 'text'

    parser = Parser(expand_entities=1, ext_ent_handler=handler)
    parser.push('<!DOCTYPE a [<!ENTITY e SYSTEM "urn:e">]>', '<a>&e;</a>')
    assert len(errors) == 4
    assert parser.finish_push().findvalue('/a') == 'text'


def test_parse_with_the_same_parser_in_a_callback():
    calls = []

    def handler(url, ident):
        calls.append(url)
        if len(calls) > 3:
            return 'end'
        return parser.parse_string(ENTITY).findvalue('//a').strip() + '.'

    parser = Parser(expand_entities=1, ext_ent_handler=handler)
    assert parser.parse_string(ENTITY).findvalue('//a') == 'end...'


def test_callback_drops_the_only_reference_to_the_parser():
    holder = [Parser(expand_xinclude=1)]

    def match(uri):
        del holder[:]
        gc.collect()
        return uri.startswith('/example/')

    cbset = file_set()
    holder[0].callbacks(match, cbset[1], cbset[2], cbset[3])
    doc = holder[0].parse_string(SIMPLE)
    assert doc.string_value() == 'test..'


def test_threads_keep_their_callbacks():
    errors = []

    def work(tag):
        def read(handle, length):
            time.sleep(0)
            if handle['done']:
                return ''
            handle['done'] = True
            return '<foo>%s</foo>' % tag

        parser = Parser(expand_xinclude=1)
        # Only /example/: the first parse of the process also loads the XML catalog.
        parser.callbacks(lambda uri: (time.sleep(0), uri.startswith('/example/'))[1],
                         lambda uri: {'done': False},
                         read, lambda handle: time.sleep(0))
        try:
            for _ in range(50):
                value = parser.parse_string(SIMPLE).findvalue('//foo')
                if value != str(tag):
                    errors.append(value)
        except Exception as error:      # noqa: reported through the list
            errors.append(error)

    threads = [threading.Thread(target=work, args=(n,)) for n in range(6)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert errors == []


def test_parse_in_a_thread_during_a_callback():
    results = []

    def handler(url, ident):
        def other():
            results.append(Parser().parse_string('<t/>').documentElement.nodeName)
        thread = threading.Thread(target=other)
        thread.start()
        thread.join()
        return 'x'

    Parser(expand_entities=1, ext_ent_handler=handler).parse_string(ENTITY)
    assert results == ['t']


def test_callback_closes_after_the_parser_is_gone():
    closed = Count(lambda fh: fh.close())
    parser = Parser(load_ext_dtd=1)
    parser.callbacks(lambda uri: True, lambda uri: open(uri),
                     lambda fh, n: fh.read(n), closed)
    parser.parse_file('example/dtd.xml')
    assert closed.take() >= 1

"""
t/14sax.t and t/48_SAX_Builder_rt_91433.t, then what makes SAX safe: handlers that
raise, parse, or use the parser that calls them.

XML::SAX::ParserFactory is not ported: the tests that use it name the class it
would pick, SAX.Parser or SAX.SAX.
"""
import gc
import os
import threading
import warnings
import weakref

import pytest

import xml_libxml as X
from xml_libxml import SAX

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'tests', 'data', 'sax')
DROMEDS = os.path.join(DATA, 'dromeds.xml')
NS = os.path.join(DATA, 'ns.xml')


class Recorder:
    """Keeps every event: (name, argument)."""

    def __init__(self):
        self.events = []

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)

        def event(*args):
            self.events.append((name,) + args)
        return event

    def names(self):
        return [e[0] for e in self.events]

    def of(self, name):
        return [e[1] if len(e) > 1 else None for e in self.events if e[0] == name]


# ------------------------------------------------------------------ t/14sax.t

class SAXTester:
    def __init__(self):
        self.starts = self.ends = 0
        self.elements = []

    def start_document(self, doc):
        self.starts += 1

    def end_document(self, doc):
        self.ends += 1

    def start_element(self, el):
        self.elements.append(el['LocalName'] in
                             ('dromedaries', 'species', 'humps', 'disposition', 'legs'))

    def end_element(self, el):
        pass

    def characters(self, chars):
        pass


def test_generate_from_a_document():
    text = open(DROMEDS).read()
    doc = X.Parser().parse_string(text)
    sax = SAXTester()
    SAX.Parser(Handler=sax).generate(doc)
    assert sax.elements == [True] * 10
    assert (sax.starts, sax.ends) == (1, 1)

    dom2 = SAX.Parser(Handler=SAX.Builder()).generate(doc)
    assert isinstance(dom2, X.Document)
    assert dom2.toString() == text


@pytest.mark.parametrize('driver', [SAX.Parser, SAX.SAX])
def test_parse_uri_and_string(driver):
    sax = SAXTester()
    parser = driver(Handler=sax)
    parser.parse_uri(DROMEDS)
    assert sax.elements == [True] * 10
    assert (sax.starts, sax.ends) == (1, 1)

    sax.elements, sax.starts, sax.ends = [], 0, 0
    parser.parse_string('<?xml version=\'1.0\' encoding="US-ASCII"?>\n<dromedaries one="1" />\n')
    assert sax.elements == [True]
    assert (sax.starts, sax.ends) == (1, 1)


class SAXNSTester:
    def __init__(self):
        self.elements, self.starts, self.ends = [], [], []

    def start_element(self, node):
        self.elements.append(node['NamespaceURI'].startswith('urn:'))

    def end_element(self, node):
        pass

    def start_prefix_mapping(self, node):
        self.starts.append(node['NamespaceURI'] in ('urn:camels', 'urn:mammals', 'urn:a'))

    def end_prefix_mapping(self, node):
        self.ends.append(node['NamespaceURI'] in ('urn:camels', 'urn:mammals', 'urn:a'))


@pytest.mark.parametrize('driver', [SAX.Parser, SAX.SAX])
def test_namespaces(driver):
    parser = driver(Handler=SAXTester())
    sax = SAXNSTester()
    parser.set_handler(sax)
    parser.parse_uri(NS)
    assert sax.elements == [True, True, True]
    assert sax.starts == [True, True, True]
    assert sax.ends == [True, True, True]


class SAXLocatorTester:
    def __init__(self):
        self.stack = []

    def set_document_locator(self, locator):
        self.locator = locator

    def __getattr__(self, name):
        if name not in ('start_document', 'end_document', 'start_element', 'end_element',
                        'start_cdata', 'end_cdata', 'start_dtd', 'end_dtd', 'characters',
                        'comment'):
            raise AttributeError(name)

        def event(*args):
            self.stack.append((name, [self.locator['LineNumber'],
                                      self.locator['ColumnNumber']]))
        return event


def test_locator():
    sax = SAXLocatorTester()
    SAX.SAX(Handler=sax).parse_string(
        '<?xml version="1.0" encoding="UTF-8"?>\n<root>\n1\n<!-- comment -->\n'
        '<![CDATA[ a < b ]]>\n</root>\n')
    assert sax.stack == [
        ('start_document', [2, 1]),
        ('start_element', [2, 6]),
        ('characters', [4, 1]),
        ('comment', [4, 17]),
        ('characters', [5, 1]),
        ('start_cdata', [5, 20]),
        ('characters', [5, 20]),
        ('end_cdata', [5, 20]),
        ('characters', [6, 1]),
        ('end_element', [6, 8]),
        ('end_document', [6, 8]),
    ]


class SAXNS2Tester:
    def __init__(self):
        self.found = []

    def start_element(self, elt):
        if elt['Name'] == 'b':
            self.found.append(elt['NamespaceURI'] == 'xml://A')


@pytest.mark.parametrize('driver', [SAX.SAX, SAX.Parser])
def test_default_namespace(driver):
    handler = SAXNS2Tester()
    driver(Handler=handler).parse_string("<a xmlns='xml://A'><b/></a>")
    assert handler.found == [True]


class SAXErrorTester:
    def end_document(self, *args):
        return 1  # not reached


class SAXErrorCallbackTester:
    fatal_called = False

    def fatal_error(self, error):
        self.fatal_called = True

    def start_element(self, el):
        # the handler can parse
        X.Parser().parse_string('<foo/>')

    def end_document(self, *args):
        return 1  # not reached


BAD = '<foo><bar/><a>Text</b></foo>'


@pytest.mark.parametrize('driver', [SAX.Parser, SAX.SAX])
def test_errors(driver):
    with pytest.raises(X.Error):
        driver(Handler=SAXErrorTester()).parse_string(BAD)


def test_error_callback():
    handler = SAXErrorCallbackTester()
    with pytest.raises(X.Error):
        SAX.SAX(Handler=handler).parse_string(BAD)
    assert handler.fatal_called


def test_parse_chunk():
    chunk = '<app>LOGOUT</app><bar/>'
    builder = SAX.Builder(Encoding='UTF-8')
    parser = SAX.SAX(Handler=builder)
    parser.start_document()
    builder.start_element({'Name': 'foo'})
    parser.parse_chunk(chunk)
    parser.parse_chunk(chunk)
    builder.end_element({'Name': 'foo'})
    parser.end_document()
    assert builder.result().documentElement.toString() == '<foo>' + chunk + chunk + '</foo>'


class MySAXException(Exception):
    def __init__(self, Message):
        super().__init__(Message)
        self.Message = Message


class MySAXHandler(SAX.Base):
    def start_element(self, elm):
        if elm['LocalName'] == 'TVChannel':
            raise MySAXException('My exception')


def test_handler_exception():
    parser = SAX.SAX(Handler=MySAXHandler())
    with pytest.raises(MySAXException) as info:
        parser.parse_string('<TVChannel TVChannelID="71" TVChannelName="ARD">\n'
                            '        <Moin>Moin</Moin>\n</TVChannel>\n')
    assert info.value.Message == 'My exception'


# ------------------------------------------------------------------ t/48_SAX_Builder_rt_91433.t

OAI = '''<?xml version="1.0" encoding="UTF-8"?>
<OAI-PMH xsi:schemaLocation="http://www.openarchives.org/OAI/2.0/ http://www.openarchives.org/OAI/2.0/OAI-PMH.xsd" xmlns="http://www.openarchives.org/OAI/2.0/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><responseDate>2013-12-16T20:19:20Z</responseDate><request metadataPrefix="RDFxml" verb="GetRecord" identifier="oai:dnb.de/authorities/1045601462">http://services.d-nb.de/oai/repository</request><GetRecord xsi:schemaLocation="http://www.w3.org/1999/02/22-rdf-syntax-ns http://www.w3.org/2000/07/rdf.xsd"><record><header><identifier>oai:dnb.de/authorities/1045601462</identifier><datestamp>2013-12-16T18:47:23Z</datestamp><setSpec>authorities</setSpec></header><metadata><rdf:RDF xmlns:gnd="http://d-nb.info/standards/elementset/gnd#" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:rda="http://rdvocab.info/" xmlns:foaf="http://xmlns.com/foaf/0.1/" xmlns:isbd="http://iflastandards.info/ns/isbd/elements/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:rdfs="http://www.w3.org/2000/01/rdf-schema#" xmlns:marcRole="http://id.loc.gov/vocabulary/relators/" xmlns:lib="http://purl.org/library/" xmlns:umbel="http://umbel.org/umbel#" xmlns:bibo="http://purl.org/ontology/bibo/" xmlns:owl="http://www.w3.org/2002/07/owl#" xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:skos="http://www.w3.org/2004/02/skos/core#">

	<rdf:Description rdf:about="http://d-nb.info/gnd/1045601462">
		<rdf:type rdf:resource="http://d-nb.info/standards/elementset/gnd#DifferentiatedPerson"/>
		<gnd:gndIdentifier>1045601462</gnd:gndIdentifier>
		<gnd:preferredNameForThePerson>Kencena, Rain</gnd:preferredNameForThePerson>
		<gnd:preferredNameEntityForThePerson rdf:parseType="Resource">
			<gnd:forename>Rain</gnd:forename>
			<gnd:surname>Kencena</gnd:surname>
		</gnd:preferredNameEntityForThePerson>
		<gnd:professionOrOccupation rdf:resource="http://d-nb.info/gnd/4281949-0"/>
		<gnd:geographicAreaCode rdf:resource="http://d-nb.info/standards/vocab/gnd/geographic-area-code#XB-TR"/>
		<gnd:dateOfBirthAndDeath>ca. 20. / 21. Jh.</gnd:dateOfBirthAndDeath>
		<gnd:gender rdf:resource="http://d-nb.info/standards/vocab/gnd/Gender#notKnown"/>
	</rdf:Description>
</rdf:RDF></metadata></record></GetRecord></OAI-PMH>
'''


class OuterSAX(SAX.Base):
    def __init__(self, **opts):
        super().__init__(**opts)
        self.set_handler(None)

    def start_element(self, element):
        if element['NamespaceURI'] != self.oaiNS:
            return super().start_element(element)
        if element['LocalName'] == 'metadata':
            self.old_handler = self.get_handler()
            self.set_handler(self.metadataHandler)
        else:
            return super().start_element(element)

    def end_element(self, element):
        if element['NamespaceURI'] != self.oaiNS:
            return super().end_element(element)
        if element['LocalName'] == 'metadata':
            self.set_handler(self.old_handler)
        else:
            super().end_element(element)


class InnerSAX(SAX.Base):
    def __init__(self, **opts):
        super().__init__(**opts)
        self.tag_stack = []
        self.documents = []

    def start_element(self, element):
        if not self.tag_stack:
            self.set_handler(SAX.Builder())
            super().start_document()
        super().start_element(element)
        self.tag_stack.append(element['Name'])

    def end_element(self, element):
        super().end_element(element)
        self.tag_stack.pop()
        if not self.tag_stack:
            handler = self.get_handler()
            self.set_handler(None)
            fragment = handler.done()
            child = fragment.firstChild
            while child is not None and child.nodeName == '#text':
                child = child.nextSibling
            tempdoc = X.Document.createDocument()
            assert tempdoc.addChild(child) is not None
            self.documents.append(tempdoc)


def test_builder_inside_a_filter():
    inner = InnerSAX()
    outer = OuterSAX(metadataHandler=inner, oaiNS='http://www.openarchives.org/OAI/2.0/')
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter('always')
        SAX.SAX(Handler=outer).parse_string(OAI)
    assert caught == []

    # What the inner handler built: the RDF part, with its namespaces
    [doc] = inner.documents
    root = doc.documentElement
    assert root.nodeName == 'rdf:RDF'
    assert root.namespaceURI == 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'
    assert len(root.getNamespaces()) == 14
    xpc = X.XPathContext(doc)
    xpc.registerNs('rdf', 'http://www.w3.org/1999/02/22-rdf-syntax-ns#')
    xpc.registerNs('gnd', 'http://d-nb.info/standards/elementset/gnd#')
    assert xpc.findvalue('//rdf:Description/@rdf:about') == 'http://d-nb.info/gnd/1045601462'
    assert xpc.findvalue('//gnd:forename') == 'Rain'


# ------------------------------------------------------------------ the events

def test_events_of_a_parser_with_a_handler():
    rec = Recorder()
    result = X.Parser(Handler=rec).parse_string(
        '<?xml version="1.0"?><!DOCTYPE a [<!ENTITY e "xx">]>'
        '<a xmlns="u:d" xmlns:p="u:p" p:x="1" y="2">t&e;&amp;<![CDATA[c]]><?pi d?>'
        '<!--c--><p:b/></a>')
    assert result is None
    assert rec.names() == [
        'set_document_locator', 'start_document', 'xml_decl', 'start_dtd', 'end_dtd',
        'start_prefix_mapping', 'start_prefix_mapping', 'start_element',
        'characters', 'characters', 'characters', 'start_cdata', 'characters', 'end_cdata',
        'processing_instruction', 'comment', 'start_element', 'end_element', 'end_element',
        'end_prefix_mapping', 'end_prefix_mapping', 'end_document']
    assert rec.of('xml_decl') == [{'Version': '1.0'}]
    assert rec.of('start_dtd') == [{'Name': 'a'}]
    assert rec.of('start_prefix_mapping') == [{'NamespaceURI': 'u:d', 'Prefix': ''},
                                              {'NamespaceURI': 'u:p', 'Prefix': 'p'}]
    assert rec.of('end_prefix_mapping') == rec.of('start_prefix_mapping')
    assert rec.of('start_element')[0] == {
        'Name': 'a', 'LocalName': 'a', 'Prefix': '', 'NamespaceURI': 'u:d',
        'Attributes': {
            '{}xmlns': {'Name': 'xmlns', 'LocalName': 'xmlns', 'Prefix': '',
                        'NamespaceURI': '', 'Value': 'u:d'},
            '{http://www.w3.org/2000/xmlns/}p': {
                'Name': 'xmlns:p', 'LocalName': 'p', 'Prefix': 'xmlns',
                'NamespaceURI': 'http://www.w3.org/2000/xmlns/', 'Value': 'u:p'},
            '{u:p}x': {'Name': 'p:x', 'LocalName': 'x', 'Prefix': 'p', 'NamespaceURI': 'u:p',
                       'Value': '1'},
            '{}y': {'Name': 'y', 'LocalName': 'y', 'Prefix': '', 'NamespaceURI': '',
                    'Value': '2'}}}
    assert rec.of('start_element')[1] == {'Name': 'p:b', 'LocalName': 'b', 'Prefix': 'p',
                                          'NamespaceURI': 'u:p', 'Attributes': {}}
    assert rec.of('end_element')[0] == {'Name': 'p:b', 'LocalName': 'b', 'Prefix': 'p',
                                        'NamespaceURI': 'u:p'}
    assert [c['Data'] for c in rec.of('characters')] == ['t', 'xx', '&', 'c']
    assert rec.of('processing_instruction') == [{'Target': 'pi', 'Data': 'd'}]
    assert rec.of('comment') == [{'Data': 'c'}]
    assert rec.of('start_cdata') == [None]


def test_end_document_gives_the_result():
    class H:
        def end_document(self, data):
            return 'result'
    parser = X.Parser(Handler=H())
    assert parser.parse_string('<a/>') == 'result'
    assert parser.parse_xml_chunk('<a/>b') == 'result'
    parser.push('<a>', '</a>')
    assert parser.finish_push() == 'result'


def test_external_subset_event():
    rec = Recorder()
    X.Parser(Handler=rec).parse_string('<!DOCTYPE a PUBLIC "pub" "sys.dtd"><a/>')
    assert rec.of('start_dtd') == [{'Name': 'a', 'PublicId': 'pub', 'SystemId': 'sys.dtd'}]
    assert rec.of('end_dtd') == [{}]


def test_attribute_values_and_entities():
    xml = '<!DOCTYPE a [<!ENTITY e "xx">]><a z="a&amp;b&e;&#65;&lt;"/>'
    for expand, value in ((0, 'a&#38;b&e;A<'), (1, 'a&bxxA<')):
        rec = Recorder()
        X.Parser(Handler=rec, expand_entities=expand).parse_string(xml)
        assert rec.of('start_element')[0]['Attributes']['{}z']['Value'] == value


def test_entities_expanded_in_content():
    rec = Recorder()
    X.Parser(Handler=rec, expand_entities=1).parse_string(
        '<!DOCTYPE a [<!ENTITY e "<b>x</b>y">]><a>&e;&e;</a>')
    assert [e['Name'] for e in rec.of('start_element')] == ['a', 'b', 'b']
    assert [c['Data'] for c in rec.of('characters')] == ['x', 'y', 'x', 'y']


def test_join_characters():
    text = '<a>one&amp;two<![CDATA[three]]>four<b/>five</a>'
    rec = Recorder()
    parser = SAX.SAX(Handler=rec)
    parser.set_feature(SAX.JOIN_CHARACTER_DATA, 1)
    assert parser.get_feature(SAX.JOIN_CHARACTER_DATA) == 1
    parser.parse_string(text)
    assert [c['Data'] for c in rec.of('characters')] == ['one&two', 'three', 'four', 'five']

    rec = Recorder()
    SAX.SAX(Handler=rec).parse_string(text)
    assert [c['Data'] for c in rec.of('characters')] == ['one', '&', 'two', 'three', 'four',
                                                        'five']


def test_handler_without_methods():
    class Nothing:
        pass
    assert X.Parser(Handler=Nothing()).parse_string('<a b="1">t<!--c--><?p?></a>') is None


def test_errors_reach_the_handler():
    rec = Recorder()
    with pytest.raises(X.Error):
        X.Parser(Handler=rec).parse_string('<a><b></a>')
    [error] = rec.of('fatal_error')[:1]
    assert isinstance(error, SAX.ParseException)
    assert error.Message.startswith('Opening and ending tag mismatch')
    assert (error.LineNumber, error.ColumnNumber) == (1, 11)
    assert str(error).endswith("[Ln: 1, Col: 11]")

    # recover: the handler is told, and the parse goes on
    rec = Recorder()
    with pytest.warns(RuntimeWarning):
        X.Parser(Handler=rec, recover=1).parse_string('<a><b></a>')
    assert rec.of('fatal_error')
    assert 'end_document' in rec.names()

    rec = Recorder()
    X.Parser(Handler=rec, recover=2).parse_string('<a><b></a>')
    assert 'end_document' in rec.names()


def test_warnings_reach_the_handler():
    rec = Recorder()
    X.Parser(Handler=rec).parse_string('<a xmlns="relative"/>')
    assert [w.Message for w in rec.of('warning')] == ['xmlns: URI relative is not absolute']


def test_builder_error_raises_the_exception():
    with pytest.raises(SAX.ParseException):
        SAX.SAX(Handler=SAX.Builder()).parse_string(BAD)


def test_builder_without_document_makes_a_fragment():
    builder = SAX.Builder()
    parser = SAX.SAX(Handler=builder)
    parser.parse_chunk('<x xmlns="u"><y/></x>text<!--c-->')
    fragment = builder.done()
    assert isinstance(fragment, X.DocumentFragment)
    assert fragment.toString() == '<x xmlns="u"><y/></x>text<!--c-->'


def test_builder_namespaces_of_siblings():
    # Perl's Builder puts b in u, as XML::NamespaceSupport keeps the default
    for xml in ('<r><a xmlns="u"/><b/></r>',
                '<r xmlns:p="v"><a xmlns:p="w"><p:c/></a><p:b/></r>'):
        assert SAX.SAX(Handler=SAX.Builder()).parse_string(xml).documentElement.toString() == xml


def test_builder_undeclared_default_namespace():
    # Perl loses xmlns="", and its text puts a and b in u.
    xml = '<r xmlns="u"><a xmlns=""><b/></a><c/></r>'
    doc = SAX.SAX(Handler=SAX.Builder()).parse_string(xml)
    assert [(e.nodeName, e.namespaceURI) for e in doc.findnodes('//*')] == [
        ('r', 'u'), ('a', None), ('b', None), ('c', 'u')]
    assert doc.documentElement.toString() == xml


def test_builder_round_trip():
    xml = ('<?xml version="1.0" encoding="ISO-8859-1"?>\n<!DOCTYPE r SYSTEM "r.dtd">\n'
           '<r xmlns:p="urn:p" a="1" p:b="2"><p:c>t\xe9<![CDATA[<x>]]></p:c><!--k--><?pi d?></r>\n')
    doc = X.Parser().parse_string(xml.encode('iso-8859-1'))
    for driver in (SAX.SAX, SAX.Parser):
        built = driver(Handler=SAX.Builder()).parse_string(xml.encode('iso-8859-1'))
        assert built.toString() == doc.toString()
        assert built.encoding == 'ISO-8859-1'


def test_generator_is_deprecated():
    doc = X.Parser().parse_string('<a x="1"><b>t</b><!--c--></a>')
    rec = Recorder()
    with pytest.warns(DeprecationWarning):
        generator = SAX.Generator(Handler=rec)
    generator.generate(doc)
    assert rec.names() == ['start_document', 'start_element', 'start_element', 'characters',
                           'end_element', 'comment', 'end_element', 'end_document']
    attr = rec.of('start_element')[0]['Attributes']['x']
    assert str(attr) == '1'
    assert attr['LocalName'] == 'x'


def test_generate_a_node_that_is_not_a_document():
    rec = Recorder()
    doc = X.Parser().parse_string('<a><b/></a>')
    assert SAX.Parser(Handler=rec).generate(doc.documentElement) is None
    assert rec.names() == ['set_document_locator']


def test_document_locator_of_the_generator():
    doc = X.Parser().parse_string('<a>\n<b/></a>')
    lines = []

    class H:
        def set_document_locator(self, locator):
            self.locator = locator

        def start_element(self, el):
            lines.append((el['Name'], self.locator['LineNumber'], self.locator['ColumnNumber'],
                          self.locator['XMLVersion']))

    SAX.Parser(Handler=H()).generate(doc)
    assert lines == [('a', 1, 1, '1.0'), ('b', 2, 1, '1.0')]


def test_base_dispatch():
    class Content:
        def start_element(self, el):
            seen.append(('content', el['Name']))

    class Errors:
        def fatal_error(self, error):
            seen.append(('error', error.Message))

    class Rest:
        def start_element(self, el):
            seen.append(('rest', el['Name']))

        def comment(self, data):
            seen.append(('comment', data['Data']))

    seen = []
    base = SAX.Base(Handler=Rest(), ContentHandler=Content(), ErrorHandler=Errors())
    base.start_element({'Name': 'x'})
    base.comment({'Data': 'c'})
    base.fatal_error(SAX.ParseException('m'))
    base.characters({'Data': 'dropped'})
    assert seen == [('content', 'x'), ('comment', 'c'), ('error', 'm')]
    assert base.get_content_handler() is base.ContentHandler
    base.set_handler('ContentHandler', None)
    base.start_element({'Name': 'y'})
    assert seen[-1] == ('rest', 'y')
    with pytest.raises(SAX.NotRecognized):
        base.get_feature('urn:nothing')


def test_parse_file_and_bytestream():
    rec = Recorder()
    with open(DROMEDS, 'rb') as fh:
        SAX.SAX(Handler=rec).parse_file(fh)
    assert len(rec.of('start_element')) == 10
    rec = Recorder()
    SAX.SAX(Handler=rec).parse_file(DROMEDS)
    assert len(rec.of('start_element')) == 10
    rec = Recorder()
    SAX.SAX(Handler=rec).parse(Source={'SystemId': NS})
    assert len(rec.of('start_element')) == 3


# ------------------------------------------------------------------ safety

class Raiser:
    """Raises at the n-th event."""

    def __init__(self, n):
        self.n = n
        self.count = 0

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)

        def event(*args):
            self.count += 1
            if self.count == self.n:
                raise KeyError(name)
        return event


DOCUMENT = ('<?xml version="1.0"?><!DOCTYPE a [<!ENTITY e "<i>e</i>">]>'
            '<a xmlns:p="u" p:x="1">t&e;<![CDATA[c]]><?pi d?><!--c--><p:b/></a>')


def test_a_raise_at_any_event_stops_the_parse():
    total = Raiser(0)
    X.Parser(Handler=total, expand_entities=1).parse_string(DOCUMENT)
    for n in range(1, total.count + 1):
        handler = Raiser(n)
        with pytest.raises(KeyError):
            X.Parser(Handler=handler, expand_entities=1).parse_string(DOCUMENT)
        assert handler.count == n
        parser = X.Parser(Handler=Raiser(n))
        with pytest.raises((KeyError, X.Error)):
            parser.push(DOCUMENT[:40], DOCUMENT[40:])
            parser.finish_push()
        # the parser goes on working
        parser.set_handler(None)
        assert parser.parse_string('<z/>').documentElement.nodeName == 'z'


def test_a_raise_at_any_event_of_a_chunk():
    chunk = '<a xmlns="u">t<b/>&amp;<![CDATA[c]]></a>u<!--c-->'
    total = Raiser(0)
    X.Parser(Handler=total).parse_xml_chunk(chunk)
    assert total.count == 15
    for n in range(1, total.count + 1):
        handler = Raiser(n)
        with pytest.raises(KeyError):
            X.Parser(Handler=handler).parse_xml_chunk(chunk)
        assert handler.count == n


def test_handler_uses_its_parser():
    parser = X.Parser()
    outcomes = []

    class H:
        def start_element(self, el):
            if el['Name'] == 'a':
                # a parse of its own, through the same parser, with the same handler
                outcomes.append(parser.parse_string('<inner/>'))
                parser.set_handler(None)
                outcomes.append(parser.parse_string('<tree/>').documentElement.nodeName)
                for call in (lambda: parser.push('<x>'), parser.finish_push,
                             parser.init_push, lambda: parser.parse_chunk('<y/>')):
                    try:
                        call()
                    except X.Error as e:
                        outcomes.append('in the middle of a push' in str(e))

        def end_document(self, data):
            return 'end'

    parser.set_handler(H())
    parser.push('<a><b/>')
    parser.push('</a>')
    assert parser.finish_push() == 'end'
    assert outcomes[:2] == ['end', 'tree']
    assert outcomes[2:] == [True] * 4

    # the handler went away during the parse: the next parse builds a tree
    assert isinstance(parser.parse_string('<c/>'), X.Document)


def test_handler_replaced_during_the_parse():
    parser = X.Parser()
    first, second = Recorder(), Recorder()

    class Switch(Recorder):
        def start_element(self, el):
            parser.set_handler(second)

    switch = Switch()
    parser.set_handler(switch)
    parser.parse_string('<a><b/></a>')
    assert second.events == []
    parser.parse_string('<c/>')
    assert [e['Name'] for e in second.of('start_element')] == ['c']
    del first


def test_handler_cycle_is_collected():
    class H:
        pass
    handler = H()
    parser = X.Parser()
    handler.parser = parser
    parser.set_handler(handler)
    parser.push('<a>')
    ref = weakref.ref(handler)
    del handler, parser
    gc.collect()
    assert ref() is None


def test_input_callbacks_with_a_handler():
    files = {'/cb/main.xml': '<!DOCTYPE m SYSTEM "/cb/m.dtd"><m>&ent;</m>',
             '/cb/m.dtd': '<!ENTITY ent "<k>v</k>">'}
    parser = X.Parser(load_ext_dtd=1, expand_entities=1)
    parser.callbacks(lambda uri: uri in files, lambda uri: [files[uri]],
                     lambda handle, length: handle.pop() if handle else '', lambda handle: None)
    rec = Recorder()
    parser.set_handler(rec)
    parser.parse_file('/cb/main.xml')
    assert [e['Name'] for e in rec.of('start_element')] == ['m', 'k']
    assert rec.of('start_dtd') == [{'Name': 'm', 'SystemId': '/cb/m.dtd'}]


def test_input_callback_raising_during_a_sax_parse():
    parser = X.Parser(load_ext_dtd=1)

    def read(handle, length):
        raise OSError('read')
    parser.callbacks(lambda uri: True, lambda uri: 1, read, lambda handle: None)
    parser.set_handler(Recorder())
    with pytest.raises(OSError):
        parser.parse_string('<!DOCTYPE m SYSTEM "/x.dtd"><m/>')


def test_validation_with_a_handler():
    xml = '<!DOCTYPE a [<!ELEMENT a (b)><!ELEMENT b EMPTY>]><a><%s/></a>'
    rec = Recorder()
    X.Parser(Handler=rec, validation=1, load_ext_dtd=1).parse_string(xml % 'b')
    assert [e['Name'] for e in rec.of('start_element')] == ['a', 'b']
    X.Parser(Handler=Recorder(), validation=1, load_ext_dtd=1).parse_string(xml % 'c')


def test_entity_expansion_limits():
    lol = ('<!DOCTYPE l [<!ENTITY a "aaaaaaaaaa">' +
           ''.join('<!ENTITY %s "%s">' % (chr(98 + i), ('&%s;' % chr(97 + i)) * 10)
                   for i in range(8)) + ']><l>&i;</l>')
    with pytest.raises(X.Error):
        X.Parser(Handler=Recorder(), expand_entities=1).parse_string(lol)


def test_threads():
    errors = []

    def work():
        try:
            for _ in range(200):
                rec = Recorder()
                parser = X.Parser(Handler=rec)
                parser.parse_xml_chunk('<a><b/></a>')
                parser.parse_string('<c><d/></c>')
                assert [e['Name'] for e in rec.of('start_element')] == ['a', 'b', 'c', 'd']
        except Exception as e:   # pragma: no cover
            errors.append(e)

    threads = [threading.Thread(target=work) for _ in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert errors == []


def test_nested_chunks():
    outer = Recorder()

    class Inner(Recorder):
        def start_element(self, el):
            X.Parser(Handler=outer).parse_xml_chunk('<n/>')
            super().__getattr__('start_element')(el)

    inner = Inner()
    X.Parser(Handler=inner).parse_xml_chunk('<a/><b/>')
    assert [e['Name'] for e in inner.of('start_element')] == ['a', 'b']
    assert [e['Name'] for e in outer.of('start_element')] == ['n', 'n']


def test_set_handler_and_options():
    parser = X.Parser()
    handler = Recorder()
    parser.set_handler(handler)
    assert parser.get_option('Handler') is handler
    parser.set_option('Handler', None)
    assert isinstance(parser.parse_string('<a/>'), X.Document)
    with pytest.raises(X.Error):
        parser._parse_sax_chunk('<a/>')


def test_text_is_utf8_whatever_it_declares():
    declared = '<?xml version="1.0" encoding="ISO-8859-1"?><a>é€</a>'
    built = SAX.SAX(Handler=SAX.Builder()).parse_string(declared)
    assert built.documentElement.textContent == 'é€'
    assert built.encoding == 'ISO-8859-1'

    parser = X.Parser(Handler=SAX.Builder())
    parser.push(declared[:50], declared[50:])
    assert parser.finish_push().documentElement.textContent == 'é€'


def test_the_xml_prefix_is_not_a_mapping():
    rec = Recorder()
    X.Parser(Handler=rec).parse_string(
        '<a xmlns:xml="http://www.w3.org/XML/1998/namespace" xml:lang="en"/>')
    assert 'start_prefix_mapping' not in rec.names()
    assert 'end_prefix_mapping' not in rec.names()
    assert list(rec.of('start_element')[0]['Attributes']) == [
        '{http://www.w3.org/XML/1998/namespace}lang']

"""
Random DOM operations over several documents, dropping references on the way.

Refused operations are fine: the point is that nothing crashes and that memory
stays valid, so this is most useful under tests/run_valgrind.sh. Each seed runs
in its own process. REGRESS_STRESS_STEPS and REGRESS_STRESS_SEEDS change the size.
"""
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS = int(os.environ.get('REGRESS_STRESS_SEEDS', '20'))
STEPS = int(os.environ.get('REGRESS_STRESS_STEPS', '400'))
TIMEOUT = int(os.environ.get('REGRESS_TIMEOUT', '60'))

PROGRAM = r"""
import gc, random, sys
sys.path.insert(0, %(root)r)
import xml_libxml as X
from xml_libxml import SAX

seed, steps = int(sys.argv[1]), int(sys.argv[2])
rng = random.Random(seed)
SOURCES = [
    '<a x="1" y="2"><b>text<c z="3"/>more</b><!--note--><d/></a>',
    '<r xmlns:p="urn:p" p:q="1"><p:s>one<p:t/></p:s><u xmlns="urn:u"><v/></u></r>',
    '<!DOCTYPE m [<!ATTLIST m id ID #IMPLIED>]><m id="k"><n>x</n><?pi data?></m>',
    '<!DOCTYPE e [<!ELEMENT e ANY><!ENTITY ent "val<i/>">]><e a="x">&ent;<f/></e>',
    '<x xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="missing.xml">'
    '<xi:fallback><y q="1">z<w/></y><xi:include href="other.xml"><xi:fallback/></xi:include>'
    '</xi:fallback></xi:include><xi:include href="#xpointer(/x/k)"/><k>n</k></x>',
]
RNG = '''<element name="a" xmlns="http://relaxng.org/ns/structure/1.0"><text/></element>'''
XSD = '''<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:element name="a" type="xs:string"/></xs:schema>'''
PARSER = X.Parser(expand_entities=0)
docs = [PARSER.parse_string(s) for s in SOURCES]
pool = []


def remember(node):
    if isinstance(node, X.Node):
        pool.append(node)
    return node


def pick():
    return rng.choice(pool) if pool else rng.choice(docs)


def pick_doc():
    return rng.choice(docs)


def scan(node):
    # Touch everything reachable from a node.
    node.nodeName, node.nodeType, node.nodeValue, node.textContent
    node.parentNode, node.ownerDocument, node.previousSibling, node.nextSibling
    node.prefix, node.namespaceURI, node.localname, node.getNamespaces()
    for child in node.childNodes():
        remember(child)
    if node.firstChild is not None:
        node.firstChild.toString()
    for attr in node.attributes():
        remember(attr)
        attr.nodeName, attr.nodeValue
    node.toString()


def op_grab():
    node = rng.choice(docs)
    for hit in node.findnodes(rng.choice(['//*', '//text()', '//@*', '//comment()', '/*'])):
        remember(hit)
    for hit in rng.choice([
            lambda: node.getElementsByTagName('*'),
            lambda: node.getElementsByLocalName('s'),
            lambda: node.getElementsByTagNameNS('*', 't'),
            lambda: node.documentElement.getChildrenByTagName('*')])():
        remember(hit)


def op_create():
    d = pick_doc()
    remember(rng.choice([
        lambda: d.createElement('new'),
        lambda: d.createTextNode('txt'),
        lambda: d.createComment('cm'),
        lambda: d.createCDATASection('cd'),
        lambda: d.createAttribute('at', 'v'),
        lambda: d.createDocumentFragment(),
        lambda: d.createElementNS('urn:e', 'e:new'),
        lambda: X.Element('free'),
        lambda: X.Text('free'),
        lambda: X.Attr('free', 'v'),
    ])())


def op_fragment():
    d = pick_doc()
    f = d.createDocumentFragment()
    for i in range(rng.randint(0, 3)):
        f.appendChild(rng.choice([d.createElement('f'), d.createTextNode('t'), pick()]))
    remember(f)


def op_move():
    a, b, c = pick(), pick(), pick()
    rng.choice([
        lambda: a.appendChild(b),
        lambda: a.insertBefore(b, c),
        lambda: a.insertBefore(b, None),
        lambda: a.insertAfter(b, c),
        lambda: a.insertAfter(b, None),
        lambda: a.replaceChild(b, c),
        lambda: a.replaceNode(b),
        lambda: a.addSibling(b),
        lambda: a.removeChild(b),
        lambda: a.unbindNode(),
    ])()


def op_documents():
    a, d = pick(), pick_doc()
    rng.choice([
        lambda: remember(d.importNode(a, rng.randint(0, 1))),
        lambda: d.adoptNode(a),
        lambda: a.setOwnerDocument(d),
        lambda: d.setDocumentElement(a),
        lambda: setattr(d, 'documentElement', a),
        lambda: remember(a.cloneNode(rng.randint(0, 1))),
        lambda: docs.append(d.cloneNode(1)) if len(docs) < 6 else None,
    ])()


def op_content():
    a = pick()
    rng.choice([
        lambda: setattr(a, 'textContent', 'x & y'),
        lambda: setattr(a, 'nodeValue', 'v'),
        lambda: a.normalize(),
        lambda: a.removeChildNodes(),
        lambda: a.setAttribute('x', 'new'),
        lambda: a.setAttribute('p:q', 'new'),
        lambda: a.setAttributeNS('urn:n', 'n:w', '1'),
        lambda: a.removeAttribute('x'),
        lambda: a.removeAttributeNS('urn:p', 'q'),
        lambda: remember(a.getAttributeNode('x')),
        lambda: a.setAttributeNode(pick()),
        lambda: a.removeAttributeNode(pick()),
        lambda: a.appendText('more'),
        lambda: a.appendData('more'),
        lambda: a.setNamespaceDeclURI('p', rng.choice([None, 'urn:new'])),
        lambda: a.setNamespaceDeclPrefix('p', 'q'),
        lambda: a.setNamespace('urn:s', 's', rng.randint(0, 1)),
        lambda: a.setNodeName('renamed'),
        lambda: a.setAttribute('xmlns:z', 'urn:z'),
        lambda: a.setAttribute('xmlns', rng.choice(['urn:d', ''])),
        lambda: a.appendWellBalancedChunk('<w>x</w>y'),
        lambda: remember(a.addNewChild('urn:p', 'p:added')),
        lambda: a.replaceData(1, 1, 'r'),
        lambda: a.deleteData(0, 1),
        lambda: a.insertData(1, 'i'),
        lambda: setattr(a, 'value', 'v2'),
        lambda: a.setCargo([1, 2]),
        lambda: a.getCargo(),
    ])()


def op_forget():
    for i in range(rng.randint(1, 8)):
        if pool:
            pool.pop(rng.randrange(len(pool)))
    if rng.random() < 0.3 and len(docs) > 1:
        docs.pop(rng.randrange(len(docs)))
        docs.append(PARSER.parse_string(rng.choice(SOURCES)))
    gc.collect()


def op_xpath_callback():
    d = pick_doc()
    x = X.XPathContext(d)
    target = pick()

    def meddle(*args):
        rng.choice([
            lambda: target.unbindNode(),
            lambda: target.removeChildNodes(),
            lambda: setattr(target, 'textContent', 'changed'),
            lambda: target.setAttribute('x', 'changed'),
            lambda: target.normalize(),
            lambda: d.adoptNode(target),
            lambda: remember(d.createElement('during')),
            lambda: pool.pop() if pool else None,
            lambda: gc.collect(),
        ])()
        return rng.choice([True, 1, 'text', d.findnodes('//*')])

    x.registerFunction('meddle', meddle)
    x.registerVarLookupFunc(lambda data, name, uri: meddle())
    for hit in x.findnodes(rng.choice(['//*[meddle()]', '//text()[meddle()]',
                                       '//@*[meddle()]', '//*[$v]', 'meddle()'])):
        remember(hit)


def op_dtd():
    a, d, e = pick(), pick_doc(), pick_doc()
    choices = [
        lambda: X.Dtd.parse_string('<!ELEMENT e ANY><!ENTITY other "o">'),
        lambda: d.createDTD('m', None, 'x.dtd'),
        lambda: d.createInternalSubset('m'),
        lambda: d.createExternalSubset('m', None, 'x.dtd'),
    ]
    dtd = d.internalSubset or e.internalSubset or d.externalSubset or rng.choice(choices)()
    if rng.random() < 0.3:
        dtd = rng.choice(choices)()
    remember(dtd)
    if rng.random() < 0.3:
        remember(dtd.firstChild)
    rng.choice([
        lambda: e.setInternalSubset(dtd),
        lambda: e.setExternalSubset(dtd),
        lambda: remember(e.removeInternalSubset()),
        lambda: remember(e.removeExternalSubset()),
        lambda: e.appendChild(dtd),
        lambda: e.insertBefore(dtd, e.documentElement),
        lambda: e.insertAfter(dtd, e.firstChild),
        lambda: e.replaceChild(dtd, e.firstChild),
        lambda: a.replaceNode(dtd),
        lambda: a.addSibling(dtd),
        lambda: a.appendChild(dtd),
        lambda: dtd.unbindNode(),
        lambda: remember(dtd.cloneNode(1)),
        lambda: e.is_valid(),
        lambda: e.is_valid(dtd),
        lambda: e.validate(dtd),
        lambda: e.toStringC14N(rng.randint(0, 1)),
        lambda: a.toStringC14N(0, '//*'),
        lambda: a.toStringEC14N(1, None, ['p']),
        lambda: e.toStringC14N_v1_1(),
        lambda: remember(e.createEntityReference('ent')),
    ])()


def op_attributes():
    a, b = pick(), pick()
    attrs, hash_view = a.attributes(), a.getAttributeHash()
    rng.choice([
        lambda: attrs.setNamedItem(b),
        lambda: attrs.removeNamedItem(rng.choice(['x', 'y', 'id', 'p:q'])),
        lambda: hash_view.update({'k': 'v', '{urn:h}k': 'w'}),
        lambda: hash_view.pop(rng.choice(['x', 'y', 'k', '{urn:h}k'])),
        lambda: list(hash_view.items()),
        lambda: a.nonBlankChildNodes(),
        lambda: remember(a.firstNonBlankChild()),
    ])()


def op_xinclude():
    d = pick_doc()
    if rng.random() < 0.5:
        docs.append(PARSER.parse_string(SOURCES[-1]))
        del docs[:-6]
        d = docs[-1]
        for node in d.findnodes('//*'):
            if rng.random() < 0.5:
                remember(node)
    d.process_xinclude({'no_xinclude_nodes': rng.randint(0, 1)})


def op_schema():
    a = pick()
    rng.choice([
        lambda: X.RelaxNG(string=RNG).validate(pick_doc()),
        lambda: X.Schema(string=XSD).validate(a),
        lambda: X.Schema(DOM=X.Parser().parse_string(XSD)).validate(pick_doc()),
        lambda: X.Pattern(rng.choice(['a', '//b', 'p:s', './/*'])).matchesNode(a),
        lambda: X.Pattern('p:s | a/b', {'p': 'urn:p'}).matchesNode(a),
    ])()


def meddle_and_give(text):
    # What a callback can do: change trees, parse, raise.
    rng.choice([
        lambda: op_move(),
        lambda: op_forget(),
        lambda: remember(PARSER.parse_string(rng.choice(SOURCES)).documentElement),
        lambda: PUSHER.push('<p>'),
        lambda: PUSHER.finish_push(rng.randint(0, 1)),
        lambda: None,
    ])()
    if rng.random() < 0.1:
        raise ValueError('callback')
    return text


INCLUDE = ('<w xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="/cb/one.xml"/>'
           '<xi:include href="/cb/two.xml" parse="text"/></w>')
ENTITY_DOC = '<!DOCTYPE e [<!ENTITY x SYSTEM "urn:x">]><e>&x;</e>'
PUSHER = X.Parser()


def callback_parser():
    parser = X.Parser(expand_xinclude=1, expand_entities=rng.randint(0, 1),
                      ext_ent_handler=lambda url, ident: meddle_and_give('<i/>'))
    parser.callbacks(lambda uri: uri.startswith('/cb/'),
                     lambda uri: {'left': rng.randint(0, 3)},
                     lambda handle, length: meddle_and_give(
                         '' if handle['left'] <= 0 else
                         (handle.__setitem__('left', handle['left'] - 1) or '<k>t</k>'
                          if handle['left'] == 1 else ' ')),
                     lambda handle: meddle_and_give(None))
    return parser


def op_callbacks():
    parser = callback_parser()
    doc = parser.parse_string(rng.choice([INCLUDE, ENTITY_DOC]))
    docs.append(doc)
    del docs[:-6]
    remember(doc.documentElement)


def op_push():
    pusher = rng.choice([PUSHER, callback_parser()])
    text = rng.choice(SOURCES + [INCLUDE, ENTITY_DOC])
    cut = rng.randint(0, len(text))
    rng.choice([
        lambda: pusher.push(text[:cut], text[cut:]),
        lambda: pusher.push(text[:cut]),
        lambda: pusher.parse_chunk(text[cut:], rng.randint(0, 1)),
        lambda: pusher.init_push(),
    ])()
    if rng.random() < 0.5:
        doc = pusher.finish_push(rng.randint(0, 1))
        if doc is not None:
            remember(doc.documentElement)


readers = []


def op_reader():
    if not readers or rng.random() < 0.2:
        if rng.random() < 0.3:
            readers.append(X.Reader(DOM=pick_doc()))
        else:
            readers.append(X.Reader(string=rng.choice(SOURCES + [INCLUDE, ENTITY_DOC]),
                                    expand_entities=rng.randint(0, 1)))
        del readers[:-3]
    reader = rng.choice(readers)
    for _ in range(rng.randint(1, 4)):
        rng.choice([
            reader.read, reader.read, reader.next, reader.nextSibling, reader.skipSiblings,
            lambda: reader.nextElement(rng.choice([None, 'b', 'm', 'e'])),
            lambda: remember(reader.copyCurrentNode(rng.randint(0, 1))),
            lambda: remember(reader.preserveNode()),
            lambda: remember(reader.document()),
            lambda: reader.preservePattern(rng.choice(['//b', 'c', '//*'])),
            lambda: reader.readOuterXml(),
            lambda: reader.readInnerXml(),
            lambda: (reader.moveToFirstAttribute(), reader.readAttributeValue()),
            lambda: (reader.moveToNextAttribute(), remember(reader.copyCurrentNode())),
            reader.moveToElement, reader.getAttributeHash, reader.nodePath,
            lambda: reader.document() and remember(rng.choice(
                reader.document().findnodes('//node()') or [None])),
            lambda: reader.finish() if rng.random() < 0.2 else None,
            lambda: reader.close() if rng.random() < 0.1 else None,
            lambda: readers.remove(reader) if rng.random() < 0.1 else None,
        ])()


class Meddler:
    # A SAX handler that changes trees, parses, pushes and raises.
    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)

        def event(*args):
            if rng.random() < 0.05:
                meddle_and_give(None)
            if rng.random() < 0.03:
                rng.choice([
                    lambda: SAX_PUSHER.push('<q>'),
                    lambda: SAX_PUSHER.finish_push(rng.randint(0, 1)),
                    lambda: SAX_PUSHER.parse_string('<s/>'),
                    lambda: SAX_PUSHER.set_handler(rng.choice([None, Meddler()])),
                ])()
            if rng.random() < 0.02:
                raise ValueError('handler')
        return event


SAX_PUSHER = X.Parser(Handler=Meddler())


def keep_built(result):
    if isinstance(result, X.Document):
        docs.append(result)
        del docs[:-6]
        remember(result.documentElement)
    elif isinstance(result, X.Node):
        remember(result)


def builder_chunk(text):
    builder = SAX.Builder()
    SAX.SAX(Handler=builder).parse_chunk(text)
    keep_built(builder.done())


def op_sax():
    text = rng.choice(SOURCES + [INCLUDE, ENTITY_DOC])
    cut = rng.randint(0, len(text))
    rng.choice([
        lambda: keep_built(SAX.SAX(Handler=SAX.Builder()).parse_string(text)),
        lambda: keep_built(SAX.Parser(Handler=SAX.Builder()).generate(pick_doc())),
        lambda: SAX.Parser(Handler=Meddler()).generate(pick_doc()),
        lambda: X.Parser(Handler=Meddler(), expand_entities=rng.randint(0, 1)).parse_string(text),
        lambda: X.Parser(Handler=Meddler()).parse_xml_chunk(text),
        lambda: builder_chunk(text),
        lambda: SAX_PUSHER.push(text[:cut], text[cut:]),
        lambda: SAX_PUSHER.push(text[:cut]),
        lambda: SAX_PUSHER.finish_push(rng.randint(0, 1)),
        lambda: SAX_PUSHER.set_handler(Meddler(), join_characters=rng.randint(0, 1)),
        lambda: (lambda parser: (parser.set_handler(Meddler()),
                                 parser.parse_string(rng.choice([INCLUDE, ENTITY_DOC]))))(
            callback_parser()),
    ])()


OPS = [op_grab, op_create, op_fragment, op_move, op_move, op_move,
       op_documents, op_content, op_content, op_forget, op_xpath_callback,
       op_dtd, op_dtd, op_attributes, op_xinclude, op_schema, op_callbacks, op_push,
       op_reader, op_reader, op_sax, op_sax]

for doc in docs:
    remember(doc.documentElement)
for step in range(steps):
    try:
        rng.choice(OPS)()
        if pool:
            scan(pick())
    except (X.Error, SAX.SAXException, TypeError, AttributeError, IndexError, KeyError,
            ValueError):
        pass

for node in pool:
    scan(node)
for doc in docs:
    doc.toString()
del pool[:]
del docs[:]
del readers[:]
gc.collect()
print('done')
""" % {'root': ROOT}


@pytest.mark.parametrize('seed', range(SEEDS))
def test_random_operations(seed):
    proc = subprocess.run([sys.executable, '-c', PROGRAM, str(seed), str(STEPS)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, errors='replace',
                          timeout=TIMEOUT)
    assert proc.returncode == 0, proc.stderr[-1000:]
    assert proc.stdout.strip() == 'done'

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

seed, steps = int(sys.argv[1]), int(sys.argv[2])
rng = random.Random(seed)
SOURCES = [
    '<a x="1" y="2"><b>text<c z="3"/>more</b><!--note--><d/></a>',
    '<r xmlns:p="urn:p" p:q="1"><p:s>one<p:t/></p:s><u xmlns="urn:u"><v/></u></r>',
    '<!DOCTYPE m [<!ATTLIST m id ID #IMPLIED>]><m id="k"><n>x</n><?pi data?></m>',
    '<!DOCTYPE e [<!ELEMENT e ANY><!ENTITY ent "val<i/>">]><e a="x">&ent;<f/></e>',
]
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


OPS = [op_grab, op_create, op_fragment, op_move, op_move, op_move,
       op_documents, op_content, op_content, op_forget]

for doc in docs:
    remember(doc.documentElement)
for step in range(steps):
    try:
        rng.choice(OPS)()
        if pool:
            scan(pick())
    except (X.Error, TypeError, AttributeError, IndexError):
        pass

for node in pool:
    scan(node)
for doc in docs:
    doc.toString()
del pool[:]
del docs[:]
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

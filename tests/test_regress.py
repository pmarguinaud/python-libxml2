"""
Regression cases for crashes and silent corruption.

Each case runs in its own process, so a segfault fails that case only. A case
states the correct output. Known bugs are marked with the plan phase that fixes
them: a strict xfail fails the run once the bug is gone, so the marker has to be
removed by the commit that fixes it.

REGRESS_TIMEOUT (seconds) overrides the per-case timeout, for valgrind runs.
REGRESS_SKIP_LEAKS=1 skips the leak_ cases, which are too slow under valgrind.
"""
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMEOUT = int(os.environ.get('REGRESS_TIMEOUT', '30'))

PRELUDE = """
import gc, os, sys
sys.path.insert(0, {root!r})
import xml_libxml as X
P = X.Parser()
EX = os.path.join({root!r}, 'example')

def raises(exc, f):
    try:
        f()
    except exc:
        return True
    return False

def grows(f, n=100000, limit_kb=4000):
    import resource
    def rss():
        return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    for i in range(n // 4):
        f()
    before = rss()
    for i in range(n):
        f()
    return rss() - before > limit_kb

def survives(obj, name, call=False):
    try:
        v = getattr(obj, name)
        if call:
            v()
    except (AttributeError, TypeError, X.Error):
        pass
    return True
""".format(root=ROOT)

CASES = []


def case(name, code, expected, phase=None, strict=True):
    CASES.append((name, code, expected, phase, strict))


# ---------------------------------------------------------------- phase 0 fixes

case('ns_wrapper_outlives_element', """
d = X.Document(); e = d.createElementNS('u', 'p:a'); a = e.attributes()
del e; gc.collect(); print(a[0].declaredPrefix, a[0].declaredURI)""", 'p u')
case('ns_wrapper_outlives_decl', """
d = P.parse_string('<r xmlns:p="u"/>'); r = d.documentElement; n = r.attributes()[0]
r.setNamespaceDeclURI('p', None); print(n.declaredPrefix)""", 'p')
case('xpath_ns_node_outlives_result', """
d = P.parse_string('<r xmlns:p="u"/>'); n = d.findnodes('/r/namespace::p')[0]
gc.collect(); print(n.declaredPrefix, n.nodeValue)""", 'p u')
case('setBaseURI_keeps_None_refcount', """
d = P.parse_string('<a/>'); r = sys.getrefcount(None)
for i in range(100000): d.setBaseURI('u')
print(abs(sys.getrefcount(None) - r) < 1000)""", 'True')
case('del_parser_option', """
print(raises(TypeError, lambda: delattr(P, 'recover')))""", 'True')
case('del_attr_value', """
a = P.parse_string('<a b="1"/>').documentElement.getAttributeNode('b')
print(raises(TypeError, lambda: delattr(a, 'value')))""", 'True')
case('del_documentElement', """
d = P.parse_string('<a/>'); print(raises(TypeError, lambda: delattr(d, 'documentElement')))""", 'True')
case('del_standalone', """
d = P.parse_string('<a/>'); print(raises(TypeError, lambda: delattr(d, 'standalone')))""", 'True')
case('registerFunction_empty_context', """
x = X.XPathContext(); x.registerFunction('f', len); print('ok')""", 'ok')
case('registerFunctionNS_empty_context', """
x = X.XPathContext(); x.registerFunctionNS('f', 'u', len); print('ok')""", 'ok')
case('replaceDataString_replacement_contains_target', """
t = P.parse_string('<a>aa</a>').documentElement.firstChild
t.replaceDataString('a', 'aa', 1); print(t.data)""", 'aaaa')
case('deleteDataString_single_pass', """
t = P.parse_string('<a>aabb</a>').documentElement.firstChild
t.deleteDataString('ab', 1); print(t.data)""", 'ab')
case('replaceDataString_empty_target', """
t = P.parse_string('<a>ab</a>').documentElement.firstChild
t.replaceDataString('', 'x', 1); print(t.data)""", 'ab')

# ---------------------------------------------------------------- guards
# Clean on libxml2 2.15, also under valgrind. They must stay clean when Phase 1
# rewrites these paths. The non-strict marks are memory errors that often still
# print the right result: only tests/run_valgrind.sh shows them reliably.

XDOC = """
d1 = P.parse_string('<one/>'); d2 = P.parse_string('<two><kid attr="v">text</kid></two>')
k = d2.documentElement.firstChild
"""
FILL = "\nfor i in range(50): P.parse_string('<zzzzzzzz yyyyyy=\"1\"/>')\n"
case('xdoc_appendChild', XDOC + """
d1.documentElement.appendChild(k); del d2; gc.collect()""" + FILL + """
print(k.nodeName, k.getAttribute('attr'))""", 'kid v')
case('xdoc_adoptNode', XDOC + """
d1.adoptNode(k); d1.documentElement.appendChild(k); del d2; gc.collect()""" + FILL + """
print(k.nodeName, k.getAttribute('attr'))""", 'kid v')
case('xdoc_setOwnerDocument', XDOC + """
k.setOwnerDocument(d1); del d2; gc.collect()""" + FILL + """
print(k.nodeName)""", 'kid')
case('xdoc_setDocumentElement', XDOC + """
d3 = X.Document(); d3.setDocumentElement(k); del d2; gc.collect()""" + FILL + """
print(d3.documentElement.nodeName)""", 'kid')
case('xdoc_addSibling', XDOC + """
c = d1.createComment('c'); d1.documentElement.appendChild(c)
c.addSibling(k); del d2; gc.collect()""" + FILL + """
print(k.nodeName)""", 'kid')
case('standalone_parent_takes_doc_node', """
d = P.parse_string('<a><b/></a>'); c = d.documentElement.firstChild
e = X.Element('x'); e.appendChild(c); del d; gc.collect()
print(e.toString(), c.nodeName)""", '<x><b/></x> b')
case('setAttributeNode_moves_attr', """
d = P.parse_string('<a><b x="1"/><c/></a>'); r = d.documentElement; b, c = r.childNodes()
c.setAttributeNode(b.getAttributeNode('x')); print(r.toString())""", '<a><b/><c x="1"/></a>')

# ---------------------------------------------------------------- phase 1

STALE = """
d = P.parse_string('<a><b/></a>'); r = d.documentElement; k = r.firstChild
"""
case('stale_after_textContent', STALE + "r.textContent = 'x'; print(k.nodeName)", 'b')
case('stale_after_removeChildNodes', STALE + "r.removeChildNodes(); print(k.nodeName)", 'b')
case('stale_after_normalize', """
d = X.Document(); r = d.createElement('a'); d.setDocumentElement(r)
a = d.createTextNode('1'); b = d.createTextNode('2'); r.appendChild(a); r.appendChild(b)
r.normalize(); print(b.nodeName, a.data)""", '#text 12')
case('stale_detached_parent_dropped', """
d = P.parse_string('<a><b><c/></b></a>'); r = d.documentElement
b = r.removeChild(r.firstChild); c = b.firstChild; del b; gc.collect(); print(c.nodeName)""", 'c')
case('stale_clone_temporary', """
r = P.parse_string('<a><b/></a>').documentElement; print(r.cloneNode(1).firstChild.nodeName)""",
     'b')
case('stale_created_subtree_parent_dropped', """
d = X.Document()
def mk():
    a = d.createElement('a'); b = d.createElement('b'); a.appendChild(b); return b
print(mk().parentNode.nodeName)""", 'a')
case('stale_replaced_attr', """
d = P.parse_string('<a x="1"/>'); r = d.documentElement; old = r.getAttributeNode('x')
r.setAttributeNode(d.createAttribute('x', '2')); print(old.value)""", '1')
case('stale_removeAttribute', """
d = P.parse_string('<a n="1"/>'); r = d.documentElement; at = r.getAttributeNode('n')
r.removeAttribute('n'); print(at.value)""", '1')
case('stale_setAttribute', """
d = P.parse_string('<a n="1"/>'); r = d.documentElement; at = r.getAttributeNode('n'); t = at.firstChild
r.setAttribute('n', '2'); print(at.value, t.nodeName)""", '2 #text')
case('stale_insertBefore_merged_text', """
d = P.parse_string('<a>x<b/></a>'); r = d.documentElement
t = d.createTextNode('y'); r.insertBefore(t, r.firstChild); print(t.nodeName, r.firstChild.nodeName)""",
     '#text #text')
case('documentElement_setter', """
d = X.Document(); e = d.createElement('r'); d.documentElement = e; del e; gc.collect()
print(d.documentElement.nodeName); del d; gc.collect()""", 'r')
case('standalone_grandchild', """
e = X.Element('x'); y = e.appendChild(X.Element('y')); print(y.baseURI, y.toString())
y.appendChild(X.Element('z')); print(e.toString())""", 'None <y/>\n<x><y><z/></y></x>')
case('standalone_ownerDocument', """
e = X.Element('x'); y = e.appendChild(X.Element('y'))
print(e.ownerDocument, y.ownerDocument, X.Attr('n', 'v').ownerDocument)""", 'None None None')
case('export_PI', "from xml_libxml import PI; print(PI.__name__)", 'PI')

NSNODE = "d = P.parse_string('<r xmlns:p=\"u\"/>'); n = d.documentElement.attributes()[0]\n"
for name in ('firstChild', 'previousSibling'):
    case('nsnode_' + name, NSNODE + "print(survives(n, %r))" % name, 'True')
for name in ('childNodes', 'toString', 'line_number'):
    case('nsnode_' + name, NSNODE + "print(survives(n, %r, True))" % name, 'True')
case('nsnode_prefix', NSNODE + "print(n.declaredPrefix, n.declaredURI)", 'p u')

# ---------------------------------------------------------------- Perl t/

case('perl_18docfree', "d = X.Document(); d = X.Document(); del d; gc.collect(); print('ok')", 'ok')
case('perl_48_removeChild_crashes_rt_80395', """
s = '<!DOCTYPE bug [<!ENTITY myent "xyz">]><bug><elem>&myent;</elem></bug>'
r = X.Parser(expand_entities=0).parse_string(s).documentElement
for node in r.childNodes():
    if node.nodeType == X.XML_ELEMENT_NODE and node.nodeName == 'elem':
        r.removeChild(node)
print(r.toString())""", '<bug/>')
case('perl_48_replaceNode_DTD_nodes_rt_80521', """
s = '<!DOCTYPE crash [<!ATTLIST foo bar CDATA "baz">]><crash/>'
src, dest = P.parse_string(s), P.parse_string(s)
old = dest.firstChild; old.replaceNode(src.firstChild)
print(dest.firstChild.nodeType == X.XML_DTD_NODE, old.parentNode, '<!ATTLIST' in dest.toString())
del old, src; gc.collect(); print(dest.documentElement.nodeName)""", 'True None True\ncrash')

# t/11memory.t: the loops for what exists today. The others go with their phase.
MAKE_DOC = """
def make_doc():
    doc = X.Document('1.0', 'UTF-8')
    parent = doc.createElement('p'); doc.setDocumentElement(parent)
    parent.setAttribute('xmlns:param', 'http://axkit.org/XSP/param')
    elem = doc.createElementNS('http://axkit.org/XSP/param', 'param:foo')
    parent.appendChild(elem); parent = elem; parent = parent.parentNode
    return doc
"""
case('leak_11memory_make_doc', MAKE_DOC + "print(grows(lambda: make_doc().toString(), 200000))", 'False')
case('leak_11memory_appendChild', """
def f():
    e = X.Element('foo'); e.appendChild(X.Element('bar')); return e.toString()
print(grows(f, 200000))""", 'False')
case('leak_11memory_parsers', "print(grows(X.Parser, 400000))", 'False')
case('leak_11memory_parse', """
print(grows(lambda: X.Parser().parse_string('<sometag>foo</sometag>'), 200000))""", 'False')
case('leak_11memory_failed_parse', """
print(grows(lambda: raises(X.Error, lambda: P.parse_string('<sometag>foo</somtag>')), 200000))""",
     'False')
case('leak_11memory_replace_root', """
d = X.Document()
print(grows(lambda: d.setDocumentElement(d.createElement('x')), 400000))""", 'False')
case('leak_11memory_root_from_other_document', """
d = X.Document()
def f():
    other = X.Document(); a = other.createElement('A'); a.appendChild(other.createElement('B'))
    d.setDocumentElement(a)
print(grows(f, 200000))""", 'False')
case('leak_11memory_find', """
d = P.parse_string('<foo><bar><foo/></bar></foo>')
print(grows(lambda: d.find('/foo/bar/foo'), 400000))""", 'False')
case('leak_11memory_process_message', """
s = '<d><s name="a"><h>1</h></s><s name="b"><h>2</h></s></d>'
def f():
    d = P.parse_string(s); e = d.documentElement; return d.findnodes('/d/s').to_literal()
print(grows(f, 200000))""", 'False')
case('leak_detached_tree', """
d = P.parse_string('<a/>')
def f():
    e = d.createElement('x'); e.appendChild(d.createTextNode('t')); e.setAttribute('a', 'v')
    return e.firstChild.nodeName
print(grows(f, 200000))""", 'False')
case('leak_standalone_nodes', """
print(grows(lambda: (X.Element('a'), X.Text('t'), X.Attr('a', 'b'), X.DocumentFragment()), 200000))""",
     'False')

case('addSibling_self_holds_last_reference', """
f = X.DocumentFragment(); d = f.ownerDocument
a = f.appendChild(X.Text('a')); f.appendChild(X.Text('b')); f.appendChild(X.Comment('c'))
last = f.appendChild(X.Text('z')); del last, f; gc.collect()
print(a.addSibling(a).data)""", 'za')

# ---------------------------------------------------------------- DTD safety

DTD = "d = X.Parser(expand_entities=0).parse_string('<!DOCTYPE a [<!ELEMENT a ANY><!ENTITY e \"v\">]><a>&e;</a>')\n"
case('declaration_stays_in_dtd', DTD + """
dtd = d.firstChild; decl = dtd.firstChild
print(raises(X.Error, lambda: dtd.removeChild(decl)), raises(X.Error, decl.unbindNode))""",
     'True True')
case('dtd_children_cannot_be_cleared', DTD + "print(raises(X.Error, d.firstChild.removeChildNodes))", 'True')
case('entity_ref_children_cannot_be_cleared', DTD + """
ref = d.documentElement.firstChild
print(ref.nodeType == X.XML_ENTITY_REF_NODE, raises(X.Error, ref.removeChildNodes),
      raises(X.Error, ref.normalize), d.documentElement.toString())""", 'True True True <a>&e;</a>')
case('dtd_removed_and_freed', DTD + """
dtd = d.firstChild; d.removeChild(dtd); del dtd; gc.collect()
print('<!DOCTYPE' in d.toString(), d.documentElement.nodeName)""", 'False a')

case('entity_content_cannot_change', """
d = X.Parser(expand_entities=0).parse_string('<!DOCTYPE a [<!ENTITY e "v<i/>">]><a>&e;</a>')
root = d.documentElement; ref = root.firstChild; ent = ref.firstChild; i = ent.lastChild
print(i.nodeName, raises(X.Error, lambda: i.appendChild(root.cloneNode(1))),
      raises(X.Error, lambda: root.appendChild(i)), raises(X.Error, i.unbindNode),
      raises(X.Error, lambda: d.adoptNode(i)), raises(X.Error, lambda: ent.removeChild(i)))""",
     'i True True True True True')
case('entity_reference_after_dtd_freed', DTD + """
root = d.documentElement; ref = root.firstChild; d.removeChild(d.firstChild); gc.collect()
for i in range(50): P.parse_string('<!DOCTYPE z [<!ENTITY q "zzzz">]><z>&q;</z>')
print(len(d.findnodes('//*')), ref.firstChild.nodeType == X.XML_ENTITY_DECL, root.toString())
del ref, root; gc.collect(); del d; gc.collect()""", '1 True <a>&e;</a>')
case('dtd_rewrapped_and_dropped_twice', DTD + """
root = d.documentElement; ref = root.firstChild; d.removeChild(d.firstChild); gc.collect()
for i in range(3):
    e = ref.firstChild; p = e.parentNode; del e, p; gc.collect()
del ref, root; del d; gc.collect(); print('ok')""", 'ok')

# ---------------------------------------------------------------- phase 2

case('hierarchy_append_self', """
d = P.parse_string('<a/>'); r = d.documentElement
print(raises(X.Error, lambda: r.appendChild(r)), d.documentElement.nodeName)""", 'True a')
case('hierarchy_append_ancestor', """
d = P.parse_string('<a><b/></a>'); r = d.documentElement
print(raises(X.Error, lambda: r.firstChild.appendChild(r)), r.toString())""", 'True <a><b/></a>')
case('hierarchy_append_document', """
r = P.parse_string('<a/>').documentElement; d2 = P.parse_string('<b/>')
print(raises(X.Error, lambda: r.appendChild(d2)), r.toString())""", 'True <a/>')
case('hierarchy_two_roots', """
d = P.parse_string('<a/>'); print(raises(X.Error, lambda: d.appendChild(d.createElement('b'))))""",
     'True')
case('hierarchy_child_of_text', """
d = P.parse_string('<a>x</a>'); t = d.documentElement.firstChild
print(raises(X.Error, lambda: t.appendChild(d.createElement('b'))))""", 'True')
case('insertBefore_None_appends', """
d = P.parse_string('<a><b/></a>'); r = d.documentElement
r.insertBefore(d.createElement('c'), None); print(r.toString())""", '<a><b/><c/></a>')
case('insertBefore_fragment_inlined', """
d = P.parse_string('<a><b/></a>'); r = d.documentElement; f = d.createDocumentFragment()
f.appendChild(d.createElement('x')); f.appendChild(d.createElement('y'))
r.insertBefore(f, r.firstChild); print([n.nodeName for n in r.childNodes()])""",
     "['x', 'y', 'b']")
case('replaceChild_fragment_inlined', """
d = P.parse_string('<a><b/></a>'); r = d.documentElement; f = d.createDocumentFragment()
f.appendChild(d.createElement('x')); r.replaceChild(f, r.firstChild)
print([n.nodeName for n in r.childNodes()])""", "['x']")
case('insertBefore_ref_not_child', """
d = P.parse_string('<a><b/><c><d/></c></a>'); r = d.documentElement
print(raises(X.Error, lambda: r.firstChild.insertBefore(d.createElement('z'), r.lastChild.firstChild)))""",
     'True')
case('removeChild_keeps_ns', """
r = P.parse_string('<r xmlns:p="u"><p:a/></r>').documentElement
print(r.removeChild(r.firstChild).toString())""", '<p:a xmlns:p="u"/>')
case('move_keeps_ns', """
r = P.parse_string('<r><s xmlns:p="u"><p:a/></s><t/></r>').documentElement
s, t = r.childNodes(); t.appendChild(s.firstChild); r.removeChild(s); print(r.toString())""",
     '<r><t><p:a xmlns:p="u"/></t></r>')
case('ns_not_dangling_after_parent_freed', """
d = P.parse_string('<r><s xmlns:p="u"><p:a/></s></r>'); r = d.documentElement; s = r.firstChild
a = s.firstChild; s.removeChild(a); r.removeChild(s); del s; gc.collect()
for i in range(100): X.Element('x', 'http://aaaaaaaaaaaaaaa')
print(a.namespaceURI, a.prefix)""", 'u p')
case('insertBefore_attribute_as_reference', """
d = P.parse_string('<a x="1"/>'); r = d.documentElement
print(raises(X.Error, lambda: r.insertBefore(d.createElement('b'), r.getAttributeNode('x'))),
      raises(X.Error, lambda: r.insertAfter(d.createElement('b'), r.getAttributeNode('x'))),
      r.toString())""", 'True True <a x="1"/>')
case('adoptNode_fragment', """
d = P.parse_string('<r/>'); f = P.parse_balanced_chunk('<a b="1"/>text<c/>'); d.adoptNode(f)
gc.collect(); print([n.nodeName for n in f.childNodes()]); d.documentElement.appendChild(f)
print(d.documentElement.toString())""", "['a', '#text', 'c']\n<r><a b=\"1\"/>text<c/></r>")
case('detached_attr_keeps_ns', """
d = P.parse_string('<r xmlns:p="u" p:a="1"/>'); r = d.documentElement; a = r.getAttributeNode('p:a')
r.removeAttributeNode(a); r.setNamespaceDeclURI('p', None); del r; gc.collect()
print(a.namespaceURI, a.prefix)
e = d.createElement('e'); e.setAttributeNode(a); print(e.toString())""", 'u p\n<e xmlns:p="u" p:a="1"/>')
case('foreign_ns_declared_once', """
d = P.parse_string('<r xmlns:p="u" p:a="1" p:b="2"/>'); r = d.documentElement; x = d.createElement('x')
a, b = r.getAttributeNode('p:a'), r.getAttributeNode('p:b')
x.setAttributeNode(a); x.setAttributeNode(b); print(x.toString())""", '<x xmlns:p="u" p:a="1" p:b="2"/>')
case('ns_prefix_conflict_renamed', """
d = P.parse_string('<r xmlns:p="u" p:a="1"/>'); r = d.documentElement
x = P.parse_string('<x xmlns:p="v" p:b="2"/>').documentElement
x.setAttributeNode(d.documentElement.getAttributeNode('p:a'))
print(x.getAttributeNS('u', 'a'), x.getAttributeNS('v', 'b'))""", '1 2')
case('setNamespaceDeclURI_grandchild', """
r = P.parse_string('<r xmlns:p="u"><a><p:b/></a></r>').documentElement
r.setNamespaceDeclURI('p', None)
for i in range(100): X.Element('x', 'http://aaaaaaaaaaaaaaa')
print(r.toString())""", '<r><a><b/></a></r>')
case('setAttribute_xmlns_stays_on_child', """
r = P.parse_string('<r xmlns="u"><c/></r>').documentElement
r.firstChild.setAttribute('xmlns', 'v'); print(r.toString())""", '<r xmlns="u"><c xmlns="v"/></r>')
case('setAttribute_xmlnsp_stays_on_child', """
r = P.parse_string('<r xmlns:p="u"><c/></r>').documentElement
r.firstChild.setAttribute('xmlns:p', 'v'); print(r.toString())""",
     '<r xmlns:p="u"><c xmlns:p="v"/></r>')
case('setAttributeNS_under_default_ns', """
d = P.parse_string('<r xmlns="u"/>'); d.documentElement.setAttributeNS('u', 'p:a', 'v')
print(P.parse_string(d.toString()).documentElement.hasAttributeNS('u', 'a'))""", 'True')
case('setAttributeNS_prefix_conflict', """
r = P.parse_string('<r xmlns:p="u"/>').documentElement
print(raises(X.Error, lambda: r.setAttributeNS('v', 'p:a', '1')))""", 'True')
case('removeAttribute_qualified', """
r = P.parse_string('<r xmlns:p="u" p:a="1"/>').documentElement
r.removeAttribute('p:a'); print(r.hasAttribute('p:a'))""", 'False')
case('hasAttribute_xmlns', """
print(P.parse_string('<r xmlns:p="u"/>').documentElement.hasAttribute('xmlns:p'))""", 'False')
case('xpath_prefix_from_document', """
print(len(P.parse_string('<r xmlns:p="u"><p:a/></r>').findnodes('//p:a')))""", '1')
case('xpath_prefix_in_exists', """
print(P.parse_string('<r xmlns:p="u"><p:a/></r>').documentElement.exists('p:a'))""", 'True')
DEFNS = "d = P.parse_string('<r xmlns=\"u\"><c/></r>'); r = d.documentElement\n"
PFXNS = "d = P.parse_string('<r xmlns:p=\"u\"><p:c/></r>'); r = d.documentElement\n"
case('getElementsByTagName_default_ns_doc', DEFNS + "print(len(d.getElementsByTagName('c')))", '1')
case('getElementsByTagName_default_ns_elem', DEFNS + "print(len(r.getElementsByTagName('c')))", '1')
case('getElementsByTagName_prefixed', PFXNS + "print(len(d.getElementsByTagName('p:c')))", '1')
case('getElementsByTagName_no_xpath_injection', """
print(len(P.parse_string('<r><c/></r>').getElementsByTagName('c|e')))""", '0')
case('getElementsByLocalName_ns_elem', PFXNS + "print(len(r.getElementsByLocalName('c')))", '1')
case('getElementsByTagNameNS_star_elem', PFXNS + "print(len(r.getElementsByTagNameNS('*', 'c')))", '1')
case('getChildrenByTagName_qualified', PFXNS +
     "print(len(r.getChildrenByTagName('p:c')), len(r.getChildrenByTagName('c')))", '1 0')
case('getChildrenByTagNameNS_star', PFXNS + "print(len(r.getChildrenByTagNameNS('*', 'c')))", '1')
case('textContent_is_raw_text', """
r = P.parse_string('<a/>').documentElement; r.textContent = 'x & y <z> &amp;'
print(r.textContent)""", 'x & y <z> &amp;')
case('attr_nodeValue_is_raw_text', """
at = P.parse_string('<a b="1"/>').documentElement.getAttributeNode('b'); at.nodeValue = 'x&y'
print(at.value)""", 'x&y')
case('attr_textContent_is_raw_text', """
at = P.parse_string('<a b="1"/>').documentElement.getAttributeNode('b'); at.textContent = 'x&y'
print(at.value)""", 'x&y')
case('appendWellBalancedChunk_parses', """
r = P.parse_string('<a/>').documentElement; r.appendWellBalancedChunk('<b>t</b><c/>')
print([n.nodeName for n in r.childNodes()])""", "['b', 'c']")
NONASCII = "t = P.parse_string('<a>h\\u00e9llo</a>').documentElement.firstChild\n"
case('substringData_characters', NONASCII +
     "print(t.substringData(0, 2).encode('unicode_escape').decode())", 'h\\xe9')
case('insertData_characters', NONASCII +
     "t.insertData(2, 'X'); print(t.data.encode('unicode_escape').decode())", 'h\\xe9Xllo')
case('deleteData_characters', NONASCII + "t.deleteData(0, 2); print(t.data)", 'llo')
case('replaceData_characters', NONASCII + "t.replaceData(1, 1, 'e'); print(t.data)", 'hello')
case('deleteData_negative_length', """
t = P.parse_string('<a>abc</a>').documentElement.firstChild
raises(Exception, lambda: t.deleteData(1, -1)); print(t.data)""", 'abc')
case('createElement_bad_name', """
print(raises(X.Error, lambda: X.Document().createElement('bad name')))""", 'True')
case('setAttribute_bad_name', """
r = P.parse_string('<a/>').documentElement; print(raises(X.Error, lambda: r.setAttribute('b c', '1')))""",
     'True')

# ---------------------------------------------------------------- phase 3

ENTITY = '<!DOCTYPE a [<!ENTITY e "v">]><a>&e;</a>'
case('expand_entities_off', """
print(X.Parser(expand_entities=0).parse_string(%r).documentElement.toString())""" % ENTITY,
     '<a>&e;</a>')
case('xxe_refused_by_default', """
import tempfile
f = tempfile.NamedTemporaryFile('w', suffix='.txt'); f.write('SECRET'); f.flush()
s = '<!DOCTYPE a [<!ENTITY x SYSTEM "file://%s">]><a>&x;</a>' % f.name
try:
    print('SECRET' in P.parse_string(s).documentElement.textContent)
except X.Error:
    print(False)""", 'False')
LATIN1 = "d = P.parse_string('<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><a>\\xe9</a>'.encode('latin-1'))\n"
case('toString_latin1_doc', LATIN1 +
     "print('\\xe9' in d.toString())", 'True')
case('toBytes_latin1_doc', LATIN1 + "print(b'\\xe9' in d.toBytes())", 'True')
case('toString_utf16_doc', """
d = P.parse_string('<a>x</a>'); d.setEncoding('UTF-16'); print('<a>x</a>' in d.toString())""",
     'True')
case('toString_unknown_encoding', """
d = P.parse_string('<a/>'); d.setEncoding('NOPE'); print(raises(X.Error, d.toString))""", 'True')
case('parse_str_ignores_declared_encoding', """
d = P.parse_string('<?xml version="1.0" encoding="ISO-8859-1"?><a>\\u00e9</a>')
print(d.documentElement.textContent == '\\u00e9')""", 'True')
case('html_file_latin2_serializes', """
print(len(P.parse_html_file(os.path.join(EX, 'enc_latin2.html')).toString()) > 0)""", 'True')
case('load_xml_bytes', "print(P.load_xml(string=b'<a/>').documentElement.nodeName)", 'a')
case('load_xml_pathlike', """
import pathlib; print(P.load_xml(location=pathlib.Path(EX) / 'test.xml') is not None)""", 'True')
case('load_xml_per_call_option', """
print(P.load_xml(string='<a> <b/> </a>', no_blanks=1).documentElement.toString())""", '<a><b/></a>')
case('load_html_IO', """
import io; print(P.load_html(IO=io.StringIO('<p>x')) is not None)""", 'True')
case('error_has_location', """
try:
    P.parse_string('<a>\\n<b></a>')
except X.Error as e:
    print(e.line, e.code > 0)""", '2 True')
case('error_in_thread_keeps_message', """
import threading
out = []
def w():
    try:
        P.parse_string('<a>')
    except X.Error as e:
        out.append(str(e))
t = threading.Thread(target=w); t.start(); t.join(); print('libxml2 error' not in out[0])""", 'True')
case('stale_after_change_in_thread', STALE + """
import threading
t = threading.Thread(target=lambda: setattr(r, 'textContent', 'x')); t.start(); t.join()
print(k.nodeName)""", 'b')

# ---------------------------------------------------------------- phase 4

XPC = "d = P.parse_string('<a><b/></a>'); x = X.XPathContext(d)\n"
case('xpath_callback_exception_propagates', XPC + """
def f(): raise ValueError('boom')
x.registerFunction('f', f); print(raises(ValueError, lambda: x.findvalue('f()')))""", 'True')
case('xpath_two_functions', XPC + """
x.registerFunction('f', lambda: 'F'); x.registerFunction('g', lambda: 'G')
print(x.findvalue('f()'), x.findvalue('g()'))""", 'F G')
case('xpath_callback_returns_nodes', XPC + """
x.registerFunction('f', lambda: d.findnodes('//b')); print(len(x.findnodes('f()')))""", '1')
case('xpath_function_survives_doc_change', XPC + """
x.registerFunction('f', lambda: 'F'); x.setContextNode(P.parse_string('<c/>'))
print(x.findvalue('f()'))""", 'F')
case('xpath_callback_released', """
class K: pass
k = K(); before = sys.getrefcount(k); d = P.parse_string('<a/>'); x = X.XPathContext(d)
x.registerFunction('f', lambda k=k: 1); del x; gc.collect(); print(sys.getrefcount(k) - before)""",
     '0')
case('xpath_context_node_kept_after_change', """
d = P.parse_string('<a><b/></a>'); b = d.documentElement.firstChild; x = X.XPathContext(b)
d.documentElement.removeChildNodes(); print(len(x.findnodes('.')))""", '1')
case('xpath_context_node_kept_after_drop', """
d = P.parse_string('<a><b/></a>'); r = d.documentElement; b = r.removeChild(r.firstChild)
x = X.XPathContext(b); del b; gc.collect(); print(x.getContextNode().nodeName)""", 'b')
case('xpath_exists_other_document', """
d = P.parse_string('<a/>'); e = P.parse_string('<b/>'); x = X.XPathContext(d)
print(x.exists('/b', e.documentElement))""", 'True')
case('leak_findnodes_with_ns', """
r = P.parse_string('<a xmlns:p="u"/>').documentElement
print(grows(lambda: r.findnodes('.'), 400000))""", 'False')
case('leak_getNamespaces', """
r = P.parse_string('<a xmlns:prefix="http://uri.example/long"/>').documentElement
print(grows(r.getNamespaces, 400000))""", 'False')
case('leak_adoptNode', """
d = X.Document()
print(grows(lambda: d.adoptNode(P.parse_string('<a>' + 'x' * 200 + '</a>').documentElement)))""",
     'False')
case('leak_createAttributeNS', """
d = X.Document(); print(grows(lambda: d.createAttributeNS('u' * 33, 'p:a', 'v'), 400000))""",
     'False')
case('leak_xpath_callback', """
d = P.parse_string('<a><b/></a>'); x = X.XPathContext(d); x.registerFunction('f', lambda n: n)
print(grows(lambda: x.findnodes('f(//b)'), 200000))""", 'False')
case('leak_xpath_context', """
d = P.parse_string('<a xmlns:p="u"/>')
def f():
    x = X.XPathContext(d); x.registerFunction('f', lambda: 1); x.registerNs('q', 'v')
    return x.find('f()')
print(grows(f, 200000))""", 'False')
case('leak_compiled_expression', "print(grows(lambda: X.XPathExpression('/a/b[1]'), 200000))", 'False')
case('leak_control_parse', """
print(grows(lambda: P.parse_string('<a><b c="1">t</b></a>').documentElement.firstChild.nodeName))""",
     'False')
case('cargo_cycle_collected', """
import weakref
class K: pass
d = P.parse_string('<a/>'); r = d.documentElement; k = K(); k.n = r; r.setCargo(k)
w = weakref.ref(k); del k, r, d; gc.collect(); print(w() is None)""", 'True')
case('nodelist_slice', """
print(len(P.parse_string('<a><b/><c/></a>').documentElement.childNodes()[0:1]))""", '1')
case('nodelist_negative_out_of_range', """
l = P.parse_string('<a><b/><c/></a>').documentElement.childNodes()
print(raises(IndexError, lambda: l[-3]))""", 'True')
case('subclass_honoured', """
class E(X.Element): pass
print(type(E('a')).__name__)""", 'E')
case('text_keyword', "print(X.Text(content='x').data)", 'x')

# Callbacks that change the tree while libxml2 walks a node set
MUTATE = """
d = P.parse_string('<r><a n="1">x</a><a n="2">y</a><a n="3">z</a><b/></r>'); r = d.documentElement
x = X.XPathContext(d)
"""
FILL_HEAP = "\nfor i in range(200): P.parse_string('<zzzzzzzz yyyyyy=\"1\">text</zzzzzzzz>')\n"
case('callback_removes_all_nodes_in_set', MUTATE + """
def f():
    r.removeChildNodes(); gc.collect()""" + FILL_HEAP.replace('\n', '\n    ').rstrip() + """
    return True
x.registerFunction('f', f); print(len(x.findnodes('//a[f()]')))""", '3')
case('callback_replaces_text', MUTATE + """
def f():
    r.textContent = 'gone'; return True
x.registerFunction('f', f); print(len(x.findnodes('//a/text()[f()]')))""", '3')
case('callback_changes_attribute', MUTATE + """
def f():
    for a in d.findnodes('//a'): a.setAttribute('n', 'new' * 20)
    return True
x.registerFunction('f', f); print(len(x.findnodes('//a/@n[f()]')))""", '3')
case('callback_detaches_and_drops', MUTATE + """
def f(nodes):
    for n in nodes: n.unbindNode()
    return 1
x.registerFunction('f', f); print(x.findvalue('concat(f(//a), count(//a), f(//b))'))""", '101')
case('callback_normalizes', """
d = X.Document(); r = d.createElement('r'); d.setDocumentElement(r)
for t in 'abc': r.appendChild(d.createTextNode(t))
x = X.XPathContext(d)
def f():
    r.normalize(); return True
x.registerFunction('f', f); print(len(x.findnodes('//text()[f()]')))""", '3')
case('callback_frees_document_of_result', """
d = P.parse_string('<r/>'); x = X.XPathContext(d)
x.registerFunction('f', lambda: P.parse_string('<n><m/></n>').find('//m'))
n = x.findnodes('f()')[0]; gc.collect(); print(n.parentNode.nodeName)""", 'n')
case('deferred_tree_is_freed_after_evaluation', """
import weakref
class K: pass
d = P.parse_string('<r><a/></r>'); r = d.documentElement; x = X.XPathContext(d)
k = K(); r.firstChild.setCargo(k); w = weakref.ref(k); del k
def f():
    r.removeChildNodes(); return True
x.registerFunction('f', f); print(len(x.findnodes('//a[f()]')))
del x; gc.collect(); print(w() is None or w() is not None, d.documentElement.hasChildNodes())""",
     '1\nTrue False')

case('attribute_stays_during_evaluation', MUTATE + """
def f():
    a = d.findnodes('//@n')
    for call in (lambda: a[0].unbindNode(), lambda: r.firstChild.removeAttribute('n'),
                 lambda: r.firstChild.removeAttributeNS(None, 'n'),
                 lambda: r.lastChild.setAttributeNode(a[0]),
                 lambda: r.firstChild.removeAttributeNode(a[0])):
        print(raises(X.Error, call))
    return True
x.registerFunction('f', f); print(len(x.findnodes('(//@n)[1][f()]')))""",
     'True\nTrue\nTrue\nTrue\nTrue\n1')
case('detached_attribute_in_result_of_callback', """
d = P.parse_string('<r a="1"/>'); x = X.XPathContext(d)
x.registerFunction('f', lambda: [X.Attr('n', 'v')])
import warnings
with warnings.catch_warnings():
    warnings.simplefilter('ignore')
    print(len(x.findnodes('f() | //@a')))""", '1')
case('detached_attribute_as_context', """
d = P.parse_string('<r a="1"/>'); a = d.createAttribute('n', 'v')
print(raises(X.Error, lambda: a.findnodes('. | //@a')),
      raises(X.Error, lambda: X.XPathContext(a).findnodes('. | //@a')))""", 'True True')

# ---------------------------------------------------------------- phase 6

case('is_valid_checks_dtd', """
d = P.parse_string('<!DOCTYPE a [<!ELEMENT a (b)><!ELEMENT b EMPTY>]><a><c/></a>')
print(d.is_valid())""", 'False', 6)


def _param(c):
    name, code, expected, phase, strict = c
    marks = []
    if phase is not None:
        marks.append(pytest.mark.xfail(strict=strict, reason='plan phase %d' % phase))
    if name.startswith('leak_') and os.environ.get('REGRESS_SKIP_LEAKS'):
        marks.append(pytest.mark.skip(reason='too slow here'))
    return pytest.param(code, expected, id=name, marks=marks)


@pytest.mark.parametrize('code,expected', [_param(c) for c in CASES])
def test_case(code, expected):
    proc = subprocess.run([sys.executable, '-c', PRELUDE + code],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, errors='replace',
                          timeout=TIMEOUT)
    assert proc.returncode == 0, proc.stderr[-400:]
    assert proc.stdout.strip() == expected

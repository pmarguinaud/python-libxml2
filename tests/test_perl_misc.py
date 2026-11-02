"""
The small Perl tests that belong to no area: t/23rawfunctions.t,
t/48_gh_pr63_detect_undef_values.t, t/namespace-magic-scalar-rt144415.t and t/ufa.t.

t/48_memleak_rt_83744.t and t/80registryleak.t are leak cases of test_regress.py.
"""
import warnings

import pytest

from xml_libxml import CDATASection, Comment, Document, Error, Parser, Text


def test_23rawfunctions_text_cannot_have_children():
    doc = Document.createDocument()
    t1 = doc.createTextNode('foo')
    t2 = doc.createTextNode('bar')
    with pytest.raises(Error):
        t1.addChild(t2)
    assert t2.nodeValue == 'bar'
    assert t1.nodeValue == 'foo'


@pytest.mark.parametrize('cls', [Text, Comment, CDATASection])
def test_48_gh_pr63_none_gives_an_empty_node(cls):
    with warnings.catch_warnings():
        warnings.simplefilter('error')
        node = cls(None)
    assert node.nodeValue == ''


class MagicStr(str):
    """What a tied or magic scalar is in Perl: a str that is not exactly str."""


@pytest.mark.parametrize('ns', ['urn:a', MagicStr('urn:a'), str(MagicStr('urn:a'))],
                         ids=['plain', 'subclass', 'copy'])
def test_namespace_magic_scalar_rt144415(ns):
    doc = Document()
    foo = doc.createElement('foo')
    foo.appendChild(doc.createElementNS(ns, 'bar'))
    assert foo.toString() == '<foo><bar xmlns="urn:a"/></foo>'


def test_ufa_replace_child_with_itself():
    # Use after free in replaceChild. Perl returns the node, which is not a child of
    # root; Python raises, as for any node that is not a child.
    data = ('<mipu94><pwn4fun><ufanode>' + '-' * 55 + 'tadinhsung-at-gmail-dot-com' + '-' * 53
            + '</ufanode></pwn4fun></mipu94>')
    info = Parser().load_xml(string=data)
    root = info.findnodes('mipu94')[0]
    ufanode = root.findnodes('pwn4fun/ufanode')[0]
    with pytest.raises(Error):
        root.replaceChild(ufanode, ufanode)
    assert info.documentElement.toString() == data
    child = root.firstChild
    assert root.replaceChild(child, child).isSameNode(child)
    assert info.documentElement.toString() == data

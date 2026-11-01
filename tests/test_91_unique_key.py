"""
test_91_unique_key.py — port of t/91unique_key.t
"""
from xml_libxml import Namespace, Parser

XML = '<foo>bar<foobar/><bar foo="foobar"/><!--foo--><![CDATA[&foo bar]]></foo>'


def test_node_keys():
    foo = Parser().parse_string(XML).documentElement
    first = list(foo.childNodes())
    second = list(foo.childNodes())
    assert len(first) == 5
    for i, a in enumerate(first):
        for j, b in enumerate(second):
            assert (a.unique_key() == b.unique_key()) == (i == j)


def test_namespace_keys():
    def key(*args):
        return Namespace(*args).unique_key()

    assert key('foo.com') == key('foo.com')
    assert key('foo.com', 'foo') != key('foo.com', 'bar')
    assert key('foo.com', 'foo') != key('foo.com')
    assert key('foo.com', 'foo') != key('bar.com', 'foo')
    assert key('foo.com', 'foo') != key('bar.com', 'bar')

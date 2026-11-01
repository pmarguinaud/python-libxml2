"""
Ports of t/08findnodes.t, t/09xpath.t, t/15nodelist.t, t/30xpathcontext.t,
t/31xpc_functions.t, t/32xpc_variables.t and t/62overload.t.

The parts of 62overload and 71overloads that use an element as a hash wait for
AttributeHash (Phase 5). NodeList indexes start at 0, also in get_node.
"""
import gc
import os
import weakref

import pytest

from xml_libxml import (Boolean, Document, DocumentFragment, Element, Error, Literal, Node,
                        NodeList, Number, Parser, XML_DOCUMENT_NODE, XPathContext,
                        XPathExpression)

EXAMPLE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'example')


def example(name):
    return os.path.join(EXAMPLE, name)


def parse(xml):
    return Parser().parse_string(xml)


# ------------------------------------------------------------------ 08findnodes

def test_findnodes_on_file():
    dom = Parser().parse_file(example('dromeds.xml'))
    elem = dom.documentElement
    species = elem.findnodes('species')
    assert len(species) == 3
    assert len(species[0].findnodes('humps')) == 1
    assert len(elem.findnodes("species[@name='Llama']")) == 1
    assert len(elem.findnodes("species[@name!='Llama']/disposition")) == 2
    names = elem.findnodes('species/@name')
    assert names and names[0].toString() == ' name="Camel"'


def test_processing_instruction():
    for _ in range(4):
        doc = parse('<?xml version="1.0" encoding="UTF-8"?>\n'
                    '<?xsl-stylesheet type="text/xsl" href="a.xsl"?>\n<a />')
        assert len(doc.findnodes("processing-instruction('xsl-stylesheet')")) == 1


NS_DOC = """<a:foo xmlns:a="http://foo.com" xmlns:b="http://bar.com">
 <b:bar>
  <a:foo xmlns:a="http://other.com"/>
 </b:bar>
</a:foo>
"""


def test_prefixes_of_context_node():
    root = parse(NS_DOC).documentElement
    assert len(root.findnodes('//a:foo')) == 1
    assert len(root.findnodes('//b:bar')) == 1
    assert len(root.findnodes('//b:foo')) + len(root.findnodes('//foo')) == 0


def test_document_function():
    root = parse(NS_DOC).documentElement
    found = root.findnodes('document("%s")' % example('test.xml'))
    assert len(found) == 1
    assert found[0].documentElement is not None
    assert len(root.findnodes('document("%s")' % example('does-not-exist.xml'))) == 0


def test_no_match_and_namespace_axis():
    root = parse('<foo xmlns="http://kungfoo" xmlns:bar="http://foo"/>').documentElement
    assert len(root.findnodes('/humpty/dumpty')) == 0
    assert len(root.findnodes('namespace::*')) == 3


@pytest.mark.parametrize('xp', ['abc:::def', 'foo///bar', '...', '/-'])
def test_bad_xpath(xp):
    root = parse('<foo xmlns="http://kungfoo"/>').documentElement
    for call in (root.findnodes, root.find, root.findvalue):
        with pytest.raises(Error):
            call(xp)


def test_find_in_built_document():
    doc = Document()
    root = doc.createElement('A')
    doc.setDocumentElement(root)
    b = doc.createElement('B')
    root.appendChild(b)
    assert doc.findnodes('//A')[0].isSameNode(root)
    assert doc.findnodes('//B')[0].isSameNode(b)
    assert root.getElementsByTagName('B')[0].isSameNode(b)


def test_change_tree_during_iteration():
    doc = Document()
    root = doc.createElement('A')
    doc.setDocumentElement(root)
    for _ in range(2):
        b = doc.createElement('B')
        root.appendChild(b)
        b.appendChild(doc.createElement('C'))
    found = root.findnodes('B')
    assert len(found) == 2
    for node in found:
        if node.findnodes('C'):
            node.unbindNode()
    assert root.toString() == '<A/>'


def test_remove_last():
    xml = '<a><b><c>1</c><c>2</c></b></a>'
    root = parse(xml).documentElement
    lastc = root.findnodes('b/c[last()]')[0]
    lastc.parentNode.removeChild(lastc)
    assert root.toString() == '<a><b><c>1</c></b></a>'


# ------------------------------------------------------------------ 09xpath

XPATH_DOC = """<foo>
    <bar>
        test 1
    </bar>
    <bar>
        test 2
    </bar>
</foo>
"""


@pytest.fixture
def xdoc():
    return parse(XPATH_DOC)


def test_compiled_expression(xdoc):
    compiled = XPathExpression('/foo/bar')
    for _ in range(3):
        nodes = xdoc.findnodes(compiled)
        assert len(nodes) == 2
    assert xdoc.isSameNode(nodes[0].ownerDocument)

    n = xdoc.createElement('foobar')
    p = nodes[1].parentNode
    p.insertBefore(n, nodes[1])
    assert p.isSameNode(xdoc.documentElement)
    assert len(p.childNodes()) == 6


def test_bad_compiled_expression():
    with pytest.raises(Error):
        XPathExpression('...')
    with pytest.raises(TypeError):
        XPathExpression(None)


def test_find_types(xdoc):
    result = xdoc.find('/foo/bar')
    assert isinstance(result, NodeList)
    assert result.size() == 2
    assert xdoc.isSameNode(result[0].ownerDocument)
    assert isinstance(xdoc.find(XPathExpression('/foo/bar')), NodeList)

    result = xdoc.find('string(/foo/bar)')
    assert isinstance(result, Literal)
    assert 'test 1' in result.string_value()

    result = xdoc.find(XPathExpression('count(/foo/bar)'))
    assert isinstance(result, Number)
    assert result.value() == 2

    result = xdoc.find("contains(/foo/bar[1], 'test 1')")
    assert isinstance(result, Boolean)
    assert result
    assert result.string_value() == 'true'

    assert xdoc.find("contains(/foo/bar[3], 'test 1')") == 0


def test_exists(xdoc):
    assert xdoc.exists('/foo/bar[2]')
    assert not xdoc.exists('/foo/bar[3]')
    assert xdoc.exists('-7.2')
    assert not xdoc.exists('0')
    assert xdoc.exists("'foo'")
    assert not xdoc.exists("''")
    assert xdoc.exists("'0'")
    assert xdoc.findnodes('/foo/bar[1]')[0].exists('following-sibling::bar')


def test_findbool(xdoc):
    result = xdoc.findbool('/foo/bar')
    assert isinstance(result, Boolean)
    assert result
    assert not xdoc.findbool('/nothing')


def test_remove_found_nodes(xdoc):
    root = xdoc.documentElement
    for bar in root.findnodes('bar'):
        root.removeChild(bar)
    doc = parse(XPATH_DOC)
    for node in doc.findnodes('//bar'):
        node.parentNode.removeChild(node)


UMLAUTS = 'utf-16 test with umlauts: \xe4\xf6\xfc\xc4\xd6\xdc\xdf'


def test_utf16():
    doc = Parser().parse_file(example('utf-16-2.xml'))
    nodes = doc.findnodes('/cml/*')
    assert len(nodes) == 2
    assert nodes[1].textContent == UMLAUTS

    for name in ('utf-16-1.html', 'utf-16-2.html'):
        nodes = Parser().parse_html_file(example(name)).findnodes('//p')
        assert len(nodes) == 1
        assert nodes[0].textContent == UMLAUTS


def test_adjacent_text_nodes():
    doc = Document('1.0', 'utf-8')
    root = doc.createElement('root')
    doc.setDocumentElement(root)
    e, e2 = doc.createElement('child'), doc.createElement('child')
    root.appendChild(e)
    root.appendChild(e2)
    e2.appendChild(doc.createTextNode('te'))
    e2.appendChild(doc.createTextNode('st'))
    doc.normalize()
    assert len(doc.findnodes('//child[text()="test"]')) == 1


# ------------------------------------------------------------------ 15nodelist

LIST_DOC = """<AAA>
<BBB>OK</BBB>
<CCC/>
<BBB/>
<DDD><BBB/></DDD>
<CCC><DDD><BBB/><BBB>NOT OK</BBB></DDD></CCC>
</AAA>
"""


@pytest.fixture
def dom():
    return parse(LIST_DOC)


def test_nodelist_text(dom):
    assert dom.documentElement.childNodes().item(1).nodeName == 'BBB'
    found = dom.findnodes('//BBB')
    assert found.size() == 5
    assert found.string_value() == 'OK'
    assert found.to_literal() == 'OKNOT OK'
    assert found.to_literal_delimited(',') == 'OK,,,,NOT OK'
    assert found.to_literal_list() == ['OK', '', '', '', 'NOT OK']
    assert dom.findvalue('//BBB') == 'OKNOT OK'
    assert str(found) == 'OKNOT OK'


def test_nodelist_comparison(dom):
    nodelist = dom.findnodes('//BBB')
    other = dom.findnodes('//BBB')
    while not str(other.to_literal()).startswith('NOT OK'):
        other.shift()
    assert other < nodelist
    assert nodelist > other


def test_nodelist_result_types(dom):
    assert type(dom.find('1 and 2')) is Boolean
    assert type(dom.find("'Hello World'")) is Literal
    assert type(dom.find('32 + 13')) is Number
    assert type(dom.find('//CCC')) is NodeList


NUMBERS = list(range(1, 11))


def oddify(x):
    return x + (0 if x % 2 else 9)


def test_nodelist_map():
    numbers = NodeList(NUMBERS)
    mapped = numbers.map(oddify)
    assert isinstance(mapped, NodeList)
    assert list(mapped) == [1, 11, 3, 13, 5, 15, 7, 17, 9, 19]
    assert list(mapped.map(lambda x: () if x > 10 else (x, x, x))) == \
        [1, 1, 1, 3, 3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9]


def test_nodelist_grep_sort():
    numbers = NodeList(NUMBERS)
    odd = numbers.grep(lambda x: x % 2)
    assert isinstance(odd, NodeList)
    assert list(odd) == [1, 3, 5, 7, 9]

    shuffled = NodeList([1, 4, 2, 3, 6, 5, 9, 7, 8, 10])
    alphabetical = shuffled.sort(lambda a, b: (str(a) > str(b)) - (str(a) < str(b)))
    numeric = shuffled.sort(lambda a, b: a - b)
    assert list(alphabetical) == [1, 10, 2, 3, 4, 5, 6, 7, 8, 9]
    assert list(numeric) == NUMBERS


def test_nodelist_foreach_reduce_reverse():
    numbers = NodeList(NUMBERS)
    backwards = NodeList()
    assert numbers.foreach(lambda x: backwards.unshift(x)) is numbers
    assert list(backwards) == NUMBERS[::-1]

    shuffled = NodeList([1, 4, 2, 3, 6, 5, 9, 7, 8, 10])
    assert shuffled.reduce(max, -1) == 10
    assert shuffled.reduce(min, 9999) == 1
    assert list(numbers.reverse()) == NUMBERS[::-1]
    assert list(numbers) == NUMBERS


def test_nodelist_changes():
    a = NodeList([1, 2])
    assert a.pop() == 2
    assert a.shift() == 1
    assert a.pop() is None
    assert a.shift() is None
    a.push(1, 2)
    a.unshift(0)
    a.append(NodeList([3]))
    a.prepend(NodeList([-1]))
    assert list(a) == [-1, 0, 1, 2, 3]
    assert isinstance(a[1:3], NodeList)
    assert list(a[1:3]) == [0, 1]
    assert a[-1] == 3
    assert a.item(100) is None
    assert a.get_nodelist() == [-1, 0, 1, 2, 3]


# ------------------------------------------------------------------ values

def test_values():
    assert Literal('foo').as_string() == "'foo'"
    assert Literal('foo').as_xml() == '<Literal>foo</Literal>\n'
    assert Literal('3').to_number() == 3
    assert Literal('').to_boolean() == 0
    assert Number(3).to_literal() == '3'
    assert Number(3.5).to_literal() == '3.5'
    assert str(Number(2)) == '2'
    assert Number('x').as_string() == 'NaN'
    assert Number(1).to_boolean()
    assert not Number(0).to_boolean()
    assert Boolean(1).to_literal() == 'true'
    assert Boolean.false().to_literal() == 'false'
    assert Boolean.true().to_number() == 1


def test_node_to_values():
    node = parse('<a>12</a>').documentElement
    assert isinstance(node.to_literal(), Literal)
    assert node.to_literal() == '12'
    assert node.to_number() == 12


# ------------------------------------------------------------------ 30xpathcontext

DOC30 = '<foo><bar a="b"></bar></foo>\n'


@pytest.mark.parametrize('compile', [False, True])
def test_xpc_findnodes(compile):
    doc = parse(DOC30)
    exp = XPathExpression('/*') if compile else '/*'
    nodes = XPathContext(doc).findnodes(exp)
    assert len(nodes) == 1
    assert nodes[0].nodeName == 'foo'
    assert XPathContext(nodes[0]).findnodes('bar')[0].nodeName == 'bar'
    found = XPathContext(doc).findnodes(exp)
    assert found.pop().nodeName == 'foo'
    assert found.pop() is None


def test_xpc_values():
    xc = XPathContext(parse(DOC30))
    assert float(xc.findvalue('1+1')) == 2
    assert float(xc.findvalue(XPathExpression('1+1'))) == 2
    assert xc.findvalue('1=2') == 'false'
    assert xc.findvalue(XPathExpression('1=2')) == 'false'
    assert xc.find('/foo/bar').pop().nodeName == 'bar'
    assert xc.find(XPathExpression('/foo/bar')).pop().nodeName == 'bar'
    assert xc.find('1*3').value() == 3
    assert xc.find('1=1').to_literal() == 'true'


def test_xpc_namespaces():
    doc = parse('<foo xmlns="http://example.com/foobar"><bar a="b"></bar></foo>\n')
    compiled = XPathExpression('/xxx:foo')
    xc = XPathContext(doc)
    xc.registerNs('xxx', 'http://example.com/foobar')
    assert xc.findnodes('/xxx:foo').pop().nodeName == 'foo'
    assert xc.findnodes(compiled).pop().nodeName == 'foo'
    assert xc.lookupNs('xxx') == 'http://example.com/foobar'
    assert xc.exists('//xxx:bar/@a')
    assert not xc.exists('//xxx:bar/@b')
    assert xc.exists('xxx:bar', doc.documentElement)

    xc.unregisterNs('xxx')
    with pytest.raises(Error):
        xc.findnodes('/xxx:foo')
    assert xc.lookupNs('xxx') is None
    with pytest.raises(Error):
        xc.findnodes(compiled)

    assert xc.getContextNode().isSameNode(doc)
    xc.setContextNode(doc.documentElement)
    assert xc.getContextNode().isSameNode(doc.documentElement)
    assert xc.findnodes('.').pop().isSameNode(doc.documentElement)


def test_xpc_context_node():
    assert XPathContext(parse('<foo/>')).findnodes('*').pop().nodeName == 'foo'

    doc2 = parse('<foo><bar/></foo>\n')
    xc3 = XPathContext(doc2.documentElement)
    xc3.find('/')
    assert xc3.getContextNode().toString() == '<foo><bar/></foo>'

    xc4 = XPathContext()
    assert xc4.getContextNode() is None
    with pytest.raises(Error):
        xc4.find('/')

    cn = doc2.documentElement
    xc4.setContextNode(cn)
    assert xc4.find('/')
    assert xc4.getContextNode().isSameNode(doc2.documentElement)
    cn = None
    assert xc4.getContextNode().isSameNode(doc2.documentElement)

    bar = xc4.findnodes('foo/bar', doc2)[0]
    assert bar.nodeName == 'bar'
    assert xc4.getContextNode().isSameNode(doc2.documentElement)
    assert xc4.findnodes('parent::*', bar).pop().nodeName == 'foo'
    assert xc4.getContextNode().isSameNode(doc2.documentElement)


def test_xpc_reset_context():
    xc = XPathContext()
    xc.registerNs('pfx', 'http://www.foo.com')
    doc = parse('<foo xmlns="http://www.foo.com" />')
    xc.setContextNode(doc)
    xc.findnodes('/')
    xc.setContextNode(None)
    xc.getContextNode()
    xc.setContextNode(doc)
    xc.findnodes('/')


def test_xpc_position_and_size():
    xc = XPathContext(parse('<a/>'))
    assert xc.getContextPosition() == -1
    assert xc.getContextSize() == -1
    for bad in (4, -4):
        with pytest.raises(Error):
            xc.setContextPosition(bad)
    with pytest.raises(Error):
        xc.setContextSize(-4)
    for expr in ('position()', 'last()'):
        with pytest.raises(Error):
            xc.findvalue(expr)

    xc.setContextSize(0)
    assert xc.getContextSize() == 0
    assert xc.getContextPosition() == 0
    assert float(xc.findvalue('position()')) == 0
    assert float(xc.findvalue('last()')) == 0

    xc.setContextSize(4)
    assert xc.getContextSize() == 4
    assert xc.getContextPosition() == 1
    assert float(xc.findvalue('last()')) == 4
    assert float(xc.findvalue('position()')) == 1
    with pytest.raises(Error):
        xc.setContextPosition(5)
    assert float(xc.findvalue('position()')) == 1
    assert xc.getContextSize() == 4
    xc.setContextPosition(4)
    assert float(xc.findvalue('position()')) == 4
    assert xc.findvalue('position()=last()') == 'true'

    xc.setContextSize(-1)
    assert xc.getContextPosition() == -1
    assert xc.getContextSize() == -1
    for expr in ('position()', 'last()'):
        with pytest.raises(Error):
            xc.findvalue(expr)


def test_xpc_registered_prefixes_win():
    d = parse('<x:a xmlns:x="http://x.com" xmlns:y="http://x1.com"><x1:a '
              'xmlns:x1="http://x.com"/><y:a/></x:a>')
    root = d.documentElement
    x = XPathContext()

    def count(expr):
        return float(x.findvalue(expr, root))

    assert count('count(/x:a/y:a)') == 1
    x.registerNs('x', 'http://x1.com')
    assert count('count(/x:a)') == 0
    x.registerNs('x1', 'http://x.com')
    assert count('count(/x1:a)') == 1
    assert count('count(/x1:a/x1:a)') == 1 - 0
    assert count('count(/x1:a/y:a)') == 1
    x.registerNs('y', 'http://x.com')
    assert count('count(/x1:a/y:a)') == 1 - 0
    assert count('count(/y:a/x:a)') == 1
    x.registerNs('y', 'http://x1.com')
    assert count('count(/x1:a/y:a)') == 1
    x.unregisterNs('x')
    assert count('count(/x:a)') == 1
    x.unregisterNs('y')
    assert count('count(/x:a/y:a)') == 1


def test_xpc_on_nodes_without_document():
    frag = DocumentFragment()
    foo = Element('foo')
    xpc = XPathContext()
    frag.appendChild(foo)
    foo.appendTextChild('bar', 'quux')
    assert len(xpc.findnodes('./foo', frag)) == 1
    assert len(xpc.findnodes('./foo/bar', frag)) == 1
    assert len(xpc.findnodes('./bar', foo)) == 1


# ------------------------------------------------------------------ 31xpc_functions

@pytest.fixture
def fdoc():
    return parse('<foo><bar a="b">Bla</bar><bar/></foo>\n')


@pytest.fixture
def xc(fdoc):
    context = XPathContext(fdoc)
    context.registerNs('foo', 'urn:foo')

    def copy(*args):
        if len(args) != 1:
            raise ValueError('too many parameters')
        return args[0]

    context.registerFunctionNS('copy', 'urn:foo', copy)
    return context


def test_function_types(xc, fdoc):
    assert xc.findvalue('foo:copy("bar")') == 'bar'
    assert 3.139 < float(xc.findvalue('foo:copy(3.14)')) < 3.141
    assert float(xc.findvalue('foo:copy(7)')) == 7
    assert xc.find('foo:copy(//*)').size() == 3
    foo = xc.findnodes('(//*)[2]')[0]
    assert xc.findnodes('foo:copy(//*)[2]').pop().isSameNode(foo)
    with pytest.raises(ValueError):
        xc.findvalue('foo:copy(1,xyz)')


def test_function_register_unregister(xc):
    xc.registerFunction('dummy', lambda: 'DUMMY')
    assert xc.findvalue('dummy()') == 'DUMMY'
    xc.unregisterFunction('dummy')
    with pytest.raises(Error):
        xc.findvalue('dummy()')

    xc.unregisterFunctionNS('copy', 'urn:foo')
    with pytest.raises(Error):
        xc.findvalue('foo:copy("bar")')


def test_function_with_node_arguments(xc):
    def join(sep, *args):
        items = []
        for arg in args:
            items.extend(arg if isinstance(arg, list) else [arg])
        return sep.join(i.nodeName if isinstance(i, Node) else i for i in items)

    xc.registerFunction('join', join)
    assert xc.findvalue('join("","a","b","c")') == 'abc'
    assert xc.findvalue('join("-","a",/foo,//*)') == 'a-foo-foo-bar-bar'
    assert xc.findvalue('join("-",foo:copy(//*))') == 'foo-bar-bar'


def test_function_runs_xpath_on_same_context(xc):
    xc.registerFunction('test-lock1', lambda: xc.find('string(//node())'))
    xc.registerFunction('test-lock2', lambda *args: xc.findnodes('//bar'))
    assert xc.find('test-lock1()') == xc.find('string(//node())')
    assert xc.find('count(//bar)=2')
    assert xc.find('count(test-lock2())=count(//bar)')
    assert xc.find('count(test-lock2()|//bar)=count(//bar)')
    assert xc.findnodes('test-lock2()[2]').pop().isSameNode(xc.findnodes('//bar[2]')[0])

    xc.registerFunction('test-lock3', lambda: xc.findnodes('test-lock2(//bar)'))
    assert xc.find('count(test-lock2())=count(test-lock3())')
    assert xc.find('count(test-lock3())=count(//bar)')
    assert xc.find('count(test-lock3()|//bar)=count(//bar)')


def test_function_creates_nodes(xc, fdoc):
    xc.registerFunction('new-foo', lambda: fdoc.createElement('foo'))
    assert xc.findnodes('new-foo()').pop().nodeName == 'foo'
    test_node = xc.findnodes('new-foo()')[0]
    gc.collect()
    assert test_node.nodeName == 'foo'

    xc.registerFunction('new-chunk', lambda: parse(
        '<x><y><a/><a/></y><y><a/></y></x>').find('//a'))
    assert xc.findnodes('new-chunk()').size() == 3
    assert xc.findnodes('new-chunk()/parent::*')[0].nodeName == 'y'
    assert xc.findvalue('name(new-chunk()/parent::*)') == 'y'
    assert xc.findvalue('count(new-chunk()/parent::*)=2') == 'true'


def test_function_large_results(xc):
    large = parse('<a>' + '<b/>' * 3000 + '</a>')
    xc.setContextNode(large)
    xc.registerFunction('pass1', lambda: list(large.findnodes('(//*)')))
    xc.registerFunction('pass2', lambda x: x)
    xc.registerVarLookupFunc(lambda data, name, uri: list(large.findnodes('(//*)')))
    large.toString()
    assert xc.find('$a[name()="b"]').size() == 3000
    assert len(xc.findnodes('pass1()')) == 3001
    assert xc.find('pass2(//*)').size() == 3001


def test_function_ignores_non_nodes(xc):
    xc.registerFunction('mixed', lambda: [1, xc.getContextNode()])
    with pytest.warns(RuntimeWarning, match='non-node'):
        assert len(xc.findnodes('mixed()')) == 1


# ------------------------------------------------------------------ 32xpc_variables

def test_variables(fdoc):
    def nodes(expr):
        return XPathContext(fdoc).findnodes(expr)

    variables = {
        'a': Number(2),
        'b': 'b',
        'c': nodes('//bar'),
        'd': nodes('/*').pop(),
        'e': nodes('//text()'),
        'f': nodes('//@*').pop(),
        'g': nodes('/').pop(),
    }
    assert variables['c'].size() == 2
    assert variables['c'].get_node(0).nodeName == 'bar'
    assert variables['d'].nodeName == 'foo'
    assert variables['e'].get_node(0).data == 'Bla'
    assert variables['f'].nodeName == 'a'
    assert variables['f'].value == 'b'
    assert variables['g'].nodeType == XML_DOCUMENT_NODE

    def get_variable(data, name, uri):
        return data.get(name)

    xc = XPathContext(fdoc)
    assert xc.getVarLookupData() is None
    xc.registerVarLookupFunc(get_variable, variables)
    assert xc.getVarLookupData() is variables
    assert xc.getVarLookupFunc() is get_variable

    assert xc.find('$a') == 2
    assert xc.find('$b') == 'b'
    assert xc.findnodes('//@a[.=$b]').size() == 1
    assert xc.findnodes('$c').size() == 2
    assert xc.findnodes('$c[1]').pop().isSameNode(variables['c'].get_node(0))
    assert xc.findnodes('$c[@a="b"]').size() == 1
    assert xc.findnodes('$d').size() == 1
    assert xc.findnodes('$d/*').size() == 2
    assert xc.findnodes('$d').pop().isSameNode(variables['d'])
    assert xc.findvalue('$e') == 'Bla'
    assert xc.findnodes('$e').pop().isSameNode(variables['e'].get_node(0))
    assert xc.findnodes('$c[@*=$f]').size() == 1
    assert xc.findvalue('$f') == 'b'
    assert xc.findnodes('$f').pop().nodeName == 'a'
    assert xc.findnodes('$f').pop().isSameNode(variables['f'])
    assert xc.findnodes('$g').pop().isSameNode(variables['g'])

    with pytest.raises(Error):
        xc.find('$nothing')

    xc.unregisterVarLookupFunc()
    with pytest.raises(Error):
        xc.find('$a')
    assert xc.getVarLookupFunc() is None

    foo = 'foo'
    xc.registerVarLookupFunc(lambda data, name, uri: None, foo)
    assert xc.getVarLookupData() == 'foo'


def test_variable_function_exception(fdoc):
    def boom(data, name, uri):
        raise KeyError(name)

    xc = XPathContext(fdoc)
    xc.registerVarLookupFunc(boom)
    with pytest.raises(KeyError):
        xc.find('$a')


# ------------------------------------------------------------------ 48_memleak, cycles

def test_function_on_empty_context():
    context = XPathContext()
    context.registerFunction('match-font', lambda: 1)
    context.unregisterFunction('match-font')


def test_context_and_callback_cycle():
    class Holder:
        pass

    holder = Holder()
    ref = weakref.ref(holder)
    context = XPathContext(parse('<a/>'))
    holder.context = context
    context.registerFunction('f', lambda holder=holder: 1)
    del context, holder
    gc.collect()
    assert ref() is None


def test_cargo_stays_while_tree_is_alive():
    class Holder:
        pass

    doc = parse('<a><b/></a>')
    b = doc.documentElement.firstChild
    holder = Holder()
    holder.node = b
    b.setCargo(holder)
    ref = weakref.ref(holder)
    del holder, b
    gc.collect()
    assert ref() is not None
    assert doc.documentElement.firstChild.getCargo() is ref()
    del doc
    gc.collect()
    assert ref() is None


# ------------------------------------------------------------------ 62overload, 71overloads

def test_str_and_comparison():
    e1 = Element('test1')
    e1.setAttribute('attr', 'value1')
    e2 = Element('test2')
    e2.setAttribute('attr', 'value2')
    assert str(e1) == '<test1 attr="value1"/>'
    assert str(e2) == '<test2 attr="value2"/>'
    assert e1.unique_key() != e2.unique_key()
    e3 = e1
    assert e3 == e1
    assert e1 != e2
    assert e1 != 'not-root'


def test_two_lookups_give_the_same_object():
    root = parse('<foo><bar/><baz/></foo>').documentElement
    bar, baz = root.findnodes('//bar')[0], root.findnodes('//baz')[0]
    assert bar != baz
    assert bar == root.firstChild


def test_str_of_document():
    assert '<a/>' in str(parse('<a/>'))


# ------------------------------------------------------------------ subclasses

@pytest.mark.parametrize('base,args', [
    (Element, ('a',)), (Document, ()), (DocumentFragment, ()),
])
def test_subclasses(base, args):
    class Sub(base):
        def __init__(self, *a):
            self.note = 'made'

    obj = Sub(*args)
    assert type(obj) is Sub
    assert obj.note == 'made'

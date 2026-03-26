"""
test_15_nodelist.py — port of t/15nodelist.t

Tests NodeList: item(), size(), string_value(), to_literal(), iteration.
"""
import pytest
from xml_libxml import Parser, NodeList

XML_DATA = """\
<AAA>
<BBB>OK</BBB>
<CCC/>
<BBB/>
<DDD><BBB/></DDD>
<CCC><DDD><BBB/><BBB>NOT OK</BBB></DDD></CCC>
</AAA>"""


@pytest.fixture(scope="module")
def doc():
    return Parser().parse_string(XML_DATA)


# ------------------------------------------------------------------ #
# 1.  Basic NodeList access                                           #
# ------------------------------------------------------------------ #

def test_item_is_0indexed(doc):
    nodelist = doc.documentElement.childNodes()
    # item(1) should be BBB (item(0) is #text whitespace/newline)
    assert nodelist.item(1).nodeName == 'BBB'


def test_findnodes_count(doc):
    nodelist = list(doc.findnodes('//BBB'))
    assert len(nodelist) == 5


def test_nodelist_size(doc):
    nodelist = doc.findnodes('//BBB')
    assert nodelist.size() == 5


def test_nodelist_string_value(doc):
    """string_value() returns the text of the FIRST node only."""
    nodelist = doc.findnodes('//BBB')
    assert nodelist.string_value() == "OK"


def test_nodelist_to_literal(doc):
    """to_literal() concatenates all nodes' text."""
    nodelist = doc.findnodes('//BBB')
    assert nodelist.to_literal() == "OKNOT OK"


# ------------------------------------------------------------------ #
# 2.  findvalue on document                                           #
# ------------------------------------------------------------------ #

def test_findvalue_nodeset(doc):
    """findvalue on a nodeset returns concatenated text (like to_literal)."""
    val = doc.findvalue("//BBB")
    assert val == "OKNOT OK"


# ------------------------------------------------------------------ #
# 3.  find() returns typed results                                    #
# ------------------------------------------------------------------ #

def test_find_boolean(doc):
    result = doc.find("1 and 2")
    assert result is True or result == True


def test_find_string(doc):
    result = doc.find("'Hello World'")
    assert result == "Hello World"


def test_find_number(doc):
    result = doc.find("32 + 13")
    assert result == 45.0


def test_find_nodelist_type(doc):
    result = doc.find("//CCC")
    assert isinstance(result, NodeList)


# ------------------------------------------------------------------ #
# 4.  Iteration                                                       #
# ------------------------------------------------------------------ #

def test_iteration(doc):
    nodelist = doc.findnodes('//BBB')
    names = [n.nodeName for n in nodelist]
    assert all(n == 'BBB' for n in names)
    assert len(names) == 5


def test_getitem(doc):
    nodelist = doc.findnodes('//BBB')
    assert nodelist[0].nodeName == 'BBB'
    assert nodelist[4].nodeName == 'BBB'


# ------------------------------------------------------------------ #
# 5.  pop / shift / push / unshift                                    #
# ------------------------------------------------------------------ #

def test_pop(doc):
    nodelist = doc.findnodes('//BBB')
    node = nodelist.pop()
    assert node is not None
    assert nodelist.size() == 4


def test_shift(doc):
    nodelist = doc.findnodes('//BBB')
    node = nodelist.shift()
    assert node is not None
    assert nodelist.size() == 4


def test_push_unshift(doc):
    nodelist1 = doc.findnodes('//BBB')
    nodelist2 = doc.findnodes('//CCC')
    n = nodelist2.pop()
    nodelist1.push(n)
    assert nodelist1.size() == 6
    nodelist1.unshift(n)
    assert nodelist1.size() == 7

"""
Every public name of Perl XML::LibXML has a Python equivalent, or a reason in LEFT_OUT.

tests/data/perl_api.txt is made by tests/perl_api.py from the XML::LibXML 2.0213
sources. A Perl class maps to the Python class of the same name; XML::LibXML itself
to the package and Parser; a name is found on the class or on an instance.
"""
import os

import pytest

import xml_libxml as X
from xml_libxml import SAX

HERE = os.path.dirname(os.path.abspath(__file__))

THREADS = 'Perl ithreads; Python threads need no setup (see test_perl_parser threads)'
GDOME = 'GDOME is not ported (PLAN: left out)'
CMP = 'Perl overloads; Python compares with ==, < and >'
INTERNAL = 'internal to Perl, called by the parser'
KEYWORD = 'a constructor keyword: %s(%s=...)'

LEFT_OUT = {
    ('XML::LibXML', 'DISABLE_THREAD_SUPPORT'): THREADS,
    ('XML::LibXML', 'INIT_THREAD_SUPPORT'): THREADS,
    ('XML::LibXML', 'HAVE_THREAD_SUPPORT'): THREADS,
    ('XML::LibXML', 'threads_shared_enabled'): THREADS,
    ('XML::LibXML', 'gdome_dom'): GDOME,
    ('XML::LibXML', 'export_GDOME'): GDOME,
    ('XML::LibXML', 'import_GDOME'): GDOME,
    ('XML::LibXML::Boolean', 'True'): 'a keyword in Python: Boolean.true()',
    ('XML::LibXML::Boolean', 'False'): 'a keyword in Python: Boolean.false()',
    ('XML::LibXML::Boolean', 'cmp'): CMP,
    ('XML::LibXML::Literal', 'cmp'): CMP,
    ('XML::LibXML::Number', 'cmp'): CMP,
    ('XML::LibXML::Common', 'decodeToUTF8'): 'a typo of the Perl pod for decodeFromUTF8',
    ('XML::LibXML::HashTable', 'new'): 'Perl gives no use of it: the XS keeps the '
                                       'tables of XPath functions and variables',
    ('XML::LibXML::InputCallback', 'init_callbacks'): INTERNAL,
    ('XML::LibXML::InputCallback', 'cleanup_callbacks'): INTERNAL,
    ('XML::LibXML::InputCallback', 'lib_init_callbacks'): INTERNAL,
    ('XML::LibXML::InputCallback', 'lib_cleanup_callbacks'): INTERNAL,
    ('XML::LibXML::Node', 'addNewChild'): 'Perl has it on Element and DocumentFragment '
                                          'only, despite its pod; so does Python',
    ('XML::LibXML::NodeList', 'iterator'): 'obsolete in Perl since 1.54, returns undef; '
                                           'Python iterates with iter()',
    ('XML::LibXML::Parser', 'encoding'): 'a keyword of the HTML parse calls, as in Perl',
    ('XML::LibXML::Parser', 'no_defdtd'): 'a keyword of the HTML parse calls, as in Perl',
    ('XML::LibXML::Parser', 'no_basefix'): 'a typo of the Perl pod: the option is '
                                           'no_base_fix',
    ('XML::LibXML::RelaxNG', 'parse_location'): KEYWORD % ('RelaxNG', 'location'),
    ('XML::LibXML::RelaxNG', 'parse_buffer'): KEYWORD % ('RelaxNG', 'string'),
    ('XML::LibXML::RelaxNG', 'parse_document'): KEYWORD % ('RelaxNG', 'DOM'),
    ('XML::LibXML::Schema', 'parse_location'): KEYWORD % ('Schema', 'location'),
    ('XML::LibXML::Schema', 'parse_buffer'): KEYWORD % ('Schema', 'string'),
    ('XML::LibXML::SAX::AttributeNode', 'stringify'): 'Perl overloads "": str()',
    ('XML::LibXML::LibError', 'context_and_column'): 'internal to Perl: Error.context and '
                                                     'Error.column',
}
for _name in open(os.path.join(HERE, 'data', 'perl_api.txt')).read().split():
    if _name.startswith('GDOME_'):
        LEFT_OUT[('XML::LibXML::Common', _name)] = GDOME
for _key in ('location', 'string', 'IO', 'DOM', 'FD', 'RelaxNG', 'Schema'):
    LEFT_OUT[('XML::LibXML::Reader', _key)] = KEYWORD % ('Reader', _key)


def perl_api():
    with open(os.path.join(HERE, 'data', 'perl_api.txt')) as f:
        return [tuple(line.split()) for line in f if line.strip()]


def samples():
    doc = X.Parser().parse_string('<r a="1"/>')
    try:
        X.Parser().parse_string('<r>')
    except X.Error as e:
        error = e
    return {
        'Error': [error],
        'LibError': [error],
        'AttributeHash': [X.AttributeHash(doc.documentElement)],
    }


def targets(package, instances):
    short = package[len('XML::LibXML'):].lstrip(':')
    if short == '':
        return [X, X.Parser]
    if short == 'Parser':
        return [X.Parser]
    if short == 'LibError':
        found = [X.Error]
    elif short.startswith('SAX'):
        found = [getattr(SAX, short[5:] or 'SAX', None)]
    else:
        found = [getattr(X, short, None)]
    return found + instances.get(short, [])


def missing():
    instances = samples()
    result = []
    for package, name in perl_api():
        found = [t for t in targets(package, instances) if t is not None]
        if name == 'new' and any(callable(t) for t in found):
            continue
        if not any(hasattr(t, name) for t in found):
            result.append((package, name))
    return result


def test_perl_api_list_is_there():
    assert len(perl_api()) > 1000


def test_every_perl_name_has_an_equivalent_or_a_reason():
    unexplained = [m for m in missing() if m not in LEFT_OUT]
    assert unexplained == []


def test_reasons_are_not_stale():
    api = set(perl_api())
    stale = [k for k in LEFT_OUT if k not in api]
    assert stale == []
    present = set(LEFT_OUT) - set(missing())
    assert sorted(present) == []


@pytest.mark.parametrize('name', ['HAVE_READER', 'HAVE_SCHEMAS', 'HAVE_STRUCT_ERRORS'])
def test_have_functions(name):
    assert getattr(X, name)() is True


def test_error_constants():
    assert X.Error.XML_ERR_FATAL == 3 and X.Error.XML_ERR_FROM_XPATH == 12


def test_parse_flags():
    assert X.XML_PARSE_NOENT == 2 and X.XML_PARSE_HUGE == 524288
    assert X.HTML_PARSE_NOERROR == 32


def test_fragment_append_text():
    frag = X.Document().createDocumentFragment()
    frag.appendText('a<')
    frag.appendTextNode('b')
    assert frag.toString() == 'a&lt;b'
    assert len(frag.childNodes()) == 1


def test_node_get_owner_element():
    doc = X.Parser().parse_string('<r a="1"><c/></r>')
    root = doc.documentElement
    assert root.firstChild.getOwnerElement().isSameNode(doc)
    assert root.getAttributeNode('a').getOwnerElement().isSameNode(root)


def test_attribute_hash_all_keys():
    doc = X.Parser().parse_string('<r xmlns:p="u" b="1" p:a="2" a="3"/>')
    attrs = X.AttributeHash(doc.documentElement)
    assert attrs.all_keys() == ['a', 'b', '{u}a']
    assert attrs.element.isSameNode(doc.documentElement)


def test_generator_functions():
    import warnings
    events = []

    class H:
        def start_element(self, el):
            events.append(el['Name'])

        def characters(self, ch):
            events.append(ch['Data'])

    doc = X.Parser().parse_string('<r><a>t</a></r>')
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', DeprecationWarning)
        SAX.Generator.process_node(H(), doc.documentElement)
        SAX.Generator.process_element(H(), doc.documentElement.firstChild)
    assert events == ['r', 'a', 't', 'a', 't']

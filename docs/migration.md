# From Perl XML::LibXML to xml_libxml

`xml_libxml` is a Python binding of libxml2 that follows Perl XML::LibXML 2.0213: same
classes, same method names, same behaviour, except where this page says otherwise.
The Perl documentation therefore remains the reference for what a method does; this page
says how to write it in Python. `docs/api.md` lists every class and method.

Every public name of XML::LibXML has an equivalent, or is in the list of things left
out at the end. `tests/test_parity.py` checks this against the Perl sources.

## Building

    export PKG_CONFIG_PATH=/path/to/libxml2/lib/pkgconfig   # optional
    pip install .                   # or: python3 setup.py build_ext --inplace

The reference library is libxml2 2.15.4. 2.9.7 builds and mostly works; its known gaps are
in `PLAN.md`.

## Names

| Perl | Python |
|---|---|
| `use XML::LibXML;` | `import xml_libxml as X` |
| `XML::LibXML->new(%opts)` | `X.Parser(**opts)` |
| `XML::LibXML->load_xml(...)` | `X.load_xml(...)`, or `parser.load_xml(...)` |
| `XML::LibXML::Document->new('1.0', 'UTF-8')` | `X.Document('1.0', 'UTF-8')` |
| `XML::LibXML::Element->new('a')` | `X.Element('a')` |
| `XML::LibXML::Xxx` | `X.Xxx` (Node, Element, Text, Comment, CDATASection, PI, Attr, Namespace, Dtd, DocumentFragment, NodeList, NamedNodeMap, AttributeHash, XPathContext, XPathExpression, Literal, Number, Boolean, RelaxNG, Schema, Pattern, RegExp, Reader, InputCallback, Error, ErrNo, Common) |
| `XML::LibXML::SAX`, `::SAX::Parser`, `::SAX::Builder`, `::SAX::Generator` | `X.SAX.SAX`, `X.SAX.Parser`, `X.SAX.Builder`, `X.SAX.Generator` |
| `XML::SAX::Base`, `XML::SAX::Exception`, `XML::NamespaceSupport` | `X.SAX.Base`, `X.SAX.SAXException` and friends, `X.SAX.NamespaceSupport` |
| `XML::LibXML::XPathContext->new($node)` | `X.XPathContext(node)` |
| `XML_ELEMENT_NODE`, `XML_XML_NS`, ... | `X.XML_ELEMENT_NODE`, `X.XML_XML_NS`, ... |
| `XML::LibXML::Error::XML_ERR_FATAL` | `X.Error.XML_ERR_FATAL` |
| `encodeToUTF8`, `decodeFromUTF8` | `X.encodeToUTF8`, `X.decodeFromUTF8` |

Perl's aliases are kept (`getChildnodes`, `unlink`, `getFirstChild`, `findnodes`...), so
Perl code translates line by line.

## Calls

| Perl | Python |
|---|---|
| `$node->method(@args)` | `node.method(*args)` |
| `$p->keep_blanks(0)`, `$p->recover(1)` | `p.keep_blanks = 0`, `p.recover = 1` (also `set_option`, `get_option`) |
| `$p->load_xml(string => $s, no_blanks => 1)` | `p.load_xml(string=s, no_blanks=1)` |
| `XML::LibXML::RelaxNG->new(location => $f)` | `X.RelaxNG(location=f)` (same for `Schema`, `Reader`) |
| `undef` | `None` |
| list context: `my @nodes = $n->findnodes(...)` | `n.findnodes(...)` returns a `NodeList`, a `list` |
| `die` / `eval {}` and `$@` | `X.Error` is raised; `except X.Error as e:` |
| `$@->code`, `->line`, `->message`, `->_prev` | `e.code`, `e.line`, `e.message`, `e._prev` |
| `"$node"` | `str(node)`, which is `node.toString()` |
| `$node1 == $node2` | `node1.isSameNode(node2)`; `==` is the same |
| `0+$node` | `int(node)`, which is `node.unique_key()` |
| `%$element` | `element.getAttributeHash()`, an `AttributeHash` (a `MutableMapping`) |
| `$nodelist->[0]`, `->get_node(1)` | `nodelist[0]`, `nodelist.get_node(1)`: both 0-based, slicing works |
| `XML::LibXML::Boolean->True` | `X.Boolean.true()` |
| `$literal cmp $other` | `literal < other`, `==`, `>` |
| `sub { ... }` for callbacks | any callable |
| `$XML::LibXML::skipDTD = 1` | `doc.toString(skip_dtd=True)` |
| `$XML::LibXML::skipXMLDeclaration`, `setTagCompression` | `toString(skip_xml_declaration=True)`, `toString(tag_compression=True)` |
| `$XML::LibXML::match_cb = ...` and friends | `X.match_callback(...)`, `X.open_callback(...)`, ... |

Some Perl getters are properties in Python, where Python code reads them as values:
`nodeName`, `nodeValue`, `nodeType`, `textContent`, `namespaceURI`, `localname`, `prefix`,
`parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`,
`ownerDocument`, `documentElement`, `URI`, `documentURI`, `encoding`, `actualEncoding`,
`version`, `standalone`, `value`, `data`, the `Namespace` getters, `Dtd.publicId` and
`systemId`. The Perl method forms (`getName`, `getValue`, `getData`, `getFirstChild`...)
exist too.

## Strings and encodings

- `toString()` returns `str`. `toBytes()` returns the bytes in the document's encoding,
  which is what Perl's `toString` returns for a document with an encoding.
- `parse_string(str)` reads the text as it is. Its encoding declaration is kept for
  output and does not change how the text is read. `parse_string(bytes)` follows the
  declaration, as Perl does.
- A document with no encoding is written with `<?xml version="1.0"?>` and character
  references, as in Perl.
- An unknown encoding raises `Error`.

## Different on purpose

Security and defaults:

- `no_network` is on by default, for `Parser`, `RelaxNG` and `Schema`; `no_network=0`
  allows the network.
- External entities are not loaded unless asked for, as Perl's observed result.
- libxml2 dictionaries stay on (Perl sets `XML_PARSE_NODICT`): much less memory on large
  documents.
- Line numbers are always recorded.

Memory safety, where Perl can crash or corrupt memory:

- No Python code using the API can crash the interpreter. Nodes stay valid as long as
  Python refers to them, whatever happens to their tree. Operations that libxml2 cannot
  do safely raise `Error` instead:
  - changing a tree while an XPath evaluation runs on it in a way that frees nodes;
  - changing a document that a `Reader` is reading;
  - pushing to a parser during its own push.
- A detached node is the root of its own tree. `getOwner()` gives that root; Perl gives
  a hidden `DocumentFragment`.
- `copyCurrentNode` of a `Reader` returns a node with its own document
  (`ownerDocument` is `None`).
- A DTD from another document is moved by a copy.

Fixed Perl bugs:

- `setInternalSubset` accepts a DTD from another document.
- `SAX.Builder` keeps the DOCTYPE when it writes the document out, and namespaces of
  siblings stay apart.
- `setAttribute('xmlns', '')` declares `xmlns=""` on an element outside the default
  namespace of its parent, as a parse does. Perl ignores it, and the text then puts the
  element in the parent's namespace. A plain `appendChild` does not add `xmlns=""`:
  `<r xmlns="u"><a/></r>` stays as Perl writes it.

SAX:

- A handler method that is missing is skipped. Perl dies.
- Every parse with a handler ends with `end_document`, and returns its result.
- A handler that raises stops the parse, and the exception comes out of the parse.

Other:

- Predicates return `bool` (`isValid`, `matchesNode`, `hasAttributes`...). Perl returns
  1 or 0.
- `NodeList.sort` takes a comparison and returns a new list, as in Perl.
  `NodeList.append` takes a list.
- `registerFunction` takes a callable, not the name of a function.
- `replaceDataRegEx` takes a Python regular expression. `$1` and `\1` both work in the
  replacement.
- `getAttribute(name, 1)` and names given as encoded bytes are left out: a `str` has no
  encoding.
- `replaceChild(node, node)` raises when `node` is not a child, as for any other
  node. Perl returns `node`.
- `parse_fh` reads the whole file at once.
- `compact` for HTML is accepted and ignored.

## Left out

- **Perl ithreads:** `threads_shared_enabled`, `INIT_THREAD_SUPPORT`,
  `DISABLE_THREAD_SUPPORT`, `HAVE_THREAD_SUPPORT`, `:threads_shared`. Python threads
  need no setup: each thread can parse, and errors stay in their thread.
- **GDOME:** `gdome_dom`, `export_GDOME`, `import_GDOME`, `GDOME_*`.
- **Perl internals:** `InputCallback.init_callbacks` and `cleanup_callbacks`,
  `HashTable`, `ParserContext`, `_leaked_nodes`, `Devel`.
- `NodeList.iterator`, obsolete in Perl since 1.54. Use `iter()` or a `for` loop.
- `XML::SAX::ParserFactory`, which is not part of XML::LibXML.
- `RelaxNG.parse_location` and the other constructors under another name: use the
  keywords `location=`, `string=`, `DOM=`.

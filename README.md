# xml_libxml

libxml2 for Python, with the API of Perl XML::LibXML 2.0213: DOM, XPath, parser and push
parser, HTML, DTD, RelaxNG and XML Schema validation, XInclude, C14N, Reader, SAX, input
callbacks, structured errors.

Using the API from Python code cannot crash the interpreter: nodes stay valid as long as
Python refers to them, and what libxml2 cannot do safely raises `xml_libxml.Error`.

    import xml_libxml as X

    doc = X.Parser().parse_string('<r><a id="1">text</a></r>')
    for a in doc.findnodes('//a[@id]'):
        print(a.getAttribute('id'), a.textContent)
    print(doc.toString())

## Building

Requires libxml2 (2.15 is the reference; 2.9.7 works with known gaps) and Python 3.6 or
later with its headers.

    export PKG_CONFIG_PATH=/path/to/libxml2/lib/pkgconfig   # if not the system one
    python3 setup.py build_ext --inplace
    python3 -m pytest tests                                  # tests/run_valgrind.sh too

## Documentation

- `docs/migration.md`: how Perl XML::LibXML code is written in Python, and what differs.
- `docs/api.md`: the classes and methods, made by `docs/make_api.py`.
- The Perl documentation of XML::LibXML describes the methods themselves.

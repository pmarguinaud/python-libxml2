"""
XML::LibXML::Reader: a pull parser over xmlTextReader.

Reader(location=, string=, IO=, DOM= or FD=, URI=, encoding=, RelaxNG=, Schema=,
and parser options). Iterating over it reads node after node and gives the reader.

Nodes of the reader come as copies (copyCurrentNode). document() and preserveNode()
give the document the reader builds: until the reading ends, the reader keeps what
Python refers to, and the document cannot change. A reader that walks a document
(DOM=) stays on its last node at the end: the document can change once the reader is
closed or gone.
"""
import re

from xml_libxml._libxml import Reader as _Reader

XML_READER_TYPE_NONE = 0
XML_READER_TYPE_ELEMENT = 1
XML_READER_TYPE_ATTRIBUTE = 2
XML_READER_TYPE_TEXT = 3
XML_READER_TYPE_CDATA = 4
XML_READER_TYPE_ENTITY_REFERENCE = 5
XML_READER_TYPE_ENTITY = 6
XML_READER_TYPE_PROCESSING_INSTRUCTION = 7
XML_READER_TYPE_COMMENT = 8
XML_READER_TYPE_DOCUMENT = 9
XML_READER_TYPE_DOCUMENT_TYPE = 10
XML_READER_TYPE_DOCUMENT_FRAGMENT = 11
XML_READER_TYPE_NOTATION = 12
XML_READER_TYPE_WHITESPACE = 13
XML_READER_TYPE_SIGNIFICANT_WHITESPACE = 14
XML_READER_TYPE_END_ELEMENT = 15
XML_READER_TYPE_END_ENTITY = 16
XML_READER_TYPE_XML_DECLARATION = 17

XML_READER_NONE = -1
XML_READER_START = 0
XML_READER_ELEMENT = 1
XML_READER_END = 2
XML_READER_EMPTY = 3
XML_READER_BACKTRACK = 4
XML_READER_DONE = 5
XML_READER_ERROR = 6

_PROPS = {
    'load_ext_dtd': 1,          # load the external subset
    'complete_attributes': 2,   # default DTD attributes
    'validation': 3,            # validate with the DTD
    'expand_entities': 4,       # substitute entities
}


class Reader(_Reader):
    __doc__ = __doc__

    def getParserProp(self, name):
        prop = _PROPS.get(name)
        return None if prop is None else self._getParserProp(prop)

    def setParserProp(self, *args, **kwds):
        options = {}
        for arg in args:
            options.update(arg)
        options.update(kwds)
        for name, value in options.items():
            if name not in _PROPS:
                raise TypeError("'%s' is not a parser property of a Reader" % name)
            self._setParserProp(_PROPS[name], bool(value))

    def close(self):
        return self._close()

    def preservePattern(self, pattern, ns_map=None):
        if ns_map is None:
            return self._preservePattern(pattern)
        if not isinstance(ns_map, dict):
            raise TypeError('ns_map must be a dict of prefix: uri')
        namespaces = []
        for prefix, uri in ns_map.items():
            namespaces += [uri, prefix]
        return self._preservePattern(pattern, namespaces)

    def nodePath(self):
        # The positions in the part of the document the reader keeps mean little.
        path = self._nodePath()
        return None if path is None else re.sub(r'\[\d+\]', '', path)


for _name in list(globals()):
    if _name.startswith('XML_READER_'):
        setattr(Reader, _name, globals()[_name])
del _name

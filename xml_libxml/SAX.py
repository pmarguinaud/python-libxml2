"""SAX for xml_libxml.

The classes of XML::LibXML::SAX, XML::LibXML::SAX::Parser, XML::LibXML::SAX::Builder
and XML::LibXML::SAX::Generator, with what they use of the Perl XML::SAX
distribution: Base (XML::SAX::Base), ParseException (XML::SAX::Exception::Parse),
NamespaceSupport (XML::NamespaceSupport) and DocumentLocator.

Events are method calls with a dict, whose keys are those of Perl SAX: an element
is {'Name', 'LocalName', 'Prefix', 'NamespaceURI', 'Attributes'}, and Attributes maps
'{uri}localname' to {'Name', 'Value', 'NamespaceURI', 'Prefix', 'LocalName'}.

    SAX(Handler=h).parse_string(xml)    # libxml2 parses, h gets the events
    Parser(Handler=h).generate(doc)     # the events of a document
    Builder()                           # a handler that builds a document
"""
import collections.abc
import os
import warnings

import xml_libxml as _X

JOIN_CHARACTER_DATA = 'http://xmlns.perl.org/sax/join-character-data'
NAMESPACES_FEATURE = 'http://xml.org/sax/features/namespaces'
XMLNS_NS = 'http://www.w3.org/2000/xmlns/'
XML_NS = 'http://www.w3.org/XML/1998/namespace'


# ------------------------------------------------------------------ exceptions

class SAXException(Exception):
    """XML::SAX::Exception: Message, and whatever else is given."""

    def __init__(self, Message='', **fields):
        super().__init__(Message)
        self.Message = Message
        for key, value in fields.items():
            setattr(self, key, value)

    def __str__(self):
        line = getattr(self, 'LineNumber', None)
        if line is None:
            return str(self.Message)
        return '%s [Ln: %s, Col: %s]' % (self.Message, line, getattr(self, 'ColumnNumber', None))

    def throw(self):
        raise self


class ParseException(SAXException):
    """XML::SAX::Exception::Parse: Message, LineNumber, ColumnNumber, SystemId,
    PublicId. A handler's warning, error and fatal_error get one."""

    def __init__(self, Message='', LineNumber=None, ColumnNumber=None, SystemId=None,
                 PublicId=None, **fields):
        super().__init__(Message, LineNumber=LineNumber, ColumnNumber=ColumnNumber,
                         SystemId=SystemId, PublicId=PublicId, **fields)


class NotRecognized(SAXException):
    pass


class NotSupported(SAXException):
    pass


# ------------------------------------------------------------------ XML::SAX::Base

# The handlers an event goes to, in this order, as in XML::SAX::Base.
_EVENTS = {
    'set_document_locator': ('ContentHandler', 'DocumentHandler'),
    'start_document': ('ContentHandler', 'DocumentHandler'),
    'end_document': ('ContentHandler', 'DocumentHandler'),
    'start_element': ('ContentHandler', 'DocumentHandler'),
    'end_element': ('ContentHandler', 'DocumentHandler'),
    'characters': ('ContentHandler', 'DocumentHandler'),
    'ignorable_whitespace': ('ContentHandler', 'DocumentHandler'),
    'processing_instruction': ('ContentHandler', 'DocumentHandler'),
    'start_prefix_mapping': ('ContentHandler',),
    'end_prefix_mapping': ('ContentHandler',),
    'skipped_entity': ('ContentHandler',),
    'comment': ('DocumentHandler', 'LexicalHandler'),
    'start_cdata': ('DocumentHandler', 'LexicalHandler'),
    'end_cdata': ('DocumentHandler', 'LexicalHandler'),
    'start_dtd': ('LexicalHandler',),
    'end_dtd': ('LexicalHandler',),
    'start_entity': ('LexicalHandler',),
    'end_entity': ('LexicalHandler',),
    'xml_decl': ('DTDHandler',),
    'notation_decl': ('DTDHandler',),
    'unparsed_entity_decl': ('DTDHandler',),
    'attlist_decl': ('DTDHandler',),
    'doctype_decl': ('DTDHandler',),
    'entity_decl': ('DTDHandler',),
    'element_decl': ('DeclHandler',),
    'attribute_decl': ('DeclHandler',),
    'internal_entity_decl': ('DeclHandler',),
    'external_entity_decl': ('DeclHandler',),
    'entity_reference': ('DocumentHandler',),
    'resolve_entity': ('EntityResolver',),
    'warning': ('ErrorHandler',),
    'error': ('ErrorHandler',),
    'fatal_error': ('ErrorHandler',),
}

_HANDLER_KINDS = ('Handler', 'ContentHandler', 'DocumentHandler', 'DTDHandler',
                  'LexicalHandler', 'ErrorHandler', 'DeclHandler', 'EntityResolver')


def _forwarder(event, kinds):
    def method(self, *args):
        return self._forward(event, kinds, args)
    method.__name__ = event
    method.__doc__ = 'Give %s to the handler.' % event
    return method


class Base:
    """XML::SAX::Base: options become attributes, and each event goes to the
    first handler that has a method for it (ContentHandler, ..., then Handler).
    An event no handler takes is dropped. Drivers define _parse_string,
    _parse_systemid and _parse_bytestream."""

    def __init__(self, *args, **options):
        if len(args) == 1 and isinstance(args[0], dict):
            options = dict(args[0], **options)
        elif args:
            raise TypeError('Base takes options as keywords, or a dict')
        for kind in _HANDLER_KINDS:
            setattr(self, kind, None)
        self.Features = {NAMESPACES_FEATURE: 1}
        for key, value in options.items():
            setattr(self, key, value)

    def _forward(self, event, kinds, args):
        for kind in kinds + ('Handler',):
            handler = getattr(self, kind, None)
            if handler is None:
                continue
            method = getattr(handler, event, None)
            if method is not None:
                return method(*args)
        return None

    # handlers

    def set_handler(self, *args):
        """set_handler(handler) or set_handler(kind, handler), as in Perl."""
        if len(args) == 1:
            kind, handler = 'Handler', args[0]
        elif len(args) == 2:
            kind, handler = args
        else:
            raise TypeError('set_handler([kind,] handler)')
        setattr(self, kind or 'Handler', handler)
        return 1

    def get_handler(self, kind='Handler'):
        return getattr(self, kind or 'Handler', None)

    def set_content_handler(self, handler): return self.set_handler('ContentHandler', handler)
    def set_document_handler(self, handler): return self.set_handler('DocumentHandler', handler)
    def set_dtd_handler(self, handler): return self.set_handler('DTDHandler', handler)
    def set_lexical_handler(self, handler): return self.set_handler('LexicalHandler', handler)
    def set_decl_handler(self, handler): return self.set_handler('DeclHandler', handler)
    def set_error_handler(self, handler): return self.set_handler('ErrorHandler', handler)
    def set_entity_resolver(self, handler): return self.set_handler('EntityResolver', handler)
    def get_content_handler(self): return self.get_handler('ContentHandler')
    def get_document_handler(self): return self.get_handler('DocumentHandler')
    def get_dtd_handler(self): return self.get_handler('DTDHandler')
    def get_lexical_handler(self): return self.get_handler('LexicalHandler')
    def get_decl_handler(self): return self.get_handler('DeclHandler')
    def get_error_handler(self): return self.get_handler('ErrorHandler')
    def get_entity_resolver(self): return self.get_handler('EntityResolver')

    # features

    def get_feature(self, name):
        if name not in self.Features:
            raise NotRecognized('Unknown feature %s' % name)
        return self.Features[name]

    def set_feature(self, name, value):
        if name not in self.Features:
            raise NotRecognized('Unknown feature %s' % name)
        self.Features[name] = value
        return 1

    def get_features(self):
        return dict(self.Features)

    # parsing

    def parse(self, Source=None, **options):
        """parse(Source={'String': ...} or {'SystemId': ...} or {'ByteStream': fh})"""
        source = dict(Source or {}, **options.pop('Source', {}))
        if source.get('ByteStream') is not None and hasattr(self, '_parse_bytestream'):
            return self._parse_bytestream(source['ByteStream'])
        if source.get('String') is not None and hasattr(self, '_parse_string'):
            return self._parse_string(source['String'])
        if source.get('SystemId') is not None and hasattr(self, '_parse_systemid'):
            return self._parse_systemid(source['SystemId'])
        raise SAXException('No _parse_* routine defined on this driver for this source')

    def parse_string(self, string):
        return self.parse(Source={'String': string})

    def parse_uri(self, uri):
        return self.parse(Source={'SystemId': uri})

    def parse_file(self, file):
        """A path, or a file object."""
        if isinstance(file, (str, bytes, os.PathLike)):
            return self.parse_uri(file)
        return self.parse(Source={'ByteStream': file})


for _event, _kinds in _EVENTS.items():
    setattr(Base, _event, _forwarder(_event, _kinds))


# ------------------------------------------------------------------ XML::NamespaceSupport

class NamespaceSupport:
    """What XML::LibXML::SAX::Builder uses of XML::NamespaceSupport."""

    def __init__(self):
        # [default URI, {prefix: uri}, [prefixes declared here]]
        self._contexts = [[None, {'xml': XML_NS}, []]]

    def push_context(self):
        default, prefixes, _ = self._contexts[-1]
        self._contexts.append([default, dict(prefixes), []])

    def pop_context(self):
        if len(self._contexts) <= 1:
            raise SAXException('Trying to pop context without push context')
        self._contexts.pop()

    def declare_prefix(self, prefix, uri):
        if prefix is None:
            return 0
        if prefix == 'xml' and uri != XML_NS:
            raise SAXException('The xml prefix can only be bound to the %s namespace.' % XML_NS)
        if uri == XML_NS:
            if prefix != 'xml':
                raise SAXException('the %s namespace can only be bound to the xml prefix.' % XML_NS)
            return 1
        if prefix.lower().startswith('xml'):
            return 0
        context = self._contexts[-1]
        if prefix == '':
            context[0] = uri
        else:
            if uri == '':
                raise SAXException('Cannot declare prefix %s' % prefix)
            context[1][prefix] = uri
        context[2].append(prefix)
        return 1

    def undeclare_prefix(self, prefix):
        """Unlike Perl, this works for the default namespace too: it gets the
        value of the enclosing context back, and other elements no longer see it
        as declared."""
        if prefix is None:
            return
        context = self._contexts[-1]
        if prefix not in context[2]:
            if prefix == '' or prefix not in context[1]:
                return
            raise SAXException('prefix %s not declared in this context' % prefix)
        context[2] = [p for p in context[2] if p != prefix]
        outer = self._contexts[-2] if len(self._contexts) > 1 else [None, {}, []]
        if prefix == '':
            context[0] = outer[0]
        else:
            del context[1][prefix]

    def get_declared_prefixes(self):
        return list(self._contexts[-1][2])

    def get_uri(self, prefix):
        context = self._contexts[-1]
        if prefix == '':
            return context[0]
        return context[1].get(prefix)


# ------------------------------------------------------------------ locator

class DocumentLocator(collections.abc.Mapping):
    """XML::SAX::DocumentLocator: a read-only dict whose values are computed when
    read, from functions."""

    KEYS = ('PublicId', 'SystemId', 'LineNumber', 'ColumnNumber', 'Encoding', 'XMLVersion')

    def __init__(self, PublicId, SystemId, LineNumber, ColumnNumber, Encoding, XMLVersion):
        self._get = dict(zip(self.KEYS, (PublicId, SystemId, LineNumber, ColumnNumber,
                                         Encoding, XMLVersion)))

    def __getitem__(self, key):
        return self._get[key]()

    def __iter__(self):
        return iter(self.KEYS)

    def __len__(self):
        return len(self.KEYS)


# ------------------------------------------------------------------ XML::LibXML::SAX

class SAX(Base):
    """XML::LibXML::SAX: libxml2 parses, and the events go to the handlers.

    The libxml2 parser is LibParser if it is given, else one made with
    expand_entities=1, as in Perl. parse_string, parse_uri and parse_file return
    what end_document returns."""

    def set_feature(self, name, value):
        if name == JOIN_CHARACTER_DATA:
            self.JOIN_CHARACTERS = value
            return 1
        return super().set_feature(name, value)

    def get_feature(self, name):
        if name == JOIN_CHARACTER_DATA:
            return getattr(self, 'JOIN_CHARACTERS', 0)
        return super().get_feature(name)

    def _lib_parser(self):
        parser = getattr(self, 'LibParser', None)
        if parser is None:
            parser = self.LibParser = _X.Parser(expand_entities=1)
        return parser

    def _run(self, call):
        parser = self._lib_parser()
        parser.set_handler(self, join_characters=bool(getattr(self, 'JOIN_CHARACTERS', 0)))
        try:
            return call(parser)
        finally:
            parser.set_handler(None)

    def _parse_bytestream(self, fh):
        return self._run(lambda parser: parser.parse_fh(fh))

    def _parse_string(self, string):
        return self._run(lambda parser: parser.parse_string(string))

    def _parse_systemid(self, uri):
        return self._run(lambda parser: parser.parse_file(uri))

    def _parse_characterstream(self, stream):
        raise NotSupported('not implemented yet')

    def parse_chunk(self, chunk):
        """The events of a well-balanced chunk, with no start_document nor
        end_document: the caller sends those."""
        self._run(lambda parser: parser._parse_sax_chunk(chunk))


# ------------------------------------------------------------------ XML::LibXML::SAX::Parser

class Parser(Base):
    """XML::LibXML::SAX::Parser: the events of a document that xml_libxml builds.
    generate(doc) gives the events of a document already built."""

    def _parser(self):
        return _X.Parser()

    def _parse_bytestream(self, fh):
        return self.generate(self._parser().parse_fh(fh))

    def _parse_string(self, string):
        return self.generate(self._parser().parse_string(string))

    def _parse_systemid(self, uri):
        return self.generate(self._parser().parse_file(uri))

    def _parse_characterstream(self, stream):
        raise NotSupported('parsing a characterstream is not supported at this time')

    def generate(self, node):
        """Events for a document: returns what end_document returns. As in Perl,
        another node only gets set_document_locator."""
        doc = node.ownerDocument
        locator = {'PublicId': None, 'SystemId': None, 'Encoding': None, 'XMLVersion': None}
        dtd = doc.externalSubset if doc is not None else None
        if dtd is not None:
            locator["PublicId"] = dtd.publicId
            locator["SystemId"] = dtd.systemId
        if doc is not None:
            locator['Encoding'] = doc.encoding
            locator['XMLVersion'] = doc.version
        self.current_node = None

        def line():
            current = self.current_node
            return current.line_number() if current is not None else None

        self.set_document_locator(DocumentLocator(
            lambda: locator['PublicId'], lambda: locator['SystemId'], line, lambda: 1,
            lambda: locator['Encoding'], lambda: locator['XMLVersion']))

        if node.nodeType in (_X.XML_DOCUMENT_NODE, _X.XML_HTML_DOCUMENT_NODE):
            self.start_document({})
            self.xml_decl({'Version': node.getVersion(), 'Encoding': node.getEncoding()})
            self.process_node(node)
            return self.end_document({})
        return None

    def process_node(self, node):
        saved, self.current_node = getattr(self, 'current_node', None), node
        try:
            kind = node.nodeType
            if kind == _X.XML_COMMENT_NODE:
                self.comment({'Data': node.getData()})
            elif kind == _X.XML_TEXT_NODE:
                self.characters({'Data': node.nodeValue})
            elif kind == _X.XML_CDATA_SECTION_NODE:
                # Perl gives only characters: these are the events of a parse
                self.start_cdata()
                self.characters({'Data': node.nodeValue})
                self.end_cdata()
            elif kind == _X.XML_ELEMENT_NODE:
                self.process_element(node)
            elif kind in (_X.XML_ENTITY_REF_NODE, _X.XML_DOCUMENT_NODE,
                          _X.XML_HTML_DOCUMENT_NODE, _X.XML_DOCUMENT_FRAG_NODE):
                for child in node.childNodes():
                    self.process_node(child)
            elif kind == _X.XML_PI_NODE:
                self.processing_instruction({'Target': node.nodeName, 'Data': node.getData()})
            elif kind == _X.XML_DTD_NODE:
                # Perl gives nothing: these are the events of a parse
                dtd = {'Name': node.nodeName}
                if node.publicId:
                    dtd['PublicId'] = node.publicId
                if node.systemId:
                    dtd['SystemId'] = node.systemId
                self.start_dtd(dtd)
                self.end_dtd({})
            # XInclude markers and the rest give no event, as in Perl
        finally:
            self.current_node = saved

    def process_element(self, element):
        attributes = {}
        declared = element.getNamespaces()

        for ns in declared:
            self.start_prefix_mapping({'NamespaceURI': ns.href, 'Prefix': ns.localname or ''})

        for attr in element.attributes():
            if isinstance(attr, _X.Namespace):
                local = attr.localname
                if not local:
                    attributes['{}xmlns'] = {'Name': 'xmlns', 'LocalName': 'xmlns', 'Prefix': '',
                                             'Value': attr.href, 'NamespaceURI': ''}
                else:
                    attributes['{%s}%s' % (XMLNS_NS, local)] = {
                        'Name': 'xmlns:' + local, 'Value': attr.href, 'NamespaceURI': XMLNS_NS,
                        'Prefix': 'xmlns', 'LocalName': local}
            else:
                uri = attr.namespaceURI or ''
                prefix = attr.prefix or ''
                name = attr.name or ''
                if ':' not in name and prefix:
                    name = '%s:%s' % (prefix, name)
                attributes['{%s}%s' % (uri, attr.localname)] = {
                    'Name': name, 'Value': attr.value, 'NamespaceURI': uri, 'Prefix': prefix,
                    'LocalName': attr.localname}

        node = {'Name': element.nodeName, 'Attributes': attributes,
                'NamespaceURI': element.namespaceURI or '', 'Prefix': element.prefix or '',
                'LocalName': element.localname}
        self.start_element(node)
        for child in element.childNodes():
            self.process_node(child)
        end = dict(node)
        del end['Attributes']
        self.end_element(end)

        for ns in declared:
            self.end_prefix_mapping({'NamespaceURI': ns.href, 'Prefix': ns.localname or ''})


# ------------------------------------------------------------------ XML::LibXML::SAX::Builder

class Builder:
    """XML::LibXML::SAX::Builder: a handler that builds a document, or a
    DocumentFragment when the events start without start_document. Options:
    Encoding and Version, for the XML declaration."""

    def __init__(self, **options):
        self.Encoding = None
        self.Version = None
        for key, value in options.items():
            setattr(self, key, value)
        self._dom = None
        self._parent = None
        self._ns = None
        self._last = None
        self._in_cdata = 0
        self._use_ns_stack = 0

    def result(self):
        return self._last

    def done(self):
        dom = self._dom if self._dom is not None else self._parent
        self._ns = None
        self._parent = None
        self._dom = None
        self._last = dom
        return dom

    def _fragment(self):
        """Events without a document build a fragment."""
        if self._dom is None and self._parent is None:
            self._parent = _X.DocumentFragment()
            self._ns = NamespaceSupport()
            self._ns.push_context()

    def set_document_locator(self, locator=None):
        pass

    def start_dtd(self, dtd=None):
        """The DOCTYPE, with its identifiers. Perl makes an external subset, which
        the document does not write out: here it is the internal one, as when
        libxml2 parses."""
        dtd = dtd or {}
        if (dtd.get('Name') is not None and self._dom is not None
                and self._dom.internalSubset is None
                and (dtd.get('SystemId') is not None or dtd.get('PublicId') is not None)):
            self._dom.createInternalSubset(dtd['Name'], dtd.get('PublicId'), dtd.get('SystemId'))

    def end_dtd(self, dtd=None):
        pass

    def start_document(self, doc=None):
        self._dom = _X.Document.createDocument()
        if self.Encoding is not None:
            self.xml_decl({'Version': self.Version or '1.0', 'Encoding': self.Encoding})
        self._ns = NamespaceSupport()
        self._ns.push_context()
        self._parent = None

    def xml_decl(self, decl=None):
        decl = decl or {}
        if self._dom is None:
            return
        if decl.get('Version') is not None:
            self._dom.setVersion(decl['Version'])
        if decl.get('Encoding') is not None:
            self._dom.setEncoding(decl['Encoding'])

    def end_document(self, doc=None):
        return self.done()

    def start_prefix_mapping(self, ns):
        self._fragment()
        self._use_ns_stack = 1
        self._ns.declare_prefix(ns.get('Prefix'), ns.get('NamespaceURI'))

    def end_prefix_mapping(self, ns):
        if self._ns is not None:
            self._ns.undeclare_prefix(ns.get('Prefix'))

    def start_element(self, el):
        self._fragment()
        uri = el.get('NamespaceURI') or ''
        name = el['Name']

        if self._parent is not None:
            node = self._parent.addNewChild(uri, name)
        else:
            if uri:
                node = self._dom.createRawElementNS(uri, name)
            else:
                node = self._dom.createRawElement(name)
            self._dom.setDocumentElement(node)

        # the namespaces declared for this element
        skip_ns = 0
        for prefix in self._ns.get_declared_prefixes():
            skip_ns = 1
            declared = self._ns.get_uri(prefix)
            if declared is not None and declared == uri:
                continue
            node.setNamespace(declared, prefix or None, 0)

        self._parent = node
        self._ns.push_context()

        for key, attr in (el.get('Attributes') or {}).items():
            if isinstance(attr, dict):
                attr_name = attr.get('Name')
                if not attr_name:
                    continue
                attr_prefix = attr.get('Prefix')
                if self._use_ns_stack and (attr_name == 'xmlns' or attr_prefix == 'xmlns'):
                    continue
                if attr_prefix == 'xmlns' and not skip_ns:
                    value = attr.get('Value')
                    node.setNamespace(value, attr.get('LocalName'), 1 if value == uri else 0)
                else:
                    node.setAttributeNS(attr.get('NamespaceURI') or '', attr_name,
                                        attr.get('Value'))
            else:
                node.setAttribute(key, attr)

    def end_element(self, el=None):
        if self._parent is None:
            return
        self._ns.pop_context()
        self._parent = self._parent.parentNode

    def start_cdata(self, *args):
        self._in_cdata = 1

    def end_cdata(self, *args):
        self._in_cdata = 0

    def characters(self, chars=None):
        self._fragment()
        if self._parent is None or not chars or chars.get('Data') is None:
            return
        data = chars['Data']
        if self._dom is not None:
            if self._in_cdata:
                node = self._dom.createCDATASection(data)
            else:
                self._parent.appendText(data)
                return
        elif self._in_cdata:
            node = _X.CDATASection(data)
        else:
            node = _X.Text(data)
        self._parent.addChild(node)

    def comment(self, chars=None):
        self._fragment()
        if not chars or chars.get('Data') is None:
            return
        if self._dom is not None:
            node = self._dom.createComment(chars['Data'])
        else:
            node = _X.Comment(chars['Data'])
        (self._parent if self._parent is not None else self._dom).addChild(node)

    def processing_instruction(self, pi=None):
        if self._dom is None or not pi:
            return
        node = self._dom.createPI(pi.get('Target'), pi.get('Data'))
        (self._parent if self._parent is not None else self._dom).addChild(node)

    def warning(self, error):
        pass

    def error(self, error):
        self._ns = None
        self._parent = None
        self._dom = None
        raise error

    def fatal_error(self, error):
        self.error(error)


# ------------------------------------------------------------------ XML::LibXML::SAX::Generator

class AttributeNode(dict):
    """XML::LibXML::SAX::AttributeNode: an attribute that reads as its value."""

    def __str__(self):
        return str(self.get('Value'))


class Generator:
    """XML::LibXML::SAX::Generator, deprecated as in Perl: use Parser."""

    def __init__(self, *args, **options):
        warnings.warn('xml_libxml.SAX.Generator is deprecated, use xml_libxml.SAX.Parser',
                      DeprecationWarning, stacklevel=2)
        if len(args) == 1:
            options.setdefault('Handler', args[0])
        elif args:
            raise TypeError('Generator(handler) or Generator(Handler=handler)')
        self.Handler = options.pop('Handler', None)
        for key, value in options.items():
            setattr(self, key, value)

    def generate(self, node):
        document = {'Parent': None}
        _call(self.Handler, 'start_document', document)
        _generator_node(self.Handler, node)
        return _call(self.Handler, 'end_document', document)


def _call(handler, event, *args):
    method = getattr(handler, event, None)
    return method(*args) if method is not None else None


def _generator_node(handler, node):
    kind = node.nodeType
    if kind == _X.XML_COMMENT_NODE:
        _call(handler, 'comment', {'Data': node.getData()})
    elif kind in (_X.XML_TEXT_NODE, _X.XML_CDATA_SECTION_NODE):
        _call(handler, 'characters', {'Data': node.getData()})
    elif kind == _X.XML_ELEMENT_NODE:
        _generator_element(handler, node)
    elif kind == _X.XML_ENTITY_REF_NODE:
        for child in node.childNodes():
            _generator_node(handler, child)
    elif kind == _X.XML_DOCUMENT_NODE:
        for child in node.childNodes():
            if child.nodeType == _X.XML_ELEMENT_NODE:
                _generator_element(handler, child)
                break
    else:
        warnings.warn('unknown node type: %d' % kind)


def _generator_element(handler, element):
    attributes = {}
    for attr in element.attributes():
        attributes[attr.nodeName] = AttributeNode(
            Name=attr.nodeName, Value=attr.getData(), NamespaceURI=attr.namespaceURI,
            Prefix=attr.prefix, LocalName=attr.localname)
    node = {'Name': element.nodeName, 'Attributes': attributes,
            'NamespaceURI': element.namespaceURI, 'Prefix': element.prefix,
            'LocalName': element.localname}
    _call(handler, 'start_element', node)
    for child in element.childNodes():
        _generator_node(handler, child)
    _call(handler, 'end_element', node)


__all__ = ['SAX', 'Parser', 'Builder', 'Generator', 'Base', 'NamespaceSupport',
           'DocumentLocator', 'SAXException', 'ParseException', 'NotRecognized',
           'NotSupported', 'AttributeNode', 'JOIN_CHARACTER_DATA']

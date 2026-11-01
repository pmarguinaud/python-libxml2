"""
The exception raised by xml_libxml, following XML::LibXML::Error.

An Error has the fields of a libxml2 error. A parse usually reports several, so
they are chained: `_prev` is the error before this one, and str() shows them all.
"""
import warnings

#: 0: warnings of libxml2 are dropped, 1: passed to warnings.warn, 2: kept as errors
WARNINGS = 0

XML_ERR_NONE = 0
XML_ERR_WARNING = 1
XML_ERR_ERROR = 2
XML_ERR_FATAL = 3

XML_ERR_FROM_NONE = 0
XML_ERR_FROM_PARSER = 1
XML_ERR_FROM_TREE = 2
XML_ERR_FROM_NAMESPACE = 3
XML_ERR_FROM_DTD = 4
XML_ERR_FROM_HTML = 5
XML_ERR_FROM_MEMORY = 6
XML_ERR_FROM_OUTPUT = 7
XML_ERR_FROM_IO = 8
XML_ERR_FROM_FTP = 9
XML_ERR_FROM_HTTP = 10
XML_ERR_FROM_XINCLUDE = 11
XML_ERR_FROM_XPATH = 12
XML_ERR_FROM_XPOINTER = 13
XML_ERR_FROM_REGEXP = 14
XML_ERR_FROM_DATATYPE = 15
XML_ERR_FROM_SCHEMASP = 16
XML_ERR_FROM_SCHEMASV = 17
XML_ERR_FROM_RELAXNGP = 18
XML_ERR_FROM_RELAXNGV = 19
XML_ERR_FROM_CATALOG = 20
XML_ERR_FROM_C14N = 21
XML_ERR_FROM_XSLT = 22
XML_ERR_FROM_VALID = 23
XML_ERR_FROM_CHECK = 24
XML_ERR_FROM_WRITER = 25
XML_ERR_FROM_MODULE = 26
XML_ERR_FROM_I18N = 27
XML_ERR_FROM_SCHEMATRONV = 28

ERROR_DOMAINS = ("", "parser", "tree", "namespace", "validity",
                 "HTML parser", "memory", "output", "I/O", "ftp",
                 "http", "XInclude", "XPath", "xpointer", "regexp",
                 "Schemas datatype", "Schemas parser", "Schemas validity",
                 "Relax-NG parser", "Relax-NG validity",
                 "Catalog", "C14N", "XSLT", "validity", "error-checking",
                 "xmlwriter", "dynamic loading", "i18n",
                 "Schematron validity")

MAX_ERROR_PREV_DEPTH = 100


class Error(Exception):
    """An error reported by libxml2, or by these bindings."""

    def __init__(self, error=None):
        record = error if isinstance(error, dict) else {'message': error}
        super().__init__(record.get('message'))
        self._domain = record.get('domain', 0)
        self.level = record.get('level', XML_ERR_ERROR)
        self.code = record.get('code', -1)
        self.message = record.get('message')
        self.file = record.get('file')
        self.line = record.get('line')
        self.column = record.get('column')
        self.context = record.get('context')
        self.str1 = record.get('str1')
        self.str2 = record.get('str2')
        self.str3 = record.get('str3')
        self.num1 = record.get('num1')
        self.num2 = record.get('num2')
        self.nodename = None
        self._prev = None
        self._prev_depth = 0

    @property
    def int1(self):
        return self.num1

    @property
    def int2(self):
        return self.num2

    @property
    def domain(self):
        """The name of the libxml2 module that reported the error."""
        if self._domain < len(ERROR_DOMAINS):
            return ERROR_DOMAINS[self._domain]
        return 'domain_%d' % self._domain

    def as_string(self):
        """The whole chain of errors, as Perl prints it."""
        msg = self._prev.as_string() if self._prev is not None else ''

        if self.level == XML_ERR_NONE:
            level = ''
        elif self.level == XML_ERR_WARNING:
            level = 'warning'
        else:
            level = 'error'

        where = ''
        if self.file is not None:
            where = '%s:%s' % (self.file, self.line)
        elif self._domain == XML_ERR_FROM_PARSER and self.line:
            where = 'Entity: line %s' % self.line
        if self.nodename:
            where += ': element ' + self.nodename

        if where:
            msg += where + ': '
        msg += '%s %s :' % (self.domain, level)
        msg += ' %s\n' % (self.message or '').rstrip('\n')

        if self._domain == XML_ERR_FROM_XPATH and self.str1 is not None:
            msg += self.str1 + '\n'
            msg += ' ' * (self.num1 or 0) + '^\n'
        elif self.context is not None:
            msg += self.context + '\n'
            before = self.context[:self.column or 0]
            msg += ''.join('\t' if c == '\t' else ' ' for c in before) + '^\n'
        return msg

    def __str__(self):
        return self.as_string()

    def dump(self):
        return repr({k: v for k, v in vars(self).items() if k != '_prev'})


def _chain(records):
    """Turn the records of one operation into a chain, or None if nothing counts."""
    last = None
    for record in records:
        error = Error(record)
        if error.level == XML_ERR_WARNING and WARNINGS != 2:
            if WARNINGS:
                warnings.warn(str(error))
            continue
        if last is not None:
            if last._prev_depth >= MAX_ERROR_PREV_DEPTH:
                continue
            error._prev = last
            error._prev_depth = last._prev_depth + 1
        last = error
    return last

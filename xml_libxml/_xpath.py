"""
XPath results, following XML::LibXML::NodeList, Literal, Number and Boolean.

The three value classes derive from str, float and int, so a result can be used
as the plain value it is.
"""
import functools
import math


class Literal(str):
    """An XPath string."""

    def value(self):
        return str(self)

    def string_value(self):
        return str(self)

    def as_string(self):
        return "'%s'" % str(self).replace("'", '&apos;')

    def as_xml(self):
        return '<Literal>%s</Literal>\n' % str(self)

    def evaluate(self):
        return self

    def to_literal(self):
        return self

    def to_number(self):
        return Number(str(self))

    def to_boolean(self):
        return Boolean(len(self) > 0)


class Number(float):
    """An XPath number. NaN if built from something that is not a number."""

    def __new__(cls, value=0):
        try:
            return float.__new__(cls, value)
        except (TypeError, ValueError):
            return float.__new__(cls, 'nan')

    def __str__(self):
        return self.as_string()

    def value(self):
        return float(self)

    def as_string(self):
        if math.isnan(self):
            return 'NaN'
        if math.isinf(self):
            return 'Infinity' if self > 0 else '-Infinity'
        if float(self).is_integer():
            return str(int(self))
        return repr(float(self))

    def string_value(self):
        return self.as_string()

    def as_xml(self):
        return '<Number>%s</Number>\n' % self.as_string()

    def evaluate(self):
        return self

    def to_number(self):
        return self

    def to_literal(self):
        return Literal(self.as_string())

    def to_boolean(self):
        return Boolean(bool(self) and not math.isnan(self))


class Boolean(int):
    """An XPath boolean."""

    def __new__(cls, value=False):
        return int.__new__(cls, 1 if value else 0)

    @classmethod
    def true(cls):
        return cls(True)

    @classmethod
    def false(cls):
        return cls(False)

    def value(self):
        return int(self)

    def to_number(self):
        return Number(int(self))

    def to_boolean(self):
        return self

    def to_literal(self):
        return Literal('true' if self else 'false')

    def string_value(self):
        return str(self.to_literal())


def _text(node):
    return node.string_value() if hasattr(node, 'string_value') else node.textContent


class NodeList(list):
    """
    A list of nodes, or of anything else, with the methods of XML::LibXML::NodeList.

    Indexes start at 0, also in get_node. Where Perl and list disagree, Perl
    wins: append takes a list, reverse and sort return a new list, and pop on an
    empty list gives None.
    """

    def __getitem__(self, index):
        result = super().__getitem__(index)
        return type(self)(result) if isinstance(index, slice) else result

    def __str__(self):
        return str(self.to_literal())

    def __lt__(self, other):
        return str(self) < str(other)

    def __le__(self, other):
        return str(self) <= str(other)

    def __gt__(self, other):
        return str(self) > str(other)

    def __ge__(self, other):
        return str(self) >= str(other)

    def size(self):
        return len(self)

    def get_node(self, pos):
        return list.__getitem__(self, pos)

    def item(self, pos):
        return list.__getitem__(self, pos) if -len(self) <= pos < len(self) else None

    def get_nodelist(self):
        return list(self)

    def pop(self):
        return list.pop(self) if self else None

    def shift(self):
        return list.pop(self, 0) if self else None

    def push(self, *items):
        self.extend(items)

    def unshift(self, *items):
        self[0:0] = items

    def append(self, nodelist):
        self.extend(nodelist)

    def prepend(self, nodelist):
        self[0:0] = list(nodelist)

    def to_boolean(self):
        return Boolean(len(self) > 0)

    def string_value(self):
        return _text(list.__getitem__(self, 0)) if self else ''

    def to_literal(self):
        return Literal(''.join(_text(node) for node in self))

    def to_literal_delimited(self, separator):
        return Literal(separator.join(_text(node) for node in self))

    def to_literal_list(self):
        return [_text(node) for node in self]

    def to_number(self):
        return Number(str(self.to_literal()))

    def map(self, func):
        """Apply func. It can return a list or tuple, which is flattened."""
        results = type(self)()
        for item in self:
            value = func(item)
            if isinstance(value, (list, tuple)):
                results.extend(value)
            else:
                results.extend([value])
        return results

    def grep(self, func):
        return type(self)(item for item in self if func(item))

    def sort(self, func):
        """Sort with a comparison function, into a new list."""
        return type(self)(sorted(self, key=functools.cmp_to_key(func)))

    def foreach(self, func):
        for item in self:
            func(item)
        return self

    def reverse(self):
        return type(self)(reversed(list(self)))

    def reduce(self, func, *initial):
        return functools.reduce(func, list(self), *initial)

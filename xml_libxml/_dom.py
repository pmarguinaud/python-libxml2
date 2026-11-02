"""
What XML::LibXML writes in Perl: NamedNodeMap, AttributeHash and replaceDataRegEx.
"""
import collections.abc
import re

XML_NAMESPACE_DECL = 18


class NamedNodeMap(list):
    """
    The attributes of a node. As in Perl it is a snapshot of them at creation,
    and it follows what is done through it, not other changes to the element.
    """

    def __init__(self, nodes=()):
        super().__init__(nodes)
        self._map = {node.nodeName: node for node in self}

    def length(self):
        return len(self)

    def nodes(self):
        return list(self)

    def item(self, index):
        return self[index] if -len(self) <= index < len(self) else None

    def getNamedItem(self, name):
        return self._map.get(name)

    def setNamedItem(self, node):
        if node is None or not self:
            return None
        name = node.nodeName
        if node.nodeType == XML_NAMESPACE_DECL:
            return None

        replaced = None
        if name in self._map:
            if node.isSameNode(self._map[name]):
                return None
            replaced = self._map[name].replaceNode(node)
        else:
            self[0].addSibling(node)

        self._map[name] = node
        list.append(self, node)
        return replaced

    def removeNamedItem(self, name):
        node = self._map.pop(name, None)
        if node is not None:
            node.unbindNode()
            self[:] = [n for n in self if not node.isSameNode(n)]
        return node

    def getNamedItemNS(self, uri, name):
        return None

    def setNamedItemNS(self, uri, node):
        return None

    def removeNamedItemNS(self, uri, name):
        return None


class AttributeHash(collections.abc.MutableMapping):
    """
    The attributes of an element as a dict, in Clark notation for namespaces:
    {uri}local. What Perl gives for %$element.
    """

    def __init__(self, element):
        self.element = element

    @staticmethod
    def from_clark(key):
        match = re.search(r'\{(.+)\}(.+)', key)
        return match.groups() if match else (None, key)

    @staticmethod
    def to_clark(uri, local):
        return local if uri is None else '{%s}%s' % (uri, local)

    def __getitem__(self, key):
        uri, local = self.from_clark(key)
        if key not in self:
            raise KeyError(key)
        if uri is not None:
            return self.element.getAttributeNS(uri, local)
        return self.element.getAttribute(local)

    def __setitem__(self, key, value):
        uri, local = self.from_clark(key)
        if uri is not None:
            self.element.setAttributeNS(uri, 'xxx:' + local, str(value))
        else:
            self.element.setAttribute(local, str(value))

    def __delitem__(self, key):
        if key not in self:
            raise KeyError(key)
        uri, local = self.from_clark(key)
        if uri is not None:
            self.element.removeAttributeNS(uri, local)
        else:
            self.element.removeAttribute(local)

    def __contains__(self, key):
        uri, local = self.from_clark(key)
        if uri is not None:
            return bool(self.element.hasAttributeNS(uri, local))
        return bool(self.element.hasAttribute(local))

    def __iter__(self):
        return iter(sorted(self.to_clark(a.namespaceURI, a.localname)
                           for a in self.element.attributes()
                           if a.nodeType != XML_NAMESPACE_DECL))

    def __len__(self):
        return sum(1 for _ in self)

    def all_keys(self):
        """The keys, sorted, as in Perl."""
        return list(self)


def replace_data_regex(node, left, right='', flags=None):
    """replaceDataRegEx: a Python regular expression, with $1 or \\1 in right."""
    if left is None:
        return
    flags = flags or ''
    options = 0
    for letter, option in (('i', re.I), ('m', re.M), ('s', re.S), ('x', re.X)):
        if letter in flags:
            options |= option
    replacement = re.sub(r'\$(\d+)', r'\\\1', right or '')
    node.setData(re.sub(left, replacement, node.nodeValue,
                        count=0 if 'g' in flags else 1, flags=options))

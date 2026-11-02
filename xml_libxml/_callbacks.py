"""
XML::LibXML::InputCallback, and the global callbacks.

Perl has them as $XML::LibXML::match_cb and so on, or class methods of XML::LibXML.
Here they are functions of the module. The methods of the same names on a Parser
are for that parser.
"""
from xml_libxml._libxml import _set_global_callbacks


class InputCallback:
    """
    A stack of callback sets, each (match, open, read, close), for
    Parser.input_callbacks. The latest registered is tried first.

    match(uri) tells if the set handles this URI. open(uri) returns a handle, or None
    or 0 to leave it to what comes next. read(handle, length) returns str or bytes,
    empty or None at the end. close(handle) ends it.
    """

    def __init__(self):
        self._callbacks = []

    def register_callbacks(self, callbacks):
        if not isinstance(callbacks, (list, tuple)) or len(callbacks) != 4:
            raise TypeError('expected four callbacks: match, open, read, close')
        self._callbacks.insert(0, tuple(callbacks))

    def unregister_callbacks(self, callbacks=None):
        """Remove the sets with this match function, or else the latest one."""
        if callbacks is None:
            if self._callbacks:
                del self._callbacks[0]
            return
        if not isinstance(callbacks, (list, tuple)) or len(callbacks) != 4:
            raise TypeError('expected four callbacks: match, open, read, close')
        self._callbacks = [c for c in self._callbacks if c[0] is not callbacks[0]]


_global = [None, None, None, None]


def _update():
    _set_global_callbacks(tuple(_global) if all(_global) else None)


def _accessor(index, name):
    def accessor(*args):
        if len(args) > 1:
            raise TypeError('%s takes one argument at most' % name)
        if args:
            _global[index] = args[0]
            _update()
        return _global[index]
    accessor.__name__ = name
    accessor.__doc__ = 'Get or set the global %s.' % name
    return accessor


match_callback = _accessor(0, 'match_callback')
open_callback = _accessor(1, 'open_callback')
read_callback = _accessor(2, 'read_callback')
close_callback = _accessor(3, 'close_callback')


def callbacks(*args):
    """Get the four global callbacks, or set them. They apply to all parsers."""
    if args:
        if len(args) != 4:
            raise TypeError('expected four callbacks: match, open, read, close')
        _global[:] = args
        _update()
    return tuple(_global)

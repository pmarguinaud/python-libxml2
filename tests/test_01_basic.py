"""
test_01_basic.py — port of t/01basic.t

3 tests: load, instantiate, version.
"""
import pytest
from xml_libxml import Parser, LIBXML_VERSION, LIBXML_DOTTED_VERSION


def test_loaded():
    """Module loaded fine."""
    assert Parser is not None


def test_instantiate():
    """Can initialize a new Parser instance."""
    p = Parser()
    assert p is not None


def test_version():
    """LIBXML_VERSION is an integer > 0."""
    assert isinstance(LIBXML_VERSION, int)
    assert LIBXML_VERSION > 0
    assert isinstance(LIBXML_DOTTED_VERSION, str)
    # e.g. "2.9.14"
    parts = LIBXML_DOTTED_VERSION.split('.')
    assert len(parts) >= 2

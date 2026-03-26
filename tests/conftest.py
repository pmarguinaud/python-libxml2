"""
Shared fixtures and helpers for xml_libxml tests.
"""
import os
import pytest
import sys

# Ensure the package is importable from the project root
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from xml_libxml import Parser

EXAMPLE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'example')


def example_path(filename):
    return os.path.join(EXAMPLE_DIR, filename)


@pytest.fixture
def parser():
    return Parser()


@pytest.fixture
def example_dir():
    return EXAMPLE_DIR

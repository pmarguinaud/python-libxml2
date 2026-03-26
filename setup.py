from setuptools import setup, Extension
import subprocess

def pkg_config(lib, flag):
    try:
        return subprocess.check_output(
            ['pkg-config', flag, lib], stderr=subprocess.DEVNULL
        ).decode().split()
    except Exception:
        return []

cflags = pkg_config('libxml-2.0', '--cflags')
libs   = pkg_config('libxml-2.0', '--libs')

# Parse include dirs and lib dirs/names from pkg-config output
include_dirs = [f[2:] for f in cflags if f.startswith('-I')]
library_dirs = [f[2:] for f in libs   if f.startswith('-L')]
libraries    = [f[2:] for f in libs   if f.startswith('-l')]

# Fallback defaults
if not include_dirs:
    include_dirs = ['/usr/include/libxml2']
if not libraries:
    libraries = ['xml2']

libxml_ext = Extension(
    'xml_libxml._libxml',
    sources=[
        'src/libxml_module.c',
        'src/libxml_utils.c',
        'src/libxml_node.c',
        'src/libxml_document.c',
        'src/libxml_element.c',
        'src/libxml_text.c',
        'src/libxml_comment.c',
        'src/libxml_cdata.c',
        'src/libxml_attr.c',
        'src/libxml_nodelist.c',
        'src/libxml_xpath.c',
        'src/libxml_parser.c',
    ],
    include_dirs=include_dirs + ['src'],
    library_dirs=library_dirs,
    libraries=libraries,
    extra_compile_args=['-Wall', '-Wno-unused-function'],
)

setup(
    name='xml_libxml',
    version='0.1.0',
    packages=['xml_libxml'],
    ext_modules=[libxml_ext],
)

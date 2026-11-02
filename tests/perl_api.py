"""
List the public API of Perl XML::LibXML, from its sources.

    python3 tests/perl_api.py XML-LibXML-2.0213 > tests/data/perl_api.txt

Each output line is `Package name`. The names come from the XS functions and their
aliases, the subs and glob aliases of the .pm files, the =item entries of the pods and
the export lists. Names that start with `_`, Perl hooks (DESTROY, CLONE, ...) and pod
items that are not names are dropped. tests/test_parity.py checks the list against
the Python package.
"""
import os
import re
import sys

PERL_HOOKS = {'DESTROY', 'CLONE', 'CLONE_SKIP', 'AUTOLOAD', 'BEGIN', 'END', 'import',
              'VERSION', 'TIEHASH', 'FETCH', 'STORE', 'EXISTS', 'DELETE', 'CLEAR',
              'FIRSTKEY', 'NEXTKEY', 'SCALAR', 'new_from_ref'}
NAME = re.compile(r'^[A-Za-z]\w*$')


def keep(name):
    return NAME.match(name) and name not in PERL_HOOKS


def from_xs(path, out):
    package = None
    previous = ''
    in_alias = False
    with open(path, encoding='latin-1') as f:
        for line in f:
            m = re.match(r'^MODULE\s*=\s*\S+\s+PACKAGE\s*=\s*(\S+)', line)
            if m:
                package = m.group(1)
                in_alias = False
                continue
            if package is None:
                continue
            if re.match(r'^\s+ALIAS:', line):
                in_alias = True
                continue
            if in_alias:
                m = re.match(r'^\s+((?:\w+::)*\w+)\s*=\s*\d+', line)
                if m:
                    full = m.group(1)
                    if '::' in full:
                        pkg, _, name = full.rpartition('::')
                    else:
                        pkg, name = package, full
                    out.add((pkg, name))
                    continue
                in_alias = False
            m = re.match(r'^(\w+)\s*\(', line)
            if m and previous.strip() and not previous.startswith((' ', '\t', '#')):
                out.add((package, m.group(1)))
            previous = line


def from_pm(path, out):
    package = None
    in_constants = False
    with open(path, encoding='latin-1') as f:
        for line in f:
            if line.startswith('__END__'):
                break
            if in_constants:
                m = re.match(r'^\s*(\w+)\s*=>', line)
                if m:
                    out.add((package, m.group(1)))
                in_constants = not re.match(r'^\s*\}', line)
                continue
            if re.match(r'^use\s+constant\s*\{', line):
                in_constants = True
                continue
            m = re.match(r'^package\s+([\w:]+)\s*;', line)
            if m:
                package = m.group(1)
                continue
            if package is None:
                continue
            m = re.match(r'^use\s+constant\s+(\w+)\s*=>', line)
            if m:
                out.add((package, m.group(1)))
                continue
            m = re.match(r'^\s*sub\s+(\w+)', line)
            if m:
                out.add((package, m.group(1)))
                continue
            m = re.match(r'^\s*\*(\w+)\s*=\s*\\?&', line)
            if m:
                out.add((package, m.group(1)))


def from_pod(path, package, out):
    with open(path, encoding='latin-1') as f:
        for line in f:
            m = re.match(r'^=item\s+(?:\$\w+\s*=\s*)?(?:\$\w+->|[\w:]+->)?(\w+)(?![<:\w])', line)
            if m:
                out.add((package, m.group(1)))


def from_exports(path, package, out):
    text = open(path, encoding='latin-1').read()
    m = re.search(r'all\s*=>\s*\[qw\((.*?)\)\]', text, re.S)
    for name in m.group(1).split():
        out.add((package, name))


def main(root):
    found = set()
    from_xs(os.path.join(root, 'LibXML.xs'), found)
    from_pm(os.path.join(root, 'LibXML.pm'), found)
    from_exports(os.path.join(root, 'LibXML.pm'), 'XML::LibXML', found)
    lib = os.path.join(root, 'lib', 'XML', 'LibXML')
    from_exports(os.path.join(lib, 'Common.pm'), 'XML::LibXML::Common', found)
    for dirpath, _, files in os.walk(lib):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            if name.endswith('.pm'):
                from_pm(path, found)
            elif name.endswith('.pod'):
                rel = os.path.relpath(path[:-4], lib).replace(os.sep, '::')
                from_pod(path, 'XML::LibXML::' + rel, found)
    from_pod(os.path.join(root, 'LibXML.pod'), 'XML::LibXML', found)
    for package, name in sorted(found):
        if keep(name) and '::_' not in package:
            print(package, name)


if __name__ == '__main__':
    main(sys.argv[1])

#!/usr/bin/env python3

import re
import sys
import argparse
import subprocess
from pathlib import Path

utilsdir = Path(__file__).parent / 'gst-dll-tools'
if not list(utilsdir.glob('*')):
    subprocess.run(['git', 'submodule', 'update', '--init'], check=True)

sys.path.append(str(utilsdir))

try:
    import pefile
except ImportError:
    print('You need to install pefile. Try pip3 install pefile')
    exit(1)

from utils import dlldeps
from utils import gstprefix
from utils import vs

EXAMPLE_VCXPROJ = 'gst-uwp-example.vcxproj'
PLUGINS_LIST_FILE = 'GstWrapper.cpp'

parser = argparse.ArgumentParser(description=
        f'Rewrite {EXAMPLE_VCXPROJ} and {EXAMPLE_VCXPROJ}.filters with the '
        f'latest assets as loaded in {PLUGINS_LIST_FILE}. Call once for each '
        'prefix (arch, buildtype) you want to add plugins from.')
parser.add_argument('prefix', type=str,
                    help='directory to find all gstreamer plugins in')

args = parser.parse_args()

def get_plugin_names():
    regex = re.compile(r'.*"gst(.*).dll".*')
    with open(PLUGINS_LIST_FILE, 'r') as f:
        for line in f:
            m = regex.match(line)
            if m:
                yield m.groups()[0]

prefix = Path(args.prefix).resolve()

contents = list(prefix.glob('*'))
if set(contents) == {'arm64', 'x86', 'x86_64'}:
    prefixes = [prefix]
else:
    prefixes = contents

dllnames = list(get_plugin_names())

for prefix in prefixes:
    print(f'Ensuring all plugins exist in {prefix.name}...', end='', flush=True)
    notfound = gstprefix.check_plugins_exist(dllnames, prefix)
    if notfound:
        print(f'\nSome plugins were not found in {prefix.name}:')
        for each in notfound:
            print(each)
        exit(1)
    print(' Yes.')

    print('Getting DLL dependencies for all plugins...', end='', flush=True)
    dllpaths = []
    for dll in gstprefix.get_plugins_deps(dllnames, prefix):
        print('.', end='', flush=True)
        dllpath = prefix / dll
        if dllpath not in dllpaths:
            dllpaths.append(dllpath)
    print(' Done.')

    print('Ensuring all DLLs have the same arch / buildtype...', end='', flush=True)
    arch, buildtype = dlldeps.get_dlls_arch_buildtype(dllpaths)
    print(' Yes.')

    # Check whether the .vcxproj and .vcxproj.filters files exist
    vcxproj = Path(EXAMPLE_VCXPROJ)
    if not vcxproj.is_file():
        print(f'{vcxproj} does not exist')
        exit(1)

    vcxproj_filters = vcxproj.with_name(vcxproj.name + '.filters')
    if not vcxproj_filters.is_file():
        print(f'{vcxproj_filters} was not found')
        exit(1)

    proj_tree = vs.parse_file(vcxproj)
    filters_tree = vs.parse_file(vcxproj_filters)

    print(f'Updating assets in {vcxproj} and {vcxproj_filters}...', end='', flush=True)
    vs.update_assets(proj_tree, filters_tree, dllpaths, arch, buildtype)
    print(' Done.')

    print(f'Updating includes and libraries in {vcxproj}...', end='', flush=True)
    vs.update_deps(proj_tree, prefix, arch, buildtype)
    print(' Done.')

    print(f'Writing changes to {vcxproj} and {vcxproj_filters}...', end='', flush=True)
    vs.write_xmltree(proj_tree, vcxproj)
    vs.write_xmltree(filters_tree, vcxproj_filters)
    print(' Done.')

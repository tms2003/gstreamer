#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import sys
import argparse
from pathlib import Path

from utils import dlldeps
from utils import gstprefix
from utils import vs

parser = argparse.ArgumentParser(description='Rewrite a vcxproj file with the specified DLLs as assets shipping as part of a package')
parser.add_argument('vcxproj', type=str,
                    help='vcxproj file')
parser.add_argument('plugins', type=str, nargs='+',
                    help='gstreamer plugin names to use')
parser.add_argument('--prefix', '-p', required=True, type=str,
                    help='prefix to look for gstreamer plugins')

args = parser.parse_args()

vcxproj = Path(args.vcxproj)
if not vcxproj.is_file():
    raise RuntimeError('{} is not a vcxproj file'.format(vcxproj))

vcxproj_filters = vcxproj.with_name(vcxproj.name + '.filters')
if not vcxproj_filters.is_file():
    raise RuntimeError('{} was not found'.format(vcxproj_filters))

prefix = Path(args.prefix).resolve()
notfound = gstprefix.check_plugins_exist(args.plugins, prefix)
if notfound:
    print('Some plugins were not found:')
    for each in notfound:
        print(each)
    exit(1)

# Find DLL deps for each plugin, and de-duplicate
dllpaths = []
for dll in gstprefix.get_plugins_deps(sorted(args.plugins), prefix):
    dllpath = prefix / dll
    if dllpath not in dllpaths:
        dllpaths.append(dllpath)

arch, buildtype = dlldeps.get_dlls_arch_buildtype(dllpaths)

# Check whether the .vcxproj and .vcxproj.filters files exist
vcxproj = Path(vcxproj)
if not vcxproj.is_file():
    print(f'{vcxproj} does not exist')
    exit(1)

vcxproj_filters = vcxproj.with_name(vcxproj.name + '.filters')
if not vcxproj_filters.is_file():
    print(f'{vcxproj_filters} was not found')
    exit(1)

proj_tree = vs.parse_file(vcxproj)
filters_tree = vs.parse_file(vcxproj_filters)

vs.update_assets(proj_tree, filters_tree, dllpaths, arch, buildtype)

vs.write_xmltree(proj_tree, vcxproj)
vs.write_xmltree(filters_tree, vcxproj_filters)

#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import sys
import argparse
from pathlib import Path

from utils import gstprefix

parser = argparse.ArgumentParser(description='Get dependencies of the specified gstreamer plugins')
parser.add_argument('plugins', type=str, nargs='+',
                    help='gstreamer plugin names to find DLL dependencies for')
parser.add_argument('--prefix', '-p', required=True, type=str,
                    help='prefix to look for gstreamer plugins')
parser.add_argument('--system', action='store_true', default=False,
                    help='also list system DLLs')

args = parser.parse_args()

prefix = Path(args.prefix)

notfound = gstprefix.check_plugins_exist(args.plugins, prefix)
if notfound:
    print('Some plugins were not found:')
    for each in notfound:
        print(each)
    exit(1)

# Find DLL deps for each plugin
dlls = gstprefix.get_plugins_deps(sorted(args.plugins), prefix, system=args.system)
for dll in dlls:
    print(dll)

#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import argparse
from pathlib import Path

from utils import gstprefix
from utils.dlldeps import get_dependents, find_dll

parser = argparse.ArgumentParser(description='Get dependencies of the specified DLL')
parser.add_argument('dllpaths', type=str, nargs='+',
                    help='DLLs to find dependencies for')
parser.add_argument('--recursive', action='store_true', default=False,
                    help='find dependencies recursively')
parser.add_argument('--system', action='store_true', default=False,
                    help='also list system DLLs')

args = parser.parse_args()


printed = set()
for dllpath in args.dllpaths:
    dllpath = Path(dllpath)

    dlldirs = None
    prefix = None
    if args.recursive:
        prefix = gstprefix.get_prefix(dllpath)
        if prefix is None:
            raise RuntimeError('Could not compute prefix for {}'.format(dllpath))
        dlldirs = gstprefix.get_dlldirs(prefix)

    deps = get_dependents(dllpath, recurse_dirs=dlldirs)

    if prefix:
        print(dllpath.relative_to(prefix))
    else:
        print(dllpath.name)
    for dep in deps:
        if dep in printed:
            continue
        if not args.system and gstprefix.is_system_dll(dep):
            continue

        printed.add(dep)
        if not dlldirs:
            print(dep)
        else:
            dllpath = find_dll(dep, dlldirs)
            if dllpath is not None:
                print(dllpath.relative_to(prefix))
            elif args.system:
                print(dep)
            else:
                print(f'NOT FOUND IN PREFIX: {dep}')

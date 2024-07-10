#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import tarfile
import tempfile
import argparse
from pathlib import Path

from utils import gstprefix

parser = argparse.ArgumentParser(description='Verify that plugins and all deps are in the specified tarball')
parser.add_argument('plugins', type=str, nargs='+',
                    help='gstreamer plugin names to find DLL dependencies for')
parser.add_argument('--tarball', '-t', required=True, type=str,
                    help='tarball to look for gstreamer plugins')

args = parser.parse_args()

with tempfile.TemporaryDirectory(dir='.') as d:
    print('Extracting tarball...', end='', flush=True)
    with tarfile.open(args.tarball) as tf:
        tf.extractall(d)
    print(' Done.')

    extracted = Path(d)
    if 'universal' in args.tarball:
        prefixes = extracted.glob('*')
    else:
        prefixes = [extracted]

    print('Checking whether plugins and their deps are present...', end='', flush=True)
    notfound = {}
    for prefix in prefixes:
        ret = gstprefix.check_plugins_exist(args.plugins, prefix)
        if ret:
            if prefix.name not in notfound:
                notfound[prefix.name] = []
            notfound[prefix.name] += ret

    if notfound:
        print('Some plugins were not found:')
        for prefix, each in notfound.items():
            print(prefix, ' '.join(each))
        exit(1)

    # Find DLL deps for each plugin
    dlls = gstprefix.get_plugins_deps(sorted(args.plugins), prefix)
    for dll in dlls:
        print('.', end='', flush=True)

print(' All good!')

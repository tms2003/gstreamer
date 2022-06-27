#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import pefile

from pathlib import Path
from functools import lru_cache

# https://docs.microsoft.com/en-us/windows/win32/sysinfo/image-file-machine-constants
PE_FILE_MACHINE_TABLE = {
    0x014c: 'Win32',    # IMAGE_FILE_MACHINE_I386
    0x01c0: 'ARM',      # IMAGE_FILE_MACHINE_ARM
    0x8664: 'x64',      # IMAGE_FILE_MACHINE_AMD64
    0xAA64: 'ARM64',    # IMAGE_FILE_MACHINE_ARM64
}

@lru_cache(maxsize=None)
def find_dll(dllname, dirs):
    for d in dirs:
        dllpath = Path(d) / dllname
        if dllpath.is_file():
            return dllpath
    return None

def dll_in_dirs(dllname, dirs):
    if find_dll(dllname, dirs) is not None:
        return True
    return False

@lru_cache(maxsize=None)
def get_pefile_dict(dllpath):
    p = pefile.PE(dllpath)
    d = p.dump_dict()
    # If we don't close it manually, we will keep the file open till the
    # process exits
    p.close()
    return d

@lru_cache(maxsize=None)
def get_dependents(dllpath, recurse_dirs=None):
    d = get_pefile_dict(dllpath)
    dlls = set()
    for imports in d['Imported symbols']:
        if len(imports) <= 1:
            continue
        for symbol in imports:
            if 'DLL' in symbol:
                dllname = symbol['DLL'].decode(encoding='utf-8')
                if dllname not in dlls:
                    yield dllname
                    dlls.add(dllname)
    if not recurse_dirs:
        return
    # Recursively find deps for DLLs inside the specified dirs
    for depdllname in filter(lambda x: dll_in_dirs(x, recurse_dirs), frozenset(dlls)):
        depdllpath = find_dll(depdllname, recurse_dirs)
        depiter = get_dependents(depdllpath, recurse_dirs)
        for recdepdllname in depiter:
            if recdepdllname not in dlls:
                yield recdepdllname
                dlls.add(recdepdllname)

def get_arch(dllpath):
    d = get_pefile_dict(dllpath)
    machine = d['FILE_HEADER']['Machine']['Value']
    return PE_FILE_MACHINE_TABLE[machine]

def get_buildtype(dllpath):
    deps = get_dependents(dllpath)
    if 'ucrtbased.dll' in deps:
        return 'Debug'
    return 'Release'

def get_dlls_arch_buildtype(dllpaths):
    '''
    Ensure all plugins have the same arch and buildtype, and return it
    '''
    if not dllpaths:
        raise RuntimeError('No dlls specified')

    plugin_arch = None
    plugin_buildtype = None

    for dllpath in dllpaths:
        # Ensure arch is the same across all DLLs
        arch = get_arch(dllpath)
        if plugin_arch is None:
            plugin_arch = arch
        elif plugin_arch != arch:
            raise RuntimeError(f'arch of {dllpath} is {arch}, but other plugins are {plugin_arch}')
        # Ensure buildtype is the same across all DLLs
        buildtype = get_buildtype(dllpath)
        if plugin_buildtype is None:
            plugin_buildtype = buildtype
        elif plugin_buildtype != buildtype:
            raise RuntimeError(f'buildtype of {dllpath} is {buildtype}, but other plugins are {plugin_buildtype}')

    return plugin_arch, plugin_buildtype

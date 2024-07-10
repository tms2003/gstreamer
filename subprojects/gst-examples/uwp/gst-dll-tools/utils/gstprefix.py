#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

from functools import lru_cache

from utils import dlldeps

# This list is only used for detecting which DLLs are supposed to be in the
# prefix but weren't for some reason
SYSTEM_DLL_PREFIXES = (
    'api-ms-win-',
    'vcruntime',
    'msvc',
    'd3dcompiler_',
)

SYSTEM_DLL_NAMES = (
    'advapi32',
    'bcrypt',
    'd3d11',
    'd3d9',
    'dnsapi',
    'dsound',
    'dxgi',
    'gdi32',
    'iphlpapi',
    'kernel32',
    'ksuser',
    'mf',
    'mfplat',
    'mfreadwrite',
    'mmdevapi',
    'msimg32',
    'ole32',
    'oleaut32',
    'opengl32',
    'setupapi',
    'shell32',
    'shlwapi',
    'ucrtbase',
    'ucrtbased',
    'user32',
    'usp10',
    'winmm',
    'ws2_32',
    'wsock32',
)

# Dependencies that are opened dynamically at runtime with g_module_open(),
# LoadLibrary, etc.
MODULE_DEPS = {
    'libEGL.dll': ('libGLESv2.dll',),
}

@lru_cache(maxsize=None)
def is_system_dll(dllname):
    dllname = dllname.lower()[:-4]
    if dllname.startswith(SYSTEM_DLL_PREFIXES):
        return True
    if dllname in SYSTEM_DLL_NAMES:
        return True
    return False

def get_module_deps(dllname):
    if dllname not in MODULE_DEPS:
        return []
    return list(MODULE_DEPS[dllname])

@lru_cache(maxsize=None)
def get_prefix(dllpath):
    parents = dllpath.parents
    if parents[0].name == 'gstreamer-1.0' and parents[1].name == 'lib':
        return parents[2]
    if parents[0].name == 'bin':
        return parents[1]
    return none

@lru_cache(maxsize=None)
def get_dlldirs(prefix):
    '''
    Directories where DLLs will be located inside the prefix. Usually:
    prefix/bin
    prefix/lib/gstreamer-1.0
    '''
    return (prefix / 'bin', prefix / 'lib' / 'gstreamer-1.0')

def check_plugins_exist(plugins, prefix):
    plugindir = prefix / 'lib' / 'gstreamer-1.0'
    # Check that all plugins were found
    notfound = []
    for plugin in plugins:
        if not (plugindir / f'gst{plugin}.dll').is_file():
            notfound.append(plugin)
    return notfound

def get_plugins_deps(plugins, prefix, system=False):
    plugindir = prefix / 'lib' / 'gstreamer-1.0'
    dlldirs = (prefix / 'bin', plugindir)
    # Find DLL deps for each plugin
    found = set()
    for plugin in plugins:
        plugin = plugindir / f'gst{plugin}.dll'
        deps = dlldeps.get_dependents(plugin, recurse_dirs=dlldirs)
        yield plugin.relative_to(prefix)
        for dep in deps:
            if dep in found:
                continue
            if is_system_dll(dep):
                if system:
                    yield dep
                    found.add(dep)
                continue
            for extra in get_module_deps(dep):
                extrapath = dlldeps.find_dll(extra, dlldirs)
                if not extrapath:
                    raise RuntimeError(f'{extra} was not find inside {dlldirs}')
                yield extrapath.relative_to(prefix)
                found.add(extra)
            dllpath = dlldeps.find_dll(dep, dlldirs)
            if not dllpath:
                raise RuntimeError(f'{dep} was not find inside {dlldirs}')
            yield dllpath.relative_to(prefix)
            found.add(dep)

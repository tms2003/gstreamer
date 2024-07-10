#!/usr/bin/env python3
#
# vim: set sts=4 sw=4 et :

import xml.etree.ElementTree as ET

from pathlib import WindowsPath

from utils import dlldeps

VCXPROJ_INDENT = '  '
VCXPROJ_NS = "http://schemas.microsoft.com/developer/msbuild/2003"
ET.register_namespace('', VCXPROJ_NS)

GST_INCLUDE_PATHS = (
    'include',
    'include/glib-2.0',
    'lib/glib-2.0/include',
    'include/gstreamer-1.0',
    'lib/gstreamer-1.0/include/gst/gl',
)

# The user will have to add libs that they use in their code manually based on
# what APIs they use, but we can add a default list to make it easier
GST_DEFAULT_LIBS = (
    'glib-2.0.lib',
    'gobject-2.0.lib',
    'gio-2.0.lib',
    'gmodule-2.0.lib',
    'gstreamer-1.0.lib',
    # Common gstreamer libs
    'gstapp-1.0.lib',
    'gstplayer-1.0.lib',
    'gstvideo-1.0.lib',
    'gstgl-1.0.lib',
    'gstsdp-1.0.lib',
    'gstwebrtc-1.0.lib',
    'ges-1.0.lib',
    'gstpbutils-1.0.lib',
)


def get_include_paths(prefix):
    ret = ''
    for inc in GST_INCLUDE_PATHS:
        ret += '{};'.format(WindowsPath(prefix) / inc)
    return ret

def get_library_paths(prefix):
    return str(WindowsPath(prefix) / 'lib;')

def get_default_libs():
    return ';'.join(GST_DEFAULT_LIBS) + ';'

def get_xmlns(tree):
    root_tag = tree.getroot().tag
    return root_tag[:root_tag.index('}')+1]

def get_condition(arch, buildtype):
    return f"'$(Configuration)|$(Platform)'=='{buildtype}|{arch}'"

def find_asset_node(tree, ns):
    '''
    Look for:
    <ItemGroup>
      <None Include="...">
        ...
      </None>
      ...
    </ItemGroup>
    '''
    for ig in tree.findall('{%s}ItemGroup' % ns):
        none = ig.find('{%s}None' % ns)
        if none is None or 'Include' not in none.attrib:
            continue
        return ig

def clear_asset_node(node, ns, tag, attr, text):
    key = value = None
    if attr:
        attr = dict(attr)
        key, value = attr.popitem()
    assert(not attr)
    for asset in node.findall('{%s}None' % ns):
        for child in asset.findall('{%s}%s' % (ns, tag)):
            if key and (key not in child.attrib or child.attrib[key] != value):
                continue
            if child.text == text:
                node.remove(asset)

def add_asset(proj_node, filters_node, dllpath: str, arch: str, buildtype: str):
    # Write out node for .vcxproj
    #
    # Clear all asset entries for this Condition
    attr = {'Condition': get_condition(arch, buildtype)}
    clear_asset_node(proj_node, VCXPROJ_NS, 'DeploymentContent', attr, 'true')
    # Write out a new entry only for this dll
    none = ET.SubElement(proj_node, 'None', Include=dllpath)
    none.text = '\n' + (VCXPROJ_INDENT * 3)
    none.tail = '\n' + (VCXPROJ_INDENT * 2)
    dc = ET.SubElement(none, 'DeploymentContent', **attr)
    dc.text = 'true'
    dc.tail = '\n' + (VCXPROJ_INDENT * 2)
    proj_none = none

    # Write out node for .vcxproj.filters
    #
    # Clear all asset entries for this Condition
    text = f'Assets\\{buildtype}-{arch}'
    clear_asset_node(filters_node, VCXPROJ_NS, 'Filter', {}, text)
    # Write out a new entry only for this dll
    none = ET.SubElement(filters_node, 'None', Include=dllpath)
    none.text = '\n' + (VCXPROJ_INDENT * 3)
    none.tail = '\n' + (VCXPROJ_INDENT * 2)
    dc = ET.SubElement(none, 'Filter')
    dc.text = text
    dc.tail = '\n' + (VCXPROJ_INDENT * 2)
    filters_none = none
    return proj_none, filters_none

def update_assets(proj_tree, filters_tree, dlls, arch, buildtype):
    # Parse vcxproj file
    proj_node = find_asset_node(proj_tree, VCXPROJ_NS)
    if proj_node is None:
        raise RuntimeError('Could not find an asset ItemGroup inside vcxproj')

    filters_node = find_asset_node(filters_tree, VCXPROJ_NS)
    if filters_node is None:
        raise RuntimeError('Could not find an asset ItemGroup inside vcxproj_filters')

    plugin_arch = None
    plugin_buildtype = None

    for dllpath in dlls:
        proj_none, filters_none = \
                add_asset(proj_node, filters_node, str(dllpath), arch, buildtype)
    proj_none.tail = '\n' + VCXPROJ_INDENT
    filters_none.tail = '\n' + VCXPROJ_INDENT

def find_paths_node(tree, ns, condition):
    '''
    Look for:
    <PropertyGroup Condition=...>
      <IncludePath>$(IncludePath)</IncludePath>
      <LibraryPath>$(LibraryPath)</LibraryPath>
    </PropertyGroup>
    '''
    for pg in tree.findall('{%s}PropertyGroup' % ns):
        if pg.attrib.get('Condition', None) != condition:
            continue
        if pg.find('{%s}IncludePath' % ns) is None:
            continue
        if pg.find('{%s}LibraryPath' % ns) is None:
            continue
        return pg

def find_deps_node(tree, ns, condition):
    '''
    Look for:
    <ItemDefinitionGroup Condition=...>
      <Link>
        <AdditionalDependencies>%(AdditionalDependencies)</AdditionalDependencies>
      </Link>
    </ItemDefinitionGroup>
    '''
    for idg in tree.findall('{%s}ItemDefinitionGroup' % ns):
        if idg.attrib.get('Condition', None) != condition:
            continue
        link = idg.find('{%s}Link' % ns)
        if link is None:
            continue
        deps = link.find('{%s}AdditionalDependencies' % ns)
        if deps is not None:
            return deps

def update_deps(tree, prefix, arch, buildtype):
    node = find_paths_node(tree, VCXPROJ_NS, get_condition(arch, buildtype))
    incpath_node = node.find('{%s}IncludePath' % VCXPROJ_NS)
    incpath_node.text = get_include_paths(prefix) + '$(IncludePath)'
    libpath_node = node.find('{%s}LibraryPath' % VCXPROJ_NS)
    libpath_node.text = get_library_paths(prefix) + '$(LibraryPath)'

    node = find_deps_node(tree, VCXPROJ_NS, get_condition(arch, buildtype))
    node.text = get_default_libs() + '%(AdditionalDependencies)'

def parse_file(f):
    return ET.parse(f)

def write_xmltree(tree, fpath):
    # We can't use .write(xml_declaration=True) because that outputs ' instead of "
    data = '<?xml version="1.0" encoding="utf-8"?>\n'
    data += ET.tostring(tree.getroot(), encoding='unicode')
    with open(fpath, 'w', encoding='utf-8') as f:
        f.write(data)

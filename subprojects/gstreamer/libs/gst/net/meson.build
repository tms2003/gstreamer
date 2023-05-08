gst_net_sources = files(
  'gstnetaddressmeta.c',
  'gstnetclientclock.c',
  'gstnetcontrolmessagemeta.c',
  'gstnettimepacket.c',
  'gstnettimeprovider.c',
  'gstptpclock.c',
  'gstntppacket.c',
  'gstnetutils.c',
)

gst_net_headers = files(
 'gstnet.h',
 'gstnetaddressmeta.h',
 'gstnetclientclock.h',
 'gstnetcontrolmessagemeta.h',
 'gstnettimepacket.h',
 'gstnettimeprovider.h',
 'gstnetutils.h',
 'gstptpclock.h',
 'net-prelude.h',
 'net.h',
)
install_headers(gst_net_headers, subdir : 'gstreamer-1.0/gst/net/')

gst_net_gen_sources = []

libgst_net_sources = gst_net_sources
if host_system == 'windows'
  libgst_net_rc_cdata = gst_version_cdata
  libgst_net_rc_cdata.set('GST_FILE_TYPE', 'VFT_DLL')
  libgst_net_rc_cdata.set('GST_BIN_DESC', 'GStreamer network library')
  if cc.get_argument_syntax() == 'msvc'
    libgst_net_rc_cdata.set('GST_BIN_NAME', 'gstnet-@0@-@1@'.format(apiversion, soversion))
  else
    libgst_net_rc_cdata.set('GST_BIN_NAME', 'libgstnet-@0@-@1@'.format(apiversion, soversion))
  endif
  libgst_net_rc = configure_file(
    input: join_paths(meson.project_source_root(), 'gstreamer.rc.in'),
    output: 'gstnet-@0@.rc'.format(apiversion),
    configuration: libgst_net_rc_cdata,
  )
  libgst_net_res = windows.compile_resources(libgst_net_rc)
  libgst_net_sources += [libgst_net_res]
endif

gst_net = library('gstnet-@0@'.format(apiversion),
  libgst_net_sources,
  c_args : gst_c_args + ['-DBUILDING_GST_NET', '-DG_LOG_DOMAIN="GStreamer-Net"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gio_dep, gst_base_dep, dl_dep],
)

library_def = {'lib': gst_net}
pkg_name = 'gstreamer-net-1.0'
pkgconfig.generate(gst_net,
  libraries : [libgst],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Network-enabled GStreamer plug-ins and clocking',
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/net/net.h' ]
  gir = {
    'sources' : gst_net_sources + gst_net_headers,
    'namespace' : 'GstNet',
    'nsversion' : apiversion,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'dependencies' : [gst_base_dep],
    'include_directories' : [configinc, libsinc],
    'includes' : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gio-2.0', 'Gst-1.0'],
    'install' : true,
    'extra_args' : gst_gir_extra_args,
  }
  library_def += {'gir': [gir]}
  if not static_build
    gst_net_gir = gnome.generate_gir(gst_net, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_net_gir]}
    gst_net_gen_sources += [gst_net_gir]
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_net_dep = declare_dependency(link_with : gst_net,
  include_directories : [libsinc],
  sources: gst_net_gen_sources,
  dependencies : [gst_base_dep])

meson.override_dependency(pkg_name, gst_net_dep)

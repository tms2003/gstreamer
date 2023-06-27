overlay_sources = [
  'gstbaseclockoverlay.c',
  'gstbasesubtitleoverlay.c',
  'gstbasesubtitleoverlaybin.c',
  'gstbasetextlayoutoverlay.c',
  'gstbasetimeoverlay.c',
]

overlay_headers = [
  'gstbaseclockoverlay.h',
  'gstbasesubtitleoverlay.h',
  'gstbasesubtitleoverlaybin.h',
  'gstbasetextlayoutoverlay.h',
  'gstbasetimeoverlay.h',
]

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_TEXT_LAYOUT_OVERLAY',
  '-DG_LOG_DOMAIN="GStreamer-TextLayoutOverlay"'
]

subdir('libcaption')

gsttextlayoutoverlay = library('gsttextlayoutoverlay-' + api_version,
  overlay_sources,
  c_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  dependencies : [gstbase_dep, gstvideo_dep, libcaption_dep],
  install : true,
)

library_def = {'lib': gsttextlayoutoverlay}
pkg_name = 'gstreamer-textlayoutoverlay-1.0'
gen_sources = []
if build_gir
  gir = {
    'sources' : overlay_sources + overlay_headers,
    'namespace' : 'GstTextLayoutOverlay',
    'nsversion' : api_version,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'includes' : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0'],
    'install' : true,
    'extra_args' : gir_init_section + ['-DGST_USE_UNSTABLE_API'] +
      ['--c-include=gst/textlayoutoverlay/gsttextlayoutoverlay.h'],
    'dependencies' : [gstbase_dep, gstvideo_dep]
  }
  library_def += {'gir': [gir]}
  if not static_build
    textlayoutoverlay_gir = gnome.generate_gir(gsttextlayoutoverlay, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [textlayoutoverlay_gir]}
    gen_sources += textlayoutoverlay_gir
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gsttextlayoutoverlay_dep = declare_dependency(link_with : gsttextlayoutoverlay,
  include_directories : [libsinc],
  sources: gen_sources,
  dependencies : [gstbase_dep, gstvideo_dep])

meson.override_dependency(pkg_name, gsttextlayoutoverlay_dep)

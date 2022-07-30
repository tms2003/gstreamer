gsteme_headers = files(
  'eme.h',
  'eme-prelude.h',
  'gstemeutils.h',
  'gstmediakeysystemaccess.h',
  'gstmediakeys.h',
  'gstmediakeysession.h',
)

gsteme_sources = files(
  'gstmediakeysystemaccess.c',
  'gstmediakeys.c',
  'gstmediakeysession.c',
  'gstemeutils.c',
  'gstemelogging.c',
)

gsteme_header_dir = 'gstreamer-' + api_version + '/gst/eme/'

gsteme_enums = gnome.mkenums_simple('eme-enumtypes',
  sources : gsteme_headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/eme/eme-prelude.h>',
  decorator: 'GST_EME_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), gsteme_header_dir),
)

gsteme_enums_c = gsteme_enums[0]
gsteme_enums_h = gsteme_enums[1]

gsteme_deps = [gst_dep]

gsteme = library('gsteme-' + api_version,
  gsteme_sources + [gsteme_enums_c, gsteme_enums_h],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_EME', '-DG_LOG_DOMAIN="GStreamer-EME"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : gsteme_deps,
)

pkg_name = 'gstreamer-eme-' + api_version
pkgconfig.generate(gsteme,
  libraries : [gst_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer Support for W3C Encrypted Media Extensions',
)

library_def = {'lib': gsteme}
gen_sources = []
if build_gir
  gir = {
    'sources' : [
      gsteme_sources,
      gsteme_headers,
      gsteme_enums_h,
      gsteme_enums_c,
    ],
    'namespace' : 'GstEme',
    'nsversion' : api_version,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'includes' : ['Gst-1.0'],
    'install' : true,
    'extra_args' : gir_init_section + ['-DGST_USE_UNSTABLE_API'] + ['--c-include=gst/eme/eme.h'],
    'dependencies' : gsteme_deps,
  }
  library_def += {'gir': [gir]}
  if not static_build
    eme_gir = gnome.generate_gir(gsteme, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [eme_gir]}
    gen_sources += eme_gir
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gsteme_dep = declare_dependency(
  link_with : [gsteme],
  include_directories : [libsinc],
  dependencies : gsteme_deps,
  sources: [gsteme_enums_h],
)
install_headers(gsteme_headers, subdir: gsteme_header_dir)
meson.override_dependency(pkg_name, gsteme_dep)

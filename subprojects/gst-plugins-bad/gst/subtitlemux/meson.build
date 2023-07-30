subtitlemux_sources = [
  'gstpangoparser.c',
  'gstsubtitlemux.c',
  'plugin.c',
]

extra_args = []

pango_dep = dependency('pangocairo',
  version : '>=1.50.0',
  required : false,
  fallback: ['pango', 'libpangocairo_dep'],
  default_options: ['cairo=enabled'])

if pango_dep.found()
  extra_args += ['-DHAVE_PANGO']
endif

gstsubtitlemux = library('gstsubtitlemux',
  subtitlemux_sources,
  c_args : gst_plugins_bad_args + extra_args,
  include_directories: [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, pango_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsubtitlemux]

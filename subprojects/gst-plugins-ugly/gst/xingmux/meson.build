xing_sources = [
  'plugin.c',
  'gstxingmux.c',
]

gstxingmux = library('gstxingmux',
  xing_sources,
  c_args : ugly_args + ['-DG_LOG_DOMAIN="GStreamer-xingmux"'],
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstxingmux, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstxingmux]

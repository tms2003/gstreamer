autocon_sources = [
  'gstautoselect.c',
  'gstautovideodecoder.c',
  'plugin.c',
]

gstautoselect = library('gstautoselect',
  autocon_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstautoselect]

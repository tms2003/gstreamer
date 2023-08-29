pango_sources = [
  'gstpangoclockoverlay.c',
  'gstpangooverlayobject.c',
  'gstpangosubtitleoverlay.c',
  'gstpangotextoverlay.c',
  'gstpangotimeoverlay.c',
  'plugin.c',
]

extra_args = ['-DGST_USE_UNSTABLE_API']

pango_dep = dependency('pangocairo',
  version : '>=1.50.0',
  required : get_option('pango2'),
  fallback: ['pango', 'libpangocairo_dep'],
  default_options: ['cairo=enabled'])

if pango_dep.found()
  gstpango2 = library('gstpango2',
    pango_sources,
    c_args : gst_plugins_bad_args + extra_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gsttextlayoutoverlay_dep, pango_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstpango2]
endif

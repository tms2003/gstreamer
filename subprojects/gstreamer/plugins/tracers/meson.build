if not tracer_hooks
  if get_option('coretracers').enabled()
    error('coretracers plugin enabled but not tracer_hooks')
  endif
  subdir_done()
endif

gst_tracers_sources = [
  'gstlatency.c',
  'gstleaks.c',
  'gststats.c',
  'gsttracers.c',
  'gstfactories.c'
]

if gst_debug
  gst_tracers_sources += ['gstlog.c']
endif

if cdata.has('HAVE_GETRUSAGE')
  gst_tracers_sources += ['gstrusage.c']
endif

thread_dep = dependency('threads', required : false)

if host_system == 'windows'
  rc_cdata = gst_version_cdata
  rc_cdata.set('GST_FILE_TYPE', 'VFT_DLL')
  rc_cdata.set('GST_BIN_DESC', 'GStreamer tracers library')
  if cc.get_argument_syntax() == 'msvc'
    rc_cdata.set('GST_BIN_NAME', 'gstcoretracers')
  else
    rc_cdata.set('GST_BIN_NAME', 'libgstcoretracers')
  endif
  rc = configure_file(
    input: join_paths(meson.project_source_root(), 'gstreamer.rc.in'),
    output: 'gstcoretracers.rc',
    configuration: rc_cdata,
  )
  res = windows.compile_resources(rc)
  gst_tracers_sources += [res]
endif

gst_tracers = library('gstcoretracers',
  gst_tracers_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, thread_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gst_tracers]

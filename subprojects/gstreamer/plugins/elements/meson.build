gst_elements_sources = [
  'gstcapsfilter.c',
  'gstclocksync.c',
  'gstconcat.c',
  'gstdataurisrc.c',
  'gstdownloadbuffer.c',
  'gstcoreelementsplugin.c',
  'gstelements_private.c',
  'gstfakesink.c',
  'gstfakesrc.c',
  'gstfdsink.c',
  'gstfdsrc.c',
  'gstfilesrc.c',
  'gstfilesink.c',
  'gstfunnel.c',
  'gstidentity.c',
  'gstinputselector.c',
  'gstmultiqueue.c',
  'gstoutputselector.c',
  'gstqueue2.c',
  'gstqueue.c',
  'gstsparsefile.c',
  'gststreamiddemux.c',
  'gsttee.c',
  'gsttypefindelement.c',
  'gstvalve.c',
]

if host_system == 'windows'
  rc_cdata = gst_version_cdata
  rc_cdata.set('GST_FILE_TYPE', 'VFT_DLL')
  rc_cdata.set('GST_BIN_DESC', 'GStreamer core elements library')
  if cc.get_argument_syntax() == 'msvc'
    rc_cdata.set('GST_BIN_NAME', 'gstcoreelements')
  else
    rc_cdata.set('GST_BIN_NAME', 'libgstcoreelements')
  endif
  rc = configure_file(
    input: join_paths(meson.project_source_root(), 'gstreamer.rc.in'),
    output: 'gstcoreelements.rc',
    configuration: rc_cdata,
  )
  res = windows.compile_resources(rc)
  gst_elements_sources += [res]
endif

gst_elements = library('gstcoreelements',
  gst_elements_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gst_elements]

gstcodecparsers_dep = dependency('gstreamer-codecparsers-1.0', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstcodecparsers_dep'])

gstflv = library('gstflv',
  'gstflvdemux.c', 'gstflvmux.c', 'gstflvplugin.c','gstflvelement.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstpbutils_dep, gstvideo_dep, gsttag_dep, gstaudio_dep, gstcodecparsers_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstflv]

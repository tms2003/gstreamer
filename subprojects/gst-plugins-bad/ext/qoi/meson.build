qoi_sources = [
  'gstqoi.c',
  'gstqoienc.c',
  'gstqoidec.c',
]

gstqoi = library('gstqoi',
qoi_sources,
c_args : gst_plugins_bad_args,
link_args : noseh_link_args,
include_directories : [configinc, libsinc, 'qoi'],
dependencies : [gstbase_dep, gstvideo_dep],
install : true,
install_dir : plugins_install_dir,
)
plugins += [gstqoi]

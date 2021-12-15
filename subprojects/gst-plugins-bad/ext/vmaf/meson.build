vmaf_sources = [
  'gstvmafconvert.c',
  'gstvmafelement.c',
  'gstvmafplugin.c',
]

libvmaf_dep = dependency('libvmaf', required : get_option('vmaf'))
if libvmaf_dep.found()
  gstvmaf = library('gstvmaf',
    vmaf_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DHAVE_LIBVMAF'],
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, libvmaf_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstvmaf, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstvmaf]
endif


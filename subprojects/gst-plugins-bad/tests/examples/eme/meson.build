if not (gsteme_dep.found() and gstemeopencdm_dep.found())
  subdir_done()
endif

sparkle_cdm_widevine_subproject = subproject('sparkle-cdm-widevine')

libsoup3_dep = dependency('libsoup-3.0', fallback : ['libsoup3', 'libsoup_dep'])
executable('emedemo',
  'emedemo.c',
  install: false,
  include_directories : [configinc],
  dependencies : [gst_dep, gsteme_dep, gstplay_dep, libsoup3_dep],
  c_args : [gst_plugins_bad_args, '-DGST_USE_UNSTABLE_API'],
)

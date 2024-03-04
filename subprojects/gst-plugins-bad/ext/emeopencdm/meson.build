if not get_option('emeopencdm').enabled()
  gstemeopencdm_dep = disabler()
  subdir_done()
endif

sparkle_cdm_subproject = subproject('sparkle-cdm')
sparkle_cdm_dep = sparkle_cdm_subproject.get_variable('sparkle_cdm_dep')

emeopencdm_sources = files(
  'gstopencdmmediakeysystemaccess.c',
  'gstopencdmmediakeysession.c',
  'gstopencdmmediakeys.c',
  'gstopencdmmediakeysession.c',
  'gstopencdmprotectionsystem.c',
  'gstopencdmdecryptor.c',
  'gstemeopencdmlogging.c',
  'gstemeopencdm.c',
)

gstemeopencdm_plugin = library('gstemeopencdm',
  emeopencdm_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gsteme_dep, sparkle_cdm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstemeopencdm_plugin,
  install_dir : plugins_pkgconfig_install_dir,
)

gstemeopencdm_dep = declare_dependency(link_with : gstemeopencdm_plugin)

plugins += [gstemeopencdm_plugin]

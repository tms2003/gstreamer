analyticmeta_sources = files( 'generic/gstanalysismeta.c',
                              'classification/gstanalysisclassificationmtd.c',
                              'object_detection/gstobjectdetectionmtd.c',
                              'tracking/gstobjecttrackingmtd.c')

analyticmeta_headers = files( 'generic/gstanalysismeta.h', 
                              'analytic-meta-prelude.h',
                              'classification/gstanalysisclassificationmtd.h',
                              'object_detection/gstobjectdetectionmtd.h',
                              'tracking/gstobjecttrackingmtd.h')

pkg_name = 'gstreamer-analytic-meta-1.0'
gstanalysismeta = library('gstanalysismeta-' + api_version,
  analyticmeta_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_ANALYTIC_META', '-DG_LOG_DOMAIN="Gstreamer-Analytic-Meta"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep])

gst_libraries += [[pkg_name, {'lib': gstanalysismeta}]]

gstanalysismeta_dep = declare_dependency(link_with : gstanalysismeta,
  include_directories : [libsinc],
  dependencies : [gstbase_dep])

meson.override_dependency(pkg_name, gstanalysismeta_dep)

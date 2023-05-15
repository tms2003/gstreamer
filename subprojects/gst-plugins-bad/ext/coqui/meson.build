
if not get_option('coqui').enabled()
  subdir_done()
endif

gst_coqui_sources = [
  'gstcoqui.c',
  'gstcoquistt.c',
]

# FIXME: Coqui's Bazel build system doesn't seem to produce pkg-config files.

cc = meson.get_compiler('c')
native_client_dir = get_option('coqui-native-client-dir')
coqui_dep = cc.find_library('stt', required: true, dirs: native_client_dir)

gst_coqui_deps = [
  gst_dep, gstbase_dep, gstaudio_dep, coqui_dep,
]

gstcoqui = library('gstcoqui',
  gst_coqui_sources,
  dependencies: gst_coqui_deps,
  c_args : gst_plugins_bad_args + ['-DHAVE_CONFIG_H=1'],
  include_directories: [include_directories(native_client_dir), configinc],
  install: true,
  install_dir : plugins_install_dir
)

pkgconfig.generate(gstcoqui, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstcoqui]

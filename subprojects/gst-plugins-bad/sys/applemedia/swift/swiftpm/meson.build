swiftpm = find_program('swift', required : true)

applemedia_swiftpm_sources = [
  'Package.swift',
  'Sources/GstSwiftMacros/GstSwiftMacros.swift',
  'Sources/GstSwiftMacros/GstDebugMacros.swift',
]

target_arch = target_machine.cpu_family()
if target_arch == 'x86_64'
  arch = 'x86_64'
elif target_arch == 'aarch64'
  arch = 'arm64'
else
  error('Unsupported architecture: ' + target_arch)
endif

meson_build_type = get_option('buildtype')
if ['debug', 'debugoptimized'].contains(meson_build_type)
  build_type = 'debug'
else
  build_type = 'release'
endif

# This retrieves external SwiftPM 'deps we use (swift-collections, macros)
# and builds them into a static library, which we can then use for our Meson-only build.
applemedia_swiftpm_deps = custom_target('applemedia-swiftpm-deps',
  output: 'libGstSwiftDeps.a',
  depend_files: files(applemedia_swiftpm_sources),
  command: [
    swiftpm, 'build',
    '--arch', arch,
    '--configuration', build_type,
    '--package-path', meson.current_source_dir(),      # package path = working source dir
    '--build-path', '@PRIVATE_DIR@',                   # build path = output dir
    '&&',
    'cp', '@PRIVATE_DIR@/' + build_type + '/libGstSwiftDeps.a', '@OUTPUT@',
  ],
)

applemedia_swiftpm_deps_dep = declare_dependency(
  sources: applemedia_swiftpm_deps[0], # This is so this gets actually built before the main swift part
                                       # (because not only the lib, but also the include dirs made by swiftPM are needed)
  link_with: applemedia_swiftpm_deps,  # And this is to actually use the lib
)

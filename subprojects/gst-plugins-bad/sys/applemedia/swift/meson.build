# TODO: is this the correct way to make meson notice changes in these files?
applemedia_swift_sources = [
  'Package.swift',
  'Sources/GstSCKitSrc/SCKitAudioSrc.swift',
  'Sources/GstSCKitSrc/Macros.swift',
  'Sources/GstSCKitSrc/Utils.swift',
  'Sources/GstSwiftMacros/GstSwiftMacros.swift',
  'Sources/GstSwiftMacros/GstDebugMacros.swift',
  'Sources/CGStreamer/gst-shim.h',
  'Sources/CGStreamer/module.modulemap'
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

xcodeselect = find_program('xcode-select')
if not xcodeselect.found()
  error('xcode-select not found, cannot link Swift libs correctly')
endif
xcode_path = run_command(xcodeselect, ['--print-path']).stdout().strip()

screencapturekit_dep = dependency('ScreenCaptureKit', required : applemedia_option)
if screencapturekit_dep.found()
  cdata.set('HAVE_SCREENCAPTUREKIT', 1)
  applemedia_sources += files('sckitaudiosrc.m')

  sckitsrc_swift = custom_target('sckitsrc-swift',
    input: files(applemedia_swift_sources),
    output: 'libgstsckitsrc.a',
    command: [
      swiftpm, 'build',
      '--product', 'gstsckitsrc',
      '--arch', arch,
      '--configuration', build_type,
      '--package-path', meson.current_source_dir(),      # package path = working source dir
      '--build-path', '@PRIVATE_DIR@',                   # build path = output dir
      '&&',
      'cp', '@PRIVATE_DIR@/' + build_type + '/libgstsckitsrc.a', '@OUTPUT@',
      '&&',
      # This will copy the bridging header to the source dir.
      # TODO: Make this a separate run target, similar to GstSharp bindinate calls?
      # FIXME: when swift file gets changed, this sometimes does not make it in time
      # for the ObjC code to pick it up (and complains about missing types etc.)
      'cp',
      '@PRIVATE_DIR@/' + build_type + '/GstSCKitSrc.build/GstSCKitSrc-Swift.h',
      meson.current_source_dir() + '/GstSCKitSrc-Swift.h',
    ],
  )

  # libswift_Concurrency, needed for async support in Swift, is only present as a system lib on macOS 12.0+.
  # We're only running code using it on 12.0+ anyway, but the whole build is targeting an older macOS
  # version (10.13), which causes this lib to be linked with @rpath instead of directly to /usr/lib/swift.
  # To work around this, we weak-link this particular lib and add an RPATH to the swift lib dir.
  applemedia_rpath = '/usr/lib/swift'
  sckitsrc_swift_dep = declare_dependency(
    sources: sckitsrc_swift,
    dependencies: [screencapturekit_dep],
    link_args: [
      # This one is needed for the linker to pick up Swift's compatibility libs.
      '-L' + xcode_path + '/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx',
      '-weak-lswift_Concurrency',
      '-Wl,-rpath,' + applemedia_rpath,
      '-mmacosx-version-min=10.13',
    ],
  )

  applemedia_deps += [sckitsrc_swift_dep]
endif

# TODO: Should this be separate?
# run_target('sckitsrc-gen-bridging-header',
#   depends:
# )

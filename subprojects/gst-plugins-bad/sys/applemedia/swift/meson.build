swiftpm = find_program('swift')

target_arch = target_machine.cpu_family()
if target_arch == 'x86_64'
  arch = 'x86_64'
elif target_arch == 'aarch64'
  arch = 'arm64'
else
  error('Unsupported architecture: ' + target_arch)
endif

meson_build_type = get_option('buildtype')
if meson_build_type == 'debug' or meson_build_type == 'debugoptimized'
  build_type = 'debug'
else
  build_type = 'release'
endif

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

# We build the CoreVideo/CoreMedia buffer wrappers here for the second time
# to make sure they are available for the Swift-based plugin.
# TODO: rename symbols!

applemedia_swift_objc_sources = applemedia_shared_sources + [
  'plugin.m',
]

applemedia_swift_deps = []
screencapturekit_dep = dependency('ScreenCaptureKit', required : applemedia_option)
if screencapturekit_dep.found()
  applemedia_swift_objc_sources += ['sckitaudiosrc.m']

  # Without this, linker complains about a missing framework
  # and errors out with missing gst_core_media_buffer_new() symbol.
  # Don't ask me why this is only needed here and not for the rest of applemedia.
  coreaudiotypes_dep = dependency('CoreAudio', required : applemedia_option)

  sckitsrc_swift = custom_target('sckitsrc-swift',
    input: files(applemedia_swift_sources),
    output: 'libgstsckitsrc.a',
    command: [
      swiftpm, 'build',
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

  xcode_path = run_command('xcode-select', ['--print-path']).stdout().strip()
  sckitsrc_swift_dep = declare_dependency(
    sources: sckitsrc_swift,
    dependencies: [screencapturekit_dep, coreaudiotypes_dep],
    link_args: [
      # libswift_Concurrency is only present as a system lib on macOS 12.0+.
      # We're only running code using it on those versions anyway, but the whole build
      # is targeting older macOS, which causes this lib to be linked with @rpath instead of /usr/lib/swift.
      # To work around this, we weak-link this particular lib and add an RPATH to the swift lib dir.
      '-weak-lswift_Concurrency',
      '-Wl,-rpath,/usr/lib/swift',
      # This is needed for the linker to pick up Swift's compatibility libs.
      '-L' + xcode_path + '/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx',
      '-L/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx',
    ],
  )

  applemedia_swift_deps += [sckitsrc_swift_dep, screencapturekit_dep, coreaudiotypes_dep]
endif

# TODO: Should this be separate?
# run_target('sckitsrc-gen-bridging-header',
#   depends:
# )

gstapplemediaswift = library('gstapplemediaswift',
  applemedia_swift_objc_sources,
  c_args : gst_plugins_bad_args + applemedia_args,
  objc_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args,
  objcpp_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args + applemedia_objcpp_args,
  link_args : [],
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gstpbutils_dep, gst_dep, gstbase_dep, gstgl_dep, gstglproto_dep, gstcodecparsers_dep] + applemedia_frameworks + applemedia_swift_deps,
  override_options : ['cpp_std=c++11'],
  install : true,
  install_dir : plugins_install_dir,
  install_rpath: '/usr/lib/swift',
)

plugins += [gstapplemediaswift]

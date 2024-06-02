# TODO: is this the correct way to make meson notice changes in these files?
sources = [
  'Package.swift',
  'Sources/sckitsrc/sckitaudiosrc.swift',
  'Sources/sckitsrc/macros.swift',
  'Sources/sckitsrc/utils.swift',
  'Sources/gstSwiftMacros/gstSwiftMacros.swift',
  'Sources/gstSwiftMacros/gstDebugMacros.swift',
  'Sources/CGStreamer/gst-shim.h',
  'Sources/CGStreamer/module.modulemap'
]

swiftpm = find_program('swift')

# Without this, linker complains about a missing framework
# and errors out with missing gst_core_media_buffer_new() symbol.
# Don't ask me why this is only needed here and not for the rest of applemedia.
coreaudiotypes_dep = dependency('CoreAudio', required : applemedia_option)

sckitsrc_swift = custom_target('sckitsrc-swift',
    input: files(sources),
    output: 'libgstsckitsrc.a',
    command: [
      swiftpm, 'build',
      # package path = working source dir
      '--package-path', meson.current_source_dir(),
      # build path = output dir
      '--build-path', '@PRIVATE_DIR@',
      '-c', 'debug',
      '&&',
      'cp', '@PRIVATE_DIR@/debug/libgstsckitsrc.a', '@OUTPUT@',
      '&&',
      # This will copy the bridging header to the source dir.
      # "debug" is just a symlink to the correct arch-something-OS triple directory!!
      # FIXME: what if we're cross-compiling?
      # Theoretically its contents should only change when the swift code changes.
      # TODO: Make this a separate run target, similar to GstSharp bindinate calls?
      # FIXME: when swift file gets changed, this sometimes does not make it in time
      # for the ObjC code to pick it up (and complains about missing types etc.)
      'cp', '@PRIVATE_DIR@/debug/sckitsrc.build/sckitsrc-Swift.h', meson.current_source_dir() + '/sckitsrc-Swift.h',
    ],
)

sckitsrc_swift_dep = declare_dependency(
  sources: sckitsrc_swift,
  dependencies: [screencapturekit_dep, coreaudiotypes_dep],
  # FIXME: this is an ugly hardcoded path, there has to be a way to replace it
  link_args: ['-L/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx'],
)

# TODO: Should this be separate?
# run_target('sckitsrc-gen-bridging-header',
#   depends: 
# )

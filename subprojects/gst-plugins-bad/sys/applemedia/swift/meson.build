add_languages('swift', required: true)

# Getting dependencies and building macros is done via SwiftPM
subdir('swiftpm')

applemedia_swift_sources = [
  'sckitsrc/Macros.swift',
  'sckitsrc/Utils.swift',
  'sckitsrc/SCKitAudioSrc.swift',
  'c-bridge.h',
]

applemedia_sources += files('sckitaudiosrc.m')

current_dir = meson.current_build_dir()
applemedia_swift_args = [
  '-Xfrontend', '-load-plugin-executable',
  '-Xfrontend', current_dir + '/swiftpm/libGstSwiftDeps.a.p/' + build_type + '/GstSwiftMacros#GstSwiftMacros',
  '-I' + current_dir + '/swiftpm/libGstSwiftDeps.a.p/' + build_type,
  # FIXME! (idea kinda borrowed from gst-editing-services)
  # How can we access the generated headers correctly? (enumtypes.h etc.)
  '-I' + meson.current_build_dir() + '/../../../../gstreamer',
  '-I' + meson.current_build_dir() + '/../../../../gstreamer/libs',
  '-I' + meson.current_build_dir() + '/../../../../gst-plugins-base',
  '-I' + meson.current_build_dir() + '/../../../../gst-plugins-base/gst-libs',
  # For now, the bridging header is moved to the source dir so it can stay in git or something.
  # Ideally this should be hidden in the build dir somewhere, but that might need Meson-side changes.
  '-emit-objc-header-path', meson.current_source_dir() + '/GstSCKitSrc-Swift.h',
]

xcodeselect = find_program('xcode-select')
if not xcodeselect.found()
  error('xcode-select not found, cannot link Swift libs correctly')
endif
xcode_path = run_command(xcodeselect, ['--print-path']).stdout().strip()

# libswift_Concurrency, needed for async support in Swift, is only present as a system lib on macOS 12.0+.
# We're only running code using it on 12.0+ anyway, but the whole build is targeting an older macOS
# version (10.13), which causes this lib to be linked with @rpath instead of directly to /usr/lib/swift.
# To work around this, we weak-link this particular lib and add an RPATH to the swift lib dir.
# Important: this relies on Meson 1.5.0+ to not remove this rpath upon installation.
applemedia_swift_link_args = [
  '-Wl,-rpath,/usr/lib/swift',
  # This one is needed for the linker to pick up Swift's compatibility libs.
  '-L' + xcode_path + '/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx',
  # Weak link this one so the applemedia plugin won't fail on macOS <12 at runtime.
  '-weak-lswift_Concurrency',
]

gstapplemedia_swift = static_library('gstapplemediaswift',
  applemedia_swift_sources,
  dependencies: [gst_dep, gstbase_dep, gstaudio_dep, applemedia_swiftpm_deps_dep],
  swift_args: applemedia_swift_args,
)

gstapplemedia_swift_dep = declare_dependency(
  link_with: gstapplemedia_swift,
  link_args: applemedia_swift_link_args,
)

applemedia_deps += [gstapplemedia_swift_dep]

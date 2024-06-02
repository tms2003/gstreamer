// swift-tools-version: 5.10
// The swift-tools-version declares the minimum version of Swift required to build this package.

import CompilerPluginSupport
import PackageDescription

let package = Package(
  name: "sckitsrc-swift",
  platforms: [
    .macOS(.v13)
  ],
  products: [
    .library(
      // This is the output lib name (libgstsckitsrc.dylib)
      name: "gstsckitsrc",
      type: .static,
      targets: ["sckitsrc"]),
    .library(
      name: "CGStreamer",
      targets: ["CGStreamer"]),
  ],
  dependencies: [
    .package(
      url: "https://github.com/apple/swift-collections.git",
      .upToNextMinor(from: "1.1.0")  // or `.upToNextMajor
    ),
    .package(url: "https://github.com/apple/swift-syntax", from: "510.0.2"),
  ],
  targets: [
    // Macros have to be a separate target
    .macro(
      name: "gstSwiftMacros",
      dependencies: [
        "CGStreamer",
        .product(name: "SwiftCompilerPlugin", package: "swift-syntax"),
      ]
    ),
    .target(
      name: "sckitsrc",
      dependencies: [
        "gstSwiftMacros",
        "CGStreamer",
        .product(name: "Collections", package: "swift-collections"),
      ],
      // interopMode(C) instead of Cxx because that was causing some extern-related errors
      // https://stackoverflow.com/questions/77203592/xcode-15-import-of-c-module-darwin-c-time-appears-within-extern-c-language
      swiftSettings: [
        .interoperabilityMode(.C)
        // The setting below is how you tell the compiler to just generate the header file exactly where you want it.
        // Otherwise, it will be found under .build/arm64-apple-macosx/debug/sckitsrc.build/.
        // .unsafeFlags(["-emit-clang-header-path", "./sckitsrc-Swift.h"])
      ]
      // you can also do C/Cxx settings here, defines etc.
    ),
    .systemLibrary(
      name: "CGStreamer",
      pkgConfig: "gstreamer-1.0",
      providers: [
        .brew(["gstreamer-1.0"]),
        .apt(["libgstreamer1.0-dev"]),
      ]
    ),
  ]
)

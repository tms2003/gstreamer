## Profiling with Frame Pointers

This document will cover the setup/system required in order to profile an application with `perf` 's frame-pointer backend which is the default, as well as with [Sysprof](https://apps.gnome.org/Sysprof/) which is built on top of that.

It doesn't attempt to explain the reasoning behind the requirement more than the bare minimum, if you are interested in the internal working of the perf stack refer to resources linked through out here.

## Prerequisites:

* Custom C/XXFlag arguments

GCC and Clang default to omitting frame-pointers from the binaries they built unless you explicitly pass `-fno-omit-frame-pointer` and `-mno-omit-leaf-frame-pointer`

If you are targeting `AArch64/armv8` frame pointers are part of the Platform ABI itself so you are covered by that.

Additionally you will also want to compiler with debug symbols enabled as we will see later on.

* Custom RUSTFLAGS

Much like GCC and Clang, the Rust Compiler also defaults to not having frame-pointers by default on most architectures, however the process is a bit more involved here.

Since rust applications usually statically link all their dependencies you will you need to add the following `RUSTFLAG` into your build environment `-C force-frame-pointers=yes`.

However it's a bit more involved than this. Depending on where you get your Rust tool chain, the Standard library may or may not have been built with Frame Pointers and thus you will need to recompile that as well.

If your distribution is adding `force-frame-pointers=yes` and you are using your distribution's package of the `rustc` then it should be okay.

If you are using the binaries provided by the Rust project through [rustup](https://rustup.rs/), those are [currently built](https://github.com/rust-lang/rust/issues/103711) without Frame Pointers and you will need to use an experimental Cargo flag in order to recompile the standard library and bundle it in your application.

`cargo build -Zbuild-std --target=x86_64-unknown-linux-gnu`

For more details about Rust check out [this Blogpost](https://blogs.gnome.org/haeckerfelix/2023/12/03/profiling-rust-applications-with-sysprof/)

* A distribution/runtime environment built with frame pointers in mind.

Most distributions will not add the `CFLAGS` mentioned above, and you ideally need the entire toolchain and your app to be compiled with them, else you will not be able to get frames from `glibc` or similar.

Starting with Fedora 38+, all binaries are built with frame-pointers. Ubuntu 24.04 will also be suitable as described in [this blogpost](https://ubuntu.com/blog/ubuntu-performance-engineering-with-frame-pointers-by-default). Arch Linux is also in the [process](https://gitlab.archlinux.org/archlinux/rfcs/-/merge_requests/26) of building their binaries with Frame Pointers.

Flatpak Runtimes based on of [Freedesktop-Sdk](https://freedesktop-sdk.gitlab.io/) have been built with frame-pointers for a couple of years now. So if you happen to target any of the Freedesktop-Sdk, GNOME, KDE or Elementary Runtimes there shouldn't be anything needed.

* Debug Info to resolve the symbols

Having Frame Pointers will allows to capture frames of the stack, but in order to unwind them into human readable from we will also need to have Debug Information of the entire stack available so we will be able to resolve the symbol names.

Distributions usually have "Debug" packages you can install. Flatpak runtimes usually provide a `.Debug` Extension for both the application and Runtime. Example: for [Glide](https://philn.github.io/glide/) it would be `org.gnome.Sdk.Debug` and `net.baseart.Glide.Debug`.

* Sysprof

Sysprof is a System **sampling** profiler, which means it takes multiple snapshots of the stack each second and tries to determine how often things are used based on the aggregate data.

This is in contrast to a tracing profiler where it will record every function entry and exit, so Sysprof being a sampling profiler has a constant overhead regardless of the code run.

There's a guide on how to use sysprof in the [GNOME Developer Documentation](https://developer.gnome.org/documentation/tools/sysprof.html)

There's also a recommended post in the [Fedora Magazine](https://fedoramagazine.org/performance-profiling-in-fedora-linux/) which dives into details on how Sysprof works internally

FIXME: add an example of sysprofing gst-launch

* Note about Sysprof and Containers

Running sysprof on the host system is the ideal and recommended use case, however sysprof has full support for profiling Flatpak Applications and GNOME Builder has a functional implementation of setting up the Container/Namespace/environment in general and launching the application with sysprof.

Profiling inside OCI/Docker containers is also possible if they meet all the runtime requirements, however note that it's significantly harder to resolve symbols, less tested and you might encounter bugs while doing so.

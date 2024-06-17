This directory contains sources for elements written in Swift, made for macOS/iOS and other Apple platforms.

Currently, it only contains sckitaudiosrc, but hopefully we can add more useful Swift elements in the future.

There isn't a way to directly create an element in Swift, so we have to create it in Obj-C and use a bridging header to call into Swift code from there.

To make this interop possible, the 'GstSCKitSrc-Swift.h` bridging header is created/updated when building the Swift package. It can be imported into the Obj-C code to directly use methods from there in Obj-C.

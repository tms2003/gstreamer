This directory contains sources for the Swift part of the skkitsrc element.

It uses a bit of an experimental approach to make the Swift code usable from our Obj-C element.

To make this interop possible, the 'sckitsrc-Swift.h` bridging header is created on every Meson build. It can be imported into the Obj-C code to use methods from the Swift module.

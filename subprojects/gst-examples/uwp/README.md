# Example app for using GStreamer on UWP

This example shows a basic UWP app that uses GStreamer.

## Requirements

* Visual Studio 2019
  - [x] Universal Windows Platform
  - [x] Windows SDK 10.0.18362 (or newer)
* Git must be available in `PATH`
  - If you don't know how to do that, install
    [git for windows](https://gitforwindows.org/) with the default options.
* [Python 3.6 or newer](https://www.python.org/downloads/windows/)
  - [x] pip3 install pefile
* [GStreamer 1.18 (or newer) UWP binaries](https://gstreamer.freedesktop.org/data/pkg/windows/1.18.0/uwp/)
  - You can also download the uwp+debug-universal tarball for debug binaries
  - Extract each tarball into separate directories. We will call each of these
    "a `prefix`" in the rest of the document. [Example below](#Example).

## Usage

The main project solution is `gst-uwp-example.sln`. Before using it in Visual
Studio, you **MUST** run `update-vcxproj-assets.py` to add the required
gstreamer plugin assets.

```shell
> python update-vcxproj-assets.py --help
usage: update-vcxproj-assets.py [-h] prefix

Rewrite gst-uwp-example.vcxproj and gst-uwp-example.vcxproj.filters 
with the latest assets as loaded in GstWrapper.cpp. Call once for 
each prefix (arch, buildtype) you want to add plugins from.

positional arguments:
  prefix      directory to find all gstreamer plugins in

optional arguments:
  -h, --help  show this help message and exit
```

The script will get the list of required plugin names from `GstWrapper.cpp`,
and it will look inside `prefix` for the actual plugin files and their
dependencies.

You must call it once for each prefix you want to use. 

## Example

This example will assume you're using Git Bash which is shipped with Git for
Windows. You can do the same things using whatever terminal you prefer, but the
commands and syntax will be different of course.

For example, (assuming GStreamer v1.18.0), you can download the release and debug tarballs:
```
gstreamer-1.0-uwp-universal-1.18.0.tar.xz
gstreamer-1.0-uwp+debug-universal-1.18.0.tar.xz
```
Each contains binaries for `arm64`, `x86`, and `x86_64`.

And then you might extract the tarballs:
```sh
$ mkdir gstreamer-uwp
$ cd gstreamer-uwp
$ tar -xf ../gstreamer-1.0-uwp-universal-1.18.0.tar.xz
$ ls
arm64 x86 x86_64
$ cd ..
$ mkdir gstreamer-uwp+debug
$ cd gstreamer-uwp+debug
$ tar -xf ../gstreamer-1.0-uwp+debug-universal-1.18.0.tar.xz
$ ls
arm64 x86 x86_64
$ cd ..
```

Then you would enter the example repo and run the script once for each extracted tarball:
```sh
$ cd gst-uwp-example
$ python update-vcxproj-assets.py ../gstreamer-uwp
$ python update-vcxproj-assets.py ../gstreamer-uwp+debug
```

**That's it!** Now you can open `gst-uwp-example.sln` using Visual Studio and
do whatever you want. 

If you change the list of plugins in `GstWrapper.cpp`, you can always re-run
the script and it will only change the relevant parts of the project build
files.

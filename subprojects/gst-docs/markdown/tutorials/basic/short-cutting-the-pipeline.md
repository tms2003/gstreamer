#  Basic tutorial 8: Short-cutting the pipeline


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

Pipelines constructed with GStreamer do not need to be completely
closed. Data can be injected into the pipeline and extracted from it at
any time, in a variety of ways. This tutorial shows:

  - How to inject external data into a general GStreamer pipeline.

  - How to extract data from a general GStreamer pipeline.

  - How to access and manipulate this data.

[](tutorials/playback/short-cutting-the-pipeline.md) explains
how to achieve the same goals in a playbin-based pipeline.

## Introduction

Applications can interact with the data flowing through a GStreamer
pipeline in several ways. This tutorial describes the easiest one, since
it uses elements that have been created for this sole purpose.

The element used to inject application data into a GStreamer pipeline is
`appsrc`, and its counterpart, used to extract GStreamer data back to
the application is `appsink`. To avoid confusing the names, think of it
from GStreamer's point of view: `appsrc` is just a regular source, that
provides data magically fallen from the sky (provided by the
application, actually). `appsink` is a regular sink, where the data
flowing through a GStreamer pipeline goes to die (it is recovered by the
application, actually).

`appsrc` and `appsink` are so versatile that they offer their own API
(see their documentation), which can be accessed by linking against the
`gstreamer-app` library. In this tutorial, however, we will use a
simpler approach and control them through signals.

`appsrc` can work in a variety of modes: in **pull** mode, it requests
data from the application every time it needs it. In **push** mode, the
application pushes data at its own pace. Furthermore, in push mode, the
application can choose to be blocked in the push function when enough
data has already been provided, or it can listen to the
`enough-data` and `need-data` signals to control flow. This example
implements the latter approach. Information regarding the other methods
can be found in the `appsrc` documentation.

### Buffers

Data travels through a GStreamer pipeline in chunks called **buffers**.
Since this example produces and consumes data, we need to know about
`GstBuffer`s.

Source Pads produce buffers, that are consumed by Sink Pads; GStreamer
takes these buffers and passes them from element to element.

A buffer simply represents a unit of data, do not assume that all
buffers will have the same size, or represent the same amount of time.
Neither should you assume that if a single buffer enters an element, a
single buffer will come out. Elements are free to do with the received
buffers as they please. `GstBuffer`s may also contain more than one
actual memory buffer. Actual memory buffers are abstracted away using
`GstMemory` objects, and a `GstBuffer` can contain multiple `GstMemory` objects.

Every buffer has attached time-stamps and duration, that describe in
which moment the content of the buffer should be decoded, rendered or
displayed. Time stamping is a very complex and delicate subject, but
this simplified vision should suffice for now.

As an example, a `filesrc` (a GStreamer element that reads files)
produces buffers with the “ANY” caps and no time-stamping information.
After demuxing (see [](tutorials/basic/dynamic-pipelines.md))
buffers can have some specific caps, for example “video/x-h264”. After
decoding, each buffer will contain a single video frame with raw caps
(for example, “video/x-raw-yuv”) and very precise time stamps indicating
when should that frame be displayed.

### This tutorial

This tutorial expands [](tutorials/basic/multithreading-and-pad-availability.md) in
two ways: firstly, the `audiotestsrc` is replaced by an `appsrc` that
will generate the audio data. Secondly, a new branch is added to the
`tee` so data going into the audio sink and the wave display is also
replicated into an `appsink`. The `appsink` uploads the information back
into the application, which then just notifies the user that data has
been received, but it could obviously perform more complex tasks.

![](images/tutorials/basic-tutorial-8.png)

## A crude waveform generator

Copy this code into a text file named `basic-tutorial-8.c` (or find it
in your GStreamer installation).

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c }}
{{ END_LANG.md }}

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> `` gcc basic-tutorial-8.c -o basic-tutorial-8 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-audio-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial plays an audible tone for varying frequency through the audio card and opens a window with a waveform representation of the tone. The waveform should be a sinusoid, but due to the refreshing of the window might not appear so.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

The code to create the pipeline (Lines 131 to 205) is an enlarged
version of [Basic tutorial 7: Multithreading and Pad
Availability](tutorials/basic/multithreading-and-pad-availability.md).
It involves instantiating all the elements, link the elements with
Always Pads, and manually link the Request Pads of the `tee` element.

Regarding the configuration of the `appsrc` and `appsink` elements:

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[191:200] }}
{{ END_LANG.md }}

The first property that needs to be set on the `appsrc` is `caps`. It
specifies the kind of data that the element is going to produce, so
GStreamer can check if linking with downstream elements is possible
(this is, if the downstream elements will understand this kind of data).
This property must be a `GstCaps` object, which is easily built from a
string with `gst_caps_from_string()`.

We then connect to the `need-data` and `enough-data` signals. These are
fired by `appsrc` when its internal queue of data is running low or
almost full, respectively. We will use these signals to start and stop
(respectively) our signal generation process.

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[202:206] }}
{{ END_LANG.md }}

Regarding the `appsink` configuration, we connect to the
`new-sample` signal, which is emitted every time the sink receives a
buffer. Also, the signal emission needs to be enabled through the
`emit-signals` property, because, by default, it is disabled.

Starting the pipeline, waiting for messages and final cleanup is done as
usual. Let's review the callbacks we have just
registered:

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[81:90] }}
{{ END_LANG.md }}

This function is called when the internal queue of `appsrc` is about to
starve (run out of data). The only thing we do here is register a GLib
idle function with `g_idle_add()` that feeds data to `appsrc` until it
is full again. A GLib idle function is a method that GLib will call from
its main loop whenever it is “idle”, this is, when it has no
higher-priority tasks to perform. It requires a GLib `GMainLoop` to be
instantiated and running, obviously.

This is only one of the multiple approaches that `appsrc` allows. In
particular, buffers do not need to be fed into `appsrc` from the main
thread using GLib, and you do not need to use the `need-data` and
`enough-data` signals to synchronize with `appsrc` (although this is
allegedly the most convenient).

We take note of the sourceid that `g_idle_add()` returns, so we can
disable it
later.

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[92:102] }}
{{ END_LANG.md }}

This function is called when the internal queue of `appsrc` is full
enough so we stop pushing data. Here we simply remove the idle function
by using `g_source_remove()` (The idle function is implemented as a
`GSource`).

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[29:64] }}
{{ END_LANG.md }}

This is the function that feeds `appsrc`. It will be called by GLib at
times and rates which are out of our control, but we know that we will
disable it when its job is done (when the queue in `appsrc` is full).

Its first task is to create a new buffer with a given size (in this
example, it is arbitrarily set to 1024 bytes) with
`gst_buffer_new_and_alloc()`.

We count the number of samples that we have generated so far with the
`CustomData.num_samples` variable, so we can time-stamp this buffer
using the `GST_BUFFER_TIMESTAMP` macro in `GstBuffer`.

Since we are producing buffers of the same size, their duration is the
same and is set using the `GST_BUFFER_DURATION` in `GstBuffer`.

`gst_util_uint64_scale()` is a utility function that scales (multiply
and divide) numbers which can be large, without fear of overflows.

In order access the memory of the buffer you first have to map it with
`gst_buffer_map()`, which will give you a pointer and a size inside the
`GstMapInfo` structure which `gst_buffer_map()` will populate on success.
Be careful not to write past the end of the buffer: you allocated it,
so you know its size in bytes and samples.

We will skip over the waveform generation, since it is outside the scope
of this tutorial (it is simply a funny way of generating a pretty
psychedelic wave).

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[67:71] }}
{{ END_LANG.md }}

Note that there is also `gst_app_src_push_buffer()` as part of the
`gstreamer-app-1.0` library, which is perhaps a better function to use
to push a buffer into appsrc than the signal emission above, because it has
a proper type signature so it's harder to get wrong. However, be aware
that if you use `gst_app_src_push_buffer()` it will take ownership of the
buffer passed instead, so in that case you won't have to unref it after pushing. 

Once we have the buffer ready, we pass it to `appsrc` with the
`push-buffer` action signal (see information box at the end of [](tutorials/playback/playbin-usage.md)), and then
`gst_buffer_unref()` it since we no longer need it.

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-8.c[104:120] }}
{{ END_LANG.md }}

Finally, this is the function that gets called when the
`appsink` receives a buffer. We use the `pull-sample` action signal to
retrieve the buffer and then just print some indicator on the screen.

Note that there is also `gst_app_src_pull_sample()` as part of the
`gstreamer-app-1.0` library, which is perhaps a better function to use
to pull a sample/buffer out of an appsink than the signal emission above,
because it has a proper type signature so it's harder to get wrong.

In order to get to the data pointer we need to use `gst_buffer_map()` just
like above, which will populate a `GstMapInfo` helper struct with a pointer to
the data and the size of the data in bytes. Don't forget to `gst_buffer_unmap()`
the buffer again when done with the data.

Remember that this buffer does not have to match the buffer that we produced in
the `push_data` function, any element in the path could have altered the
buffers in any way (Not in this example: there is only a `tee` in the
path between `appsrc` and `appsink`, and the `tee` does not change the content
of the buffers).

We then `gst_sample_unref()` the retrieved sample, and this tutorial is done.

## Conclusion

This tutorial has shown how applications can:

  - Inject data into a pipeline using the `appsrc`element.
  - Retrieve data from a pipeline using the `appsink` element.
  - Manipulate this data by accessing the `GstBuffer`.

In a playbin-based pipeline, the same goals are achieved in a slightly
different way. [](tutorials/playback/short-cutting-the-pipeline.md) shows
how to do it.

It has been a pleasure having you here, and see you soon!

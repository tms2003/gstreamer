#!/usr/bin/env python3
import sys
import gi
import logging
import signal

gi.require_version("GLib", "2.0")
gi.require_version("GObject", "2.0")
gi.require_version("Gst", "1.0")

from gi.repository import Gst, GLib, GObject


logging.basicConfig(level=logging.DEBUG, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)


def tutorial():
    # Create the elements
    class CustomData:
        def __init__(self):
            self.source = Gst.ElementFactory.make("uridecodebin", "source")
            self.convert = Gst.ElementFactory.make("audioconvert", "convert")
            self.resample = Gst.ElementFactory.make("audioresample", "resample")
            self.sink = Gst.ElementFactory.make("autovideosink", "sink")
            self.pipeline = None

    data = CustomData()

    # Create the empty pipeline
    data.pipeline = Gst.Pipeline.new("test-pipeline")

    if not data.pipeline or not data.source or not data.convert or not data.resample or not data.sink:
        logger.error("Not all elements could be created.")
        return 1

    # Build the pipeline. Note that we are NOT linking the source at this
    # point. We will do it later.

    data.pipeline.add(data.source)
    data.pipeline.add(data.convert)
    data.pipeline.add(data.resample)
    data.pipeline.add(data.sink)
    if not data.convert.link(data.resample) or not data.resample.link(data.sink):
        logger.error("Elements could not be linked.")
        return 1

    # Set the URI to play
    # FIXME this doesn't work
    # data.source.uri = "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"
    data.source.set_property('uri', "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm")

    # Connect to the pad-added signal
    def pad_added_handler(src, new_pad, data):
        logger.info("Received new pad '{}' from '{}'".format(new_pad.name, src.name))

        sink_pad = data.convert.get_static_pad('sink')
        if sink_pad.is_linked():
            logger.info("We are already linked.  Ignoring.")
            return

        # FIXME This is not supposed to return EMPTY
        logger.info("sink caps {}".format(sink_pad.query_caps().to_string()))

        new_pad_caps = new_pad.get_current_caps()
        new_pad_struct = new_pad_caps.get_structure(0)
        new_pad_type = new_pad_struct.get_name()
        if not new_pad_type.startswith("audio/x-raw"):
            logger.info("It has type '{}' which is not raw audio. Ignoring.".format(
                new_pad_type))
            return

        ret = new_pad.link(sink_pad)
        if ret != Gst.PadLinkReturn.OK:
            logger.info("Type is '{}' but link failed.".format(new_pad_type))
        else:
            logger.info("Link succeeded (type '{}')".format(new_pad_type))

    data.source.connect('pad-added', pad_added_handler, data)

    # Start playing
    ret = data.pipeline.set_state(Gst.State.PLAYING)
    if ret == Gst.StateChangeReturn.FAILURE:
        logger.error("Unable to set the pipeline to the playing state.")
        return 1

    # Wait for EOS or error
    bus = data.pipeline.get_bus()
    while True:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR | Gst.MessageType.EOS)

        # Parse message
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                err, debug_info = msg.parse_error()
                logger.error("Error received from element {}: {}".format(
                             msg.src.get_name(), err.message))
                logger.error("Debugging information: {}".format(debug_info or 'none'))
                break
            elif msg.type == Gst.MessageType.EOS:
                logger.info("End-Of-Stream reached.")
                break
            elif msg.type == Gst.MessageType.STATE_CHANGED:
                if msg.src == data.pipeline:
                    old_state, new_state, pending_state = msg.parse_state_changed()
                    logger.info("Pipeline state changed from {} to {}".format(
                        Gst.Element.state_get_name(old_state),
                        Gst.Element.state_get_name(new_state)))
            else:
                # This should not happen as we only asked for ERRORs and EOS
                logger.error("Unexpected message received.")

    data.pipeline.set_state(Gst.State.NULL)


if __name__ == '__main__':
    # initialize GStreamer
    Gst.init(sys.argv[1:])
    ret = tutorial()
    sys.exit(ret)

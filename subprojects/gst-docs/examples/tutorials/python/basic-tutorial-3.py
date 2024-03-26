#!/usr/bin/env python3
import sys
import gi
import logging

gi.require_version("GLib", "2.0")
gi.require_version("GObject", "2.0")
gi.require_version("Gst", "1.0")

from gi.repository import Gst

logging.basicConfig(level=logging.DEBUG, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)

TERMINATE = False

# Initialize GStreamer
Gst.init(sys.argv[1:])

# Create the elements
source = Gst.ElementFactory.make("uridecodebin", "source")
convert = Gst.ElementFactory.make("audioconvert", "convert")
resample = Gst.ElementFactory.make("audioresample", "resample")
sink = Gst.ElementFactory.make("autoaudiosink", "sink")

# Create the empty pipeline
pipeline = Gst.Pipeline.new("test-pipeline")

if not source or not convert or not resample or not sink or not pipeline:
    logger.error("Not all elements could be created.")
    sys.exit(1)

pipeline.add(source)
pipeline.add(convert)
pipeline.add(resample)
pipeline.add(sink)
if not convert.link(resample) or not resample.link(sink):
    logger.error("Elements could not be linked.")
    sys.exit(1)

# Set the URI to play
source.props.uri = "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"

@staticmethod
def pad_added_handler(src, new_pad):
    sink_pad = convert.get_static_pad("sink")
    
    logger.info(f"Received new pad '{new_pad.get_name()}' from '{src.get_name()}':")
    
    # If our converter is already linked, we have nothing to do here
    if sink_pad.is_linked():
        logger.error("We are already linked. Ignoring.")
        return

    # Check the new pad's type 
    new_pad_caps = new_pad.get_current_caps()
    new_pad_struct = new_pad_caps.get_structure(0)
    new_pad_type: str = new_pad_struct.get_name()
    if not new_pad_type.startswith("audio/x-raw"):
        logger.info(f"It has type '{new_pad_type}' which is not raw audio. Ignoring.")
        return
    
    # Attempt the link 
    ret = new_pad.link(sink_pad)
    if ret != 0:
        logger.error(f"Type is '{new_pad_type}' but link failed.")
    else:
        logger.info(f"Link succeeded (type '{new_pad_type}').")

# Connect to the pad-added signal 
source.connect("pad-added", pad_added_handler)

# Start playing
ret = pipeline.set_state(Gst.State.PLAYING)
if ret == Gst.StateChangeReturn.FAILURE:
    logger.error("Unable to set the pipeline to the playing state.")
    sys.exit(1)
    
# Listen to the bus
bus = pipeline.get_bus()

while True:
    msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR | Gst.MessageType.EOS)

    # Parse message
    if msg:
        match msg.type:
            case Gst.MessageType.ERROR:
                err, debug_info = msg.parse_error()
                logger.error(f"Error received from element {msg.src.get_name()}: {err.message}")
                logger.error(f"Debugging information: {debug_info if debug_info else 'none'}")
                TERMINATE = True
            case Gst.MessageType.EOS:
                logger.info("End-Of-Stream reached.")
                TERMINATE = True
            case Gst.MessageType.STATE_CHANGED:
                # We are only interested in state-changed messages from the pipeline
                if msg.src == pipeline:
                    old_state, new_state, pending_state = msg.parse_state_changed()
                    logger.info(f"Pipeline state changed from {pipeline.state_get_name(old_state)} to {pipeline.state_get_name(new_state)}:")
            case _:
                logger.error("Unexpected message received.")
    
    if TERMINATE:
        break

# Free resources
pipeline.set_state(Gst.State.NULL)
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
            self.playbin = Gst.ElementFactory.make("playbin", "playbin")
            self.playing = False
            self.terminate = False
            self.seek_enabled = False
            self.seek_done = False
            self.duration = Gst.CLOCK_TIME_NONE

    data = CustomData()

    if not data.playbin:
        logger.error("Not all elements could be created.")
        return 1

    # Set the URI to play
    # data.playbin.uri = "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"
    data.playbin.set_property('uri', "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm")

    # Start playing
    ret = data.playbin.set_state(Gst.State.PLAYING)
    if ret == Gst.StateChangeReturn.FAILURE:
        logger.error("Unable to set the pipeline to the playing state.")
        return 1

    def handle_message(data, msg):
        if msg.type == Gst.MessageType.ERROR:
            err, debug_info = msg.parse_error()
            logger.error("Error received from element {}: {}".format(
                         msg.src.get_name(), err.message))
            logger.error("Debugging information: {}".format(debug_info or 'none'))
            data.terminate = True
        elif msg.type == Gst.MessageType.EOS:
            logger.info("End-Of-Stream reached.")
            data.terminate = True
        elif msg.type == Gst.MessageType.DURATION_CHANGED:
            # The duration has changed, mark the current one as invalid
            data.duration = Gst.CLOCK_TIME_NONE
        elif msg.type == Gst.MessageType.STATE_CHANGED:
            if msg.src == data.playbin:
                old_state, new_state, pending_state = msg.parse_state_changed()
                logger.info("Pipeline state changed from {} to {}".format(
                    Gst.Element.state_get_name(old_state),
                    Gst.Element.state_get_name(new_state)))

                # Remember whether we are in the PLAYING state or not
                data.playing = (new_state == Gst.State.PLAYING)

                if data.playing:
                    query = Gst.Query.new_seeking(Gst.Format.TIME)
                    if data.playbin.query(query):
                        _, data.seek_enabled, start, end = query.parse_seeking()
                        if data.seek_enabled:
                            logging.info("Seeking is ENABLED from {} to {}".format(start, end))
                        else:
                            logging.info("Seeking is DISABLED for this stream")
                    else:
                        logging.info("Seeking query failed")
        else:
            # This should not happen as we only asked for ERRORs and EOS
            logger.error("Unexpected message received.")

    # Listen to the bus
    bus = data.playbin.get_bus()
    while not data.terminate:
        try:
            msg = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR | Gst.MessageType.EOS | Gst.MessageType.DURATION_CHANGED)

            # Parse message
            if msg:
                handle_message(data, msg)
            else:
                if data.playing:
                    ret, current = data.playbin.query_position(Gst.Format.TIME)
                    if not ret:
                        logger.info("Could not query current position")

                    if data.duration == Gst.CLOCK_TIME_NONE:
                        ret, data.duration = data.playbin.query_duration(Gst.Format.TIME)
                        if not ret:
                            logger.info("Could not query current duration")

                    # Print current position and total duration
                    logger.info("Position {} / {}".format(GST_TIME_FORMAT(current), GST_TIME_FORMAT(data.duration)))

                    # If seeking is enabled, we have not done it yet, and the time is right, seek
                    if data.seek_enabled and not data.seek_done and current > 10 * Gst.SECOND:
                        logging.info("Reached 10s, performing seek...")
                        data.playbin.seek_simple(
                            Gst.Format.TIME,
                            Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
                            30 * Gst.SECOND)
                        data.seek_done = True
        except KeyboardInterrupt:
            logging.info("Received keyboard interrupt")
            data.terminate = True

    data.playbin.set_state(Gst.State.NULL)


def GST_TIME_FORMAT(time):
    if time == Gst.CLOCK_TIME_NONE:
        return "99:99:99.999999999"
    return "%u:%02u:%02u.%09u" % (
        int(time / (Gst.SECOND * 60 * 60)),
        int(time / (Gst.SECOND * 60) % 60),
        int(time / Gst.SECOND % 60),
        int(time % Gst.SECOND))


if __name__ == '__main__':
    # initialize GStreamer
    Gst.init(sys.argv[1:])
    ret = tutorial()
    sys.exit(ret)

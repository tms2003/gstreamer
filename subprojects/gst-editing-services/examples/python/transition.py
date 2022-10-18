#!/usr/bin/env python3
#
# GStreamer
#
# Copyright (C) 2022 Fluendo S.A.
# Copyright (C) 2022 Fabian Orccon <cfoch.fabian@gmail.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
# Boston, MA 02110-1335, USA.
import argparse
import os
import gi

gi.require_version('Gst', '1.0')
gi.require_version('GES', '1.0')

from gi.repository import Gst, GES, GLib  # noqa


DEFAULT_CLIP_DURATION = 5 * Gst.SECOND
DEFAULT_TRANSITION_DURATION = 3 * Gst.SECOND


class Transition:
    def __init__(self, transition_id):
        timeline = GES.Timeline.new()
        video_caps = Gst.Caps.from_string("video/x-raw")
        video_track = GES.Track.new(GES.TrackType.VIDEO, video_caps)
        timeline.add_track(video_track)
        layer = timeline.append_layer()

        clip1 = GES.TestClip()
        clip1.props.start = 0
        clip1.props.duration = DEFAULT_CLIP_DURATION
        clip1.props.vpattern = GES.VideoTestPattern.RED
        clip1.props.priority = 1

        transition_asset = GES.Asset.request(GES.TransitionClip, transition_id)
        transition_clip = GES.TransitionClip()
        transition_clip.set_asset(transition_asset)
        transition_clip.props.start =\
            clip1.props.duration - DEFAULT_TRANSITION_DURATION
        transition_clip.props.duration = DEFAULT_TRANSITION_DURATION
        transition_clip.props.in_point = 0

        clip2 = GES.TestClip()
        clip2.props.start = transition_clip.props.start
        clip2.props.duration = DEFAULT_CLIP_DURATION
        clip2.props.vpattern = GES.VideoTestPattern.BLUE
        clip2.props.priority = 2

        layer.add_clip(clip1)
        layer.add_clip(clip2)
        layer.add_clip(transition_clip)

        self.pipeline = pipeline = GES.Pipeline()
        pipeline.set_timeline(timeline)
        pipeline.set_state(Gst.State.PLAYING)
        bus = pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.bus_message_cb)

        self.loop = GLib.MainLoop()

    def bus_message_cb(self, unused_bus, message):
        if message.type == Gst.MessageType.EOS:
            print("eos")
            self.loop.quit()
        elif message.type == Gst.MessageType.ERROR:
            error = message.parse_error()
            print("error %s" % error[1])
            self.loop.quit()

    def start(self):
        self.loop.run()


Gst.init(None)
GES.init()

transition_choices = [asset.props.id
                      for asset in GES.list_assets(GES.TransitionClip)]

parser = argparse.ArgumentParser()
parser.add_argument('--transition', default=transition_choices[0],
                    choices=transition_choices, required=False)
args = parser.parse_args()

transition = Transition(args.transition)
transition.start()

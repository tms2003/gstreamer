/* GStreamer unit tests for decodebin3
 * Copyright (C) 2022 Aleksandr Slobodeniuk <aslobodeniuk@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Included into decodebin3.c */

/* Fake demuxer for decodebin3 demuxer test */
static GType gst_fake_tsdemux_get_type (void);

typedef struct _GstFakeTsdemux GstFakeTsdemux;
typedef GstElementClass GstFakeTsdemuxClass;

struct _GstFakeTsdemux
{
  GstElement parent;

  GstPad *video_pad;
  GstPad *audio_pad;
};

G_DEFINE_TYPE (GstFakeTsdemux, gst_fake_tsdemux, GST_TYPE_ELEMENT);

static GstStaticPadTemplate gst_fake_tsdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_fake_tsdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_fake_tsdemux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );

static GstStateChangeReturn
gst_fake_tsdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstFakeTsdemux *self = (GstFakeTsdemux *) element;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->video_pad) {
        gst_element_remove_pad (element, self->video_pad);
        self->video_pad = NULL;
      }

      if (self->audio_pad) {
        gst_element_remove_pad (element, self->audio_pad);
        self->audio_pad = NULL;
      }

      break;
    default:
      break;
  }

  return
      GST_ELEMENT_CLASS (gst_fake_tsdemux_parent_class)->change_state (element,
      transition);
}

static void
gst_fake_tsdemux_class_init (GstFakeTsdemuxClass * klass)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_fake_tsdemux_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_fake_tsdemux_videosrc_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_fake_tsdemux_audiosrc_template);
  gst_element_class_set_metadata (element_class, "FakeTsdemux", "Codec/Demuxer",
      "yep", "me");

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_fake_tsdemux_change_state);
}

static gboolean
gst_fake_tsdemux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFakeTsdemux *self = (GstFakeTsdemux *) parent;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (!gst_pad_push_event (self->video_pad, gst_event_new_eos ()))
        return FALSE;
      if (!gst_pad_push_event (self->audio_pad, gst_event_new_eos ()))
        return FALSE;
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static GstFlowReturn
gst_fake_tsdemux_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFakeTsdemux *self = (GstFakeTsdemux *) parent;
  GstFlowReturn ret = GST_FLOW_OK;
  GstSegment segment;

  if (!self->video_pad) {
    GstCaps *caps;

    self->video_pad =
        gst_pad_new_from_static_template (&gst_fake_tsdemux_videosrc_template,
        "video_src");
    gst_element_add_pad (GST_ELEMENT (self), self->video_pad);

    gst_pad_push_event (self->video_pad, gst_event_new_stream_start ("video"));
    caps = gst_caps_new_simple ("video/x-h264", NULL, NULL);
    gst_pad_push_event (self->video_pad, gst_event_new_caps (caps));
    gst_caps_unref (caps);
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (self->video_pad, gst_event_new_segment (&segment));
  }

  if (!self->audio_pad) {
    GstCaps *caps;

    self->audio_pad =
        gst_pad_new_from_static_template (&gst_fake_tsdemux_videosrc_template,
        "audio_src");
    gst_element_add_pad (GST_ELEMENT (self), self->audio_pad);

    gst_element_no_more_pads (GST_ELEMENT (self));

    gst_pad_push_event (self->audio_pad, gst_event_new_stream_start ("audio"));
    caps =
        gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4, NULL);
    gst_pad_push_event (self->audio_pad, gst_event_new_caps (caps));
    gst_caps_unref (caps);
    gst_pad_push_event (self->audio_pad, gst_event_new_segment (&segment));
  }

  ret = gst_pad_push (self->video_pad, gst_buffer_ref (buf));
  if (ret == GST_FLOW_OK) {
    ret = gst_pad_push (self->audio_pad, gst_buffer_ref (buf));
  }

  gst_buffer_unref (buf);
  return ret;
}

static void
gst_fake_tsdemux_init (GstFakeTsdemux * self)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_static_template (&gst_fake_tsdemux_sink_template,
      "sink");
  gst_pad_set_event_function (pad, gst_fake_tsdemux_sink_event);
  gst_pad_set_chain_function (pad, gst_fake_tsdemux_sink_chain);
  gst_element_add_pad (GST_ELEMENT (self), pad);
}

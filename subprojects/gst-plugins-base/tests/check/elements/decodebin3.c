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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/base/gstbaseparse.h>

#include "fakeelements/fakeh264parse.c"
#include "fakeelements/fakeaacparse.c"
#include "fakeelements/faketsdemux.c"

static struct
{
  gint pads_added;
  gboolean received_data;
} fixture_demuxer;

#undef FIXTURE
#define FIXTURE fixture_demuxer

static gboolean
caps_are_parsed (GstCaps * caps)
{
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field (s, "stream-format")
      || gst_structure_has_field (s, "framed")) {
    return TRUE;
  }

  return FALSE;
}

static GstPadProbeReturn
test_demuxer_buffer_cb (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data)
{
  GstCaps *caps;

  caps = gst_pad_get_current_caps (pad);
  fail_unless (caps_are_parsed (caps));
  gst_caps_unref (caps);

  FIXTURE.received_data = TRUE;

  return GST_PAD_PROBE_OK;
}

static void
test_demuxer_pad_added_cb (GstElement * dec, GstPad * pad, gpointer user_data)
{
  GstBin *pipe = user_data;
  GstElement *sink;
  GstPad *sinkpad;

  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add (pipe, sink);
  gst_element_sync_state_with_parent (sink);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      test_demuxer_buffer_cb, NULL, NULL);

  g_atomic_int_inc (&FIXTURE.pads_added);
}

static gboolean
test_demuxer_autoplug_continue_cb (GstElement * element, GstPad * pad,
    GstCaps * caps, gpointer user_data)
{
  GST_INFO ("pad=%" GST_PTR_FORMAT " caps = %" GST_PTR_FORMAT, pad, caps);

  if (caps_are_parsed (caps)) {
    GST_INFO ("Return FALSE");
    return FALSE;
  }

  return TRUE;
}


GST_START_TEST (test_demuxer)
{
  GstStateChangeReturn sret;
  GstMessage *msg;
  GstCaps *caps;
  GstElement *pipe, *src, *filter, *dec;

  GST_INFO ("New test: demux fake ts stream with audio and video.");
  memset (&FIXTURE, 0, sizeof (FIXTURE));

  gst_element_register (NULL, "fakeh264parse", GST_RANK_PRIMARY + 101,
      gst_fake_h264_parser_get_type ());
  gst_element_register (NULL, "fakeaacparse", GST_RANK_PRIMARY + 101,
      gst_fake_aac_parser_get_type ());
  gst_element_register (NULL, "faketsdemux", GST_RANK_PRIMARY + 100,
      gst_fake_tsdemux_get_type ());

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src != NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 10, "sizetype", 2, "filltype", 2,
      "can-activate-pull", FALSE, NULL);

  filter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (filter != NULL);
  caps = gst_caps_from_string ("video/mpegts");
  g_object_set (G_OBJECT (filter), "caps", caps, NULL);
  gst_caps_unref (caps);

  dec = gst_element_factory_make ("decodebin3", NULL);
  fail_unless (dec != NULL);

  g_signal_connect (dec, "pad-added",
      G_CALLBACK (test_demuxer_pad_added_cb), pipe);
  g_signal_connect (dec, "autoplug-continue",
      G_CALLBACK (test_demuxer_autoplug_continue_cb), pipe);

  gst_bin_add_many (GST_BIN (pipe), src, filter, dec, NULL);
  gst_element_link_many (src, filter, dec, NULL);

  sret = gst_element_set_state (pipe, GST_STATE_PLAYING);
  fail_unless_equals_int (sret, GST_STATE_CHANGE_SUCCESS);

  /* wait for EOS or error */
  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  fail_unless_equals_int (FIXTURE.pads_added, 2);
  fail_unless (FIXTURE.received_data);
  GST_INFO ("test finished ok");
}

GST_END_TEST;

static struct
{
  gint ap_received;
  gint ap_expected;
  GstCaps *expected_caps;
  gboolean check_interruption;
} fixture_parser_negotiation;

#undef FIXTURE
#define FIXTURE fixture_parser_negotiation

static const gint FIXTURE_total_ap = 3;

static GstPadProbeReturn
parser_negotiation_buffer_cb (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data)
{
  GstCaps *caps;

  GST_INFO ("pad = [%" GST_PTR_FORMAT "]", pad);

  caps = gst_pad_get_current_caps (pad);
  fail_unless (gst_caps_is_equal (FIXTURE.expected_caps, caps));
  gst_caps_unref (caps);

  return GST_PAD_PROBE_OK;
}

static void
parser_negotiation_pad_added_cb (GstElement * dec, GstPad * pad,
    gpointer user_data)
{
  GstBin *pipe = user_data;
  GstElement *sink;
  GstPad *sinkpad;

  GST_INFO ("pad = [%" GST_PTR_FORMAT "]", pad);

  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add (pipe, sink);
  gst_element_sync_state_with_parent (sink);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      parser_negotiation_buffer_cb, NULL, NULL);
}

static gboolean
parser_negotiation_autoplug_continue_cb (GstElement * element, GstPad * pad,
    GstCaps * caps, gpointer user_data)
{
  GST_INFO ("pad = [%" GST_PTR_FORMAT "] caps = [%" GST_PTR_FORMAT "]", pad,
      caps);

  FIXTURE.ap_received++;

  if (FIXTURE.ap_received == FIXTURE.ap_expected) {
    /* capture caps that are going to be exposed */
    FIXTURE.expected_caps = gst_caps_ref (caps);

    if (FIXTURE.check_interruption)
      return FALSE;
  }

  return TRUE;
}

static void
test_parser_negotiation_exec (gint stop_autoplugging_at)
{
  GstStateChangeReturn sret;
  GstMessage *msg;
  GstCaps *caps;
  GstElement *pipe, *src, *filter, *dec;

  GST_INFO ("New test: must stop at %d", stop_autoplugging_at);

  memset (&FIXTURE, 0, sizeof (FIXTURE));
  FIXTURE.ap_expected = FIXTURE_total_ap;
  if (stop_autoplugging_at) {
    FIXTURE.ap_expected = stop_autoplugging_at;
    /* If we don't stop autoplugging, exposed caps will be
     * the raw caps after the decoder. Currently decodebin3 doesn't
     * emit autoplug-continue for this raw caps. */
    FIXTURE.check_interruption = TRUE;
  }

  gst_element_register (NULL, "fakeh264parse", GST_RANK_PRIMARY + 101,
      gst_fake_h264_parser_get_type ());
  gst_element_register (NULL, "fakeh264dec", GST_RANK_PRIMARY + 100,
      gst_fake_h264_decoder_get_type ());

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src != NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 5, "sizetype", 2, "filltype", 2,
      "can-activate-pull", FALSE, NULL);

  filter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (filter != NULL);
  caps = gst_caps_from_string ("video/x-h264");
  g_object_set (G_OBJECT (filter), "caps", caps, NULL);
  gst_caps_unref (caps);

  dec = gst_element_factory_make ("decodebin3", NULL);
  fail_unless (dec != NULL);

  g_signal_connect (dec, "pad-added",
      G_CALLBACK (parser_negotiation_pad_added_cb), pipe);

  g_signal_connect (dec, "autoplug-continue",
      G_CALLBACK (parser_negotiation_autoplug_continue_cb), pipe);

  gst_bin_add_many (GST_BIN (pipe), src, filter, dec, NULL);
  gst_element_link_many (src, filter, dec, NULL);

  GST_INFO ("Start playback");
  sret = gst_element_set_state (pipe, GST_STATE_PLAYING);
  fail_unless (sret == GST_STATE_CHANGE_SUCCESS ||
      sret == GST_STATE_CHANGE_ASYNC, "sret = %d");

  /* wait for EOS or error */
  GST_INFO ("Start waiting for EOS");
  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  GST_INFO ("Stopping");
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  fail_unless_equals_int (FIXTURE.ap_received, FIXTURE.ap_expected);

  gst_caps_unref (FIXTURE.expected_caps);
  GST_INFO ("test finished ok");
}

GST_START_TEST (test_parser_negotiation)
{
  gint i;
  for (i = 0; i < FIXTURE_total_ap; i++) {
    test_parser_negotiation_exec (i);
  }
}

GST_END_TEST;

static Suite *
decodebin3_suite (void)
{
  Suite *s = suite_create ("decodebin3");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_demuxer);
  tcase_add_test (tc_chain, test_parser_negotiation);

  return s;
}

GST_CHECK_MAIN (decodebin3);

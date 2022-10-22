/* GStreamer unit tests for avdemux_g723_1
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
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#define SIMPLE_G723_1_PATH GST_TEST_FILES_PATH G_DIR_SEPARATOR_S "sine.g723_1"

static void
process_added_demuxer_pad (GstElement * demuxer, GstPad * pad,
    GstBin * pipeline)
{
  GstElement *decoder = gst_bin_get_by_name (pipeline, "avdec_g723_1");
  fail_if (decoder == NULL);
  fail_unless (gst_element_link (demuxer, decoder));
  gst_object_unref (decoder);
}

static GstElement *
create_file_pipeline (const char *path, GstPadMode mode)
{
  GstElement *pipeline = gst_pipeline_new ("pipeline");
  fail_if (pipeline == NULL);

  GstElement *src = gst_element_factory_make ("filesrc", "filesrc");
  fail_if (src == NULL);
  g_object_set (src, "location", path, NULL);

  GstElement *demuxer = gst_element_factory_make ("avdemux_g723_1",
      "avdemux_g723_1");
  fail_if (demuxer == NULL);
  g_signal_connect (demuxer, "pad-added",
      G_CALLBACK (process_added_demuxer_pad), pipeline);

  GstElement *decoder = gst_element_factory_make ("avdec_g723_1",
      "avdec_g723_1");
  fail_if (decoder == NULL);

  GstElement *sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_if (sink == NULL);

  if (mode == GST_PAD_MODE_PUSH) {
    GstElement *queue = gst_element_factory_make ("queue", "queue");
    gst_bin_add_many (GST_BIN (pipeline), src, queue, demuxer, decoder, sink,
        NULL);
    fail_unless (gst_element_link_many (src, queue, demuxer, NULL));
  } else {
    gst_bin_add_many (GST_BIN (pipeline), src, demuxer, decoder, sink, NULL);
    fail_unless (gst_element_link (src, demuxer));
  }
  fail_unless (gst_element_link (decoder, sink));

  return pipeline;
}

static void
do_test_simple_file (GstPadMode mode)
{
  GstElement *pipeline = create_file_pipeline (SIMPLE_G723_1_PATH, mode);

  GstStateChangeReturn ret = gst_element_set_state (pipeline,
      GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  GstMessage *msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_simple_file_pull)
{
  do_test_simple_file (GST_PAD_MODE_PULL);
}

GST_END_TEST;

GST_START_TEST (test_simple_file_push)
{
  do_test_simple_file (GST_PAD_MODE_PUSH);
}

GST_END_TEST;

static Suite *
avdemux_g723_1_suite (void)
{
  Suite *s = suite_create ("avdemux_g723_1");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_file_pull);
  tcase_add_test (tc_chain, test_simple_file_push);

  return s;
}

GST_CHECK_MAIN (avdemux_g723_1)

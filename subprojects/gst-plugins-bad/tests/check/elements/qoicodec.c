/* GStreamer
 * unit test for qoicodec
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/app/gstappsink.h>

static void
run_test (const gchar * pipeline_string)
{
  GstElement *pipeline, *result_sink, *original_sink;
  GstSample *result_sample, *original_sample;
  GstBuffer *result_buffer, *original_buffer;
  GstMapInfo info;
  GST_DEBUG ("Testing pipeline '%s'", pipeline_string);

  pipeline = gst_parse_launch (pipeline_string, NULL);
  fail_unless (pipeline != NULL);

  result_sink = gst_bin_get_by_name (GST_BIN (pipeline), "result_sink");
  fail_unless (result_sink != NULL);

  original_sink = gst_bin_get_by_name (GST_BIN (pipeline), "original_sink");
  fail_unless (original_sink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  result_sample = gst_app_sink_pull_sample (GST_APP_SINK (result_sink));
  fail_unless (GST_IS_SAMPLE (result_sample));

  original_sample = gst_app_sink_pull_sample (GST_APP_SINK (original_sink));
  fail_unless (GST_IS_SAMPLE (original_sample));

  result_buffer = gst_sample_get_buffer (result_sample);
  fail_unless (result_buffer != NULL);

  original_buffer = gst_sample_get_buffer (original_sample);
  fail_unless (original_buffer != NULL);

  gst_buffer_map (result_buffer, &info, (GstMapFlags) (GST_MAP_READ));

  // Lossless compression will give same result after encoding->decoding
  gst_check_buffer_data (original_buffer, info.data, info.size);

  gst_sample_unref (result_sample);
  gst_sample_unref (original_sample);

  /* wait for EOS */
  result_sample = gst_app_sink_pull_sample (GST_APP_SINK (result_sink));
  fail_unless (result_sample == NULL);
  fail_unless (gst_app_sink_is_eos (GST_APP_SINK (result_sink)));

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

}

GST_START_TEST (test_qoicodec)
{
  gchar *pipeline;

  pipeline =
      g_strdup_printf
      ("videotestsrc num-buffers=1 ! tee name=split split. ! queue ! video/x-raw,width=1280,height=720,format=RGB ! qoienc ! qoidec ! appsink name=result_sink split. ! queue ! appsink name=original_sink");

  run_test (pipeline);
  g_free (pipeline);
}

GST_END_TEST;

static Suite *
qoicodec_suite (void)
{
  Suite *s = suite_create ("qoicodec");
  TCase *tc_basic = tcase_create ("general");

  suite_add_tcase (s, tc_basic);
  tcase_add_test (tc_basic, test_qoicodec);

  return s;
}

GST_CHECK_MAIN (qoicodec);

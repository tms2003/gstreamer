/* GStreamer
 *
 * Copyright (C) 2020 LTN Global Communications
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

// File generated with:
// gst-launch-1.0 videotestsrc num-buffers=1 ! "video/x-raw,width=64,height=64,format=AYUV" ! filesink location=frame.ayuv
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define DATADIR STRINGIFY(GAUSSBLUR_DATADIR)
#define FRAME_FILENAME DATADIR "/frame.ayuv"

#define TARGET_1_2_GB_CHECKSUM "efd5ebf58428a40cbbfe1ece0d6ac6ae"
#define TARGET_2_0_GB_CHECKSUM "0cfc409735cb760c558bd90d275d9b33"
#define TARGET__2_0_GB_CHECKSUM "0e72c590c53f6281a215bc0985a17fd8"

static GstBuffer *
_buffer_from_file (const gchar * filename)
{
  GstBuffer *buffer;
  gchar *contents = NULL;
  gsize length = 0;

  fail_unless (g_file_get_contents (filename, &contents, &length, NULL));

  buffer = gst_buffer_new_wrapped (contents, length);
  GST_BUFFER_OFFSET (buffer) = 0;

  return buffer;
}

static void
_check_gaussblur (const float sigma, const char *target_checksum)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gchar *checksum;
  int i;
  const int max_frames = 5;

  gchar *pipeline = g_strdup_printf ("gaussianblur sigma=%f", sigma);

  h = gst_harness_new_parse (pipeline);

  g_free (pipeline);
  ck_assert (h);

  gst_harness_set_src_caps_str (h,
      "video/x-raw,width=64,height=64,format=AYUV");
  gst_harness_set_sink_caps_str (h, "video/x-raw,format=AYUV");

  in_buf = _buffer_from_file (FRAME_FILENAME);

  for (i = 0; i < max_frames; i++) {
    ret = gst_harness_push (h, gst_buffer_ref (in_buf));
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    out_buf = gst_harness_try_pull (h);
    fail_unless (out_buf != NULL, "No output buffer");

    gst_buffer_map (out_buf, &map, GST_MAP_READ);
    checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
        (const guchar *) map.data, map.size);
    g_print ("Checksum is: %s\n", checksum);
    fail_unless (g_strcmp0 (checksum, target_checksum) == 0,
        "Checksum obtained is %s, while target is %s", checksum,
        target_checksum);
    g_free (checksum);
    gst_buffer_unmap (out_buf, &map);

    gst_buffer_unref (out_buf);
  }

  gst_buffer_unref (in_buf);

  gst_harness_teardown (h);
}

static void
_check_gaussblur_with_threads (const unsigned int threads_val)
{
  gchar *threads = g_strdup_printf ("%d", threads_val);
  g_setenv ("OMP_NUM_THREADS", threads, TRUE);
  g_free (threads);

  _check_gaussblur (1.2, TARGET_1_2_GB_CHECKSUM);
  _check_gaussblur (2, TARGET_2_0_GB_CHECKSUM);
  _check_gaussblur (-2, TARGET__2_0_GB_CHECKSUM);
}

GST_START_TEST (gaussblur_check_frame_1_thread)
{
  _check_gaussblur_with_threads (1);
}

GST_END_TEST;


GST_START_TEST (gaussblur_check_frame_2_thread)
{
  _check_gaussblur_with_threads (2);
}

GST_END_TEST;

GST_START_TEST (gaussblur_check_frame_4_thread)
{
  _check_gaussblur_with_threads (4);
}

GST_END_TEST;

static Suite *
gaussblur_suite (void)
{
  Suite *s = suite_create ("gaussblur");
  TCase *tc;

  tc = tcase_create ("gaussblur");
  suite_add_tcase (s, tc);
  tcase_add_test (tc, gaussblur_check_frame_1_thread);
  tcase_add_test (tc, gaussblur_check_frame_2_thread);
  tcase_add_test (tc, gaussblur_check_frame_4_thread);

  return s;
}

GST_CHECK_MAIN (gaussblur);

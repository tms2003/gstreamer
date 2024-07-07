/* GStreamer unit tests for avparse_g723_1
 *
 * Copyright (C) 2023 Devin Anderson <danderson@microsoft.com>
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

#define MAKE_RESOURCE_PATH(name) GST_TEST_FILES_PATH G_DIR_SEPARATOR_S name

#define SIMPLE_G723_1_PATH MAKE_RESOURCE_PATH("sine.g723_1")

// TODO: It'd be nice to use `GstHarness` for these tests, but I don't think
// it's sufficient currently.  `GstHarness` works quite well when an element is
// operating in push mode (it seems to be built for that use case), but it's
// not clear how go about interacting with `GstHarness` when an element is
// operating in pull mode.

static GstElement *
create_caps_change_base_pipeline (GstElement ** concat, GstElement ** parser)
{
  GstElement *pipeline = gst_pipeline_new ("pipeline");
  fail_if (pipeline == NULL);

  GstElement *src_1 = gst_element_factory_make ("appsrc", "appsrc_1");
  fail_if (src_1 == NULL);

  GstElement *src_2 = gst_element_factory_make ("appsrc", "appsrc_2");
  fail_if (src_2 == NULL);

  *concat = gst_element_factory_make ("concat", "concat");
  fail_if (*concat == NULL);

  *parser = gst_element_factory_make ("avparse_g723_1", "avparse_g723_1");
  fail_if (*parser == NULL);

  GstElement *sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), src_1, src_2, *concat, *parser, sink,
      NULL);

  fail_unless (gst_element_link (src_1, *concat));
  fail_unless (gst_element_link (src_2, *concat));
  fail_unless (gst_element_link (*parser, sink));

  return pipeline;
}

static GstElement *
create_caps_change_pull_pipeline (void)
{
  GstElement *concat;
  GstElement *parser;
  GstElement *pipeline = create_caps_change_base_pipeline (&concat, &parser);

  fail_unless (gst_element_link (concat, parser));

  return pipeline;
}

static GstElement *
create_caps_change_push_pipeline (void)
{
  GstElement *concat;
  GstElement *parser;
  GstElement *pipeline = create_caps_change_base_pipeline (&concat, &parser);

  GstElement *queue = gst_element_factory_make ("queue", "queue");
  fail_if (queue == NULL);

  gst_bin_add (GST_BIN (pipeline), queue);

  fail_unless (gst_element_link_many (concat, queue, parser, NULL));

  return pipeline;
}

static GstElement *
create_simple_base_pipeline (GstElement ** src, GstElement ** parser)
{
  GstElement *pipeline = gst_pipeline_new ("pipeline");
  fail_if (pipeline == NULL);

  *src = gst_element_factory_make ("appsrc", "appsrc");
  fail_if (*src == NULL);

  *parser = gst_element_factory_make ("avparse_g723_1", "avparse_g723_1");
  fail_if (*parser == NULL);

  GstElement *sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), *src, *parser, sink, NULL);

  fail_unless (gst_element_link (*parser, sink));

  return pipeline;
}

static GstElement *
create_simple_pull_pipeline (void)
{
  GstElement *parser;
  GstElement *src;
  GstElement *pipeline = create_simple_base_pipeline (&src, &parser);

  fail_unless (gst_element_link (src, parser));

  return pipeline;
}

static GstElement *
create_simple_push_pipeline (void)
{
  GstElement *parser;
  GstElement *src;
  GstElement *pipeline = create_simple_base_pipeline (&src, &parser);

  GstElement *queue = gst_element_factory_make ("queue", "queue");
  fail_if (queue == NULL);

  gst_bin_add (GST_BIN (pipeline), queue);

  fail_unless (gst_element_link_many (src, queue, parser, NULL));

  return pipeline;
}

static void
process_need_data_signal (GstElement * src, guint size, gpointer user_data)
{
  GstBuffer **buffer = (GstBuffer **) user_data;
  GstFlowReturn result;
  if (*buffer != NULL) {
    g_signal_emit_by_name (src, "push-buffer", *buffer, &result);
    fail_unless (result == GST_FLOW_OK);
    gst_buffer_unref (*buffer);
    *buffer = NULL;
  } else {
    g_signal_emit_by_name (src, "end-of-stream", &result);
    fail_unless (result == GST_FLOW_OK);
  }
}

static void
run_state_error_test (GstElement * pipeline)
{
  GstStateChangeReturn ret = gst_element_set_state (pipeline,
      GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_FAILURE);

  GstMessage *msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ERROR);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
}

static void
run_success_test (GstElement * pipeline)
{
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
}

static void
setup_app_src (GstElement * pipeline, const char *src_id, GstCaps * caps,
    const char *path, GstBuffer ** buffer)
{
  GstElement *src = gst_bin_get_by_name (GST_BIN (pipeline), src_id);
  fail_if (src == NULL);

  g_object_set (src, "caps", caps, NULL);

  gchar *data;
  gsize size;
  fail_unless (g_file_get_contents (path, &data, &size, NULL));

  *buffer = gst_buffer_new_memdup ((gconstpointer) data, size);
  g_free (data);

  g_signal_connect (src, "need-data", G_CALLBACK (process_need_data_signal),
      buffer);

  gst_object_unref (src);
}

static void
setup_bad_caps_pipeline (GstElement * pipeline, const char *path,
    GstBuffer ** buffer)
{
  GstCaps *caps = gst_caps_new_empty_simple ("audio/wut-idk");
  setup_app_src (pipeline, "appsrc", caps, path, buffer);
  gst_caps_unref (caps);
}

static void
setup_caps_change_pipeline (GstElement * pipeline, const char *path_1,
    const char *path_2, GstBuffer ** buffer_1, GstBuffer ** buffer_2)
{
  GstCaps *caps = gst_caps_new_simple ("audio/G723",
      "rate", G_TYPE_INT, 8000, "channels", G_TYPE_INT, 1, NULL);
  setup_app_src (pipeline, "appsrc_1", caps, path_1, buffer_1);
  gst_caps_unref (caps);

  caps = gst_caps_new_simple ("audio/G723",
      "rate", G_TYPE_INT, 8000, "channels", G_TYPE_INT, 2, NULL);
  setup_app_src (pipeline, "appsrc_2", caps, path_2, buffer_2);
  gst_caps_unref (caps);
}

static void
setup_simple_pipeline (GstElement * pipeline, const char *path,
    GstBuffer ** buffer)
{
  GstCaps *caps = gst_caps_new_simple ("audio/G723",
      "rate", G_TYPE_INT, 8000, "channels", G_TYPE_INT, 1, NULL);
  setup_app_src (pipeline, "appsrc", caps, path, buffer);
  gst_caps_unref (caps);
}

GST_START_TEST (test_bad_caps_pull)
{
  GstBuffer *buffer;
  GstElement *pipeline = create_simple_pull_pipeline ();
  setup_bad_caps_pipeline (pipeline, SIMPLE_G723_1_PATH, &buffer);
  run_state_error_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_bad_caps_push)
{
  GstBuffer *buffer;
  GstElement *pipeline = create_simple_push_pipeline ();
  setup_bad_caps_pipeline (pipeline, SIMPLE_G723_1_PATH, &buffer);
  run_state_error_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_caps_change_pull)
{
  GstBuffer *buffer_1;
  GstBuffer *buffer_2;
  GstElement *pipeline = create_caps_change_pull_pipeline ();
  setup_caps_change_pipeline (pipeline, SIMPLE_G723_1_PATH, SIMPLE_G723_1_PATH,
      &buffer_1, &buffer_2);
  run_success_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_caps_change_push)
{
  GstBuffer *buffer_1;
  GstBuffer *buffer_2;
  GstElement *pipeline = create_caps_change_push_pipeline ();
  setup_caps_change_pipeline (pipeline, SIMPLE_G723_1_PATH, SIMPLE_G723_1_PATH,
      &buffer_1, &buffer_2);
  run_success_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_simple_file_pull)
{
  GstBuffer *buffer;
  GstElement *pipeline = create_simple_pull_pipeline ();
  setup_simple_pipeline (pipeline, SIMPLE_G723_1_PATH, &buffer);
  run_success_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_simple_file_push)
{
  GstBuffer *buffer;
  GstElement *pipeline = create_simple_push_pipeline ();
  setup_simple_pipeline (pipeline, SIMPLE_G723_1_PATH, &buffer);
  run_success_test (pipeline);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
avparse_g723_1_suite (void)
{
  Suite *s = suite_create ("avparse_g723_1");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_bad_caps_pull);
  tcase_add_test (tc_chain, test_bad_caps_push);

  tcase_add_test (tc_chain, test_caps_change_pull);
  tcase_add_test (tc_chain, test_caps_change_push);

  tcase_add_test (tc_chain, test_simple_file_pull);
  tcase_add_test (tc_chain, test_simple_file_push);

  return s;
}

GST_CHECK_MAIN (avparse_g723_1)

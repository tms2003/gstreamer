/* GStreamer
 * Copyright (C) 2022 GStreamer developers
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

static void
test_index_common (GstMemIndex * index)
{
  GstClockTime res_stream_time;
  guint64 res_offset;
  gboolean ret;

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 1,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 10);
  fail_unless_equals_uint64 (res_offset, 100);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 10,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 10);
  fail_unless_equals_uint64 (res_offset, 100);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 11,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 11,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 12);
  fail_unless_equals_uint64 (res_offset, 120);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_EXACT,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 11,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_NONE, 19,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 18);
  fail_unless_equals_uint64 (res_offset, 180);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_EXACT,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 19,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 19,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_AFTER,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 0,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 1);
  fail_unless_equals_uint64 (res_offset, 10);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_EXACT,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 0,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 0,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);
}

GST_START_TEST (test_lookup)
{
  GstMemIndex *index = gst_mem_index_new ();
  gint i;

  /* Set up an index:
   *
   * Stream ID: foo
   *
   * flags:  K   D   D   D   D   K   D   D   D   D
   * time:   1   2   4   6   8   10  12  14  16  18
   * offset: 10  20  40  60  80  100 120 140 160 180
   */

  gst_index_add_unit (GST_INDEX (index),
      "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 1, 10, TRUE, NULL);

  for (i = 1; i < 5; i++) {
    gst_index_add_unit (GST_INDEX (index),
        "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE, NULL);
  }

  gst_index_add_unit (GST_INDEX (index),
      "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 10, 100, TRUE, NULL);

  for (i = 6; i < 10; i++) {
    gst_index_add_unit (GST_INDEX (index),
        "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE, NULL);
  }

  test_index_common (index);

  g_object_unref (index);
}

GST_END_TEST;

GST_START_TEST (test_serialize)
{
  GstMemIndex *index = gst_mem_index_new ();
  GstMemIndex *index2 = NULL;
  gint i;
  GVariant *variant;

  /* Set up an index:
   *
   * flags:  K   D   D   D   D   K   D   D   D   D
   * time:   1   2   4   6   8   10  12  14  16  18
   * offset: 10  20  40  60  80  100 120 140 160 180
   */

  gst_index_add_unit (GST_INDEX (index),
      "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 1, 10, TRUE, NULL);

  for (i = 1; i < 5; i++) {
    gst_index_add_unit (GST_INDEX (index),
        "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE, NULL);
  }

  gst_index_add_unit (GST_INDEX (index),
      "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 10, 100, TRUE, NULL);

  for (i = 6; i < 10; i++) {
    gst_index_add_unit (GST_INDEX (index),
        "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE, NULL);
  }

  test_index_common (index);

  variant = gst_mem_index_to_variant (index);

  g_assert_nonnull (variant);

  index2 = gst_mem_index_new_from_variant (variant);

  g_assert_nonnull (index2);

  test_index_common (index2);

  g_variant_unref (variant);
  g_object_unref (index);
  g_object_unref (index2);
}

GST_END_TEST;

GST_START_TEST (test_scanned_ranges)
{
  GstMemIndex *index = gst_mem_index_new ();
  gint i;
  GstClockTime res_stream_time;
  guint64 res_offset;
  gboolean ret;

  /* Set up a non-contiguous index:
   *
   * flags:  K   (discont)   K   D   D   D
   * time:   1   (discont)   10  12  14  16  18
   * offset: 10  (discont)   100 120 140 160 180
   */

  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 1, 10, TRUE, NULL) == TRUE);

  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 10, 100, FALSE, NULL) == TRUE);

  for (i = 6; i < 10; i++) {
    fail_unless (gst_index_add_unit (GST_INDEX (index),
            "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE,
            NULL) == TRUE);
  }

  /* Can't add unit within already scanned range */
  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 1, 10, TRUE, NULL) == FALSE);

  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 11, 110, TRUE, NULL) == FALSE);

  /* Can't add non-contiguous unit at the start of the second range either,
   * only a contiguous unit is accepted here in order to close the gap,
   * checked later. */
  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 10, 100, FALSE,
          NULL) == FALSE);

  /* Check that a contiguous lookup fails within the discont range */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 2,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  /* The same not-contiguous lookup should work */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_NONE, 2,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 1);
  fail_unless_equals_uint64 (res_offset, 10);

  /* Contiguous lookups at the edges should work */

  /* End of first range */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 1,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 1);
  fail_unless_equals_uint64 (res_offset, 10);

  /* Start of second range */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 10,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 10);
  fail_unless_equals_uint64 (res_offset, 100);

  /* Now fill up the discontinuity:
   *
   * Stream ID: foo
   *
   * flags:  K   D   D   D   D   K   D   D   D   D
   * time:   1   2   4   6   8   10  12  14  16  18
   * offset: 10  20  40  60  80  100 120 140 160 180
   */

  for (i = 1; i < 5; i++) {
    fail_unless (gst_index_add_unit (GST_INDEX (index),
            "foo", GST_INDEX_UNIT_TYPE_NONE, i * 2, i * 20, TRUE,
            NULL) == TRUE);

    if (i == 1) {
      /* The contiguous lookup that was failing initially should already work */
      ret = gst_index_lookup_unit_time (GST_INDEX (index),
          "foo",
          GST_INDEX_LOOKUP_METHOD_BEFORE,
          GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 2,
          &res_stream_time, &res_offset, NULL);

      fail_unless (ret == TRUE);
      fail_unless_equals_uint64 (res_stream_time, 1);
      fail_unless_equals_uint64 (res_offset, 10);
    }
  }

  /* The gap isn't closed yet, there is still a non-scanned time
   * interval (8-10 exclusive)
   */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 9,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == FALSE);

  /* At this point we still have two distinct groups, the discontinuity
   * must be closed by adding the initial non-contiguous unit contiguously.
   */
  gst_index_add_unit (GST_INDEX (index),
      "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 10, 100, TRUE, NULL);

  /* And now the gap is closed */
  ret = gst_index_lookup_unit_time (GST_INDEX (index),
      "foo",
      GST_INDEX_LOOKUP_METHOD_BEFORE,
      GST_INDEX_UNIT_TYPE_SYNC_POINT, GST_INDEX_LOOKUP_FLAG_CONTIGUOUS, 9,
      &res_stream_time, &res_offset, NULL);

  fail_unless (ret == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 1);
  fail_unless_equals_uint64 (res_offset, 10);

  g_object_unref (index);
}

GST_END_TEST;

GST_START_TEST (test_index_type)
{
  GstMemIndex *index = gst_mem_index_new ();
  GstClockTime res_stream_time;
  guint64 res_offset;

  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 1, GST_INDEX_OFFSET_NONE, TRUE,
          NULL) == TRUE);

  /* This may evolve, but at the moment the memory index does not allow
   * storage of heterogenous units, eg one unit with only an offset,
   * another with both an offset and a stream time */
  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 2, 20, TRUE, NULL) == FALSE);

  /* Adding the unit without an offset goes fine */
  fail_unless (gst_index_add_unit (GST_INDEX (index),
          "foo", GST_INDEX_UNIT_TYPE_SYNC_POINT, 2, GST_INDEX_OFFSET_NONE, TRUE,
          NULL) == TRUE);

  /* Can't lookup by offset in a time-only index */
  fail_unless (gst_index_lookup_unit_offset (GST_INDEX (index),
          "foo",
          GST_INDEX_LOOKUP_METHOD_BEFORE,
          GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_NONE, 42,
          &res_stream_time, &res_offset, NULL) == FALSE);

  /* Works fine when looking up by time */
  fail_unless (gst_index_lookup_unit_time (GST_INDEX (index),
          "foo",
          GST_INDEX_LOOKUP_METHOD_BEFORE,
          GST_INDEX_UNIT_TYPE_NONE, GST_INDEX_LOOKUP_FLAG_NONE, 1,
          &res_stream_time, &res_offset, NULL) == TRUE);
  fail_unless_equals_uint64 (res_stream_time, 1);
  fail_unless_equals_uint64 (res_offset, GST_INDEX_OFFSET_NONE);


  g_object_unref (index);
}

GST_END_TEST;

static Suite *
gst_mem_index_suite (void)
{
  Suite *s = suite_create ("GstMemIndex");
  TCase *tc_chain = tcase_create ("memory index tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_lookup);
  tcase_add_test (tc_chain, test_serialize);
  tcase_add_test (tc_chain, test_scanned_ranges);
  tcase_add_test (tc_chain, test_index_type);

  return s;
}

GST_CHECK_MAIN (gst_mem_index);

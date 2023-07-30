/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <string.h>

#define LAYOUT_WIDTH 640
#define LAYOUT_HEIGHT 480
#define LAYOUT_X (-10)
#define LAYOUT_Y 20

GST_START_TEST (test_global_attributes)
{
  GstTextLayout *layout;
  const gchar *text;
  gint int_val;
  guint uint_val;
  GstWordWrapMode wrap_mode;
  GstTextAlignment text_alignment;
  GstParagraphAlignment paragrapha_alignment;

  layout = gst_text_layout_new ("test_global_attributes");
  fail_unless (layout);

  text = gst_text_layout_get_text (layout);
  fail_if (g_strcmp0 (text, "test_global_attributes"));

  /* layout position / resolution */
  fail_unless (gst_text_layout_set_width (layout, LAYOUT_WIDTH));
  fail_unless (gst_text_layout_set_height (layout, LAYOUT_HEIGHT));
  fail_unless (gst_text_layout_set_xpos (layout, LAYOUT_X));
  fail_unless (gst_text_layout_set_ypos (layout, LAYOUT_Y));

  uint_val = gst_text_layout_get_width (layout);
  assert_equals_int (uint_val, LAYOUT_WIDTH);
  uint_val = gst_text_layout_get_height (layout);
  assert_equals_int (uint_val, LAYOUT_HEIGHT);
  int_val = gst_text_layout_get_xpos (layout);
  assert_equals_int (int_val, LAYOUT_X);
  int_val = gst_text_layout_get_ypos (layout);
  assert_equals_int (int_val, LAYOUT_Y);

  /* wrap mode, alignments and directions */
  fail_unless (gst_text_layout_set_word_wrap (layout, GST_WORD_WRAP_CHAR));
  fail_unless (gst_text_layout_set_text_alignment (layout,
          GST_TEXT_ALIGNMENT_CENTER));
  fail_unless (gst_text_layout_set_paragraph_alignment (layout,
          GST_PARAGRAPH_ALIGNMENT_CENTER));

  wrap_mode = gst_text_layout_get_word_wrap (layout);
  fail_unless (wrap_mode == GST_WORD_WRAP_CHAR);
  text_alignment = gst_text_layout_get_text_alignment (layout);
  fail_unless (text_alignment == GST_TEXT_ALIGNMENT_CENTER);
  paragrapha_alignment = gst_text_layout_get_paragraph_alignment (layout);
  fail_unless (paragrapha_alignment == GST_PARAGRAPH_ALIGNMENT_CENTER);

  gst_text_layout_unref (layout);
}

GST_END_TEST;

GST_START_TEST (test_ranged_attributes)
{
  GstTextLayout *layout;
  GstTextAttr *attr;
  const gchar *str;
  gint int_val;
  GstTextAttrIterator *iter;
  GstTextAttrType attr_type;
  guint start, len;
  guint size;
  guint i;

  layout = gst_text_layout_new ("test_ranged_attributes");
  fail_unless (layout);

  iter = gst_text_layout_get_attr_iterator (layout);
  fail_unless (iter);
  /* No attribute specified, should be empty */
  size = gst_text_attr_iterator_get_size (iter);
  assert_equals_int (size, 0);
  gst_text_attr_iterator_free (iter);

  /*
   * +---------+
   * |    F    |
   * +---------+
   * 0         9
   */
  attr = gst_text_attr_string_new ("foo", GST_TEXT_ATTR_FONT_FAMILY, 0, 9);
  fail_unless (attr);
  fail_unless (gst_text_layout_set_attr (layout, attr));

  iter = gst_text_layout_get_attr_iterator (layout);
  fail_unless (iter);
  size = gst_text_attr_iterator_get_size (iter);
  assert_equals_int (size, 1);

  attr = gst_text_attr_iterator_get_attr (iter, 0);
  attr_type = gst_text_attr_identify (attr, &start, &len);
  assert_equals_int (attr_type, GST_TEXT_ATTR_FONT_FAMILY);
  fail_unless (gst_text_layout_attr_get_string (attr, &str));
  fail_if (g_strcmp0 (str, "foo"));
  assert_equals_int (start, 0);
  assert_equals_int (len, 9);
  gst_text_attr_iterator_free (iter);

  /*
   *     3   6
   *     +---+              +---+
   *     | U |              | U |
   * +---+---+---+  ->  +---+---+---+
   * |     F     |      | F | F | F |
   * +-----------+      +---+---+---+
   * 0           9      0   3   6   9
   */
  attr = gst_text_attr_int_new (GST_TEXT_UNDERLINE_SINGLE,
      GST_TEXT_ATTR_UNDERLINE, 3, 3);
  fail_unless (attr);
  fail_unless (gst_text_layout_set_attr (layout, attr));

  /* First period should hold only single font-family */
  iter = gst_text_layout_get_attr_iterator (layout);
  fail_unless (iter);
  size = gst_text_attr_iterator_get_size (iter);
  assert_equals_int (size, 1);
  attr = gst_text_attr_iterator_get_attr (iter, 0);
  fail_unless (attr);
  attr_type = gst_text_attr_identify (attr, &start, &len);
  assert_equals_int (attr_type, GST_TEXT_ATTR_FONT_FAMILY);
  fail_unless (gst_text_layout_attr_get_string (attr, &str));
  fail_if (g_strcmp0 (str, "foo"));
  assert_equals_int (start, 0);
  assert_equals_int (len, 3);

  /* Advance to next period */
  fail_unless (gst_text_attr_iterator_next (iter));

  /* Now hold underline and font-family */
  size = gst_text_attr_iterator_get_size (iter);
  assert_equals_int (size, 2);

  for (i = 0; i < size; i++) {
    attr = gst_text_attr_iterator_get_attr (iter, 0);
    fail_unless (attr);
    attr_type = gst_text_attr_identify (attr, &start, &len);
    fail_unless (attr_type == GST_TEXT_ATTR_FONT_FAMILY ||
        attr_type == GST_TEXT_ATTR_UNDERLINE);
    if (attr_type == GST_TEXT_ATTR_FONT_FAMILY) {
      fail_unless (gst_text_layout_attr_get_string (attr, &str));
      fail_if (g_strcmp0 (str, "foo"));
    } else {
      fail_unless (gst_text_layout_attr_get_int (attr, &int_val));
      assert_equals_int (int_val, GST_TEXT_UNDERLINE_SINGLE);
    }
    assert_equals_int (start, 3);
    assert_equals_int (len, 3);
  }

  /* Advance to next period */
  fail_unless (gst_text_attr_iterator_next (iter));

  /* the last period should hold only font-family */
  size = gst_text_attr_iterator_get_size (iter);
  assert_equals_int (size, 1);
  attr = gst_text_attr_iterator_get_attr (iter, 0);
  fail_unless (attr);
  attr_type = gst_text_attr_identify (attr, &start, &len);
  assert_equals_int (attr_type, GST_TEXT_ATTR_FONT_FAMILY);
  fail_unless (gst_text_layout_attr_get_string (attr, &str));
  fail_if (g_strcmp0 (str, "foo"));
  assert_equals_int (start, 6);
  assert_equals_int (len, 3);

  gst_text_attr_iterator_free (iter);

  gst_text_layout_unref (layout);
}

GST_END_TEST;

static Suite *
textlayout_suite (void)
{
  Suite *s = suite_create ("textlayout");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_global_attributes);
  tcase_add_test (tc, test_ranged_attributes);

  return s;
}

GST_CHECK_MAIN (textlayout);

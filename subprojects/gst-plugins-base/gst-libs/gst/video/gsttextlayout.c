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

#include <gst/video/video.h>
#include <string.h>

GST_DEFINE_MINI_OBJECT_TYPE (GstTextLayout, gst_text_layout);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;
  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("textlayout", 0, "textlayout");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

struct _GstTextAttr
{
  GstTextAttrType type;

  guint start_index;
  guint end_index;

  GValue value;
};

enum
{
  ATTR_FONT_FAMILY = 0,
  ATTR_FONT_SIZE,
  ATTR_FONT_WEIGHT,
  ATTR_FONT_STYLE,
  ATTR_FONT_STRETCH,
  ATTR_UNDERLINE,
  ATTR_STRIKETHROUGH,
  ATTR_FOREGROUND_COLOR,
  ATTR_BACKGROUND_COLOR,
  ATTR_OUTLINE_COLOR,
  ATTR_UNDERLINE_COLOR,
  ATTR_STRIKETHROUGH_COLOR,
  ATTR_SHADOW_COLOR,
  ATTR_LAST
};

struct _GstTextLayout
{
  GstMiniObject parent;

  gchar *text;
  gsize text_len;
  guint width;
  guint height;
  gint xpos;
  gint ypos;
  GstTextAlignment text_align;
  GstParagraphAlignment paragraph_align;
  GstWordWrapMode wrap_mode;
  GPtrArray *attr_list[ATTR_LAST];
};

struct _GstTextAttrIterator
{
  guint start_index;
  guint end_index;

  GPtrArray *attr_list[ATTR_LAST];
  GPtrArray *current;
};

/**
 * gst_text_attr_string_new:
 * @value: an attribute value
 * @type: a #GstTextAttrType
 * @start: the start index of the attribute
 * @length: the length of the attribute
 *
 * Creates a string type attribute. @start must be less than %G_MAXUINT
 * and @length should be nonzero
 *
 * Returns: (transfer full) (nullable): A new #GstTextAttr if succeeded or %NULL
 * otherwise
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_string_new (const gchar * value, GstTextAttrType type,
    guint start, guint length)
{
  GstTextAttr *attr;
  guint64 end;

  g_return_val_if_fail (value, NULL);

  if (type != GST_TEXT_ATTR_FONT_FAMILY)
    return NULL;

  if (start == G_MAXUINT || length == 0)
    return NULL;

  end = (guint64) start + length;
  end = MIN (end, G_MAXUINT);

  attr = g_new0 (GstTextAttr, 1);
  attr->type = type;
  attr->start_index = start;
  attr->end_index = (guint) end;
  g_value_init (&attr->value, G_TYPE_STRING);
  g_value_set_string (&attr->value, value);

  return (GstTextAttr *) attr;
}

/**
 * gst_text_attr_double_new:
 * @value: an attribute value
 * @type: a #GstTextAttrType
 * @start: the start index of the attribute
 * @length: the length of the attribute
 *
 * Creates a double type attribute. @start must be less than %G_MAXUINT
 * and @length should be nonzero
 *
 * Returns: (transfer full) (nullable): A new #GstTextAttr if succeeded or %NULL
 * otherwise
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_double_new (gdouble value, GstTextAttrType type, guint start,
    guint length)
{
  GstTextAttr *attr;
  guint64 end;

  if (type != GST_TEXT_ATTR_FONT_SIZE)
    return NULL;

  if (start == G_MAXUINT || length == 0)
    return NULL;

  end = (guint64) start + length;
  end = MIN (end, G_MAXUINT);

  attr = g_new0 (GstTextAttr, 1);
  attr->type = type;
  attr->start_index = start;
  attr->end_index = (guint) end;
  g_value_init (&attr->value, G_TYPE_DOUBLE);
  g_value_set_double (&attr->value, value);

  return attr;
}

/**
 * gst_text_attr_int_new:
 * @value: an attribute value
 * @type: a #GstTextAttrType
 * @start: the start index of the attribute
 * @length: the length of the attribute
 *
 * Creates a int type attribute. @start must be less than %G_MAXUINT
 * and @length should be nonzero
 *
 * Returns: (transfer full) (nullable): A new #GstTextAttr if succeeded or %NULL
 * otherwise
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_int_new (gint value, GstTextAttrType type, guint start,
    guint length)
{
  GstTextAttr *attr;
  guint64 end;

  switch (type) {
    case GST_TEXT_ATTR_FONT_WEIGHT:
    case GST_TEXT_ATTR_FONT_STYLE:
    case GST_TEXT_ATTR_FONT_STRETCH:
    case GST_TEXT_ATTR_UNDERLINE:
    case GST_TEXT_ATTR_STRIKETHROUGH:
      break;
    default:
      return NULL;
  }

  if (start == G_MAXUINT || length == 0)
    return NULL;

  end = (guint64) start + length;
  end = MIN (end, G_MAXUINT);

  attr = g_new0 (GstTextAttr, 1);
  attr->type = type;
  attr->start_index = start;
  attr->end_index = (guint) end;
  g_value_init (&attr->value, G_TYPE_INT);
  g_value_set_int (&attr->value, value);

  return attr;
}

static inline guint64
gst_text_attr_color_pack (const GstTextColor * color)
{
  return (((guint64) color->red) << 48) | (((guint64) color->green) << 32)
      | (((guint64) color->blue) << 16) | ((guint64) color->alpha);
}

static inline void
gst_text_attr_color_unpack (guint64 packed, GstTextColor * color)
{
  color->red = ((packed >> 48) & 0xffff);
  color->green = ((packed >> 32) & 0xffff);
  color->blue = ((packed >> 16) & 0xffff);
  color->alpha = (packed & 0xffff);
}

/**
 * gst_text_attr_color_new:
 * @value: an attribute value
 * @type: a #GstTextAttrType
 * @start: the start index of the attribute
 * @length: the length of the attribute
 *
 * Creates a color type attribute. @start must be less than %G_MAXUINT
 * and @length should be nonzero
 *
 * Returns: (transfer full) (nullable): A new #GstTextAttr if succeeded or %NULL
 * otherwise
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_color_new (const GstTextColor * value, GstTextAttrType type,
    guint start, guint length)
{
  GstTextAttr *attr;
  guint64 end;

  g_return_val_if_fail (value, NULL);

  switch (type) {
    case GST_TEXT_ATTR_FOREGROUND_COLOR:
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
    case GST_TEXT_ATTR_OUTLINE_COLOR:
    case GST_TEXT_ATTR_UNDERLINE_COLOR:
    case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
    case GST_TEXT_ATTR_SHADOW_COLOR:
      break;
    default:
      return NULL;
  }

  if (start == G_MAXUINT || length == 0)
    return NULL;

  end = (guint64) start + length;
  end = MIN (end, G_MAXUINT);

  attr = g_new0 (GstTextAttr, 1);
  attr->type = type;
  attr->start_index = start;
  attr->end_index = (guint) end;
  g_value_init (&attr->value, G_TYPE_UINT64);
  g_value_set_uint64 (&attr->value, gst_text_attr_color_pack (value));

  return attr;
}

/**
 * gst_text_attr_identify:
 * @attr: a #GstTextAttr,
 * @start: (out) (optional): the start index of the attribute
 * @length: (out) (optional): the length of the attribute
 *
 * Returns: a #GstTextAttrType
 *
 * Since: 1.24
 */
GstTextAttrType
gst_text_attr_identify (const GstTextAttr * attr, guint * start, guint * length)
{
  g_return_val_if_fail (attr, GST_TEXT_ATTR_INVALID);

  if (start)
    *start = attr->start_index;

  if (length)
    *length = attr->end_index - attr->start_index;

  return attr->type;
}

/**
 * gst_text_layout_attr_get_string:
 * @attr: a #GstTextAttr
 * @value: (out): a pointer to string-typed attribute value
 *
 * Parses a string type attribute.
 *
 * Returns: %TRUE if succeeded.
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_attr_get_string (const GstTextAttr * attr, const gchar ** value)
{
  g_return_val_if_fail (attr, FALSE);
  g_return_val_if_fail (value, FALSE);

  if (attr->type != GST_TEXT_ATTR_FONT_FAMILY)
    return FALSE;

  *value = g_value_get_string (&attr->value);

  return TRUE;
}

/**
 * gst_text_layout_attr_get_double:
 * @attr: a #GstTextAttr
 * @value: (out): a pointer to double-typed attribute value
 *
 * Parses a double-typed attribute.
 *
 * Returns: %TRUE if succeeded.
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_attr_get_double (const GstTextAttr * attr, gdouble * value)
{
  g_return_val_if_fail (attr, FALSE);
  g_return_val_if_fail (value, FALSE);

  if (attr->type != GST_TEXT_ATTR_FONT_SIZE)
    return FALSE;

  *value = g_value_get_double (&attr->value);

  return TRUE;
}

/**
 * gst_text_layout_attr_get_int:
 * @attr: a #GstTextAttr
 * @value: (out): a pointer to attribute value
 *
 * Parses an integer attribute.
 *
 * Returns: %TRUE if succeeded.
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_attr_get_int (const GstTextAttr * attr, gint * value)
{
  g_return_val_if_fail (attr, FALSE);
  g_return_val_if_fail (value, FALSE);

  switch (attr->type) {
    case GST_TEXT_ATTR_FONT_WEIGHT:
    case GST_TEXT_ATTR_FONT_STYLE:
    case GST_TEXT_ATTR_FONT_STRETCH:
    case GST_TEXT_ATTR_UNDERLINE:
    case GST_TEXT_ATTR_STRIKETHROUGH:
      break;
    default:
      return FALSE;
  }

  *value = g_value_get_int (&attr->value);

  return TRUE;
}

/**
 * gst_text_layout_attr_get_color:
 * @attr: a #GstTextAttr
 * @value: (out): a pointer to #GstTextColor
 *
 * Parses a brush color attribute.
 *
 * Returns: %TRUE if succeeded.
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_attr_get_color (const GstTextAttr * attr, GstTextColor * value)
{
  guint64 color;

  g_return_val_if_fail (attr, FALSE);
  g_return_val_if_fail (value, FALSE);

  switch (attr->type) {
    case GST_TEXT_ATTR_FOREGROUND_COLOR:
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
    case GST_TEXT_ATTR_OUTLINE_COLOR:
    case GST_TEXT_ATTR_UNDERLINE_COLOR:
    case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
    case GST_TEXT_ATTR_SHADOW_COLOR:
      break;
    default:
      return FALSE;
  }

  color = g_value_get_uint64 (&attr->value);
  gst_text_attr_color_unpack (color, value);

  return TRUE;
}

/**
 * gst_text_attr_copy:
 * @attr: a #GstTextAttr
 *
 * Creates a copy of the @attr
 *
 * Returns: (transfer full) (nullable): a #GstTextAttr if succeeded
 * or %NULL otherwise
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_copy (GstTextAttr * attr)
{
  GstTextAttr *copy;

  g_return_val_if_fail (attr, NULL);
  g_return_val_if_fail (attr->start_index < attr->end_index, NULL);

  copy = g_new0 (GstTextAttr, 1);
  copy->type = attr->type;
  copy->start_index = attr->start_index;
  copy->end_index = attr->end_index;
  switch (attr->type) {
    case GST_TEXT_ATTR_FONT_FAMILY:
      g_value_init (&copy->value, G_TYPE_STRING);
      break;
    case GST_TEXT_ATTR_FONT_SIZE:
      g_value_init (&copy->value, G_TYPE_DOUBLE);
      break;
    case GST_TEXT_ATTR_FONT_WEIGHT:
    case GST_TEXT_ATTR_FONT_STYLE:
    case GST_TEXT_ATTR_FONT_STRETCH:
    case GST_TEXT_ATTR_UNDERLINE:
    case GST_TEXT_ATTR_STRIKETHROUGH:
      g_value_init (&copy->value, G_TYPE_INT);
      break;
    case GST_TEXT_ATTR_FOREGROUND_COLOR:
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
    case GST_TEXT_ATTR_OUTLINE_COLOR:
    case GST_TEXT_ATTR_UNDERLINE_COLOR:
    case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
    case GST_TEXT_ATTR_SHADOW_COLOR:
      g_value_init (&copy->value, G_TYPE_UINT64);
      break;
    default:
      g_assert_not_reached ();
      g_free (copy);
      return NULL;
  }

  g_value_copy (&attr->value, &copy->value);

  return copy;
}

/**
 * gst_text_attr_free:
 * @attr: (transfer full) (nullable): a #GstTextAttr
 *
 * Frees @attr resource
 *
 * Since: 1.24
 */
void
gst_text_attr_free (GstTextAttr * attr)
{
  if (!attr)
    return;

  g_value_reset (&attr->value);
  g_free (attr);
}

static gint
gst_text_attr_cmp_range (const GstTextAttr * old_attr,
    const GstTextAttr * new_attr)
{
  if (new_attr->start_index <= old_attr->start_index
      && new_attr->end_index >= old_attr->end_index) {
    return 0;
  } else if (new_attr->start_index <= old_attr->start_index) {
    return -1;
  } else {
    return 1;
  }
}

static gboolean
gst_text_attr_is_equal (const GstTextAttr * attr1, const GstTextAttr * attr2)
{
  GstTextAttr *real1;
  GstTextAttr *real2;

  g_assert (attr1->type == attr2->type);

  real1 = (GstTextAttr *) attr1;
  real2 = (GstTextAttr *) attr2;

  switch (attr1->type) {
    case GST_TEXT_ATTR_FONT_FAMILY:
    {
      const gchar *v1;
      const gchar *v2;

      v1 = g_value_get_string (&real1->value);
      v2 = g_value_get_string (&real2->value);

      return g_strcmp0 (v1, v2) == 0;
    }
    case GST_TEXT_ATTR_FONT_SIZE:
    {
      gdouble v1, v2;

      v1 = g_value_get_double (&real1->value);
      v2 = g_value_get_double (&real2->value);

      return v1 == v2;
    }
    case GST_TEXT_ATTR_FONT_WEIGHT:
    {
      guint v1, v2;

      v1 = g_value_get_uint (&real1->value);
      v2 = g_value_get_uint (&real2->value);

      return v1 == v2;
    }
    case GST_TEXT_ATTR_FONT_STYLE:
    case GST_TEXT_ATTR_FONT_STRETCH:
    case GST_TEXT_ATTR_UNDERLINE:
    case GST_TEXT_ATTR_STRIKETHROUGH:
    {
      gint v1, v2;

      v1 = g_value_get_enum (&real1->value);
      v2 = g_value_get_enum (&real2->value);

      return v1 == v2;
    }
    case GST_TEXT_ATTR_FOREGROUND_COLOR:
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
    case GST_TEXT_ATTR_OUTLINE_COLOR:
    case GST_TEXT_ATTR_UNDERLINE_COLOR:
    case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
    case GST_TEXT_ATTR_SHADOW_COLOR:
    {
      guint64 v1, v2;

      v1 = g_value_get_uint64 (&real1->value);
      v2 = g_value_get_uint64 (&real2->value);

      return v1 == v2;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  return FALSE;
}

static GPtrArray *
gst_text_layout_insert_attr (GPtrArray * array, GstTextAttr * attr)
{
  guint i = 0;

  if (!array) {
    array =
        g_ptr_array_new_with_free_func ((GDestroyNotify) gst_text_attr_free);
    g_ptr_array_add (array, attr);
    GST_TRACE ("Creating new attr array");
    return array;
  }

  while (TRUE) {
    GstTextAttr *old;
    gint ret;

    if (array->len <= i) {
      GST_TRACE ("Inserting attr (%u, %u) at %u", attr->start_index,
          attr->start_index, i);
      g_ptr_array_add (array, attr);
      break;
    }

    old = g_ptr_array_index (array, i);
    ret = gst_text_attr_cmp_range (old, attr);
    GST_TRACE ("Comparing old (%u, %u) / new (%u, %u) at %u result: %d",
        old->start_index, old->end_index, attr->start_index, attr->end_index,
        i, ret);
    if (ret == 0) {
      GST_TRACE ("Removing old attr (%u, %u) at %u", old->start_index,
          old->end_index, i);
      g_ptr_array_remove_index (array, i);
    } else if (ret < 0) {
      /*
       * [Case A]
       * +-----+
       * |  N  |
       * +---------+
       * |  O      |
       * +---------+
       *
       * [Case B]
       * +-----+
       * |  N  |
       * +--+--+---+
       *    |  O   |
       *    +------+
       *
       * [Case C]
       * +-----+
       * |  N  |
       * +-----+ +-----+
       *         |  O  |
       *         +-----+
       */
      if (attr->start_index == old->start_index) {
        /* [Case A] */
        if (gst_text_attr_is_equal (old, attr)) {
          GST_TRACE ("Keep old at %u", i);
          gst_text_attr_free (attr);
        } else {
          GST_TRACE ("Inserting at %u, updating old range (%u, %u) -> (%u, %u)",
              i, old->start_index, old->end_index,
              attr->end_index, old->end_index);
          old->start_index = attr->end_index;
          g_ptr_array_insert (array, i, attr);
        }
      } else if (attr->end_index < old->start_index &&
          attr->end_index > old->start_index) {
        /* [Case B] */
        if (gst_text_attr_is_equal (old, attr)) {
          GST_TRACE ("Keep old at %u, update range (%u, %u) -> (%u, %u)",
              i, old->start_index, old->end_index,
              attr->start_index, old->end_index);
          old->start_index = attr->start_index;
          gst_text_attr_free (attr);
        } else {
          GST_TRACE ("Inserting at %u, updating old range (%u, %u) -> (%u, %u)",
              i, old->start_index, old->end_index,
              attr->end_index, old->end_index);
          old->start_index = attr->end_index;
          g_ptr_array_insert (array, i, attr);
        }
      } else {
        /* [Case C] */
        GST_TRACE ("Inserting at %u", i);
        g_ptr_array_insert (array, i, attr);
      }
      break;
    } else {
      /*
       * [Case A]
       *    +-----+
       *    |  N  |
       * +--+-----+
       * |  O     |
       * +--------+
       *
       * [Case B]
       *    +-----+
       *    |  N  |
       * +--+-----+--+
       * |  O        |
       * +-----------+

       * [Case C]
       *    +-----+
       *    |  N  |
       * +--+--+--+
       * |  O  |
       * +-----+
       *
       * [Case D]
       *         +-----+
       *         |  N  |
       * +-----+ +-----+
       * |  O  |
       * +-----+

       */
      if (attr->end_index == old->end_index) {
        /* [Case A] */
        if (gst_text_attr_is_equal (old, attr)) {
          GST_TRACE ("Keep old at %u", i);
          gst_text_attr_free (attr);
        } else {
          GST_TRACE ("Inserting at %u, updating old range (%u, %u) -> (%u, %u)",
              i + 1, old->start_index, old->end_index,
              old->start_index, attr->start_index);
          old->end_index = attr->start_index;
          g_ptr_array_insert (array, i + 1, attr);
        }
        break;
      } else if (attr->end_index < old->end_index) {
        /* [Case B] */
        if (gst_text_attr_is_equal (old, attr)) {
          GST_TRACE ("Keep old at %u", i);
          gst_text_attr_free (attr);
        } else {
          GstTextAttr *clone = gst_text_attr_copy (old);

          GST_TRACE ("Inserting at %u, "
              "split old (%u, %u) to (%u, %u) and (%u, %u)",
              i + 1, old->start_index, old->end_index,
              old->start_index, attr->start_index,
              attr->end_index, old->end_index);

          clone->start_index = attr->end_index;
          clone->end_index = old->end_index;
          old->end_index = attr->start_index;

          g_ptr_array_insert (array, i + 1, attr);
          g_ptr_array_insert (array, i + 2, clone);
        }
        break;
      } else if (attr->start_index <= old->end_index) {
        /* [Case C] */
        if (gst_text_attr_is_equal (old, attr)) {
          GST_TRACE ("Removing old at %u, updating range (%u, %u) -> (%u, %u)",
              i, attr->start_index, attr->end_index,
              old->start_index, attr->end_index);

          attr->start_index = old->start_index;
          g_ptr_array_remove_index (array, i);
        } else {
          GST_TRACE ("Updating old at %u, (%u, %u) -> (%u, %u)",
              i, old->start_index, old->end_index,
              old->start_index, attr->start_index);
          old->end_index = attr->start_index;
          i++;
        }
      } else {
        GST_TRACE ("Skip old at %u", i);
        i++;
      }
    }
  }

  return array;
}

static GPtrArray *
gst_text_layout_copy_array_with_offset (GPtrArray * array, guint offset)
{
  GPtrArray *copy;
  guint i;

  if (!array)
    return NULL;

  copy = g_ptr_array_new_with_free_func ((GDestroyNotify) gst_text_attr_free);
  for (i = 0; i < array->len; i++) {
    GstTextAttr *src = g_ptr_array_index (array, i);
    GstTextAttr *dst;

    if (offset != 0) {
      guint64 start = src->start_index;
      guint64 end = src->end_index;
      start += offset;
      if (start >= G_MAXUINT)
        break;

      end += offset;
      end = MIN (end, G_MAXUINT);

      dst = gst_text_attr_copy (src);
      dst->start_index = (guint) start;
      dst->end_index = (guint) end;
    } else {
      dst = gst_text_attr_copy (src);
    }

    g_ptr_array_add (copy, dst);
  }

  return copy;
}

static GPtrArray *
gst_text_layout_copy_array (GPtrArray * array)
{
  return gst_text_layout_copy_array_with_offset (array, 0);
}

static void
gst_text_layout_copy_values (GstTextLayout * dst, const GstTextLayout * src)
{
  guint i;

  dst->width = src->width;
  dst->height = src->height;
  dst->xpos = src->xpos;
  dst->ypos = src->ypos;
  dst->text_align = src->text_align;
  dst->paragraph_align = src->paragraph_align;
  dst->wrap_mode = src->wrap_mode;

  for (i = 0; i < G_N_ELEMENTS (src->attr_list); i++)
    dst->attr_list[i] = gst_text_layout_copy_array (src->attr_list[i]);
}

static GstTextLayout *
_gst_text_layout_copy (const GstTextLayout * src)
{
  GstTextLayout *dst;

  dst = gst_text_layout_new (src->text);
  gst_text_layout_copy_values (dst, src);

  return dst;
}

static void
_gst_text_layout_free (GstTextLayout * layout)
{
  guint i;
  g_free (layout->text);

  for (i = 0; i < G_N_ELEMENTS (layout->attr_list); i++) {
    if (layout->attr_list[i])
      g_ptr_array_unref (layout->attr_list[i]);
  }

  g_free (layout);
}

/**
 * gst_text_layout_new:
 * @text: the text
 *
 * Creates a text layout with empty attribute
 *
 * Returns: (transfer full): the new #GstTextLayout
 *
 * Since: 1.24
 */
GstTextLayout *
gst_text_layout_new (const gchar * text)
{
  GstTextLayout *self;
  gsize text_len;

  g_return_val_if_fail (text, NULL);

  text_len = strlen (text);
  g_return_val_if_fail (text_len <= G_MAXUINT, NULL);

  self = g_new0 (GstTextLayout, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (self), 0, GST_TYPE_TEXT_LAYOUT,
      (GstMiniObjectCopyFunction) _gst_text_layout_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_text_layout_free);

  self->text = g_strdup (text);
  self->text_len = text_len;
  self->width = G_MAXUINT;
  self->height = G_MAXUINT;
  self->text_align = GST_TEXT_ALIGNMENT_UNKNOWN;
  self->paragraph_align = GST_PARAGRAPH_ALIGNMENT_UNKNOWN;
  self->wrap_mode = GST_WORD_WRAP_UNKNOWN;

  return self;
}

/**
 * gst_text_layout_ref:
 * @layout: a #GstTextLayout
 *
 * Increase the refcount of the given layout by one
 *
 * Returns: (transfer full): @layout
 *
 * Since: 1.24
 */
GstTextLayout *
gst_text_layout_ref (GstTextLayout * layout)
{
  return (GstTextLayout *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (layout));
}

/**
 * gst_text_layout_unref:
 * @layout: (transfer full): a #GstTextLayout
 *
 * Decreases the refcount of the layout
 *
 * Since: 1.24
 */
void
gst_text_layout_unref (GstTextLayout * layout)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (layout));
}

/**
 * gst_text_layout_copy:
 * @layout: a #GstTextLayout
 *
 * Creates a copy of the given @layout
 *
 * Returns: (transfer full): a new copy of @layout
 *
 * Since: 1.24
 */
GstTextLayout *
gst_text_layout_copy (GstTextLayout * layout)
{
  return (GstTextLayout *) gst_mini_object_copy (GST_MINI_OBJECT_CAST (layout));
}

static void
gst_text_layout_append_array (GPtrArray ** dst, GPtrArray * src, guint offset)
{
  guint i;

  if (!src)
    return;

  if (*dst == NULL) {
    *dst = gst_text_layout_copy_array_with_offset (src, offset);
    return;
  }

  for (i = 0; i < src->len; i++) {
    GstTextAttr *s = g_ptr_array_index (src, i);
    GstTextAttr *d;

    if (offset != 0) {
      guint64 start = s->start_index;
      guint64 end = s->end_index;
      start += offset;
      if (start >= G_MAXUINT)
        break;

      end += offset;
      end = MIN (end, G_MAXUINT);

      d = gst_text_attr_copy (s);
      d->start_index = (guint) start;
      d->end_index = (guint) end;
    } else {
      d = gst_text_attr_copy (s);
    }

    if (i == 0)
      gst_text_layout_insert_attr (*dst, d);
    else
      g_ptr_array_add (*dst, d);
  }
}

/**
 * gst_text_layout_concat:
 * @layout1: (transfer none): a #GstTextLayout
 * @layout2: (transfer none): a #GstTextLayout
 * @glue: (nullable): a string to be inserted
 *
 * Creates new #GstTextLayout object which perserves all attributes of
 * @layout1 and @layout2. If @glue is specified, the string will be inserted
 * between the original strings of @layout1 and @layout2
 *
 * Total string length should be less than or equal to %G_MAXUINT, otherwise
 * this method will return %NULL
 *
 * Returns: (transfer full) (nullable): The merged #GstTextLayout if succeeded,
 * otherwise %NULL
 *
 * Since: 1.24
 */
GstTextLayout *
gst_text_layout_concat (GstTextLayout * layout1, GstTextLayout * layout2,
    const gchar * glue)
{
  GstTextLayout *layout;
  gsize glue_len = 0;
  guint64 total_len;
  gchar *text;
  guint offset = 0;
  guint i;

  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout1), NULL);
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout2), NULL);

  total_len = (guint64) layout1->text_len + layout2->text_len;
  if (total_len > G_MAXUINT)
    return NULL;

  if (glue && glue[0] != '\0') {
    glue_len = strlen (glue);
    if (glue_len >= G_MAXUINT)
      return NULL;

    total_len += glue_len;
    if (total_len > G_MAXUINT)
      return NULL;
  }

  offset = layout1->text_len + glue_len;
  text = g_strdup_printf ("%s%s%s", layout1->text, glue ? glue : "",
      layout2->text);
  if (!text)
    return NULL;

  layout = gst_text_layout_new (text);
  gst_text_layout_copy_values (layout, layout1);

  for (i = 0; i < G_N_ELEMENTS (layout->attr_list); i++) {
    gst_text_layout_append_array (&layout->attr_list[i],
        layout2->attr_list[i], offset);
  }

  return layout;
}

/**
 * gst_clear_text_layout:
 * @layout: a pointer to a #GstTextLayout reference
 *
 * Clears a reference to a #GstTextLayout
 *
 * Since: 1.24
 */
void
gst_clear_text_layout (GstTextLayout ** layout)
{
  gst_clear_mini_object ((GstMiniObject **) layout);
}

/**
 * gst_text_layout_get_text:
 * @layout: a #GstTextLayout
 *
 * Gets the string of @layout
 *
 * Returns: the string of @layout
 *
 * Since: 1.24
 */
const gchar *
gst_text_layout_get_text (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), NULL);

  return layout->text;
}

static inline gboolean
gst_text_layout_is_writable (GstTextLayout * layout)
{
  return gst_mini_object_is_writable (GST_MINI_OBJECT_CAST (layout));
}

/**
 * gst_text_layout_set_width:
 * @layout: a #GstTextLayout
 * @width: width of layout
 *
 * Sets the width of @layout
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_width (GstTextLayout * layout, guint width)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->width = width;

  return TRUE;
}

/**
 * gst_text_layout_get_width:
 * @layout: a #GstTextLayout
 *
 * Gets configured layout width
 *
 * Returns: the width of @layout
 *
 * Since: 1.24
 */
guint
gst_text_layout_get_width (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), 0);

  return layout->width;
}

/**
 * gst_text_layout_set_height:
 * @layout: a #GstTextLayout
 * @height: height of layout
 *
 * Sets the height of @layout
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_height (GstTextLayout * layout, guint height)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->height = height;

  return TRUE;
}

/**
 * gst_text_layout_get_height:
 * @layout: a #GstTextLayout
 *
 * Gets configured layout height
 *
 * Returns: the height of @layout
 *
 * Since: 1.24
 */
guint
gst_text_layout_get_height (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), 0);

  return layout->height;
}

/**
 * gst_text_layout_set_xpos:
 * @layout: a #GstTextLayout
 * @xpos: x position
 *
 * Sets x position the @layout relative to top-left position of video frame
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_xpos (GstTextLayout * layout, gint xpos)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->xpos = xpos;

  return TRUE;
}

/**
 * gst_text_layout_get_xpos:
 * @layout: a #GstTextLayout
 *
 * Gets configrued x position of the @layout
 *
 * Returns: the x position of @layout
 *
 * Since: 1.24
 */
gint
gst_text_layout_get_xpos (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), 0);

  return layout->xpos;
}

/**
 * gst_text_layout_set_ypos:
 * @layout: a #GstTextLayout
 * @xpos: y position
 *
 * Sets y position the @layout relative to top-left position of video frame
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_ypos (GstTextLayout * layout, gint ypos)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->ypos = ypos;

  return TRUE;
}

/**
 * gst_text_layout_get_ypos:
 * @layout: a #GstTextLayout
 *
 * Gets configrued y position of the @layout
 *
 * Returns: the y position of @layout
 *
 * Since: 1.24
 */
gint
gst_text_layout_get_ypos (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), 0);

  return layout->ypos;
}

/**
 * gst_text_layout_set_word_wrap:
 * @layout: a #GstTextLayout
 * @wrap_mode: a #GstWordWrapMode
 *
 * Sets @wrap_mode to @layout
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_word_wrap (GstTextLayout * layout,
    GstWordWrapMode wrap_mode)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->wrap_mode = wrap_mode;

  return TRUE;
}

/**
 * gst_text_layout_get_word_wrap:
 * @layout: a #GstTextLayout
 *
 * Gets configured #GstWordWrapMode
 *
 * Returns: Configured #GstWordWrapMode of @layout
 *
 * Since: 1.24
 */
GstWordWrapMode
gst_text_layout_get_word_wrap (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), GST_WORD_WRAP_WORD);

  return layout->wrap_mode;
}

/**
 * gst_text_layout_set_text_alignment:
 * @layout: a #GstTextLayout
 * @align: a #GstTextAlignment
 *
 * Sets @align to @layout
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_text_alignment (GstTextLayout * layout,
    GstTextAlignment align)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->text_align = align;

  return TRUE;
}

/**
 * gst_text_layout_get_text_alignment:
 * @layout: a #GstTextLayout
 *
 * Gets configured #GstTextAlignment
 *
 * Returns: Configured #GstTextAlignment of @layout
 *
 * Since: 1.24
 */
GstTextAlignment
gst_text_layout_get_text_alignment (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout),
      GST_TEXT_ALIGNMENT_UNKNOWN);

  return layout->text_align;
}

/**
 * gst_text_layout_set_paragraph_alignment:
 * @layout: a #GstTextLayout
 * @align: a #GstTextAlignment
 *
 * Sets @align to @layout
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_paragraph_alignment (GstTextLayout * layout,
    GstParagraphAlignment align)
{
  g_return_val_if_fail (gst_text_layout_is_writable (layout), FALSE);

  layout->paragraph_align = align;

  return TRUE;
}

/**
 * gst_text_layout_get_paragraph_alignment:
 * @layout: a #GstTextLayout
 *
 * Gets configured #GstParagraphAlignment
 *
 * Returns: Configured #GstParagraphAlignment of @layout
 *
 * Since: 1.24
 */
GstParagraphAlignment
gst_text_layout_get_paragraph_alignment (GstTextLayout * layout)
{
  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout),
      GST_PARAGRAPH_ALIGNMENT_UNKNOWN);

  return layout->paragraph_align;
}

/**
 * gst_text_layout_set_attr:
 * @layout: a #GstTextLayout
 * @attr: (transfer full): a #GstTextLayout
 *
 * Sets @attr to @layout. Pre-existing attributes at the range of @attr
 * with the same type will be overwritten by @attr
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.24
 */
gboolean
gst_text_layout_set_attr (GstTextLayout * layout, GstTextAttr * attr)
{
  GPtrArray **array = NULL;

  if (!attr)
    return FALSE;

  if (!gst_text_layout_is_writable (layout)) {
    gst_text_attr_free (attr);
    return FALSE;
  }

  if (attr->start_index == G_MAXUINT || attr->start_index >= attr->end_index) {
    gst_text_attr_free (attr);
    return FALSE;
  }

  switch (attr->type) {
    case GST_TEXT_ATTR_FONT_FAMILY:
      array = &layout->attr_list[ATTR_FONT_FAMILY];
      break;
    case GST_TEXT_ATTR_FONT_SIZE:
      array = &layout->attr_list[ATTR_FONT_SIZE];
      break;
    case GST_TEXT_ATTR_FONT_WEIGHT:
      array = &layout->attr_list[ATTR_FONT_WEIGHT];
      break;
    case GST_TEXT_ATTR_FONT_STYLE:
      array = &layout->attr_list[ATTR_FONT_STYLE];
      break;
    case GST_TEXT_ATTR_FONT_STRETCH:
      array = &layout->attr_list[ATTR_FONT_STRETCH];
      break;
    case GST_TEXT_ATTR_UNDERLINE:
      array = &layout->attr_list[ATTR_UNDERLINE];
      break;
    case GST_TEXT_ATTR_STRIKETHROUGH:
      array = &layout->attr_list[ATTR_STRIKETHROUGH];
      break;
    case GST_TEXT_ATTR_FOREGROUND_COLOR:
      array = &layout->attr_list[ATTR_FOREGROUND_COLOR];
      break;
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
      array = &layout->attr_list[ATTR_BACKGROUND_COLOR];
      break;
    case GST_TEXT_ATTR_OUTLINE_COLOR:
      array = &layout->attr_list[ATTR_OUTLINE_COLOR];
      break;
    case GST_TEXT_ATTR_UNDERLINE_COLOR:
      array = &layout->attr_list[ATTR_UNDERLINE_COLOR];
      break;
    case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
      array = &layout->attr_list[ATTR_STRIKETHROUGH_COLOR];
      break;
    case GST_TEXT_ATTR_SHADOW_COLOR:
      array = &layout->attr_list[ATTR_SHADOW_COLOR];
      break;
    default:
      gst_text_attr_free (attr);
      return FALSE;
  }

  *array = gst_text_layout_insert_attr (*array, attr);

  return TRUE;
}

/**
 * gst_text_layout_get_attr_iterator:
 * @layout: a #GstTextLayout
 *
 * Gets #GstTextAttrIterator for the currently configured attributes.
 *
 * Free returned #GstTextAttrIterator using gst_text_attr_iterator_free()
 * after use.
 *
 * Returns: (transfer full): a #GstTextAttrIterator
 */
GstTextAttrIterator *
gst_text_layout_get_attr_iterator (GstTextLayout * layout)
{
  GstTextAttrIterator *iter;
  guint i;

  g_return_val_if_fail (GST_IS_TEXT_LAYOUT (layout), NULL);

  iter = g_new0 (GstTextAttrIterator, 1);

  for (i = 0; i < G_N_ELEMENTS (layout->attr_list); i++)
    iter->attr_list[i] = gst_text_layout_copy_array (layout->attr_list[i]);

  iter->current =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_text_attr_free);
  gst_text_attr_iterator_next (iter);

  return iter;
}

/**
 * gst_text_attr_iterator_next:
 * @layout: a #GstTextLayout
 *
 * Advance to the next period which consists of the same range of attributes
 *
 * Returns: %FALSE if the iterator is at the end of the period, otherwise %TRUE
 *
 * Since: 1.24
 */
gboolean
gst_text_attr_iterator_next (GstTextAttrIterator * iter)
{
  guint start = G_MAXUINT;
  guint end = G_MAXUINT;
  guint i;

  g_ptr_array_set_size (iter->current, 0);
  iter->start_index = G_MAXUINT;
  iter->end_index = G_MAXUINT;

  for (i = 0; i < G_N_ELEMENTS (iter->attr_list); i++) {
    GstTextAttr *attr;

    if (!iter->attr_list[i] || iter->attr_list[i]->len == 0)
      continue;

    attr = g_ptr_array_index (iter->attr_list[i], 0);
    GST_TRACE ("attr at %u range [%u, %u)", i, attr->start_index,
        attr->end_index);
    if (attr->start_index < start)
      start = attr->start_index;

    if (attr->start_index > start && attr->start_index < end)
      end = attr->start_index;
    else if (attr->end_index < end)
      end = attr->end_index;
  }

  iter->start_index = start;
  iter->end_index = end;

  GST_TRACE ("Current range [%u, %u)", start, end);

  if (start == G_MAXUINT)
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (iter->attr_list); i++) {
    GstTextAttr *attr;
    GstTextAttr *copy;

    if (!iter->attr_list[i] || iter->attr_list[i]->len == 0)
      continue;

    attr = g_ptr_array_index (iter->attr_list[i], 0);
    if (attr->start_index > start)
      continue;

    copy = gst_text_attr_copy (attr);
    copy->end_index = end;

    g_ptr_array_add (iter->current, copy);

    if (attr->end_index == end)
      g_ptr_array_remove_index (iter->attr_list[i], 0);
    else
      attr->start_index = end;
  }

  return TRUE;
}

/**
 * gst_text_attr_iterator_get_size:
 * @iter: a #GstTextAttrIterator
 *
 * Gets the number of attributes at the current period
 *
 * Returns: the number of attributes
 *
 * Since: 1.24
 */
guint
gst_text_attr_iterator_get_size (GstTextAttrIterator * iter)
{
  g_return_val_if_fail (iter, 0);

  return iter->current->len;
}

/**
 * gst_text_attr_iterator_get_size:
 * @iter: a #GstTextAttrIterator
 * @idx: an index
 *
 * Gets the #GstTextAttr at @idx
 *
 * Returns: (transfer none) (nullable): the #GstTextAttr at @idx
 *
 * Since: 1.24
 */
GstTextAttr *
gst_text_attr_iterator_get_attr (GstTextAttrIterator * iter, guint idx)
{
  g_return_val_if_fail (iter, NULL);
  g_return_val_if_fail (iter->current->len > idx, NULL);

  return (GstTextAttr *) g_ptr_array_index (iter->current, idx);
}

/**
 * gst_text_attr_iterator_free:
 * @iter: (transfer full) (nullable): a #GstTextAttrIterator
 *
 * Destroys the @iter resource
 *
 * Since: 1.24
 */
void
gst_text_attr_iterator_free (GstTextAttrIterator * iter)
{
  guint i;

  if (!iter)
    return;

  for (i = 0; i < G_N_ELEMENTS (iter->attr_list); i++) {
    if (iter->attr_list[i])
      g_ptr_array_unref (iter->attr_list[i]);
  }

  g_ptr_array_unref (iter->current);

  g_free (iter);
}

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

#include "gstpangoparser.h"

#ifdef HAVE_PANGO
typedef struct
{
  gint gst_style;
  gint pango_style;
} GstPangoFontStyleMap;

static const GstPangoFontStyleMap font_style_map[] = {
  {GST_FONT_STYLE_NORMAL, PANGO_STYLE_NORMAL},
  {GST_FONT_STYLE_OBLIQUE, PANGO_STYLE_OBLIQUE},
  {GST_FONT_STYLE_ITALIC, PANGO_STYLE_ITALIC},
};

static gint
gst_pango_font_style_to_gst (gint style)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (font_style_map); i++) {
    if (font_style_map[i].pango_style == style)
      return font_style_map[i].gst_style;
  }

  return GST_FONT_STYLE_NORMAL;
}

typedef struct
{
  gint gst_stretch;
  gint pango_stretch;
} GstPangoFontStretch;

static const GstPangoFontStretch font_stretch_map[] = {
  {GST_FONT_STRETCH_ULTRA_CONDENSED, PANGO_STRETCH_ULTRA_CONDENSED},
  {GST_FONT_STRETCH_EXTRA_CONDENSED, PANGO_STRETCH_EXTRA_CONDENSED},
  {GST_FONT_STRETCH_CONDENSED, PANGO_STRETCH_CONDENSED},
  {GST_FONT_STRETCH_SEMI_CONDENSED, PANGO_STRETCH_SEMI_CONDENSED},
  {GST_FONT_STRETCH_NORMAL, PANGO_STRETCH_NORMAL},
  {GST_FONT_STRETCH_SEMI_EXPANDED, PANGO_STRETCH_SEMI_EXPANDED},
  {GST_FONT_STRETCH_EXPANDED, PANGO_STRETCH_EXPANDED},
  {GST_FONT_STRETCH_EXTRA_EXPANDED, PANGO_STRETCH_EXTRA_EXPANDED},
  {GST_FONT_STRETCH_ULTRA_EXPANDED, PANGO_STRETCH_ULTRA_EXPANDED},
};

static gint
gst_pango_font_stretch_to_gst (PangoStretch stretch)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (font_stretch_map); i++) {
    if (font_stretch_map[i].pango_stretch == stretch)
      return font_stretch_map[i].gst_stretch;
  }

  return GST_FONT_STRETCH_NORMAL;
}

GstTextLayout *
gst_text_layout_from_pango_markup (const gchar * markup)
{
  PangoAttrList *list = NULL;
  char *str = NULL;
  GstTextLayout *layout = NULL;
  PangoAttrIterator *pango_iter;
  PangoAttrString *attr_str;
  PangoAttrInt *attr_int;
  PangoAttrSize *attr_size;
  PangoAttrColor *attr_color;

  if (!markup || markup[0] == '\0')
    return NULL;

  if (!pango_parse_markup (markup, -1, 0, &list, &str, NULL, NULL))
    return NULL;

  layout = gst_text_layout_new (str);
  pango_iter = pango_attr_list_get_iterator (list);
  do {
    GSList *attrs = pango_attr_iterator_get_attrs (pango_iter);
    GSList *attrs_iter;

    if (!attrs)
      continue;

    for (attrs_iter = attrs; attrs_iter; attrs_iter = g_slist_next (attrs_iter)) {
      PangoAttribute *attr = (PangoAttribute *) attrs_iter->data;
      guint len;
      GstTextAttr *text_attr = NULL;
      if (!attr->klass || attr->end_index <= attr->start_index)
        continue;

      len = attr->end_index - attr->start_index;
      switch (attr->klass->type) {
          /* TODO: PANGO_ATTR_LANGUAGE */
        case PANGO_ATTR_FAMILY:
          attr_str = pango_attribute_as_string (attr);
          if (attr_str) {
            text_attr = gst_text_attr_string_new (attr_str->value,
                GST_TEXT_ATTR_FONT_FAMILY, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_STYLE:
          attr_int = pango_attribute_as_int (attr);
          if (attr_int) {
            gint style = gst_pango_font_style_to_gst (attr_int->value);
            text_attr = gst_text_attr_int_new (style, GST_TEXT_ATTR_FONT_STYLE,
                attr->start_index, len);
          }
          break;
        case PANGO_ATTR_WEIGHT:
          attr_int = pango_attribute_as_int (attr);
          if (attr_int) {
            text_attr = gst_text_attr_int_new (attr_int->value,
                GST_TEXT_ATTR_FONT_WEIGHT, attr->start_index, len);
          }
          break;
          /* TODO: PANGO_ATTR_VARIANT */
        case PANGO_ATTR_STRETCH:
          attr_int = pango_attribute_as_int (attr);
          if (attr_int) {
            gint stretch = gst_pango_font_stretch_to_gst (attr_int->value);
            text_attr = gst_text_attr_int_new (stretch,
                GST_TEXT_ATTR_FONT_STRETCH, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_SIZE:
          attr_size = pango_attribute_as_size (attr);
          if (attr_size) {
            gdouble font_size = (gdouble) attr_size->size / PANGO_SCALE;
            text_attr = gst_text_attr_double_new (font_size,
                GST_TEXT_ATTR_FONT_SIZE, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_FOREGROUND:
          attr_color = pango_attribute_as_color (attr);
          if (attr_color) {
            GstTextColor color;
            color.red = attr_color->color.red;
            color.green = attr_color->color.green;
            color.blue = attr_color->color.blue;
            color.alpha = G_MAXUINT16;
            text_attr = gst_text_attr_color_new (&color,
                GST_TEXT_ATTR_FOREGROUND_COLOR, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_BACKGROUND:
          attr_color = pango_attribute_as_color (attr);
          if (attr_color) {
            GstTextColor color;
            color.red = attr_color->color.red;
            color.green = attr_color->color.green;
            color.blue = attr_color->color.blue;
            color.alpha = G_MAXUINT16;
            text_attr = gst_text_attr_color_new (&color,
                GST_TEXT_ATTR_BACKGROUND_COLOR, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_UNDERLINE:
          attr_int = pango_attribute_as_int (attr);
          if (attr_int) {
            GstTextUnderline underline = GST_TEXT_UNDERLINE_NONE;
            switch ((PangoUnderline) attr_int->value) {
              case PANGO_UNDERLINE_SINGLE:
                underline = GST_TEXT_UNDERLINE_SINGLE;
                break;
              case GST_TEXT_UNDERLINE_DOUBLE:
                underline = GST_TEXT_UNDERLINE_DOUBLE;
                break;
              default:
                break;
            }

            text_attr = gst_text_attr_int_new (underline,
                GST_TEXT_ATTR_UNDERLINE, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_STRIKETHROUGH:
          attr_int = pango_attribute_as_int (attr);
          if (attr_int) {
            GstTextStrikethrough strikethrough = GST_TEXT_STRIKETHROUGH_SINGLE;
            if (!attr_int->value)
              strikethrough = GST_TEXT_STRIKETHROUGH_NONE;
            text_attr = gst_text_attr_int_new (strikethrough,
                GST_TEXT_ATTR_STRIKETHROUGH, attr->start_index, len);
          }
          break;
          /* TODO:
           * PANGO_ATTR_RISE
           * PANGO_ATTR_SHAPE
           * PANGO_ATTR_SCALE
           * PANGO_ATTR_FALLBACK
           * PANGO_ATTR_LETTER_SPACING
           */
        case PANGO_ATTR_UNDERLINE_COLOR:
          attr_color = pango_attribute_as_color (attr);
          if (attr_color) {
            GstTextColor color;
            color.red = attr_color->color.red;
            color.green = attr_color->color.green;
            color.blue = attr_color->color.blue;
            color.alpha = G_MAXUINT16;
            text_attr = gst_text_attr_color_new (&color,
                GST_TEXT_ATTR_UNDERLINE_COLOR, attr->start_index, len);
          }
          break;
        case PANGO_ATTR_STRIKETHROUGH_COLOR:
          attr_color = pango_attribute_as_color (attr);
          if (attr_color) {
            GstTextColor color;
            color.red = attr_color->color.red;
            color.green = attr_color->color.green;
            color.blue = attr_color->color.blue;
            color.alpha = G_MAXUINT16;
            text_attr = gst_text_attr_color_new (&color,
                GST_TEXT_ATTR_STRIKETHROUGH_COLOR, attr->start_index, len);
          }
          break;
        default:
          /* TODO parse more attributes */
          break;
      }

      if (text_attr)
        gst_text_layout_set_attr (layout, text_attr);
    }
    g_slist_free_full (attrs, (GDestroyNotify) pango_attribute_destroy);
  } while (pango_attr_iterator_next (pango_iter));

  pango_attr_iterator_destroy (pango_iter);
  pango_attr_list_unref (list);
  g_free (str);

  return layout;
}
#else /* HAVE_PANGO */
static void
xml_text (GMarkupParseContext * context, const gchar * text, gsize text_len,
    gpointer user_data, GError ** error)
{
  gchar **accum = (gchar **) user_data;
  gchar *concat;

  if (*accum) {
    concat = g_strconcat (*accum, text, NULL);
    g_free (*accum);
    *accum = concat;
  } else {
    *accum = g_strdup (text);
  }
}

GstTextLayout *
gst_text_layout_from_pango_markup (const gchar * markup)
{
  GMarkupParser parser = { 0, };
  GMarkupParseContext *context;
  gchar *accum = NULL;
  GstTextLayout *layout = NULL;

  parser.text = xml_text;
  context = g_markup_parse_context_new (&parser,
      (GMarkupParseFlags) 0, &accum, NULL);

  if (!g_markup_parse_context_parse (context, "<root>", 6, NULL))
    goto error;

  if (!g_markup_parse_context_parse (context, markup, strlen (markup), NULL))
    goto error;

  if (!g_markup_parse_context_parse (context, "</root>", 7, NULL))
    goto error;

  if (!g_markup_parse_context_end_parse (context, NULL))
    goto error;

done:
  g_markup_parse_context_free (context);

  if (accum) {
    layout = gst_text_layout_new (accum);
    g_free (accum);
  }

  return layout;

error:
  g_free (accum);
  accum = NULL;
  goto done;
}
#endif

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

#include "gstpangooverlayobject.h"
#include <pango/pangocairo.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (pango_overlay_object_debug);
#define GST_CAT_DEFAULT pango_overlay_object_debug

struct _GstPangoOverlayObject
{
  GstObject parent;

  GstVideoInfo info;
  GstVideoInfo bgra_info;
  GstVideoInfo layout_info;

  PangoContext *context;
  PangoLayout *layout;

  GstBuffer *layout_buf;
  GstVideoOverlayRectangle *overlay_rect;
  GstTextLayout *prev_layout;

  guint window_width;
  guint window_height;

  gboolean attach_meta;
};

static void gst_pango_overlay_object_finalize (GObject * object);

#define gst_pango_overlay_object_parent_class parent_class
G_DEFINE_TYPE (GstPangoOverlayObject, gst_pango_overlay_object,
    GST_TYPE_OBJECT);

static void
gst_pango_overlay_object_class_init (GstPangoOverlayObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_pango_overlay_object_finalize;

  GST_DEBUG_CATEGORY_INIT (pango_overlay_object_debug,
      "pangooverlayobject", 0, "pangooverlayobject");
}

static void
gst_pango_overlay_object_init (GstPangoOverlayObject * self)
{
  PangoFontMap *fontmap;

  fontmap = pango_cairo_font_map_new ();
  self->context = pango_font_map_create_context (fontmap);
  g_object_unref (fontmap);

  pango_context_set_base_gravity (self->context, PANGO_GRAVITY_SOUTH);
}

static void
gst_pango_overlay_object_finalize (GObject * object)
{
  GstPangoOverlayObject *self = GST_PANGO_OVERLAY_OBJECT (object);

  g_clear_object (&self->context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

gboolean
gst_pango_overlay_object_start (GstPangoOverlayObject * object)
{
  if (!object->context)
    return FALSE;

  return TRUE;
}

gboolean
gst_pango_overlay_object_stop (GstPangoOverlayObject * object)
{
  g_clear_object (&object->layout);

  gst_clear_buffer (&object->layout_buf);
  g_clear_pointer (&object->overlay_rect, gst_video_overlay_rectangle_unref);

  return TRUE;
}

static GstBufferPool *
gst_pango_overlay_object_create_layout_pool (GstPangoOverlayObject * self,
    const GstVideoInfo * info)
{
  GstCaps *caps = NULL;
  GstStructure *config;
  GstBufferPool *pool = NULL;

  caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Couldn't create caps");
    return NULL;
  }

  pool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);
  gst_caps_unref (caps);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    goto error;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate pool");
    goto error;
  }

  return pool;

error:
  gst_clear_object (&pool);

  return NULL;
}

GstPangoOverlayObject *
gst_pango_overlay_object_new (void)
{
  GstPangoOverlayObject *self;

  self = g_object_new (GST_TYPE_PANGO_OVERLAY_OBJECT, NULL);
  gst_object_ref_sink (self);

  return self;
}

gboolean
gst_pango_overlay_object_set_caps (GstPangoOverlayObject * object,
    GstElement * elem, GstCaps * out_caps, gboolean * supported)
{
  gboolean is_system;
  GstCapsFeatures *features;

  *supported = FALSE;

  if (!gst_video_info_from_caps (&object->info, out_caps)) {
    GST_ERROR ("Invalid caps %" GST_PTR_FORMAT, out_caps);
    return FALSE;
  }

  object->window_width = object->info.width;
  object->window_height = object->info.height;

  gst_video_info_set_format (&object->bgra_info, GST_VIDEO_FORMAT_BGRA,
      object->info.width, object->info.height);

  features = gst_caps_get_features (out_caps, 0);
  is_system = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
  object->attach_meta = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

  if (!is_system && !object->attach_meta) {
    GST_WARNING_OBJECT (elem,
        "Not a system memory without composition meta support");
    return TRUE;
  }

  *supported = TRUE;

  return TRUE;
}

gboolean
gst_pango_overlay_object_decide_allocation (GstPangoOverlayObject * object,
    GstElement * elem, GstQuery * query)
{
  guint index;

  object->window_width = object->info.width;
  object->window_height = object->info.height;

  gst_clear_buffer (&object->layout_buf);
  g_clear_pointer (&object->overlay_rect, gst_video_overlay_rectangle_unref);

  if (object->attach_meta && gst_query_find_allocation_meta (query,
          GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &index)) {
    const GstStructure *params;
    guint width, height;
    gst_query_parse_nth_allocation_meta (query, index, &params);
    if (params) {
      if (gst_structure_get (params, "width", G_TYPE_UINT, &width,
              "height", G_TYPE_UINT, &height, NULL)) {
        GST_DEBUG_OBJECT (elem, "Window size %dx%d", width, height);
        if (width > 0 && height > 0) {
          object->window_width = width;
          object->window_height = height;
        }
      }
    }
  }

  return TRUE;
}

gboolean
gst_pango_overlay_object_accept_attribute (GstPangoOverlayObject * object,
    GstTextAttr * attr)
{
  GstTextAttrType attr_type;

  attr_type = gst_text_attr_identify (attr, NULL, NULL);

  /* XXX: pango does not respect background alpha,
   * and outline color attribute does not exist. Use default ones */
  switch (attr_type) {
    case GST_TEXT_ATTR_BACKGROUND_COLOR:
    case GST_TEXT_ATTR_OUTLINE_COLOR:
    case GST_TEXT_ATTR_SHADOW_COLOR:
      return FALSE;
    default:
      break;
  }

  return TRUE;
}

static gboolean
gst_pango_overlay_object_create_layout (GstPangoOverlayObject * self,
    GstTextLayout * layout, guint width, guint height, gdouble * max_font_size,
    GstTextColor * background_color, GstTextColor * outline_color)
{
  const gchar *text;
  GstWordWrapMode wrap_mode;
  GstTextAlignment text_align;
  GstTextAttrIterator *iter;
  PangoAttrList *attr_list;
  gboolean have_background_color = FALSE;
  gboolean have_outline_color = FALSE;

  g_clear_object (&self->layout);

  *max_font_size = 0;
  memset (background_color, 0, sizeof (GstTextColor));
  memset (outline_color, 0, sizeof (GstTextColor));

  self->layout = pango_layout_new (self->context);

  text = gst_text_layout_get_text (layout);
  pango_layout_set_text (self->layout, text, strlen (text));
  pango_layout_set_width (self->layout, width * PANGO_SCALE);
  pango_layout_set_height (self->layout, height * PANGO_SCALE);

  wrap_mode = gst_text_layout_get_word_wrap (layout);
  switch (wrap_mode) {
    case GST_WORD_WRAP_WORD:
      pango_layout_set_wrap (self->layout, PANGO_WRAP_WORD);
      break;
    case GST_WORD_WRAP_CHAR:
      pango_layout_set_wrap (self->layout, PANGO_WRAP_CHAR);
      break;
    case GST_WORD_WRAP_NO_WRAP:
      pango_layout_set_width (self->layout, -1);
      break;
    default:
      break;
  }

  text_align = gst_text_layout_get_text_alignment (layout);
  switch (text_align) {
    case GST_TEXT_ALIGNMENT_LEFT:
      pango_layout_set_alignment (self->layout, PANGO_ALIGN_LEFT);
      break;
    case GST_TEXT_ALIGNMENT_CENTER:
      pango_layout_set_alignment (self->layout, PANGO_ALIGN_CENTER);
      break;
    case GST_TEXT_ALIGNMENT_RIGHT:
      pango_layout_set_alignment (self->layout, PANGO_ALIGN_RIGHT);
      break;
    case GST_TEXT_ALIGNMENT_JUSTIFIED:
      pango_layout_set_justify (self->layout, TRUE);
      break;
    default:
      break;
  }

  attr_list = pango_attr_list_new ();
  iter = gst_text_layout_get_attr_iterator (layout);
  do {
    guint size;
    guint i;

    size = gst_text_attr_iterator_get_size (iter);
    if (!size)
      break;

    for (i = 0; i < size; i++) {
      GstTextAttr *attr;
      PangoAttribute *pango_attr = NULL;
      GstTextAttrType attr_type;
      guint start, len;

      attr = gst_text_attr_iterator_get_attr (iter, i);
      attr_type = gst_text_attr_identify (attr, &start, &len);
      switch (attr_type) {
        case GST_TEXT_ATTR_FONT_FAMILY:
        {
          const gchar *family = NULL;
          if (gst_text_layout_attr_get_string (attr, &family) &&
              family && family[0] != '\0') {
            pango_attr = pango_attr_family_new (family);
          }
          break;
        }
        case GST_TEXT_ATTR_FONT_SIZE:
        {
          gdouble font_size;
          if (gst_text_layout_attr_get_double (attr, &font_size)) {
            pango_attr = pango_attr_size_new (font_size * PANGO_SCALE);
            if (font_size > *max_font_size)
              *max_font_size = font_size;
          }
          break;
        }
        case GST_TEXT_ATTR_FONT_WEIGHT:
        {
          gint weight;
          if (gst_text_layout_attr_get_int (attr, &weight))
            pango_attr = pango_attr_weight_new (weight);
          break;
        }
        case GST_TEXT_ATTR_FONT_STYLE:
        {
          gint style;
          if (gst_text_layout_attr_get_int (attr, &style)) {
            switch ((GstFontStyle) style) {
              case GST_FONT_STYLE_NORMAL:
                pango_attr = pango_attr_style_new (PANGO_STYLE_NORMAL);
                break;
              case GST_FONT_STYLE_OBLIQUE:
                pango_attr = pango_attr_style_new (PANGO_STYLE_OBLIQUE);
                break;
              case GST_FONT_STYLE_ITALIC:
                pango_attr = pango_attr_style_new (PANGO_STYLE_ITALIC);
                break;
              default:
                break;
            }
          }
          break;
        }
        case GST_TEXT_ATTR_FONT_STRETCH:
        {
          gint stretch;
          if (gst_text_layout_attr_get_int (attr, &stretch)) {
            switch ((GstFontStretch) stretch) {
              case GST_FONT_STRETCH_ULTRA_CONDENSED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_ULTRA_CONDENSED);
                break;
              case GST_FONT_STRETCH_EXTRA_CONDENSED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_EXTRA_CONDENSED);
                break;
              case GST_FONT_STRETCH_CONDENSED:
                pango_attr = pango_attr_stretch_new (PANGO_STRETCH_CONDENSED);
                break;
              case GST_FONT_STRETCH_SEMI_CONDENSED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_SEMI_CONDENSED);
                break;
              case GST_FONT_STRETCH_NORMAL:
                pango_attr = pango_attr_stretch_new (PANGO_STRETCH_NORMAL);
                break;
              case GST_FONT_STRETCH_SEMI_EXPANDED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_SEMI_EXPANDED);
                break;
              case GST_FONT_STRETCH_EXPANDED:
                pango_attr = pango_attr_stretch_new (PANGO_STRETCH_EXPANDED);
                break;
              case GST_FONT_STRETCH_EXTRA_EXPANDED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_EXTRA_EXPANDED);
                break;
              case GST_FONT_STRETCH_ULTRA_EXPANDED:
                pango_attr =
                    pango_attr_stretch_new (PANGO_STRETCH_ULTRA_EXPANDED);
                break;
              default:
                break;
            }
          }
          break;
        }
        case GST_TEXT_ATTR_UNDERLINE:
        {
          gint underline;
          if (gst_text_layout_attr_get_int (attr, &underline)) {
            switch ((GstTextUnderline) underline) {
              case GST_TEXT_UNDERLINE_NONE:
                pango_attr = pango_attr_underline_new (PANGO_UNDERLINE_NONE);
                break;
              case GST_TEXT_UNDERLINE_SINGLE:
                pango_attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
                break;
              case GST_TEXT_UNDERLINE_DOUBLE:
                pango_attr = pango_attr_underline_new (PANGO_UNDERLINE_DOUBLE);
                break;
              default:
                break;
            }
          }
          break;
        }
        case GST_TEXT_ATTR_STRIKETHROUGH:
        {
          gint strikethrough;
          if (gst_text_layout_attr_get_int (attr, &strikethrough)) {
            if ((GstTextStrikethrough) strikethrough !=
                GST_TEXT_STRIKETHROUGH_NONE) {
              pango_attr = pango_attr_strikethrough_new (TRUE);
            } else {
              pango_attr = pango_attr_strikethrough_new (FALSE);
            }
          }
          break;
        }
        case GST_TEXT_ATTR_FOREGROUND_COLOR:
        {
          GstTextColor color;
          if (gst_text_layout_attr_get_color (attr, &color)) {
            pango_attr = pango_attr_foreground_new (color.red,
                color.green, color.blue);
            pango_attr_list_insert (attr_list, pango_attr);
            /* XXX: This alpha attribute seems to be ignored by pango */
            pango_attr = pango_attr_foreground_alpha_new (color.alpha);
          }
          break;
        }
        case GST_TEXT_ATTR_BACKGROUND_COLOR:
          /* Use global background color */
          if (!have_background_color) {
            GstTextColor color;
            if (gst_text_layout_attr_get_color (attr, &color)) {
              *background_color = color;
              have_background_color = TRUE;
            }
          }
          break;
        case GST_TEXT_ATTR_OUTLINE_COLOR:
          /* Use global outline color */
          if (!have_outline_color) {
            GstTextColor color;
            if (gst_text_layout_attr_get_color (attr, &color)) {
              *outline_color = color;
              have_outline_color = TRUE;
            }
          }
          break;
        case GST_TEXT_ATTR_UNDERLINE_COLOR:
        {
          GstTextColor color;
          if (gst_text_layout_attr_get_color (attr, &color)) {
            pango_attr = pango_attr_underline_color_new (color.red,
                color.green, color.blue);
          }
          break;
        }
        case GST_TEXT_ATTR_STRIKETHROUGH_COLOR:
        {
          GstTextColor color;
          if (gst_text_layout_attr_get_color (attr, &color)) {
            pango_attr = pango_attr_strikethrough_color_new (color.red,
                color.green, color.blue);
          }
          break;
        }
        default:
          break;
      }

      if (pango_attr)
        pango_attr_list_insert (attr_list, pango_attr);
    }
  } while (gst_text_attr_iterator_next (iter));
  gst_text_attr_iterator_free (iter);

  pango_layout_set_attributes (self->layout, attr_list);
  pango_attr_list_unref (attr_list);

  return TRUE;
}

static gboolean
gst_pango_overlay_object_draw_layout (GstPangoOverlayObject * self,
    GstTextLayout * layout, gint * x, gint * y)
{
  GstVideoFrame frame;
  guint width, height;
  cairo_t *cr;
  cairo_surface_t *surface;
  PangoRectangle ink_rect, logical_rect;
  GstTextColor background_color;
  GstTextColor outline_color;
  gdouble font_size = 0;
  gdouble outline_offset = 0;
  gdouble background_offset = 0;
  guint layout_width, layout_height;
  guint scaled_width, scaled_height;
  GstParagraphAlignment paragraph_align;
  cairo_matrix_t scale_matrix;

  if (self->layout_buf) {
    if (self->prev_layout && self->prev_layout == layout)
      return TRUE;

    gst_clear_buffer (&self->layout_buf);
    g_clear_pointer (&self->overlay_rect, gst_video_overlay_rectangle_unref);
  }

  gst_clear_text_layout (&self->prev_layout);
  self->prev_layout = gst_text_layout_ref (layout);

  width = gst_text_layout_get_width (layout);
  height = gst_text_layout_get_height (layout);

  if (!gst_pango_overlay_object_create_layout (self, layout, width, height,
          &font_size, &background_color, &outline_color)) {
    return FALSE;
  }

  if (background_color.alpha) {
    background_offset = font_size / 10.0;
    background_offset = MAX (background_offset, 1.0);
  }

  if (outline_color.alpha) {
    outline_offset = font_size / 15.0;
    outline_offset = MAX (outline_offset, 1.0);
  }

  pango_layout_get_pixel_extents (self->layout, &ink_rect, &logical_rect);

  layout_width = ink_rect.x + ink_rect.width +
      ceil (outline_offset + background_offset * 2);
  scaled_width = width = MIN (layout_width, width);

  layout_height = ink_rect.y + ink_rect.height +
      ceil (outline_offset + background_offset * 2);
  scaled_height = height = MIN (layout_height, height);

  paragraph_align = gst_text_layout_get_paragraph_alignment (layout);
  if (paragraph_align == GST_PARAGRAPH_ALIGNMENT_BOTTOM) {
    gint y_offset = gst_text_layout_get_height (layout);
    y_offset -= (gint) height;
    *y += y_offset;
  } else if (paragraph_align == GST_PARAGRAPH_ALIGNMENT_CENTER) {
    gint y_offset = gst_text_layout_get_height (layout);
    y_offset -= (gint) height;
    *y += y_offset / 2;
  }

  if (self->attach_meta && (self->window_width != self->info.width ||
          self->window_height != self->info.height)) {
    gdouble scale_x, scale_y;

    scale_x = (gdouble) self->window_width / self->info.width;
    scale_y = (gdouble) self->window_height / self->info.height;

    cairo_matrix_init_scale (&scale_matrix, scale_x, scale_y);
    scaled_width = scale_x * width;
    scaled_height = scale_y * height;
  } else {
    cairo_matrix_init_scale (&scale_matrix, 1, 1);
  }

  gst_video_info_set_format (&self->layout_info, GST_VIDEO_FORMAT_BGRA,
      scaled_width, scaled_height);
  self->layout_info.flags |= GST_VIDEO_FLAG_PREMULTIPLIED_ALPHA;

  self->layout_buf = gst_buffer_new_and_alloc (self->layout_info.size);
  gst_buffer_add_video_meta_full (self->layout_buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_BGRA, scaled_width, scaled_height, 1,
      self->layout_info.offset, self->layout_info.stride);

  gst_video_frame_map (&frame, &self->layout_info, self->layout_buf,
      GST_MAP_READWRITE);
  surface =
      cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA (&frame,
          0), CAIRO_FORMAT_ARGB32, scaled_width, scaled_height,
      GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0));
  cr = cairo_create (surface);

  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_matrix (cr, &scale_matrix);

  if (background_color.alpha) {
    cairo_save (cr);
    cairo_set_source_rgba (cr, (gdouble) background_color.red / G_MAXUINT16,
        (gdouble) background_color.green / G_MAXUINT16,
        (gdouble) background_color.blue / G_MAXUINT16,
        (gdouble) background_color.alpha / G_MAXUINT16);
    cairo_rectangle (cr, (gdouble) ink_rect.x - background_offset,
        (gdouble) ink_rect.y - background_offset,
        (gdouble) width + background_offset * 2,
        (gdouble) height + background_offset * 2);
    cairo_fill (cr);
    cairo_restore (cr);
  }

  cairo_translate (cr, background_offset, background_offset);

  if (outline_color.alpha) {
    cairo_save (cr);
    cairo_set_source_rgba (cr, (gdouble) outline_color.red / G_MAXUINT16,
        (gdouble) outline_color.green / G_MAXUINT16,
        (gdouble) outline_color.blue / G_MAXUINT16,
        (gdouble) outline_color.alpha / G_MAXUINT16);
    cairo_set_line_width (cr, outline_offset);
    pango_cairo_layout_path (cr, self->layout);
    cairo_stroke (cr);
    cairo_restore (cr);
  }

  pango_cairo_show_layout (cr, self->layout);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  gst_video_frame_unmap (&frame);

  self->overlay_rect = gst_video_overlay_rectangle_new_raw (self->layout_buf,
      *x, *y, width, height, GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

  return TRUE;
}

static gboolean
gst_pango_overlay_object_mode_attach (GstPangoOverlayObject * self,
    GstBuffer * buffer)
{
  GstVideoOverlayCompositionMeta *meta;

  meta = gst_buffer_get_video_overlay_composition_meta (buffer);
  if (meta) {
    if (meta->overlay) {
      meta->overlay =
          gst_video_overlay_composition_make_writable (meta->overlay);
      gst_video_overlay_composition_add_rectangle (meta->overlay,
          self->overlay_rect);
    } else {
      meta->overlay = gst_video_overlay_composition_new (self->overlay_rect);
    }
  } else {
    GstVideoOverlayComposition *comp =
        gst_video_overlay_composition_new (self->overlay_rect);
    meta = gst_buffer_add_video_overlay_composition_meta (buffer, comp);
    gst_video_overlay_composition_unref (comp);
  }

  return TRUE;
}

static gboolean
gst_pango_overlay_mode_blend (GstPangoOverlayObject * self,
    GstBuffer * buffer, gint x, gint y)
{
  GstVideoFrame dst_frame, src_frame;
  gboolean ret;

  if (!gst_video_frame_map (&dst_frame, &self->info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&src_frame, &self->layout_info, self->layout_buf,
          GST_MAP_READ)) {
    gst_video_frame_unmap (&dst_frame);
    GST_ERROR_OBJECT (self, "Couldn't map text buffer");
    return FALSE;
  }

  ret = gst_video_blend (&dst_frame, &src_frame, x, y, 1.0);
  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dst_frame);

  return ret;
}

GstFlowReturn
gst_pango_overlay_object_draw (GstPangoOverlayObject * object,
    GstTextLayout * layout, GstBuffer * buffer)
{
  gboolean ret = FALSE;
  gint x, y;

  x = gst_text_layout_get_xpos (layout);
  y = gst_text_layout_get_ypos (layout);

  if (!gst_pango_overlay_object_draw_layout (object, layout, &x, &y))
    return GST_FLOW_ERROR;

  if (object->attach_meta)
    ret = gst_pango_overlay_object_mode_attach (object, buffer);
  else
    ret = gst_pango_overlay_mode_blend (object, buffer, x, y);

  if (!ret)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

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
#include <config.h>
#endif

#include "gstbasetextlayoutoverlay.h"
#include "gsttextlayoutoverlay-private.h"

GST_DEBUG_CATEGORY_STATIC (base_text_layout_overlay_debug);
#define GST_CAT_DEFAULT base_text_layout_overlay_debug

#define META_TAG_VIDEO meta_tag_video_quark
static GQuark meta_tag_video_quark;

#define DEFAULT_SCALE_BASIS 640

enum
{
  PROP_0,
  PROP_VISIBLE,
  PROP_FONT_FAMILY,
  PROP_FONT_SIZE,
  PROP_AUTO_RESIZE,
  PROP_FONT_WEIGHT,
  PROP_FONT_STYLE,
  PROP_FONT_STRETCH,
  PROP_WORD_WRAP,
  PROP_TEXT_ALIGNMENT,
  PROP_PARAGRAPH_ALIGNMENT,
  PROP_TEXT,
  PROP_FOREGROUND_COLOR,
  PROP_OUTLINE_COLOR,
  PROP_UNDERLINE_COLOR,
  PROP_STRIKETHROUGH_COLOR,
  PROP_SHADOW_COLOR,
  PROP_BACKGROUND_COLOR,
  PROP_LAYOUT_X,
  PROP_LAYOUT_Y,
  PROP_LAYOUT_WIDTH,
  PROP_LAYOUT_HEIGHT,
  PROP_LAST,
};

#define DEFAULT_VISIBLE TRUE
#define DEFAULT_FONT_FAMILY "Arial"
#define DEFAULT_FONT_SIZE 14
#define DEFAULT_AUTO_RESIZE TRUE
#define DEFAULT_FONT_WEIGHT GST_FONT_WEIGHT_NORMAL
#define DEFAULT_FONT_STYLE GST_FONT_STYLE_NORMAL
#define DEFAULT_FONT_STRETCH GST_FONT_STRETCH_NORMAL
#define DEFAULT_FOREGROUND_COLOR G_MAXUINT32
#define DEFAULT_OUTLINE_COLOR 0xff000000
#define DEFAULT_SHADOW_COLOR 0x80000000
#define DEFAULT_BACKGROUND_COLOR 0x0
#define DEFAULT_LAYOUT_XY 0.04f
#define DEFAULT_LAYOUT_WH 0.92f
#define DEFAULT_WORD_WRAP GST_WORD_WRAP_WORD
#define DEFAULT_TEXT_ALIGNMENT GST_TEXT_ALIGNMENT_LEFT
#define DEFAULT_PARAGRAPH_ALIGNMENT GST_PARAGRAPH_ALIGNMENT_TOP

struct _GstBaseTextLayoutOverlayPrivate
{
  GMutex lock;

  GstTextLayout *subclass_layout;
  GstTextLayout *decorated_layout;
  guint window_width;
  guint window_height;
  guint calculated_layout_x;
  guint calculated_layout_y;
  guint calculated_layout_width;
  guint calculated_layout_height;
  guint calculated_font_size;

  /* properties */
  gboolean visible;
  gchar *font_family;
  gdouble font_size;
  gboolean auto_resize;
  GstFontWeight weight;
  GstFontStyle style;
  GstFontStretch stretch;
  GstWordWrapMode word_wrap;
  GstTextAlignment text_align;
  GstParagraphAlignment paragraph_align;
  gchar *user_text;
  guint foreground_color;
  guint outline_color;
  guint underline_color;
  guint strikethrough_color;
  guint shadow_color;
  guint background_color;
  gdouble layout_x;
  gdouble layout_y;
  gdouble layout_width;
  gdouble layout_height;
};

static void gst_base_text_layout_overlay_finalize (GObject * object);
static void gst_base_text_layout_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_text_layout_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_base_text_layout_overlay_start (GstBaseTransform * trans);
static gboolean gst_base_text_layout_overlay_stop (GstBaseTransform * trans);
static gboolean gst_base_text_layout_overlay_sink_event (GstBaseTransform *
    trans, GstEvent * event);
static GstCaps *gst_base_text_layout_overlay_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_base_text_layout_overlay_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_base_text_layout_overlay_fixate_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_base_text_layout_overlay_transform_meta (GstBaseTransform *
    trans, GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn
gst_base_text_layout_overlay_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static GstFlowReturn gst_base_text_layout_overlay_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static void gst_text_layout_calculate_size (GstBaseTextLayoutOverlay * self);
static void gst_base_text_layout_overlay_clear_layout (GstBaseTextLayoutOverlay
    * self);
static gboolean
gst_base_text_layout_overlay_accept_attribute_default (GstBaseTextLayoutOverlay
    * self, GstTextAttr * attr);
static GstFlowReturn
gst_base_text_layout_overlay_process_input_default (GstBaseTextLayoutOverlay *
    self, GstBuffer * buffer);
static GstFlowReturn
gst_base_text_layout_overlay_generate_output_default (GstBaseTextLayoutOverlay *
    self, GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf);

#define gst_base_text_layout_overlay_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstBaseTextLayoutOverlay,
    gst_base_text_layout_overlay, GST_TYPE_BASE_TRANSFORM);

static void
gst_base_text_layout_overlay_class_init (GstBaseTextLayoutOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_base_text_layout_overlay_finalize;
  object_class->set_property = gst_base_text_layout_overlay_set_property;
  object_class->get_property = gst_base_text_layout_overlay_get_property;

  gst_base_text_layout_overlay_install_properties (object_class, 0, NULL);

  trans_class->start = GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_transform_caps);
  trans_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_set_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_fixate_caps);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_transform_meta);
  trans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_prepare_output_buffer);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_transform);

  klass->process_input =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_process_input_default);
  klass->accept_attribute =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_accept_attribute_default);
  klass->generate_output =
      GST_DEBUG_FUNCPTR (gst_base_text_layout_overlay_generate_output_default);

  GST_DEBUG_CATEGORY_INIT (base_text_layout_overlay_debug,
      "basetextlayoutoverlay", 0, "basetextlayoutoverlay");

  meta_tag_video_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_STR);
}

static void
gst_base_text_layout_overlay_init (GstBaseTextLayoutOverlay * self)
{
  GstBaseTextLayoutOverlayPrivate *priv;

  self->priv = priv = gst_base_text_layout_overlay_get_instance_private (self);

  g_mutex_init (&priv->lock);
  priv->window_width = 640;
  priv->window_height = 480;

  priv->visible = DEFAULT_VISIBLE;
  priv->font_family = g_strdup (DEFAULT_FONT_FAMILY);
  priv->font_size = DEFAULT_FONT_SIZE;
  priv->auto_resize = DEFAULT_AUTO_RESIZE;
  priv->weight = DEFAULT_FONT_WEIGHT;
  priv->style = DEFAULT_FONT_STYLE;
  priv->stretch = DEFAULT_FONT_STRETCH;
  priv->word_wrap = DEFAULT_WORD_WRAP;
  priv->text_align = DEFAULT_TEXT_ALIGNMENT;
  priv->paragraph_align = DEFAULT_PARAGRAPH_ALIGNMENT;
  priv->foreground_color = DEFAULT_FOREGROUND_COLOR;
  priv->outline_color = DEFAULT_OUTLINE_COLOR;
  priv->underline_color = DEFAULT_FOREGROUND_COLOR;
  priv->strikethrough_color = DEFAULT_FOREGROUND_COLOR;
  priv->shadow_color = DEFAULT_SHADOW_COLOR;
  priv->background_color = DEFAULT_BACKGROUND_COLOR;

  priv->layout_x = priv->layout_y = DEFAULT_LAYOUT_XY;
  priv->layout_width = priv->layout_height = DEFAULT_LAYOUT_WH;
}

static void
gst_base_text_layout_overlay_finalize (GObject * object)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (object);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_color (GstBaseTextLayoutOverlay * self, const GValue * value,
    guint * prev)
{
  guint val = g_value_get_uint (value);
  if (val != *prev) {
    *prev = val;
    gst_base_text_layout_overlay_clear_layout (self);
  }
}

static gboolean
update_double (GstBaseTextLayoutOverlay * self, const GValue * value,
    gdouble * prev)
{
  gdouble val = g_value_get_double (value);
  if (val != *prev) {
    *prev = val;
    gst_base_text_layout_overlay_clear_layout (self);
    return TRUE;
  }

  return FALSE;
}

static void
update_enum (GstBaseTextLayoutOverlay * self, const GValue * value, gint * prev)
{
  gint val = g_value_get_enum (value);
  if (val != *prev) {
    *prev = val;
    gst_base_text_layout_overlay_clear_layout (self);
  }
}

static void
gst_base_text_layout_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (object);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_VISIBLE:
      priv->visible = g_value_get_boolean (value);
      break;
    case PROP_FONT_FAMILY:{
      const gchar *font_family = g_value_get_string (value);
      const gchar *font;

      if (font_family)
        font = font_family;
      else
        font = DEFAULT_FONT_FAMILY;

      if (g_strcmp0 (font, priv->font_family)) {
        g_free (priv->font_family);
        priv->font_family = g_strdup (font);
        gst_base_text_layout_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_FONT_SIZE:
    {
      gdouble font_size = g_value_get_double (value);
      if (font_size != priv->font_size) {
        priv->font_size = font_size;
        gst_base_text_layout_overlay_clear_layout (self);
        gst_text_layout_calculate_size (self);
      }
      break;
    }
    case PROP_AUTO_RESIZE:
    {
      gboolean auto_resize = g_value_get_uint (value);
      if (auto_resize != priv->auto_resize) {
        priv->auto_resize = auto_resize;
        gst_base_text_layout_overlay_clear_layout (self);
        gst_text_layout_calculate_size (self);
      }
      break;
    }
    case PROP_FONT_WEIGHT:
      update_enum (self, value, (gint *) & priv->weight);
      break;
    case PROP_FONT_STYLE:
      update_enum (self, value, (gint *) & priv->style);
      break;
    case PROP_FONT_STRETCH:
      update_enum (self, value, (gint *) & priv->stretch);
      break;
    case PROP_WORD_WRAP:
      update_enum (self, value, (gint *) & priv->word_wrap);
      break;
    case PROP_TEXT_ALIGNMENT:
      update_enum (self, value, (gint *) & priv->text_align);
      break;
    case PROP_PARAGRAPH_ALIGNMENT:
      update_enum (self, value, (gint *) & priv->paragraph_align);
      break;
    case PROP_TEXT:
      const gchar *new_text = g_value_get_string (value);

      if (g_strcmp0 (priv->user_text, new_text) != 0) {
        g_free (priv->user_text);
        priv->user_text = g_strdup (new_text);
        gst_base_text_layout_overlay_clear_layout (self);
      }
      break;
    case PROP_FOREGROUND_COLOR:
      update_color (self, value, &priv->foreground_color);
      break;
    case PROP_OUTLINE_COLOR:
      update_color (self, value, &priv->outline_color);
      break;
    case PROP_UNDERLINE_COLOR:
      update_color (self, value, &priv->underline_color);
      break;
    case PROP_STRIKETHROUGH_COLOR:
      update_color (self, value, &priv->strikethrough_color);
      break;
    case PROP_SHADOW_COLOR:
      update_color (self, value, &priv->shadow_color);
      break;
    case PROP_BACKGROUND_COLOR:
      update_color (self, value, &priv->background_color);
      break;
    case PROP_LAYOUT_X:
      if (update_double (self, value, &priv->layout_x))
        gst_text_layout_calculate_size (self);
      break;
    case PROP_LAYOUT_Y:
      if (update_double (self, value, &priv->layout_y))
        gst_text_layout_calculate_size (self);
      break;
    case PROP_LAYOUT_WIDTH:
      if (update_double (self, value, &priv->layout_width))
        gst_text_layout_calculate_size (self);
      break;
    case PROP_LAYOUT_HEIGHT:
      if (update_double (self, value, &priv->layout_height))
        gst_text_layout_calculate_size (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static void
gst_base_text_layout_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (object);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_VISIBLE:
      g_value_set_boolean (value, priv->visible);
      break;
    case PROP_FONT_FAMILY:
      g_value_set_string (value, priv->font_family);
      break;
    case PROP_FONT_SIZE:
      g_value_set_double (value, priv->font_size);
      break;
    case PROP_AUTO_RESIZE:
      g_value_set_boolean (value, priv->auto_resize);
      break;
    case PROP_FONT_WEIGHT:
      g_value_set_enum (value, priv->weight);
      break;
    case PROP_FONT_STYLE:
      g_value_set_enum (value, priv->style);
      break;
    case PROP_FONT_STRETCH:
      g_value_set_enum (value, priv->stretch);
      break;
    case PROP_WORD_WRAP:
      g_value_set_enum (value, priv->word_wrap);
      break;
    case PROP_TEXT_ALIGNMENT:
      g_value_set_enum (value, priv->text_align);
      break;
    case PROP_PARAGRAPH_ALIGNMENT:
      g_value_set_enum (value, priv->paragraph_align);
      break;
    case PROP_TEXT:
      g_value_set_string (value, priv->user_text);
      break;
    case PROP_FOREGROUND_COLOR:
      g_value_set_uint (value, priv->foreground_color);
      break;
    case PROP_OUTLINE_COLOR:
      g_value_set_uint (value, priv->outline_color);
      break;
    case PROP_UNDERLINE_COLOR:
      g_value_set_uint (value, priv->underline_color);
      break;
    case PROP_STRIKETHROUGH_COLOR:
      g_value_set_uint (value, priv->strikethrough_color);
      break;
    case PROP_SHADOW_COLOR:
      g_value_set_uint (value, priv->shadow_color);
      break;
    case PROP_BACKGROUND_COLOR:
      g_value_set_uint (value, priv->background_color);
      break;
    case PROP_LAYOUT_X:
      g_value_set_double (value, priv->layout_x);
      break;
    case PROP_LAYOUT_Y:
      g_value_set_double (value, priv->layout_y);
      break;
    case PROP_LAYOUT_WIDTH:
      g_value_set_double (value, priv->layout_width);
      break;
    case PROP_LAYOUT_HEIGHT:
      g_value_set_double (value, priv->layout_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&priv->lock);
}

static void
gst_base_text_layout_overlay_reset (GstBaseTextLayoutOverlay * self)
{
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  gst_clear_text_layout (&priv->subclass_layout);
  gst_clear_text_layout (&priv->decorated_layout);
}

static gboolean
gst_base_text_layout_overlay_start (GstBaseTransform * trans)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (trans);

  gst_base_text_layout_overlay_reset (self);

  return TRUE;
}

static gboolean
gst_base_text_layout_overlay_stop (GstBaseTransform * trans)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (trans);

  gst_base_text_layout_overlay_reset (self);

  return TRUE;
}

static GstCaps *
gst_base_text_layout_overlay_add_feature (GstCaps * caps)
{
  GstCaps *new_caps = gst_caps_new_empty ();
  guint caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstCapsFeatures *f =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *c = gst_caps_new_full (gst_structure_copy (s), NULL);

    if (!gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      gst_caps_features_add (f,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    }

    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
gst_base_text_layout_overlay_remove_feature (GstCaps * caps)
{
  GstCaps *new_caps = gst_caps_new_empty ();
  guint caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstCapsFeatures *f =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *c = gst_caps_new_full (gst_structure_copy (s), NULL);

    if (!gst_caps_features_is_any (f) &&
        !gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      gst_caps_features_add (f,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    }

    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
gst_base_text_layout_overlay_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = gst_base_text_layout_overlay_add_feature (caps);
    tmp = gst_caps_merge (tmp, gst_caps_ref (caps));
  } else {
    tmp = gst_base_text_layout_overlay_remove_feature (caps);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

/* Called with lock taken */
static gdouble
gst_text_layout_calculate_font_size (GstBaseTextLayoutOverlay * self,
    gdouble font_size)
{
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  if (priv->auto_resize)
    return font_size * priv->window_width / DEFAULT_SCALE_BASIS;

  return font_size;
}

/* Called with lock taken */
static void
gst_text_layout_calculate_size (GstBaseTextLayoutOverlay * self)
{
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  priv->calculated_font_size = gst_text_layout_calculate_font_size (self,
      priv->font_size);

  priv->calculated_layout_x = priv->layout_x * priv->window_width;
  priv->calculated_layout_y = priv->layout_y * priv->window_height;
  priv->calculated_layout_width = priv->layout_width * priv->window_width;
  priv->calculated_layout_height = priv->layout_height * priv->window_height;

  gst_clear_text_layout (&priv->decorated_layout);
}

static gboolean
gst_base_text_layout_overlay_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (trans);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;
  GstBaseTextLayoutOverlayClass *klass =
      GST_BASE_TEXT_LAYOUT_OVERLAY_GET_CLASS (self);

  if (!gst_video_info_from_caps (&self->in_info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&self->out_info, outcaps)) {
    GST_ERROR_OBJECT (self, "Invalid output caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  g_mutex_lock (&priv->lock);
  priv->window_width = self->out_info.width;
  priv->window_height = self->out_info.height;
  gst_text_layout_calculate_size (self);
  g_mutex_unlock (&priv->lock);

  if (klass->set_info) {
    return klass->set_info (self,
        incaps, &self->in_info, outcaps, &self->out_info);
  }

  return TRUE;
}

static GstCaps *
gst_base_text_layout_overlay_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *overlay_caps = NULL;
  guint i;
  guint caps_size = gst_caps_get_size (othercaps);

  /* Prefer overlaycomposition caps */
  for (i = 0; i < caps_size; i++) {
    GstCapsFeatures *f = gst_caps_get_features (othercaps, i);

    if (f && !gst_caps_features_is_any (f) &&
        gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      GstStructure *s = gst_caps_get_structure (othercaps, i);
      overlay_caps = gst_caps_new_full (gst_structure_copy (s), NULL);
      gst_caps_set_features_simple (overlay_caps, gst_caps_features_copy (f));
      break;
    }
  }

  if (overlay_caps) {
    gst_caps_unref (othercaps);
    return gst_caps_fixate (overlay_caps);
  }

  return gst_caps_fixate (othercaps);
}

static gboolean
gst_base_text_layout_overlay_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;

  tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, META_TAG_VIDEO))) {
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}

static inline guint16
convert_color_to_uint16 (guint8 color)
{
  return (((guint16) color) << 8) | (guint16) color;
}

static void
gst_base_text_layout_overlay_set_color (GstBaseTextLayoutOverlay * self,
    GstTextLayout * layout, guint color, GstTextAttrType type)
{
  GstTextColor text_color;
  GstTextAttr *attr;

  text_color.alpha = convert_color_to_uint16 ((color >> 24) & 0xff);
  text_color.red = convert_color_to_uint16 ((color >> 16) & 0xff);
  text_color.green = convert_color_to_uint16 ((color >> 8) & 0xff);
  text_color.blue = convert_color_to_uint16 (color & 0xff);

  attr = gst_text_attr_color_new (&text_color, type, 0, G_MAXUINT);
  gst_text_layout_set_attr (layout, attr);
}

static GstFlowReturn
gst_text_layout_decorate_layout (GstBaseTextLayoutOverlay * self,
    GstTextLayout * subclass_layout)
{
  GstBaseTextLayoutOverlayClass *klass =
      GST_BASE_TEXT_LAYOUT_OVERLAY_GET_CLASS (self);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;
  const gchar *text;
  GstTextLayout *dst;
  GstWordWrapMode word_wrap;
  GstTextAlignment text_align;
  GstParagraphAlignment paragrape_align;
  guint width, height;
  GstTextAttr *attr;
  GstTextAttrIterator *iter;

  width = priv->window_width;
  height = priv->window_height;

  /* Layout is not updated, reuse previous one */
  if (priv->decorated_layout && priv->subclass_layout == subclass_layout)
    return GST_FLOW_OK;

  gst_clear_text_layout (&priv->decorated_layout);

  text = gst_text_layout_get_text (subclass_layout);
  if (!text || text[0] == '\0')
    return GST_FLOW_OK;

  dst = gst_text_layout_new (text);

  gst_text_layout_set_xpos (dst, priv->calculated_layout_x);
  gst_text_layout_set_ypos (dst, priv->calculated_layout_y);
  gst_text_layout_set_width (dst, priv->calculated_layout_width);
  gst_text_layout_set_height (dst, priv->calculated_layout_height);

  word_wrap = gst_text_layout_get_word_wrap (subclass_layout);
  if (word_wrap == GST_WORD_WRAP_UNKNOWN)
    word_wrap = priv->word_wrap;
  gst_text_layout_set_word_wrap (dst, word_wrap);

  text_align = gst_text_layout_get_text_alignment (subclass_layout);
  if (text_align == GST_TEXT_ALIGNMENT_UNKNOWN)
    text_align = priv->text_align;
  gst_text_layout_set_text_alignment (dst, text_align);

  paragrape_align = gst_text_layout_get_paragraph_alignment (subclass_layout);
  if (paragrape_align == GST_PARAGRAPH_ALIGNMENT_UNKNOWN)
    paragrape_align = priv->paragraph_align;
  gst_text_layout_set_paragraph_alignment (dst, paragrape_align);

  attr = gst_text_attr_string_new (priv->font_family, GST_TEXT_ATTR_FONT_FAMILY,
      0, G_MAXUINT);
  gst_text_layout_set_attr (dst, attr);

  attr = gst_text_attr_double_new (priv->calculated_font_size,
      GST_TEXT_ATTR_FONT_SIZE, 0, G_MAXUINT);
  gst_text_layout_set_attr (dst, attr);

  attr = gst_text_attr_int_new (priv->weight, GST_TEXT_ATTR_FONT_WEIGHT,
      0, G_MAXUINT);
  gst_text_layout_set_attr (dst, attr);

  attr = gst_text_attr_int_new (priv->style, GST_TEXT_ATTR_FONT_STYLE,
      0, G_MAXUINT);
  gst_text_layout_set_attr (dst, attr);

  attr = gst_text_attr_int_new (priv->stretch, GST_TEXT_ATTR_FONT_STRETCH,
      0, G_MAXUINT);
  gst_text_layout_set_attr (dst, attr);

  gst_base_text_layout_overlay_set_color (self, dst, priv->foreground_color,
      GST_TEXT_ATTR_FOREGROUND_COLOR);
  gst_base_text_layout_overlay_set_color (self, dst, priv->background_color,
      GST_TEXT_ATTR_BACKGROUND_COLOR);
  gst_base_text_layout_overlay_set_color (self, dst, priv->outline_color,
      GST_TEXT_ATTR_OUTLINE_COLOR);
  gst_base_text_layout_overlay_set_color (self, dst, priv->underline_color,
      GST_TEXT_ATTR_UNDERLINE_COLOR);
  gst_base_text_layout_overlay_set_color (self, dst, priv->strikethrough_color,
      GST_TEXT_ATTR_STRIKETHROUGH_COLOR);
  gst_base_text_layout_overlay_set_color (self, dst, priv->shadow_color,
      GST_TEXT_ATTR_SHADOW_COLOR);

  iter = gst_text_layout_get_attr_iterator (subclass_layout);
  do {
    guint size, i;

    size = gst_text_attr_iterator_get_size (iter);
    if (!size)
      break;

    for (i = 0; i < size; i++) {
      GstTextAttrType attr_type;
      guint start, len;
      GstTextAttr *clone;

      attr = gst_text_attr_iterator_get_attr (iter, i);

      if (klass->accept_attribute && !klass->accept_attribute (self, attr))
        continue;

      attr_type = gst_text_attr_identify (attr, &start, &len);

      if (attr_type == GST_TEXT_ATTR_FONT_SIZE) {
        gdouble font_size;
        gst_text_layout_attr_get_double (attr, &font_size);
        font_size = gst_text_layout_calculate_font_size (self, font_size);

        clone = gst_text_attr_double_new (font_size, GST_TEXT_ATTR_FONT_SIZE,
            start, len);
      } else {
        clone = gst_text_attr_copy (attr);
      }

      gst_text_layout_set_attr (dst, attr);
    }
  } while (gst_text_attr_iterator_next (iter));

  gst_text_attr_iterator_free (iter);

  priv->decorated_layout = dst;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_base_text_layout_overlay_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstBaseTextLayoutOverlay *self = GST_BASE_TEXT_LAYOUT_OVERLAY (trans);
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;
  GstBaseTextLayoutOverlayClass *klass =
      GST_BASE_TEXT_LAYOUT_OVERLAY_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstTextLayout *layout = NULL;

  g_mutex_lock (&priv->lock);
  if (klass->process_input) {
    ret = klass->process_input (self, inbuf);
    if (ret != GST_FLOW_OK)
      goto out;
  }

  /* Invisible, do passthrough */
  if (!priv->visible) {
    gst_base_transform_set_passthrough (trans, TRUE);
    *outbuf = inbuf;
    goto out;
  }

  g_assert (klass->generate_layout);
  ret = klass->generate_layout (self, priv->user_text, inbuf, &layout);
  if (ret != GST_FLOW_OK) {
    gst_clear_text_layout (&layout);
    goto out;
  }

  if (!layout) {
    gst_base_transform_set_passthrough (trans, TRUE);
    *outbuf = inbuf;
    goto out;
  }

  ret = gst_text_layout_decorate_layout (self, layout);
  if (ret != GST_FLOW_OK) {
    gst_clear_text_layout (&layout);
    goto out;
  }

  /* Swap subclass layouts */
  gst_clear_text_layout (&priv->subclass_layout);
  priv->subclass_layout = g_steal_pointer (&layout);

  if (!priv->decorated_layout) {
    gst_base_transform_set_passthrough (trans, TRUE);
    *outbuf = inbuf;
    goto out;
  }

  gst_base_transform_set_passthrough (trans, FALSE);
  ret = klass->generate_output (self, priv->decorated_layout, inbuf, outbuf);

out:
  g_mutex_unlock (&priv->lock);

  return ret;
}

static GstFlowReturn
gst_base_text_layout_overlay_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  /* Nothing to do here */
  return GST_FLOW_OK;
}

static void
gst_base_text_layout_overlay_clear_layout (GstBaseTextLayoutOverlay * self)
{
  GstBaseTextLayoutOverlayPrivate *priv = self->priv;

  gst_clear_text_layout (&priv->decorated_layout);
}

static GstFlowReturn
gst_base_text_layout_overlay_process_input_default (GstBaseTextLayoutOverlay *
    self, GstBuffer * buffer)
{
  return GST_FLOW_OK;
}

static gboolean
gst_base_text_layout_overlay_accept_attribute_default (GstBaseTextLayoutOverlay
    * self, GstTextAttr * attr)
{
  return TRUE;
}

static GstFlowReturn
gst_base_text_layout_overlay_generate_output_default (GstBaseTextLayoutOverlay *
    self, GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf)
{
  if (*out_buf == NULL)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

void
gst_base_text_layout_overlay_install_properties (GObjectClass * object_class,
    guint last_prop_index, guint * n_props)
{
  if (n_props)
    *n_props = PROP_LAST - 1;

  g_object_class_install_property (object_class,
      PROP_VISIBLE + last_prop_index,
      g_param_spec_boolean ("visible", "Visible",
          "Whether to draw text", DEFAULT_VISIBLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FONT_FAMILY + last_prop_index,
      g_param_spec_string ("font-family", "Font Family",
          "Font family to use", DEFAULT_FONT_FAMILY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FONT_SIZE + last_prop_index,
      g_param_spec_double ("font-size", "Font Size",
          "Font size to use", 0.1, 1638, DEFAULT_FONT_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_AUTO_RESIZE + last_prop_index,
      g_param_spec_boolean ("auto-resize", "Auto Resize",
          "Automatically adjust font size to screen-size", DEFAULT_AUTO_RESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FONT_WEIGHT + last_prop_index,
      g_param_spec_enum ("font-weight", "Font Weight",
          "Font Weight", GST_TYPE_FONT_WEIGHT, DEFAULT_FONT_WEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FONT_STYLE + last_prop_index,
      g_param_spec_enum ("font-style", "Font Style", "Font Style",
          GST_TYPE_FONT_STYLE, DEFAULT_FONT_STYLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FONT_STRETCH + last_prop_index,
      g_param_spec_enum ("font-stretch", "Font Stretch",
          "Font Stretch", GST_TYPE_FONT_STRETCH, DEFAULT_FONT_STRETCH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_WORD_WRAP + last_prop_index,
      g_param_spec_enum ("word-wrap", "Word Wrap",
          "Word wrapping mode", GST_TYPE_WORD_WRAP_MODE,
          DEFAULT_WORD_WRAP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_TEXT_ALIGNMENT + last_prop_index,
      g_param_spec_enum ("text-alignment", "Text Alignment",
          "Text Alignment", GST_TYPE_TEXT_ALIGNMENT,
          DEFAULT_TEXT_ALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_PARAGRAPH_ALIGNMENT + last_prop_index,
      g_param_spec_enum ("paragraph-alignment",
          "Paragraph alignment", "Paragraph Alignment",
          GST_TYPE_PARAGRAPH_ALIGNMENT, DEFAULT_PARAGRAPH_ALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_TEXT + last_prop_index,
      g_param_spec_string ("text", "Text", "Text to render", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_FOREGROUND_COLOR + last_prop_index,
      g_param_spec_uint ("foreground-color", "Foreground Color",
          "Text color to use (big-endian ARGB)",
          0, G_MAXUINT32, DEFAULT_FOREGROUND_COLOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_OUTLINE_COLOR + last_prop_index,
      g_param_spec_uint ("outline-color", "Outline Color",
          "Text outline color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_OUTLINE_COLOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_UNDERLINE_COLOR + last_prop_index,
      g_param_spec_uint ("underline-color", "Underline Color",
          "Underline color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_FOREGROUND_COLOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_STRIKETHROUGH_COLOR + last_prop_index,
      g_param_spec_uint ("strikethrough-color",
          "Strikethrough Color", "Strikethrough color to use (big-endian ARGB)",
          0, G_MAXUINT32, DEFAULT_FOREGROUND_COLOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_SHADOW_COLOR + last_prop_index,
      g_param_spec_uint ("shadow-color", "Shadow Color",
          "Shadow color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_SHADOW_COLOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_BACKGROUND_COLOR + last_prop_index,
      g_param_spec_uint ("background-color", "Background Color",
          "Background color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_BACKGROUND_COLOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_LAYOUT_X + last_prop_index,
      g_param_spec_double ("layout-x", "Layout X",
          "Normalized X coordinate of text layout", 0, 1, DEFAULT_LAYOUT_XY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_LAYOUT_Y + last_prop_index,
      g_param_spec_double ("layout-y", "Layout Y",
          "Normalized Y coordinate of text layout", 0, 1, DEFAULT_LAYOUT_XY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class,
      PROP_LAYOUT_WIDTH + last_prop_index,
      g_param_spec_double ("layout-width", "Layout Width",
          "Normalized width of text layout", 0, 1, DEFAULT_LAYOUT_WH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class,
      PROP_LAYOUT_HEIGHT + last_prop_index,
      g_param_spec_double ("layout-height", "Layout Height",
          "Normalized height of text layout", 0, 1, DEFAULT_LAYOUT_WH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

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

#include "gstbasesubtitleoverlay.h"
#include "gsttextlayoutoverlay-private.h"
#include <caption.h>

GST_DEBUG_CATEGORY_STATIC (base_subtitle_overlay_debug);
#define GST_CAT_DEFAULT base_subtitle_overlay_debug

GType
gst_base_subtitle_overlay_source_get_type (void)
{
  static GType type = 0;
  static const GFlagsValue sources[] = {
    {GST_BASE_SUBTITLE_OVERLAY_SOURCE_SUBTITLE, "Subtitle", "subtitle"},
    {GST_BASE_SUBTITLE_OVERLAY_SOURCE_CC, "Closed Caption", "cc"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType tmp =
        g_flags_register_static ("GstBaseSubtitleOverlaySource", sources);
    g_once_init_leave (&type, tmp);
  }

  return type;
}

enum
{
  PROP_0,
  PROP_SOURCE,
  PROP_CC_FIELD,
  PROP_CC_TIMEOUT,
  PROP_LAST,
};

#define DEFAULT_SOURCE (GST_BASE_SUBTITLE_OVERLAY_SOURCE_SUBTITLE | GST_BASE_SUBTITLE_OVERLAY_SOURCE_CC)
#define DEFAULT_CC_FIELD -1
#define DEFAULT_CC_TIMEOUT GST_CLOCK_TIME_NONE

struct _GstBaseSubtitleOverlayPrivate
{
  GMutex lock;
  caption_frame_t frame;
  GstClockTime caption_running_time;
  GstClockTime running_time;
  guint8 selected_field;
  gchar caption[CAPTION_FRAME_TEXT_BYTES + 1];
  GPtrArray *subtitle_layouts;
  GstTextLayout *caption_layout;
  GstTextLayout *default_layout;

  GstBaseSubtitleOverlaySource source;
  gint cc_field;
  GstClockTime cc_timeout;
};

static void gst_base_subtitle_overlay_finalize (GObject * object);
static void gst_base_subtitle_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_subtitle_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_base_subtitle_overlay_start (GstBaseTransform * trans);
static gboolean gst_base_subtitle_overlay_stop (GstBaseTransform * trans);
static gboolean gst_base_subtitle_overlay_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn
gst_base_subtitle_overlay_process_input (GstBaseTextLayoutOverlay * overlay,
    GstBuffer * buffer);
static GstFlowReturn
gst_base_subtitle_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout);
static GstFlowReturn
gst_base_subtitle_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf);

#define gst_base_subtitle_overlay_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstBaseSubtitleOverlay,
    gst_base_subtitle_overlay, GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY);

static void
gst_base_subtitle_overlay_class_init (GstBaseSubtitleOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstBaseTextLayoutOverlayClass *overlay_class =
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (klass);

  object_class->finalize = gst_base_subtitle_overlay_finalize;
  object_class->set_property = gst_base_subtitle_overlay_set_property;
  object_class->get_property = gst_base_subtitle_overlay_get_property;

  g_object_class_install_property (object_class, PROP_SOURCE,
      g_param_spec_flags ("source", "Source", "Text source selection",
          GST_TYPE_BASE_SUBTITLE_OVERLAY_SOURCE, DEFAULT_SOURCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CC_FIELD,
      g_param_spec_int ("cc-field", "CC Field",
          "The closed caption field to render when available, (-1 = automatic)",
          -1, 1, DEFAULT_CC_FIELD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CC_TIMEOUT,
      g_param_spec_uint64 ("cc-timeout", "CC Timeout",
          "Duration after which to erase overlay when no cc data has arrived "
          "for the selected field, in nanoseconds unit", 16 * GST_SECOND,
          GST_CLOCK_TIME_NONE, DEFAULT_CC_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_stop);
  trans_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_sink_event);

  overlay_class->process_input =
      GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_process_input);
  overlay_class->generate_layout =
      GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_generate_layout);
  overlay_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_base_subtitle_overlay_generate_output);

  GST_DEBUG_CATEGORY_INIT (base_subtitle_overlay_debug, "baseclockoverlay", 0,
      "baseclockoverlay");
}

static void
gst_base_subtitle_overlay_init (GstBaseSubtitleOverlay * self)
{
  GstBaseSubtitleOverlayPrivate *priv;

  g_object_set (self, "text-alignment", GST_TEXT_ALIGNMENT_CENTER,
      "paragraph-alignment", GST_PARAGRAPH_ALIGNMENT_BOTTOM, NULL);

  self->priv = priv = gst_base_subtitle_overlay_get_instance_private (self);
  g_mutex_init (&priv->lock);
  priv->subtitle_layouts =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_text_layout_unref);
  priv->source = DEFAULT_SOURCE;
  priv->cc_field = DEFAULT_CC_FIELD;
  priv->cc_timeout = DEFAULT_CC_TIMEOUT;
}

static void
gst_base_subtitle_overlay_finalize (GObject * object)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (object);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  g_mutex_clear (&priv->lock);
  g_ptr_array_unref (priv->subtitle_layouts);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_subtitle_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (object);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_SOURCE:
      priv->source = g_value_get_flags (value);
      break;
    case PROP_CC_FIELD:
      priv->cc_field = g_value_get_int (value);
      if (priv->cc_field != -1)
        priv->selected_field = priv->cc_field;
      break;
    case PROP_CC_TIMEOUT:
      priv->cc_timeout = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static void
gst_base_subtitle_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (object);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_SOURCE:
      g_value_set_flags (value, priv->source);
      break;
    case PROP_CC_FIELD:
      g_value_set_int (value, priv->cc_field);
      break;
    case PROP_CC_TIMEOUT:
      g_value_set_uint64 (value, priv->cc_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

static void
gst_base_subtitle_overlay_reset (GstBaseSubtitleOverlay * self)
{
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  gst_clear_text_layout (&priv->caption_layout);
  gst_clear_text_layout (&priv->default_layout);

  caption_frame_init (&priv->frame);
  priv->running_time = GST_CLOCK_TIME_NONE;
  priv->caption_running_time = GST_CLOCK_TIME_NONE;
  priv->caption[0] = '\0';
  if (priv->cc_field == -1)
    priv->selected_field = 0xff;
  else
    priv->selected_field = priv->cc_field;
}

static gboolean
gst_base_subtitle_overlay_start (GstBaseTransform * trans)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (trans);

  gst_base_subtitle_overlay_reset (self);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_base_subtitle_overlay_stop (GstBaseTransform * trans)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (trans);

  gst_base_subtitle_overlay_reset (self);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_base_subtitle_overlay_sink_event (GstBaseTransform * trans,
    GstEvent * event)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (trans);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      priv->caption_running_time = GST_CLOCK_TIME_NONE;
      priv->running_time = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static guint
gst_base_subtitle_overlay_extract_cdp (GstBaseSubtitleOverlay * self,
    const guint8 * cdp, guint cdp_len, guint * pos)
{
  GstByteReader br;
  guint16 u16;
  guint8 u8;
  guint8 flags;
  guint len = 0;

  GST_TRACE_OBJECT (self, "Extracting CDP");

  /* Header + footer length */
  if (cdp_len < 11) {
    GST_WARNING_OBJECT (self, "cdp packet too short (%u). expected at "
        "least %u", cdp_len, 11);
    return 0;
  }

  gst_byte_reader_init (&br, cdp, cdp_len);
  u16 = gst_byte_reader_get_uint16_be_unchecked (&br);
  if (u16 != 0x9669) {
    GST_WARNING_OBJECT (self, "cdp packet does not have initial magic bytes "
        "of 0x9669");
    return 0;
  }

  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  if (u8 != cdp_len) {
    GST_WARNING_OBJECT (self, "cdp packet length (%u) does not match passed "
        "in value (%u)", u8, cdp_len);
    return 0;
  }

  /* skip framerate value */
  gst_byte_reader_skip_unchecked (&br, 1);

  flags = gst_byte_reader_get_uint8_unchecked (&br);
  /* No cc_data? */
  if ((flags & 0x40) == 0) {
    GST_DEBUG_OBJECT (self, "cdp packet does have any cc_data");
    return 0;
  }

  /* cdp_hdr_sequence_cntr */
  gst_byte_reader_skip_unchecked (&br, 2);

  /* skip timecode */
  if (flags & 0x80) {
    if (gst_byte_reader_get_remaining (&br) < 5) {
      GST_WARNING_OBJECT (self, "cdp packet does not have enough data to "
          "contain a timecode (%u). Need at least 5 bytes",
          gst_byte_reader_get_remaining (&br));
      return 0;
    }

    gst_byte_reader_skip_unchecked (&br, 5);
  }

  /* ccdata_present */
  if (flags & 0x40) {
    guint8 cc_count;

    if (gst_byte_reader_get_remaining (&br) < 2) {
      GST_WARNING_OBJECT (self, "not enough data to contain valid cc_data");
      return 0;
    }

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 != 0x72) {
      GST_WARNING_OBJECT (self, "missing cc_data start code of 0x72, "
          "found 0x%02x", u8);
      return 0;
    }

    cc_count = gst_byte_reader_get_uint8_unchecked (&br);
    if ((cc_count & 0xe0) != 0xe0) {
      GST_WARNING_OBJECT (self, "reserved bits are not 0xe0, found 0x%02x", u8);
      return 0;
    }
    cc_count &= 0x1f;

    len = 3 * cc_count;
    if (gst_byte_reader_get_remaining (&br) < len) {
      GST_WARNING_OBJECT (self, "not enough bytes (%u) left for the "
          "number of byte triples (%u)", gst_byte_reader_get_remaining (&br),
          cc_count);
      return 0;
    }

    *pos = gst_byte_reader_get_pos (&br);
  }

  /* skip everything else we don't care about */
  return len;
}

static void
gst_base_subtitle_overlay_decode_cc_data (GstBaseSubtitleOverlay * self,
    const guint8 * data, guint len, GstClockTime running_time)
{
  GstBaseSubtitleOverlayPrivate *priv = self->priv;
  GstByteReader br;

  GST_TRACE_OBJECT (self, "Decoding CC data");

  gst_byte_reader_init (&br, data, len);
  while (gst_byte_reader_get_remaining (&br) >= 3) {
    guint8 cc_type;
    guint16 cc_data;
    libcaption_stauts_t status;

    cc_type = gst_byte_reader_get_uint8_unchecked (&br);
    cc_data = gst_byte_reader_get_uint16_be_unchecked (&br);

    if ((cc_type & 0x04) != 0x04)
      continue;

    cc_type = cc_type & 0x03;
    if (cc_type != 0x00 && cc_type != 0x01)
      continue;

    if (priv->selected_field == 0xff) {
      GST_INFO_OBJECT (self, "Selected field %d", cc_type);
      priv->selected_field = cc_type;
    }

    if (cc_type != priv->selected_field)
      continue;

    status = caption_frame_decode (&priv->frame, cc_data, 0.0);
    switch (status) {
      case LIBCAPTION_READY:
      {
        size_t len = caption_frame_to_text (&priv->frame,
            priv->caption, FALSE);
        g_return_if_fail (len < G_N_ELEMENTS (priv->caption));

        priv->caption[len] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        priv->caption_layout = gst_text_layout_new (priv->caption);
        break;
      }
      case LIBCAPTION_CLEAR:
        priv->caption[0] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        break;
      default:
        break;
    }

    priv->caption_running_time = running_time;
  }
}

static void
gst_base_subtitle_overlay_decode_s334_1a (GstBaseSubtitleOverlay * self,
    const guint8 * data, guint len, GstClockTime running_time)
{
  GstBaseSubtitleOverlayPrivate *priv = self->priv;
  GstByteReader br;

  GST_TRACE_OBJECT (self, "Decoding S334-1A");

  gst_byte_reader_init (&br, data, len);
  while (gst_byte_reader_get_remaining (&br) >= 3) {
    guint8 cc_type;
    guint16 cc_data;
    libcaption_stauts_t status;

    cc_type = gst_byte_reader_get_uint8_unchecked (&br);
    cc_data = gst_byte_reader_get_uint16_be_unchecked (&br);

    cc_type = cc_type & 0x01;
    if (priv->selected_field == 0xff) {
      GST_INFO_OBJECT (self, "Selected field %d", cc_type);
      priv->selected_field = cc_type;
    }

    if (cc_type != priv->selected_field)
      continue;

    status = caption_frame_decode (&priv->frame, cc_data, 0.0);
    switch (status) {
      case LIBCAPTION_READY:
      {
        size_t len = caption_frame_to_text (&priv->frame,
            priv->caption, FALSE);
        g_return_if_fail (len < G_N_ELEMENTS (priv->caption));

        priv->caption[len] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        priv->caption_layout = gst_text_layout_new (priv->caption);
        break;
      }
      case LIBCAPTION_CLEAR:
        priv->caption[0] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        break;
      default:
        break;
    }

    priv->caption_running_time = running_time;
  }
}

static void
gst_base_subtitle_overlay_decode_raw (GstBaseSubtitleOverlay * self,
    const guint8 * data, guint len, GstClockTime running_time)
{
  GstBaseSubtitleOverlayPrivate *priv = self->priv;
  GstByteReader br;

  GST_TRACE_OBJECT (self, "Decoding CEA608 RAW");

  gst_byte_reader_init (&br, data, len);
  while (gst_byte_reader_get_remaining (&br) >= 2) {
    guint16 cc_data;
    libcaption_stauts_t status;

    cc_data = gst_byte_reader_get_uint16_be_unchecked (&br);

    status = caption_frame_decode (&priv->frame, cc_data, 0.0);
    switch (status) {
      case LIBCAPTION_READY:
      {
        size_t len = caption_frame_to_text (&priv->frame,
            priv->caption, FALSE);
        g_return_if_fail (len < G_N_ELEMENTS (priv->caption));

        priv->caption[len] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        priv->caption_layout = gst_text_layout_new (priv->caption);
        break;
      }
      case LIBCAPTION_CLEAR:
        priv->caption[0] = '\0';
        gst_clear_text_layout (&priv->caption_layout);
        break;
      default:
        break;
    }

    priv->caption_running_time = running_time;
  }
}

static gboolean
foreach_cc_meta (GstBuffer * buffer, GstMeta ** meta,
    GstBaseSubtitleOverlay * self)
{
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  if ((*meta)->info->api == GST_VIDEO_CAPTION_META_API_TYPE) {
    GstVideoCaptionMeta *cc_meta = (GstVideoCaptionMeta *) (*meta);
    switch (cc_meta->caption_type) {
      case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
        gst_base_subtitle_overlay_decode_raw (self, cc_meta->data,
            cc_meta->size, priv->running_time);
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:
        gst_base_subtitle_overlay_decode_s334_1a (self, cc_meta->data,
            cc_meta->size, priv->running_time);
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
        gst_base_subtitle_overlay_decode_cc_data (self, cc_meta->data,
            cc_meta->size, priv->running_time);
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
      {
        guint len, pos = 0;
        len = gst_base_subtitle_overlay_extract_cdp (self, cc_meta->data,
            cc_meta->size, &pos);
        if (len > 0) {
          gst_base_subtitle_overlay_decode_cc_data (self, cc_meta->data + pos,
              len, priv->running_time);
        }
        break;
      }
      default:
        break;
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_base_subtitle_overlay_process_input (GstBaseTextLayoutOverlay * overlay,
    GstBuffer * buffer)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (overlay);
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (overlay);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;

  priv->running_time = gst_segment_to_running_time (&trans->segment,
      GST_FORMAT_TIME, GST_BUFFER_PTS (buffer));

  g_mutex_lock (&priv->lock);
  gst_buffer_foreach_meta (buffer,
      (GstBufferForeachMetaFunc) foreach_cc_meta, self);

  if (priv->caption_layout &&
      GST_CLOCK_TIME_IS_VALID (priv->cc_timeout) &&
      GST_CLOCK_TIME_IS_VALID (priv->running_time) &&
      GST_CLOCK_TIME_IS_VALID (priv->caption_running_time) &&
      priv->running_time >= priv->caption_running_time) {
    GstClockTime diff = priv->running_time - priv->caption_running_time;

    if (diff > priv->cc_timeout) {
      GST_INFO_OBJECT (self, "Reached timeout, clearing closed caption");
      gst_clear_text_layout (&priv->caption_layout);
    }
  }
  g_mutex_unlock (&priv->lock);

  return GST_FLOW_OK;
}

static gboolean
foreach_subtitle_meta (GstBuffer * buffer, GstMeta ** meta, GPtrArray * layouts)
{
  if ((*meta)->info->api == GST_VIDEO_SUBTITLE_META_API_TYPE) {
    GstVideoSubtitleMeta *smeta = (GstVideoSubtitleMeta *) (*meta);
    if (smeta->layout)
      g_ptr_array_add (layouts, gst_text_layout_ref (smeta->layout));
  }

  return TRUE;
}

static GstFlowReturn
gst_base_subtitle_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (overlay);
  GstBaseSubtitleOverlayPrivate *priv = self->priv;
  GstTextLayout *ret = NULL;
  guint i;

  g_mutex_lock (&priv->lock);
  g_ptr_array_set_size (priv->subtitle_layouts, 0);

  if (text && text[0] != '\0') {
    if (priv->default_layout &&
        g_strcmp0 (gst_text_layout_get_text (priv->default_layout),
            text) != 0) {
      gst_clear_text_layout (&priv->default_layout);
    }

    if (!priv->default_layout)
      priv->default_layout = gst_text_layout_new (text);
  } else {
    gst_clear_text_layout (&priv->default_layout);
  }

  if ((priv->source & GST_BASE_SUBTITLE_OVERLAY_SOURCE_SUBTITLE) != 0) {
    gst_buffer_foreach_meta (buffer,
        (GstBufferForeachMetaFunc) foreach_subtitle_meta,
        priv->subtitle_layouts);
  }

  for (i = 0; i < priv->subtitle_layouts->len; i++) {
    GstTextLayout *sub = g_ptr_array_index (priv->subtitle_layouts, i);

    if (!ret) {
      ret = gst_text_layout_ref (sub);
    } else {
      GstTextLayout *concat;

      concat = gst_text_layout_concat (ret, sub, "\n\n");
      if (!concat)
        break;

      gst_text_layout_unref (ret);
      ret = concat;
    }
  }

  if ((priv->source & GST_BASE_SUBTITLE_OVERLAY_SOURCE_CC) != 0 &&
      priv->caption_layout) {
    if (!ret) {
      ret = gst_text_layout_ref (priv->caption_layout);
    } else {
      GstTextLayout *concat;

      concat = gst_text_layout_concat (ret, priv->caption_layout, "\n\n");
      if (concat) {
        gst_text_layout_unref (ret);
        ret = concat;
      }
    }
  }

  if (priv->default_layout) {
    if (!ret) {
      ret = gst_text_layout_ref (priv->default_layout);
    } else {
      GstTextLayout *concat;

      concat = gst_text_layout_concat (priv->default_layout, ret, " ");
      if (concat) {
        gst_text_layout_unref (ret);
        ret = concat;
      }
    }
  }

  *layout = ret;
  g_ptr_array_set_size (priv->subtitle_layouts, 0);
  g_mutex_unlock (&priv->lock);

  return GST_FLOW_OK;
}

static gboolean
gst_base_subtitle_overlay_remove_meta (GstBuffer * buffer, GstMeta ** meta,
    GstBaseSubtitleOverlay * self)
{
  if ((*meta)->info->api == GST_VIDEO_SUBTITLE_META_API_TYPE)
    *meta = NULL;

  return TRUE;
}

static GstFlowReturn
gst_base_subtitle_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf)
{
  GstBaseSubtitleOverlay *self = GST_BASE_SUBTITLE_OVERLAY (overlay);

  if (*out_buf == NULL)
    return GST_FLOW_ERROR;

  gst_buffer_foreach_meta (*out_buf,
      (GstBufferForeachMetaFunc) gst_base_subtitle_overlay_remove_meta, self);

  return
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (parent_class)->generate_output
      (overlay, layout, in_buf, out_buf);
}

void
gst_base_subtitle_overlay_install_properties (GObjectClass * object_class,
    guint last_prop_index, guint * n_props)
{
  if (n_props)
    *n_props = PROP_LAST - 1;

  g_object_class_install_property (object_class, PROP_SOURCE + last_prop_index,
      g_param_spec_flags ("source", "Source", "Text source selection",
          GST_TYPE_BASE_SUBTITLE_OVERLAY_SOURCE, DEFAULT_SOURCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_CC_FIELD + last_prop_index,
      g_param_spec_int ("cc-field", "CC Field",
          "The closed caption field to render when available, (-1 = automatic)",
          -1, 1, DEFAULT_CC_FIELD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
      PROP_CC_TIMEOUT + last_prop_index,
      g_param_spec_uint64 ("cc-timeout", "CC Timeout",
          "Duration after which to erase overlay when no cc data has arrived "
          "for the selected field, in nanoseconds unit", 16 * GST_SECOND,
          GST_CLOCK_TIME_NONE, DEFAULT_CC_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* GStreamer
 * Copyright (C) 2022 Benjamin Gaignard <benjamin.gaignard@collabora.com>
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

#include "gstvp8encoder.h"

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/base/base.h>

GST_DEBUG_CATEGORY (gst_vp8_encoder_debug);
#define GST_CAT_DEFAULT gst_vp8_encoder_debug

#define VP8ENC_DEFAULT_KEYFRAME_INTERVAL	30

#define VP8_MAX_QUALITY				63
#define VP8_MIN_QUALITY				0

#define VP8_DEFAULT_BITRATE			100000

enum
{
  PROP_0,
  PROP_KEYFRAME_INTERVAL,
  PROP_MAX_QUALITY,
  PROP_MIN_QUALITY,
  PROP_BITRATE,
};

struct _GstVp8EncoderPrivate
{
  gint keyframe_interval;

  guint32 last_keyframe;

  guint64 targeted_bitrate;
  gint max_quality;
  gint min_quality;
  gint current_quality;
  guint64 used_bytes;
  guint64 nb_frames;
};

#define parent_class gst_vp8_encoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVp8Encoder, gst_vp8_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstVp8Encoder);
    GST_DEBUG_CATEGORY_INIT (gst_vp8_encoder_debug, "vp8encoder", 0,
        "Vp8 Video Encoder"));

static void
gst_vp8_encoder_init (GstVp8Encoder * self)
{
  self->priv = gst_vp8_encoder_get_instance_private (self);
}

static void
gst_vp8_encoder_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vp8_encoder_start (GstVideoEncoder * encoder)
{
  GstVp8Encoder *self = GST_VP8_ENCODER (encoder);
  GstVp8EncoderPrivate *priv = self->priv;

  priv->last_keyframe = 0;
  priv->current_quality = priv->min_quality;
  priv->used_bytes = 0;
  priv->nb_frames = 0;

  return TRUE;
}

static gboolean
gst_vp8_encoder_stop (GstVideoEncoder * encoder)
{
  return TRUE;
}

static gboolean
gst_vp8_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  return TRUE;
}

static GstFlowReturn
gst_vp8_encoder_set_quality (GstVp8Encoder * self, GstVp8Frame * vp8_frame)
{
  GstVp8EncoderPrivate *priv = self->priv;
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
  GstVideoCodecState *output_state =
      gst_video_encoder_get_output_state (encoder);
  gint qp = priv->current_quality;
  guint64 bitrate = 0;
  guint fps_n = 30, fps_d = 1;

  if (output_state == NULL)
    return qp;

  if (GST_VIDEO_INFO_FPS_N (&output_state->info) != 0) {
    fps_n = GST_VIDEO_INFO_FPS_N (&output_state->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&output_state->info);
  }
  gst_video_codec_state_unref (output_state);

  bitrate = (priv->used_bytes * 8 * fps_n) / (priv->nb_frames * fps_d);
  if (bitrate > priv->targeted_bitrate) {
    qp++;
  }

  if (bitrate < priv->targeted_bitrate) {
    qp--;
  }

  if (qp > priv->max_quality)
    qp = priv->max_quality;
  if (qp < priv->min_quality)
    qp = priv->min_quality;

  vp8_frame->quality = qp;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vp8_encoder_set_frame_type (GstVp8Encoder * self, GstVp8Frame * vp8_frame)
{
  GstVp8EncoderPrivate *priv = self->priv;
  GstVideoCodecFrame *frame = vp8_frame->frame;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    vp8_frame->type = GstVp8Keyframe;
    return GST_FLOW_OK;
  }

  if ((frame->system_frame_number - priv->last_keyframe) >
      priv->keyframe_interval || frame->system_frame_number == 0) {
    /* Generate a keyframe */
    GST_DEBUG_OBJECT (self, "Generate a keyframe");
    vp8_frame->type = GstVp8Keyframe;
    return GST_FLOW_OK;
  }

  /* Generate a interframe */
  GST_DEBUG_OBJECT (self, "Generate a interframe");
  vp8_frame->type = GstVp8Inter;
  return GST_FLOW_OK;
}

static void
gst_vp8_encoder_mark_frame (GstVp8Encoder * self, GstVp8Frame * vp8_frame)
{
  GstVideoCodecFrame *frame = vp8_frame->frame;
  GstVp8EncoderPrivate *priv = self->priv;

  switch (vp8_frame->type) {
    case GstVp8Keyframe:
      priv->last_keyframe = frame->system_frame_number;
      break;
  }

  priv->current_quality = vp8_frame->quality;
  priv->used_bytes += gst_buffer_get_size (frame->output_buffer);
  priv->nb_frames++;
}

static GstFlowReturn
gst_vp8_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVp8Encoder *self = GST_VP8_ENCODER (encoder);
  GstVp8EncoderClass *klass = GST_VP8_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVp8Frame *vp8_frame = gst_vp8_frame_new (frame);

  ret = gst_vp8_encoder_set_frame_type (self, vp8_frame);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_vp8_encoder_set_quality (self, vp8_frame);
  if (ret != GST_FLOW_OK)
    return ret;

  /* TODO: add encoding parameters management here
   * for now just send the frame to encode */
  if (klass->encode_frame) {
    ret = klass->encode_frame (self, vp8_frame);
    if (ret == GST_FLOW_OK)
      gst_vp8_encoder_mark_frame (self, vp8_frame);
  }

  gst_vp8_frame_unref (vp8_frame);

  return ret;
}

static void
gst_vp8_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVp8Encoder *self = GST_VP8_ENCODER (object);
  GstVp8EncoderPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_KEYFRAME_INTERVAL:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, priv->keyframe_interval);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_MAX_QUALITY:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, priv->max_quality);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_MIN_QUALITY:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, priv->min_quality);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_BITRATE:
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, priv->targeted_bitrate);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vp8_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVp8Encoder *self = GST_VP8_ENCODER (object);
  GstVp8EncoderPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_KEYFRAME_INTERVAL:
      GST_OBJECT_LOCK (self);
      priv->keyframe_interval = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_MAX_QUALITY:
      GST_OBJECT_LOCK (self);
      priv->max_quality = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_MIN_QUALITY:
      GST_OBJECT_LOCK (self);
      priv->min_quality = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_BITRATE:
      GST_OBJECT_LOCK (self);
      priv->targeted_bitrate = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vp8_encoder_class_init (GstVp8EncoderClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_vp8_encoder_finalize);
  object_class->get_property = gst_vp8_encoder_get_property;
  object_class->set_property = gst_vp8_encoder_set_property;

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_vp8_encoder_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_vp8_encoder_stop);
  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vp8_encoder_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp8_encoder_handle_frame);

  /**
   * GstVp8Encoder:keyframe-interval:
   *
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_KEYFRAME_INTERVAL,
      g_param_spec_int ("keyframe-interval", "Keyframe Interval",
          "Interval between keyframes",
          0, G_MAXINT, VP8ENC_DEFAULT_KEYFRAME_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
   * GstVp8Encoder:max-quality:
   *
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_MAX_QUALITY,
      g_param_spec_int ("max-quality", "Max Quality Level",
          "Set upper quality limit (lower number equates to higher quality but more bits)",
          VP8_MIN_QUALITY, VP8_MAX_QUALITY, VP8_MAX_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

   /**
   * GstVp8Encoder:min-quality:
   *
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_MIN_QUALITY,
      g_param_spec_int ("min-quality", "Min Quality Level",
          "Set lower quality limit (lower number equates to higher quality but more bits)",
          VP8_MIN_QUALITY, VP8_MAX_QUALITY, VP8_MIN_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

   /**
   * GstVp8Encoder:bitrate:
   *
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint64 ("bitrate", "Targeted bitrate",
          "Set bitrate target",
          0, UINT_MAX, VP8_DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}

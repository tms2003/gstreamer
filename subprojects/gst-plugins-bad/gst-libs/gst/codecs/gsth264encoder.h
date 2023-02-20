/* GStreamer
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
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

#ifndef __GST_H264_ENCODER_H__
#define __GST_H264_ENCODER_H__

#include <gst/codecs/codecs-prelude.h>

#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include "gsth264frame.h"

G_BEGIN_DECLS
#define GST_TYPE_H264_ENCODER            (gst_h264_encoder_get_type())
#define GST_H264_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_ENCODER,GstH264Encoder))
#define GST_H264_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_ENCODER,GstH264EncoderClass))
#define GST_H264_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_ENCODER,GstH264EncoderClass))
#define GST_IS_H264_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_ENCODER))
#define GST_IS_H264_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_ENCODER))
#define GST_H264_ENCODER_CAST(obj)       ((GstH264Encoder*)obj)
typedef struct _GstH264Encoder GstH264Encoder;
typedef struct _GstH264EncoderClass GstH264EncoderClass;
typedef struct _GstH264EncoderPrivate GstH264EncoderPrivate;

/**
 * GstH264Encoder:
 *
 * The opaque #GstH264Encoder data structure.
 */
struct _GstH264Encoder
{
  /*< private > */
  GstVideoEncoder parent;

  /*< private > */
  GstH264EncoderPrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

/**
 * GstH264EncoderClass:
 */
struct _GstH264EncoderClass
{
  GstVideoEncoderClass parent_class;

  /**
   * GstH264EncoderClass::encode_frame:
   * @encoder: a #GstH264Encoder
   * @frame: a #GstH264Frame
   *
   * Provide the frame to be encoded with the encode parameters (to be defined)
   */
    GstFlowReturn (*encode_frame) (GstH264Encoder * encoder,
      GstH264Frame * frame);
  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstH264Encoder, gst_object_unref)
     GST_CODECS_API GType gst_h264_encoder_get_type (void);

G_END_DECLS
#endif /* __GST_H264_ENCODER_H__ */

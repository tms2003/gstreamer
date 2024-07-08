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

#ifndef __GST_VP8_ENCODER_H__
#define __GST_VP8_ENCODER_H__

#include <gst/codecs/codecs-prelude.h>

#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include "gstvp8frame.h"

G_BEGIN_DECLS
#define GST_TYPE_VP8_ENCODER            (gst_vp8_encoder_get_type())
#define GST_VP8_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VP8_ENCODER,GstVp8Encoder))
#define GST_VP8_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VP8_ENCODER,GstVp8EncoderClass))
#define GST_VP8_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VP8_ENCODER,GstVp8EncoderClass))
#define GST_IS_VP8_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VP8_ENCODER))
#define GST_IS_VP8_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VP8_ENCODER))
#define GST_VP8_ENCODER_CAST(obj)       ((GstVp8Encoder*)obj)
typedef struct _GstVp8Encoder GstVp8Encoder;
typedef struct _GstVp8EncoderClass GstVp8EncoderClass;
typedef struct _GstVp8EncoderPrivate GstVp8EncoderPrivate;

/**
 * GstVp8Encoder:
 *
 * The opaque #GstVp8Encoder data structure.
 */
struct _GstVp8Encoder
{
  /*< private > */
  GstVideoEncoder parent;

  /*< private > */
  GstVp8EncoderPrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

/**
 * GstVp8EncoderClass:
 */
struct _GstVp8EncoderClass
{
  GstVideoEncoderClass parent_class;

  /**
   * GstVp8EncoderClass::encode_frame:
   * @encoder: a #GstVp8Encoder
   * @frame: a #GstVp8Frame
   *
   * Provide the frame to be encoded with the encode parameters (to be defined)
   */
    GstFlowReturn (*encode_frame) (GstVp8Encoder * encoder,
      GstVp8Frame * frame);
  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstVp8Encoder, gst_object_unref)
     GST_CODECS_API GType gst_vp8_encoder_get_type (void);

G_END_DECLS
#endif /* __GST_VP8_ENCODER_H__ */

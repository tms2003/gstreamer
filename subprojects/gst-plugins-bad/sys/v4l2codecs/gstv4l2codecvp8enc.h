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

#ifndef __GST_V4L2_CODEC_VP8_ENC_H__
#define __GST_V4L2_CODEC_VP8_ENC_H__

#define GST_USE_UNSTABLE_API
#include <gst/codecs/gstvp8encoder.h>

#include "gstv4l2encoder.h"

G_BEGIN_DECLS

#define GST_TYPE_V4L2_CODEC_VP8_ENC           (gst_v4l2_codec_vp8_enc_get_type())
#define GST_V4L2_CODEC_VP8_ENC(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_CODEC_VP8_ENC,GstV4l2CodecVp8Enc))
#define GST_V4L2_CODEC_VP8_ENC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_CODEC_VP8_ENC,GstV4l2CodecVp8EncClass))
#define GST_V4L2_CODEC_VP8_ENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4L2_CODEC_VP8_ENC, GstV4l2CodecVp8EncClass))
#define GST_IS_V4L2_CODEC_VP8_ENC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_CODEC_VP8_ENC))
#define GST_IS_V4L2_CODEC_VP8_ENC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_CODEC_VP8_ENC))

typedef struct _GstV4l2CodecVp8Enc GstV4l2CodecVp8Enc;
typedef struct _GstV4l2CodecVp8EncClass GstV4l2CodecVp8EncClass;

struct _GstV4l2CodecVp8EncClass
{
  GstVp8EncoderClass parent_class;
  GstV4l2CodecDevice *device;
};

GType gst_v4l2_codec_vp8_enc_get_type (void);
void  gst_v4l2_codec_vp8_enc_register (GstPlugin * plugin,
                                       GstV4l2Encoder * encoder,
                                       GstV4l2CodecDevice * device,
                                       guint rank);

G_END_DECLS

#endif /* __GST_V4L2_CODEC_VP8_ENC_H__ */

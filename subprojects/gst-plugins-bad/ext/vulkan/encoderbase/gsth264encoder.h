/* GStreamer
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
 * Copyright (C) 2023 Stéphane Cerveau <scerveau@igalia.com>
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
#include <gst/codecparsers/gsth264parser.h>

#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include "gsth264frame.h"

G_BEGIN_DECLS
#define GST_TYPE_H264_ENCODER            (gst_h264_encoder_get_type())
G_DECLARE_DERIVABLE_TYPE (GstH264Encoder, gst_h264_encoder, GST, H264_ENCODER, GstVideoEncoder);
typedef struct _GstH264EncoderPrivate GstH264EncoderPrivate;

typedef struct _H264GOPFrame {
  guint8 slice_type;
  gboolean is_ref;
  guint8 pyramid_level;
  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
  gint index;
  gint poc;
} H264GOPFrame;

/**
 * GstH264EncoderClass:
 */
struct _GstH264EncoderClass
{
  GstVideoEncoderClass parent_class;
  gboolean (*new_frame)      (GstH264Encoder * encoder,
                            GstH264EncodeFrame * frame,
                            guint input_frame_count);
  gboolean (*reorder_frame)  (GstH264Encoder * base,
                              GstVideoCodecFrame * frame,
                              gboolean bump_all,
                              GstVideoCodecFrame ** out_frame);
  /**
   * GstH264EncoderClass::encode_frame:
   * @encoder: a #GstH264Encoder
   * @frame: a #GstH264EncodeFrame
   * @list0: a list of #GstH265Frame
   * @list0_num: number of #GstH265Frame for @list1
   * @list1: a list of #GstH265Frame
   * @list1_num: number of #GstH265Frame for @list1
   *
   * Provide the frame to be encoded with the encode parameters (to be defined)
   */
  GstFlowReturn (*encode_frame) (GstH264Encoder * encoder,
                                 GstH264EncodeFrame * h264_frame,
                                 GstH264EncodeFrame ** list0,
                                 guint list0_num,
                                 GstH264EncodeFrame ** list1,
                                 guint list1_num);

  void     (*prepare_output) (GstH264Encoder * encoder,
                              GstVideoCodecFrame * frame);

  gboolean (*set_format)     (GstH264Encoder * encoder, GstVideoCodecState * state);

  gboolean (*max_num_reference) (GstH264Encoder * encoder, guint32 * max_l0_reference_count, guint32 * max_l1_reference_count);
  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};


GST_CODECS_API
gboolean             gst_h264_encoder_reset            (GstVideoEncoder * encoder, gboolean hard);

GST_CODECS_API
GstH264Level         gst_h264_encoder_get_level_limit  (GstH264Encoder * encoder);

GST_CODECS_API
void                 gst_h264_encoder_set_profile      (GstH264Encoder * encoder, GstH264Profile profile);

GST_CODECS_API
GstH264Profile       gst_h264_encoder_get_profile      (GstH264Encoder * encoder);

GST_CODECS_API
GstVideoCodecState * gst_h264_encoder_get_input_state  (GstH264Encoder * encoder);



G_END_DECLS
#endif /* __GST_H264_ENCODER_H__ */

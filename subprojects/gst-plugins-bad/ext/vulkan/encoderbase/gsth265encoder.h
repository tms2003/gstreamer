/* GStreamer
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

#ifndef __GST_H265_ENCODER_H__
#define __GST_H265_ENCODER_H__

#include <gst/codecs/codecs-prelude.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include "gsth265frame.h"

G_BEGIN_DECLS
#define GST_TYPE_H265_ENCODER            (gst_h265_encoder_get_type())
G_DECLARE_DERIVABLE_TYPE (GstH265Encoder, gst_h265_encoder, GST, H265_ENCODER, GstVideoEncoder);
typedef struct _GstH265EncoderPrivate GstH265EncoderPrivate;

typedef struct _H265GOPFrame
{
  guint8 slice_type;
  gboolean is_ref;
  guint8 pyramid_level;
  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
  gint poc;
  gint index;
} H265GOPFrame;


/**
 * GstH265EncoderClass:
 */
struct _GstH265EncoderClass
{
  GstVideoEncoderClass parent_class;
  gboolean (*new_frame)      (GstH265Encoder * encoder,
                            GstH265EncodeFrame * frame,
                            guint input_frame_count);
  gboolean (*reorder_frame)  (GstH265Encoder * base,
                              GstVideoCodecFrame * frame,
                              gboolean bump_all,
                              GstVideoCodecFrame ** out_frame);
  /**
   * GstH265EncoderClass::encode_frame:
   * @encoder: a #GstH265Encoder
   * @frame: a #GstH265Frame
   * @list0: a list of #GstH265EncodeFrame
   * @list0_num: number of #GstH265EncodeFrame for @list1
   * @list1: a list of #GstH265EncodeFrame
   * @list1_num: number of #GstH265EncodeFrame for @list1
   *
   * Provide the frame to be encoded with the encode parameters (to be defined)
   */
  GstFlowReturn (*encode_frame) (GstH265Encoder * encoder,
                                 GstH265EncodeFrame * frame,
                                 GstH265EncodeFrame ** list0,
                                 guint list0_num,
                                 GstH265EncodeFrame ** list1,
                                 guint list1_num);

  void     (*prepare_output) (GstH265Encoder * encoder,
                              GstVideoCodecFrame * frame);

  gboolean (*set_format)     (GstH265Encoder * encoder, GstVideoCodecState * state);

  gboolean (*max_num_reference) (GstH265Encoder * encoder, guint32 * max_l0_reference_count, guint32 * max_l1_reference_count);
  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

GST_CODECS_API
gboolean             gst_h265_encoder_reset (GstVideoEncoder * encoder, gboolean hard);

GST_CODECS_API
guint8              gst_h265_encoder_get_level_idc  (GstH265Encoder * encoder);

GST_CODECS_API
void                 gst_h265_encoder_set_profile      (GstH265Encoder * encoder, GstH265Profile profile);

GST_CODECS_API
GstH265Profile       gst_h265_encoder_get_profile      (GstH265Encoder * encoder);

GST_CODECS_API
GstVideoCodecState * gst_h265_encoder_get_input_state  (GstH265Encoder * encoder);

GST_CODECS_API
H265GOPFrame         gst_h265_encoder_next_gop_frame   (GstH265Encoder * encoder);

G_END_DECLS
#endif /* __GST_H265_ENCODER_H__ */

/* GStreamer
 * Copyright (C) 2024 Stéphane Cerveau <scerveau@igalia.com>
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

#include "gsth265encoder.h"
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/base/base.h>

GST_DEBUG_CATEGORY (gst_h265_encoder_debug);
#define GST_CAT_DEFAULT gst_h265_encoder_debug

#define H265ENC_DEFAULT_IDR_PERIOD	30
#define H265ENC_DEFAULT_NUM_REF_FRAMES	3

typedef struct _GstH265LevelLimits
{
  const gchar *level_name;
  guint8 level_idc;
  guint32 MaxLumaPs;
  guint32 MaxCPBTierMain;
  guint32 MaxCPBTierHigh;
  guint32 MaxSliceSegPic;
  guint32 MaxTileRows;
  guint32 MaxTileColumns;
  guint32 MaxLumaSr;
  guint32 MaxBRTierMain;
  guint32 MaxBRTierHigh;
  guint32 MinCr;
} GstH265LevelLimit;

static const GstH265LevelLimit _h265_level_limits[] = {
  /* level   idc   MaxMBPS   MaxFS   MaxDpbMbs  MaxBR   MaxCPB  MinCr */
  {"1", GST_H265_LEVEL_L1, 36864, 350, 0, 16, 1, 1, 552960, 128, 0, 2},
  {"2", GST_H265_LEVEL_L2, 122880, 1500, 0, 16, 1, 1, 3686400, 1500, 0, 2},
  {"2.1", GST_H265_LEVEL_L2_1, 245760, 3000, 0, 20, 1, 1, 7372800, 3000, 0, 2},
  {"3", GST_H265_LEVEL_L3, 552960, 6000, 0, 30, 2, 2, 16588800, 6000, 0, 2},
  {"3.1", GST_H265_LEVEL_L3_1, 983040, 10000, 0, 40, 3, 3, 33177600, 10000, 0,
      2},
  {"4", GST_H265_LEVEL_L4, 2228224, 12000, 30000, 75, 5, 5, 66846720, 12000,
      30000, 4},
  {"4.1", GST_H265_LEVEL_L4_1, 2228224, 20000, 50000, 75, 5, 5, 133693440,
      20000, 50000, 4},
  {"5", GST_H265_LEVEL_L5, 8912896, 25000, 100000, 200, 11, 10, 267386880,
      25000, 100000, 6},
  {"5.1", GST_H265_LEVEL_L5_1, 8912896, 40000, 160000, 200, 11, 10, 534773760,
        40000, 160000,
      8},
  {"5.2", GST_H265_LEVEL_L5_2, 8912896, 60000, 240000, 200, 11, 10, 1069547520,
        60000, 240000,
      8},
  {"6", GST_H265_LEVEL_L6, 35651584, 60000, 240000, 600, 22, 20, 1069547520,
        60000, 240000,
      8},
  {"6.1", GST_H265_LEVEL_L6_1, 35651584, 120000, 480000, 600, 22, 20,
        2139095040, 120000,
      480000, 8},
  {"6.2", GST_H265_LEVEL_L6_2, 35651584, 240000, 800000, 600, 22, 20,
        4278190080, 240000,
      800000, 6},
};

enum
{
  PROP_0,
  PROP_IDR_PERIOD,
  PROP_NUM_REF_FRAMES,
  PROP_BFRAMES,
};

struct _GstH265EncoderPrivate
{
  guint64 used_bytes;
  guint64 nb_frames;

  GstH265Profile profile;
  GstVideoCodecState *input_state;

  gint width;
  gint height;

  guint luma_width;
  guint luma_height;

  guint min_cr;
  guint8 level_idc;

  struct
  {
    guint32 idr_period;
    guint num_ref_frames;
    guint num_bframes;
  } prop;

  struct
  {
    /* frames between two IDR [idr, ...., idr) */
    guint32 idr_period;
    /* How may IDRs we have encoded */
    guint32 total_idr_count;
    /* frames between I/P and P frames [I, B, B, .., B, P) */
    guint32 ip_period;
    /* frames between I frames [I, B, B, .., B, P, ..., I), open GOP */
    guint32 i_period;
    /* B frames between I/P and P. */
    guint32 num_bframes;
    /* Use B pyramid structure in the GOP. */
    gboolean b_pyramid;
    /* Level 0 is the simple B not acting as ref. */
    guint32 highest_pyramid_level;
    /* If open GOP, I frames within a GOP. */
    guint32 num_iframes;
    /* A map of all frames types within a GOP. */
    GArray *frame_types;
    /* current index in the frames types map. */
    guint cur_frame_index;
    /* Number of ref frames within current GOP. H265's frame num. */
    gint cur_frame_num;
    /* Max frame num within a GOP. */
    guint32 max_frame_num;
    guint32 log2_max_frame_num;
    /* Max poc within a GOP. */
    guint32 max_pic_order_cnt;
    guint32 log2_max_pic_order_cnt;

    /* Total ref frames of list0 and list1. */
    guint32 num_ref_frames;
    guint32 ref_num_list0;
    guint32 ref_num_list1;

    guint num_reorder_frames;
  } gop;
  struct
  {
    guint max_bitrate;
  } rc;

  GQueue output_list;
  GQueue ref_list;
  GQueue reorder_list;

  guint input_frame_count;

};

#define parent_class gst_h265_encoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH265Encoder, gst_h265_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstH265Encoder);
    GST_DEBUG_CATEGORY_INIT (gst_h265_encoder_debug, "h265encoder", 0,
        "H265 Video Encoder"));


struct PyramidInfo
{
  guint level;
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

static void
_set_pyramid_info (struct PyramidInfo *info, guint len,
    guint current_level, guint highest_level)
{
  guint index;

  g_assert (len >= 1);

  if (current_level == highest_level || len == 1) {
    for (index = 0; index < len; index++) {
      info[index].level = current_level;
      info[index].left_ref_poc_diff = (index + 1) * -2;
      info[index].right_ref_poc_diff = (len - index) * 2;
    }

    return;
  }

  index = len / 2;
  info[index].level = current_level;
  info[index].left_ref_poc_diff = (index + 1) * -2;
  info[index].right_ref_poc_diff = (len - index) * 2;

  current_level++;

  if (index > 0)
    _set_pyramid_info (info, index, current_level, highest_level);

  if (index + 1 < len)
    _set_pyramid_info (&info[index + 1], len - (index + 1),
        current_level, highest_level);
}

static void
gst_h265_encoder_create_gop_frame_types (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  guint i;
  guint i_frames = priv->gop.num_iframes;
  struct PyramidInfo pyramid_info[31] = { 0, };
  H265GOPFrame gop_frame;


  if (priv->gop.highest_pyramid_level > 0) {
    g_assert (priv->gop.num_bframes > 0);
    _set_pyramid_info (pyramid_info, priv->gop.num_bframes,
        0, priv->gop.highest_pyramid_level);
  }

  priv->gop.frame_types = g_array_new (TRUE, TRUE, sizeof (H265GOPFrame));
  for (i = 0; i < priv->gop.idr_period; i++) {
    if (i == 0) {
      gop_frame.slice_type = GST_H265_I_SLICE;
      gop_frame.is_ref = TRUE;
      goto add_gop_frame;
    }

    /* Intra only stream. */
    if (priv->gop.ip_period == 0) {
      gop_frame.slice_type = GST_H265_I_SLICE;
      gop_frame.is_ref = FALSE;
      goto add_gop_frame;
    }

    if (i % priv->gop.ip_period) {
      guint pyramid_index =
          i % priv->gop.ip_period - 1 /* The first P or IDR */ ;

      gop_frame.slice_type = GST_H265_B_SLICE;
      gop_frame.pyramid_level = pyramid_info[pyramid_index].level;
      gop_frame.is_ref =
          (gop_frame.pyramid_level < priv->gop.highest_pyramid_level);
      gop_frame.left_ref_poc_diff =
          pyramid_info[pyramid_index].left_ref_poc_diff;
      gop_frame.right_ref_poc_diff =
          pyramid_info[pyramid_index].right_ref_poc_diff;
      goto add_gop_frame;
    }

    if (priv->gop.i_period && i % priv->gop.i_period == 0 && i_frames > 0) {
      /* Replace P with I. */
      gop_frame.slice_type = GST_H265_I_SLICE;
      gop_frame.is_ref = TRUE;
      i_frames--;
      goto add_gop_frame;
    }

    gop_frame.slice_type = GST_H265_P_SLICE;
    gop_frame.is_ref = TRUE;
  add_gop_frame:
    g_array_append_val (priv->gop.frame_types, gop_frame);
  }

  /* Force the last one to be a P */
  gop_frame =
      g_array_index (priv->gop.frame_types, H265GOPFrame,
      priv->gop.idr_period - 1);
  if (priv->gop.idr_period > 1 && priv->gop.ip_period > 0) {
    gop_frame.slice_type = GST_H265_P_SLICE;
    gop_frame.is_ref = TRUE;
  }
  g_array_remove_index (priv->gop.frame_types, priv->gop.idr_period - 1);
  g_array_append_val (priv->gop.frame_types, gop_frame);
}

static void
gst_h265_encoder_print_gop_structure (GstH265Encoder * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GString *str;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_INFO)
    return;

  str = g_string_new (NULL);

  g_string_append_printf (str, "[ ");

  for (i = 0; i < priv->gop.idr_period; i++) {
    H265GOPFrame gop_frame =
        g_array_index (priv->gop.frame_types, H265GOPFrame, i);
    if (i == 0) {
      g_string_append_printf (str, "IDR");
      continue;
    } else {
      g_string_append_printf (str, ", ");
    }

    g_string_append_printf (str, "%s",
        gst_h265_slice_type_to_string (gop_frame.slice_type));

    if (priv->gop.b_pyramid && gop_frame.slice_type == GST_H265_B_SLICE) {
      g_string_append_printf (str, "<L%d (%d, %d)>",
          gop_frame.pyramid_level,
          gop_frame.left_ref_poc_diff, gop_frame.right_ref_poc_diff);
    }

    if (gop_frame.is_ref) {
      g_string_append_printf (str, "(ref)");
    }

  }

  g_string_append_printf (str, " ]");

  GST_INFO_OBJECT (self, "GOP size: %d, forward reference %d, backward"
      " reference %d, GOP structure: %s", priv->gop.idr_period,
      priv->gop.ref_num_list0, priv->gop.ref_num_list1, str->str);

  g_string_free (str, TRUE);
#endif
}

static gboolean
gst_h265_encoder_calculate_tier_level (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  guint i, PicSizeInSamplesY, LumaSr;
  guint32 tier_max_bitrate;
  gboolean tier_flag = TRUE;


  PicSizeInSamplesY = priv->luma_width * priv->luma_height;
  LumaSr = gst_util_uint64_scale_int_ceil (PicSizeInSamplesY,
      GST_VIDEO_INFO_FPS_N (&priv->input_state->info),
      GST_VIDEO_INFO_FPS_D (&priv->input_state->info));

  for (i = 0; i < G_N_ELEMENTS (_h265_level_limits); i++) {
    const GstH265LevelLimit *const limits = &_h265_level_limits[i];

    /* Choose level by luma picture size and luma sample rate */
    if (PicSizeInSamplesY <= limits->MaxLumaPs && LumaSr <= limits->MaxLumaSr)
      break;
  }

  if (i == G_N_ELEMENTS (_h265_level_limits))
    goto error_unsupported_level;

  priv->level_idc = _h265_level_limits[i].level_idc;
  priv->min_cr = _h265_level_limits[i].MinCr;

  if (_h265_level_limits[i].MaxBRTierHigh == 0 ||
      priv->rc.max_bitrate <= _h265_level_limits[i].MaxBRTierMain) {
    tier_flag = FALSE;
  }

  tier_max_bitrate = tier_flag ? _h265_level_limits[i].MaxBRTierHigh :
      _h265_level_limits[i].MaxBRTierMain;

  if (priv->rc.max_bitrate > tier_max_bitrate) {
    GST_INFO_OBJECT (self, "The max bitrate of the stream is %u kbps, still"
        " larger than %s profile %s level %s tier's max bit rate %d kbps",
        priv->rc.max_bitrate, gst_h265_profile_to_string (priv->profile),
        _h265_level_limits[i].level_name,
        (tier_flag ? "high" : "main"), tier_max_bitrate);
  }

  GST_DEBUG_OBJECT (self, "profile: %s, level: %s, tier :%s, MinCr: %d",
      gst_h265_profile_to_string (priv->profile),
      _h265_level_limits[i].level_name, (tier_flag ? "high" : "main"),
      priv->min_cr);

  return TRUE;

error_unsupported_level:
  {
    GST_ERROR_OBJECT (self,
        "failed to find a suitable level matching codec config");
    return FALSE;
  }
}

static void
gst_h265_encoder_generate_gop_structure (GstH265Encoder * self)
{
  GstH265EncoderClass *base_class = GST_H265_ENCODER_GET_CLASS (self);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  guint32 list0, list1, gop_ref_num;
  gint32 p_frames;
  /* If not set, generate a idr every second */
  if (priv->gop.idr_period == 0) {
    priv->gop.idr_period = (GST_VIDEO_INFO_FPS_N (&priv->input_state->info)
        + GST_VIDEO_INFO_FPS_D (&priv->input_state->info) - 1) /
        GST_VIDEO_INFO_FPS_D (&priv->input_state->info);
  }

  if (priv->gop.idr_period > 8) {
    if (priv->gop.num_bframes > (priv->gop.idr_period - 1) / 2) {
      priv->gop.num_bframes = (priv->gop.idr_period - 1) / 2;
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.num_bframes);
    }
  } else {
    /* beign and end should be ref */
    if (priv->gop.num_bframes > priv->gop.idr_period - 1 - 1) {
      if (priv->gop.idr_period > 1) {
        priv->gop.num_bframes = priv->gop.idr_period - 1 - 1;
      } else {
        priv->gop.num_bframes = 0;
      }
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.num_bframes);
    }
  }

  if (!base_class->max_num_reference
      || !base_class->max_num_reference (self, &list0, &list1)) {
    GST_INFO_OBJECT (self, "Failed to get the max num reference");
    list0 = 1;
    list1 = 0;
  }

  if (list0 > priv->gop.num_ref_frames)
    list0 = priv->gop.num_ref_frames;
  if (list1 > priv->gop.num_ref_frames)
    list1 = priv->gop.num_ref_frames;

  if (list0 == 0) {
    GST_INFO_OBJECT (self,
        "No reference support, fallback to intra only stream");

    /* It does not make sense that if only the list1 exists. */
    priv->gop.num_ref_frames = 0;

    priv->gop.ip_period = 0;
    priv->gop.num_bframes = 0;
    priv->gop.b_pyramid = FALSE;
    priv->gop.highest_pyramid_level = 0;
    priv->gop.num_iframes = priv->gop.idr_period - 1 /* The idr */ ;
    priv->gop.ref_num_list0 = 0;
    priv->gop.ref_num_list1 = 0;
    goto create_poc;
  }
  if (priv->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " no B frame allowed, fallback to I/P mode", priv->gop.num_ref_frames);
    priv->gop.num_bframes = 0;
    list1 = 0;
  }

  /* b_pyramid needs at least 1 ref for B, besides the I/P */
  if (priv->gop.b_pyramid && priv->gop.num_ref_frames <= 2) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " not enough for b_pyramid", priv->gop.num_ref_frames);
    priv->gop.b_pyramid = FALSE;
  }

  if (list1 == 0 && priv->gop.num_bframes > 0) {
    GST_INFO_OBJECT (self,
        "No hw reference support for list 1, fallback to I/P mode");
    priv->gop.num_bframes = 0;
    priv->gop.b_pyramid = FALSE;
  }

  /* I/P mode, no list1 needed. */
  if (priv->gop.num_bframes == 0)
    list1 = 0;

  /* Not enough B frame, no need for b_pyramid. */
  if (priv->gop.num_bframes <= 1)
    priv->gop.b_pyramid = FALSE;

  /* b pyramid has only one backward ref. */
  if (priv->gop.b_pyramid)
    list1 = 1;

  if (priv->gop.num_ref_frames > list0 + list1) {
    priv->gop.num_ref_frames = list0 + list1;
    GST_WARNING_OBJECT (self, "HW limits, lowering the number of reference"
        " frames to %d", priv->gop.num_ref_frames);
  }

  /* How many possible refs within a GOP. */
  gop_ref_num = (priv->gop.idr_period + priv->gop.num_bframes) /
      (priv->gop.num_bframes + 1);
  /* The end ref */
  if (priv->gop.num_bframes > 0
      /* frame_num % (priv->gop.num_bframes + 1) happens to be the end P */
      && (priv->gop.idr_period % (priv->gop.num_bframes + 1) != 1))
    gop_ref_num++;

  /* Adjust reference num based on B frames and B pyramid. */
  if (priv->gop.num_bframes == 0) {
    priv->gop.b_pyramid = FALSE;
    priv->gop.ref_num_list0 = priv->gop.num_ref_frames;
    priv->gop.ref_num_list1 = 0;
  } else if (priv->gop.b_pyramid) {
    guint b_frames = priv->gop.num_bframes;
    guint b_refs;

    /* b pyramid has only one backward ref. */
    g_assert (list1 == 1);
    priv->gop.ref_num_list1 = list1;
    priv->gop.ref_num_list0 =
        priv->gop.num_ref_frames - priv->gop.ref_num_list1;

    b_frames = b_frames / 2;
    b_refs = 0;
    while (b_frames) {
      /* At least 1 B ref for each level, plus begin and end 2 P/I */
      b_refs += 1;
      if (b_refs + 2 > priv->gop.num_ref_frames)
        break;

      priv->gop.highest_pyramid_level++;
      b_frames = b_frames / 2;
    }

    GST_INFO_OBJECT (self, "pyramid level is %d",
        priv->gop.highest_pyramid_level);
  } else {
    /* We prefer list0. Backward refs have more latency. */
    priv->gop.ref_num_list1 = 1;
    priv->gop.ref_num_list0 =
        priv->gop.num_ref_frames - priv->gop.ref_num_list1;
    /* Balance the forward and backward refs, but not cause a big latency. */
    while ((priv->gop.num_bframes * priv->gop.ref_num_list1 <= 16)
        && (priv->gop.ref_num_list1 <= gop_ref_num)
        && (priv->gop.ref_num_list1 < list1)
        && (priv->gop.ref_num_list0 / priv->gop.ref_num_list1 > 4)) {
      priv->gop.ref_num_list0--;
      priv->gop.ref_num_list1++;
    }

    if (priv->gop.ref_num_list0 > list0)
      priv->gop.ref_num_list0 = list0;
  }

  /* It's OK, keep slots for GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME frame. */
  if (priv->gop.ref_num_list0 > gop_ref_num)
    GST_DEBUG_OBJECT (self, "num_ref_frames %d is bigger than gop_ref_num %d",
        priv->gop.ref_num_list0, gop_ref_num);

  /* Include the ref picture itself. */
  priv->gop.ip_period = 1 + priv->gop.num_bframes;

  p_frames = gop_ref_num - 1 /* IDR */ ;
  if (p_frames < 0)
    p_frames = 0;
  if (priv->gop.num_iframes > p_frames) {
    priv->gop.num_iframes = p_frames;
    GST_INFO_OBJECT (self, "Too many I frames insertion, lowering it to %d",
        priv->gop.num_iframes);
  }

  if (priv->gop.num_iframes > 0) {
    guint total_i_frames = priv->gop.num_iframes + 1 /* IDR */ ;
    priv->gop.i_period =
        (gop_ref_num / total_i_frames) * (priv->gop.num_bframes + 1);
  }

create_poc:
  priv->gop.log2_max_frame_num = gst_util_ceil_log2 (priv->gop.idr_period);
  priv->gop.max_frame_num = (1 << priv->gop.log2_max_frame_num);
  priv->gop.log2_max_pic_order_cnt = priv->gop.log2_max_frame_num + 1;
  priv->gop.max_pic_order_cnt = (1 << priv->gop.log2_max_pic_order_cnt);

  gst_h265_encoder_create_gop_frame_types (self);
  gst_h265_encoder_print_gop_structure (self);
}

static inline GstH265EncodeFrame *
_enc_frame (GstVideoCodecFrame * frame)
{
  GstH265EncodeFrame *enc_frame = gst_video_codec_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}


static void
gst_h265_encoder_init (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  g_queue_init (&priv->output_list);
  g_queue_init (&priv->ref_list);
  g_queue_init (&priv->reorder_list);
}

static void
gst_h265_encoder_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h265_encoder_start (GstVideoEncoder * encoder)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  priv->used_bytes = 0;
  priv->nb_frames = 0;

  priv->width = 0;
  priv->height = 0;

  return TRUE;
}

static gboolean
gst_h265_encoder_stop (GstVideoEncoder * encoder)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  g_queue_clear_full (&priv->output_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->reorder_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  if (priv->gop.frame_types)
    g_array_free (priv->gop.frame_types, TRUE);
  return TRUE;
}

gboolean
gst_h265_encoder_reset (GstVideoEncoder * encoder, gboolean hard)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  priv->gop.idr_period = priv->prop.idr_period;
  priv->gop.num_ref_frames = priv->prop.num_ref_frames;
  priv->gop.num_bframes = priv->prop.num_bframes;
  priv->gop.total_idr_count = 0;
  priv->gop.num_iframes = 0;
  priv->gop.cur_frame_index = 0;
  priv->gop.max_pic_order_cnt = 0;
  priv->gop.cur_frame_num = 0;

  return TRUE;
}

static gboolean
gst_h265_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstH265EncoderClass *base_class = GST_H265_ENCODER_GET_CLASS (self);
  gboolean ret = TRUE;

  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);

  priv->input_state = gst_video_codec_state_ref (state);

  priv->width = GST_VIDEO_INFO_WIDTH (&priv->input_state->info);
  priv->height = GST_VIDEO_INFO_HEIGHT (&priv->input_state->info);

  priv->luma_width = GST_ROUND_UP_16 (priv->width);
  priv->luma_height = GST_ROUND_UP_16 (priv->height);

  if (!gst_h265_encoder_calculate_tier_level (self))
    return FALSE;

  if (base_class->set_format && !base_class->set_format (self, state)) {
    GST_WARNING_OBJECT (self, "Unable to set format properly");
    ret = FALSE;
    goto beach;
  }

  gst_h265_encoder_generate_gop_structure (self);

beach:
  gst_video_codec_state_unref (priv->input_state);
  return ret;
}

static void
gst_h265_encoder_mark_frame (GstH265Encoder * self,
    GstH265EncodeFrame * h265_frame)
{
  GstVideoCodecFrame *frame = h265_frame->frame;
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  if (frame->output_buffer)
    priv->used_bytes += gst_buffer_get_size (frame->output_buffer);

  priv->nb_frames++;
}

static GstFlowReturn
gst_h265_encoder_push_buffer_to_downstream (GstH265Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstH265EncoderClass *base_class = GST_H265_ENCODER_GET_CLASS (self);

  if (base_class->prepare_output)
    base_class->prepare_output (self, frame);

  GST_LOG_OBJECT (self, "Push to downstream: frame system_frame_number: %d,"
      " pts: %" GST_TIME_FORMAT ", dts: %" GST_TIME_FORMAT
      " duration: %" GST_TIME_FORMAT ", buffer size: %" G_GSIZE_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts),
      GST_TIME_ARGS (frame->dts), GST_TIME_ARGS (frame->duration),
      gst_buffer_get_size (frame->output_buffer));

  return gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
}

static GstFlowReturn
_push_out_one_buffer (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstVideoCodecFrame *frame_out;
  GstFlowReturn ret;
  guint32 system_frame_number;

  frame_out = g_queue_pop_head (&priv->output_list);
  gst_video_codec_frame_unref (frame_out);

  system_frame_number = frame_out->system_frame_number;

  ret = gst_h265_encoder_push_buffer_to_downstream (self, frame_out);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "fails to push one buffer, system_frame_number "
        "%d: %s", system_frame_number, gst_flow_get_name (ret));
  }

  return ret;
}

static gboolean
_push_one_frame (GstH265Encoder * self, GstVideoCodecFrame * gst_frame,
    gboolean last)
{
  GstH265EncodeFrame *h265_frame;
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);


  g_return_val_if_fail (priv->gop.cur_frame_index <= priv->gop.idr_period,
      FALSE);

  if (gst_frame) {
    /* Begin a new GOP, should have a empty reorder_list. */
    H265GOPFrame gop_frame = gst_h265_encoder_next_gop_frame (self);
    h265_frame = _enc_frame (gst_frame);
    h265_frame->poc = gop_frame.poc;

    if (gop_frame.index == 0) {
      g_assert (h265_frame->poc == 0);
      GST_LOG_OBJECT (self, "system_frame_number: %d, an IDR frame, starts"
          " a new GOP", gst_frame->system_frame_number);

      g_queue_clear_full (&priv->ref_list,
          (GDestroyNotify) gst_video_codec_frame_unref);

      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (gst_frame);
    }

    h265_frame->type = gop_frame.slice_type;
    h265_frame->is_ref = gop_frame.is_ref;
    h265_frame->pyramid_level = gop_frame.pyramid_level;
    h265_frame->left_ref_poc_diff = gop_frame.left_ref_poc_diff;
    h265_frame->right_ref_poc_diff = gop_frame.right_ref_poc_diff;

    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (gst_frame)) {
      GST_DEBUG_OBJECT (self, "system_frame_number: %d, a force key frame,"
          " promote its type from %s to %s", gst_frame->system_frame_number,
          gst_h265_slice_type_to_string (h265_frame->type),
          gst_h265_slice_type_to_string (GST_H265_I_SLICE));
      h265_frame->type = GST_H265_I_SLICE;
      h265_frame->is_ref = TRUE;
    }

    GST_LOG_OBJECT (self, "Push frame, system_frame_number: %d, poc %d, "
        "frame type %s", gst_frame->system_frame_number, h265_frame->poc,
        gst_h265_slice_type_to_string (h265_frame->type));

    g_queue_push_tail (&priv->reorder_list,
        gst_video_codec_frame_ref (gst_frame));
  }

  /* ensure the last one a non-B and end the GOP. */
  if (last && priv->gop.cur_frame_index < priv->gop.idr_period) {
    GstVideoCodecFrame *last_frame;

    /* Ensure next push will start a new GOP. */
    priv->gop.cur_frame_index = priv->gop.idr_period;

    if (!g_queue_is_empty (&priv->reorder_list)) {
      last_frame = g_queue_peek_tail (&priv->reorder_list);
      h265_frame = _enc_frame (last_frame);
      if (h265_frame->type == GST_H265_B_SLICE) {
        h265_frame->type = GST_H265_P_SLICE;
        h265_frame->is_ref = TRUE;
      }
    }
  }

  return TRUE;
}

struct RefFramesCount
{
  gint poc;
  guint num;
};

static void
_count_backward_ref_num (gpointer data, gpointer user_data)
{
  GstH265EncodeFrame *frame = _enc_frame (data);
  struct RefFramesCount *count = (struct RefFramesCount *) user_data;

  g_assert (frame->poc != count->poc);
  if (frame->poc > count->poc)
    count->num++;
}

static GstVideoCodecFrame *
_pop_pyramid_b_frame (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  guint i;
  gint index = -1;
  GstH265EncodeFrame *b_encframe;
  GstVideoCodecFrame *b_frame;
  struct RefFramesCount count;

  g_assert (priv->gop.ref_num_list1 == 1);

  b_frame = NULL;
  b_encframe = NULL;

  /* Find the lowest level with smallest poc. */
  for (i = 0; i < g_queue_get_length (&priv->reorder_list); i++) {
    GstH265EncodeFrame *encf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&priv->reorder_list, i);

    if (!b_frame) {
      b_frame = f;
      b_encframe = _enc_frame (b_frame);
      index = i;
      continue;
    }

    encf = _enc_frame (f);
    if (b_encframe->pyramid_level < encf->pyramid_level) {
      b_frame = f;
      b_encframe = encf;
      index = i;
      continue;
    }

    if (b_encframe->poc > encf->poc) {
      b_frame = f;
      b_encframe = encf;
      index = i;
    }
  }

again:
  /* Check whether its refs are already poped. */
  g_assert (b_encframe->left_ref_poc_diff != 0);
  g_assert (b_encframe->right_ref_poc_diff != 0);
  for (i = 0; i < g_queue_get_length (&priv->reorder_list); i++) {
    GstH265EncodeFrame *encf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&priv->reorder_list, i);

    if (f == b_frame)
      continue;

    encf = _enc_frame (f);
    if (encf->poc == b_encframe->poc + b_encframe->left_ref_poc_diff
        || encf->poc == b_encframe->poc + b_encframe->right_ref_poc_diff) {
      b_frame = f;
      b_encframe = encf;
      index = i;
      goto again;
    }
  }

  /* Ensure we already have enough backward refs */
  count.num = 0;
  count.poc = b_encframe->poc;
  g_queue_foreach (&priv->ref_list, (GFunc) _count_backward_ref_num, &count);
  if (count.num >= priv->gop.ref_num_list1) {
    GstVideoCodecFrame *f;

    /* it will unref at pop_frame */
    f = g_queue_pop_nth (&priv->reorder_list, index);
    g_assert (f == b_frame);
  } else {
    b_frame = NULL;
  }

  return b_frame;
}

static gboolean
_pop_one_frame (GstH265Encoder * self, GstVideoCodecFrame ** out_frame)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstH265EncodeFrame *encframe;
  GstVideoCodecFrame *frame;
  struct RefFramesCount count;

  g_return_val_if_fail (priv->gop.cur_frame_index <= priv->gop.idr_period,
      FALSE);

  *out_frame = NULL;

  if (g_queue_is_empty (&priv->reorder_list))
    return TRUE;

  /* Return the last pushed non-B immediately. */
  frame = g_queue_peek_tail (&priv->reorder_list);
  encframe = _enc_frame (frame);
  if (encframe->type != GST_H265_B_SLICE) {
    frame = g_queue_pop_tail (&priv->reorder_list);
    goto get_one;
  }

  if (priv->gop.b_pyramid) {
    frame = _pop_pyramid_b_frame (self);
    if (frame == NULL)
      return TRUE;
    goto get_one;
  }

  g_assert (priv->gop.ref_num_list1 > 0);

  /* If GOP end, pop anyway. */
  if (priv->gop.cur_frame_index == priv->gop.idr_period) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  /* Ensure we already have enough backward refs */
  frame = g_queue_peek_head (&priv->reorder_list);
  encframe = _enc_frame (frame);
  count.num = 0;
  count.poc = encframe->poc;
  g_queue_foreach (&priv->ref_list, _count_backward_ref_num, &count);
  if (count.num >= priv->gop.ref_num_list1) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  return TRUE;

get_one:
  g_assert (priv->gop.cur_frame_num < priv->gop.max_frame_num);

  encframe = _enc_frame (frame);
  encframe->frame_num = priv->gop.cur_frame_num;

  /* Add the frame number for ref frames. */
  if (encframe->is_ref)
    priv->gop.cur_frame_num++;

  if (encframe->frame_num == 0)
    priv->gop.total_idr_count++;

  if (priv->gop.b_pyramid && encframe->type == GST_H265_B_SLICE) {
    GST_LOG_OBJECT (self,
        "pop a pyramid B frame with system_frame_number:"
        " %d, poc: %d, frame num: %d, is_ref: %s, level %d",
        frame->system_frame_number, encframe->poc, encframe->frame_num,
        encframe->is_ref ? "true" : "false", encframe->pyramid_level);
  } else {
    GST_LOG_OBJECT (self, "pop a frame with system_frame_number: %d,"
        " frame type: %s, poc: %d, frame num: %d, is_ref: %s",
        frame->system_frame_number,
        gst_h265_slice_type_to_string (encframe->type), encframe->poc,
        encframe->frame_num, encframe->is_ref ? "true" : "false");
  }

  /* unref frame popped from queue or pyramid b_frame */
  gst_video_codec_frame_unref (frame);
  *out_frame = frame;
  return TRUE;
}

static gboolean
gst_h265_encoder_reorder_frame (GstH265Encoder * self,
    GstVideoCodecFrame * frame, gboolean bump_all,
    GstVideoCodecFrame ** out_frame)
{
  if (!_push_one_frame (self, frame, bump_all)) {
    GST_ERROR_OBJECT (self, "Failed to push the input frame"
        " system_frame_number: %d into the reorder list",
        frame->system_frame_number);

    *out_frame = NULL;
    return FALSE;
  }

  if (!_pop_one_frame (self, out_frame)) {
    GST_ERROR_OBJECT (self, "Failed to pop the frame from the reorder list");
    *out_frame = NULL;
    return FALSE;
  }

  return TRUE;
}

static gint
_sort_by_frame_num (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstH265EncodeFrame *frame1 = _enc_frame ((GstVideoCodecFrame *) a);
  GstH265EncodeFrame *frame2 = _enc_frame ((GstVideoCodecFrame *) b);

  g_assert (frame1->frame_num != frame2->frame_num);

  return frame1->frame_num - frame2->frame_num;
}

static GstVideoCodecFrame *
_find_unused_reference_frame (GstH265Encoder * self, GstH265EncodeFrame * frame)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstH265EncodeFrame *b_h265frame;
  GstVideoCodecFrame *b_frame;
  guint i;

  /* We still have more space. */
  if (g_queue_get_length (&priv->ref_list) < priv->gop.num_ref_frames)
    return NULL;

  /* Not b_pyramid, sliding window is enough. */
  if (!priv->gop.b_pyramid)
    return g_queue_peek_head (&priv->ref_list);

  /* I/P frame, just using sliding window. */
  if (frame->type != GST_H265_B_SLICE)
    return g_queue_peek_head (&priv->ref_list);

  /* Choose the B frame with lowest POC. */
  b_frame = NULL;
  b_h265frame = NULL;
  for (i = 0; i < g_queue_get_length (&priv->ref_list); i++) {
    GstH265EncodeFrame *encf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&priv->ref_list, i);
    encf = _enc_frame (f);
    if (encf->type != GST_H265_B_SLICE)
      continue;

    if (!b_frame) {
      b_frame = f;
      b_h265frame = _enc_frame (b_frame);
      continue;
    }

    b_h265frame = _enc_frame (b_frame);
    g_assert (encf->poc != b_h265frame->poc);
    if (encf->poc < b_h265frame->poc) {
      b_frame = f;
      b_h265frame = _enc_frame (b_frame);
    }
  }

  /* No B frame as ref. */
  if (!b_frame)
    return g_queue_peek_head (&priv->ref_list);

  if (b_frame != g_queue_peek_head (&priv->ref_list)) {
    b_h265frame = _enc_frame (b_frame);
    frame->unused_for_reference_pic_num = b_h265frame->frame_num;
    GST_LOG_OBJECT (self, "The frame with POC: %d, pic_num %d will be"
        " replaced by the frame with POC: %d, pic_num %d explicitly by"
        " using memory_management_control_operation=1",
        b_h265frame->poc, b_h265frame->frame_num, frame->poc, frame->frame_num);
  }

  return b_frame;
}

static gint
_poc_asc_compare (const GstH265EncodeFrame ** a, const GstH265EncodeFrame ** b)
{
  return (*a)->poc - (*b)->poc;
}

static gint
_poc_des_compare (const GstH265EncodeFrame ** a, const GstH265EncodeFrame ** b)
{
  return (*b)->poc - (*a)->poc;
}

static gboolean
_encode_one_frame (GstH265Encoder * self, GstVideoCodecFrame * gst_frame)
{
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstH265EncodeFrame *list0[16] = { NULL, };
  guint list0_num = 0;
  GstH265EncodeFrame *list1[16] = { NULL, };
  guint list1_num = 0;
  gint i;
  GstH265EncodeFrame *h265_frame;

  g_return_val_if_fail (gst_frame, FALSE);

  h265_frame = _enc_frame (gst_frame);

  /* Non I frame, construct reference list. */
  if (h265_frame->type != GST_H265_I_SLICE) {
    GstH265EncodeFrame *vaf;
    GstVideoCodecFrame *f;

    for (i = g_queue_get_length (&priv->ref_list) - 1; i >= 0; i--) {
      f = g_queue_peek_nth (&priv->ref_list, i);
      vaf = _enc_frame (f);
      if (vaf->poc > h265_frame->poc)
        continue;

      list0[list0_num] = vaf;
      list0_num++;
    }

    /* reorder to select the nearest forward frames. */
    g_qsort_with_data (list0, list0_num, sizeof (gpointer),
        (GCompareDataFunc) _poc_des_compare, NULL);

    if (list0_num > priv->gop.ref_num_list0)
      list0_num = priv->gop.ref_num_list0;
  }

  if (h265_frame->type == GST_H265_B_SLICE) {
    GstH265EncodeFrame *vaf;
    GstVideoCodecFrame *f;

    for (i = 0; i < g_queue_get_length (&priv->ref_list); i++) {
      f = g_queue_peek_nth (&priv->ref_list, i);
      vaf = _enc_frame (f);
      if (vaf->poc < h265_frame->poc)
        continue;

      list1[list1_num] = vaf;
      list1_num++;
    }

    /* reorder to select the nearest backward frames. */
    g_qsort_with_data (list1, list1_num, sizeof (gpointer),
        (GCompareDataFunc) _poc_asc_compare, NULL);

    if (list1_num > priv->gop.ref_num_list1)
      list1_num = priv->gop.ref_num_list1;
  }

  g_assert (list0_num + list1_num <= priv->gop.num_ref_frames);

  return klass->encode_frame (self, h265_frame, list0, list0_num, list1,
      list1_num);
}

static GstFlowReturn
gst_h265_encoder_encode_frame (GstH265Encoder * self,
    GstVideoCodecFrame * gst_frame, gboolean is_last)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstH265EncodeFrame *frame;
  GstVideoCodecFrame *unused_ref = NULL;

  frame = _enc_frame (gst_frame);
  frame->last_frame = is_last;

  if (frame->is_ref)
    unused_ref = _find_unused_reference_frame (self, frame);

  if (_encode_one_frame (self, gst_frame) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to encode the frame");
    return GST_FLOW_ERROR;
  }

  if (frame->is_ref) {
    if (unused_ref) {
      if (!g_queue_remove (&priv->ref_list, unused_ref))
        g_assert_not_reached ();

      gst_video_codec_frame_unref (unused_ref);
    }

    /* Add it into the reference list. */
    g_queue_push_tail (&priv->ref_list, gst_video_codec_frame_ref (gst_frame));
    g_queue_sort (&priv->ref_list, _sort_by_frame_num, NULL);

    g_assert (g_queue_get_length (&priv->ref_list) <= priv->gop.num_ref_frames);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h265_encoder_drain (GstVideoEncoder * encoder)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame_enc = NULL;
  gboolean is_last = FALSE;

  GST_DEBUG_OBJECT (self, "Encoder is draining");

  /* Kickout all cached frames */
  if (!gst_h265_encoder_reorder_frame (self, NULL, TRUE, &frame_enc)) {
    ret = GST_FLOW_ERROR;
    goto error_and_purge_all;
  }

  while (frame_enc) {
    is_last = FALSE;

    if (g_queue_is_empty (&priv->reorder_list))
      is_last = TRUE;

    ret = gst_h265_encoder_encode_frame (self, frame_enc, is_last);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    g_queue_push_tail (&priv->output_list,
        gst_video_codec_frame_ref (frame_enc));

    frame_enc = NULL;

    ret = _push_out_one_buffer (self);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    if (!gst_h265_encoder_reorder_frame (self, NULL, TRUE, &frame_enc)) {
      ret = GST_FLOW_ERROR;
      goto error_and_purge_all;
    }
  }

  g_assert (g_queue_is_empty (&priv->reorder_list));

  /* Output all frames. */
  while (!g_queue_is_empty (&priv->output_list)) {
    ret = _push_out_one_buffer (self);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return GST_FLOW_OK;

error_and_purge_all:
  if (frame_enc) {
    gst_clear_buffer (&frame_enc->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame_enc);
  }

  if (!g_queue_is_empty (&priv->output_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the output list"
        " after drain", g_queue_get_length (&priv->output_list));
    while (!g_queue_is_empty (&priv->output_list)) {
      frame_enc = g_queue_pop_head (&priv->output_list);
      gst_video_codec_frame_unref (frame_enc);
      gst_clear_buffer (&frame_enc->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame_enc);
    }
  }

  if (!g_queue_is_empty (&priv->reorder_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the reorder list"
        " after drain", g_queue_get_length (&priv->reorder_list));
    while (!g_queue_is_empty (&priv->reorder_list)) {
      frame_enc = g_queue_pop_head (&priv->reorder_list);
      gst_video_codec_frame_unref (frame_enc);
      gst_clear_buffer (&frame_enc->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame_enc);
    }
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

static GstFlowReturn
gst_h265_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstH265EncodeFrame *h265_frame = gst_h265_encode_frame_new (frame);
  GstVideoCodecFrame *frame_encode = NULL;
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  h265_frame->frame_num = priv->input_frame_count;
  h265_frame->total_frame_count = priv->input_frame_count + 1;
  gst_video_codec_frame_set_user_data (frame, h265_frame,
      gst_h265_encode_frame_unref);

  if (!klass->new_frame (self, h265_frame, priv->input_frame_count))
    goto error_new_frame;

  priv->input_frame_count++;

  if (!gst_h265_encoder_reorder_frame (self, frame, FALSE, &frame_encode))
    goto error_reorder;

  /* TODO: add encoding parameters management here
   * for now just send the frame to encode */
  while (frame_encode) {
    ret = gst_h265_encoder_encode_frame (self, frame_encode, FALSE);
    g_queue_push_tail (&priv->output_list,
        gst_video_codec_frame_ref (frame_encode));
    if (ret == GST_FLOW_OK) {
      if (frame_encode == h265_frame->frame)
        gst_h265_encoder_mark_frame (self, h265_frame);
    } else
      goto error_encode;

    frame_encode = NULL;
    if (!gst_h265_encoder_reorder_frame (self, NULL, FALSE, &frame_encode))
      goto error_reorder;
    while (g_queue_get_length (&priv->output_list) > 0)
      ret = _push_out_one_buffer (self);

  }

  goto beach;
error_new_frame:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to create the input frame."), (NULL));
    gst_clear_buffer (&frame->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame);
    ret = GST_FLOW_ERROR;
    goto beach;
  }
error_reorder:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to reorder the input frame."), (NULL));
    if (frame) {
      gst_clear_buffer (&frame->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame);
    }
    ret = GST_FLOW_ERROR;
    goto beach;
  }
error_encode:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to encode the frame %s.", gst_flow_get_name (ret)), (NULL));
    gst_clear_buffer (&frame_encode->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame_encode);
    ret = GST_FLOW_ERROR;
  }
beach:
  return ret;
}

static GstFlowReturn
gst_h265_encoder_finish (GstVideoEncoder * encoder)
{
  return gst_h265_encoder_drain (encoder);
}

static void
gst_h265_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstH265Encoder *self = GST_H265_ENCODER (object);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);

  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_IDR_PERIOD:
      g_value_set_int (value, priv->prop.idr_period);
      break;
    case PROP_NUM_REF_FRAMES:
      g_value_set_int (value, priv->prop.num_ref_frames);
      break;
    case PROP_BFRAMES:
      g_value_set_uint (value, priv->prop.num_bframes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_h265_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH265Encoder *self = GST_H265_ENCODER (object);
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (self);
  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_IDR_PERIOD:
      priv->prop.idr_period = g_value_get_int (value);
      break;
    case PROP_NUM_REF_FRAMES:
      priv->prop.num_ref_frames = g_value_get_int (value);
      break;
    case PROP_BFRAMES:
      priv->prop.num_bframes = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);

}

static void
gst_h265_encoder_class_init (GstH265EncoderClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_h265_encoder_finalize);
  object_class->get_property = gst_h265_encoder_get_property;
  object_class->set_property = gst_h265_encoder_set_property;

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_h265_encoder_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_h265_encoder_stop);
  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h265_encoder_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h265_encoder_handle_frame);
  encoder_class->reset = GST_DEBUG_FUNCPTR (gst_h265_encoder_reset);
  encoder_class->finish = GST_DEBUG_FUNCPTR (gst_h265_encoder_finish);

  /**
   * GstH265Encoder:idr-period:
   *
   * Interval between keyframes
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_IDR_PERIOD,
      g_param_spec_int ("idr-period", "IDR period",
          "Interval between keyframes",
          0, G_MAXINT, H265ENC_DEFAULT_IDR_PERIOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
   * GstH265Encoder:num-ref-frames:
   *
   * Number of reference frames.
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_NUM_REF_FRAMES,
      g_param_spec_int ("num-ref-frames", "Num Reference frames",
          "Number of reference frames",
          0, G_MAXINT, H265ENC_DEFAULT_NUM_REF_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
   * GstH265Encoder:b-frames:
   *
   * Number of B-frames between two reference frames.
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_BFRAMES,
      g_param_spec_uint ("b-frames", "B Frames",
          "Number of B frames between I and P reference frames", 0, 31, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}

guint8
gst_h265_encoder_get_level_idc (GstH265Encoder * encoder)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (encoder);
  return priv->level_idc;
}

/**
 * gst_h265_encoder_set_profile:
 * @encoder: a #GstH265Encoder
 * @profile: a #GstH265Profile
  *
 * Set the base class #GstH265Profile profile
 *
 * Since: 1.26
 */
void
gst_h265_encoder_set_profile (GstH265Encoder * encoder, GstH265Profile profile)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (encoder);
  priv->profile = profile;
}

/**
 * gst_h265_encoder_get_profile:
 * @encoder: a #GstH265Encoder
 *
 * Retrieve the base class #GstH265Profile profile
 *
 * Returns: (transfer full) : a #GstH265Profile
 *
 * Since: 1.26
 */
GstH265Profile
gst_h265_encoder_get_profile (GstH265Encoder * encoder)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (encoder);
  return priv->profile;
}

/**
 * gst_h265_encoder_get_input_state:
 * @encoder: a #GstVideoEncoder
 *
 * Get the current #GstVideoCodecState
 *
 * Returns: (transfer full) (nullable): #GstVideoCodecState describing format of video data.
 *
 * Since: 1.26
 */
GstVideoCodecState *
gst_h265_encoder_get_input_state (GstH265Encoder * encoder)
{
  GstVideoCodecState *state = NULL;
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (encoder);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  if (priv->input_state)
    state = gst_video_codec_state_ref (priv->input_state);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return state;
}

/**
 * gst_h265_encoder_get_next_gop_frame:
 * @encoder: a #GstH265Encoder
 * @index: an index in gop frames
 *
 * Get next #H265GOPFrame in the GOP strategy
 *
 * Returns: (transfer full): a #GOPFrame.
 *
 * Since: 1.26
 */
H265GOPFrame
gst_h265_encoder_next_gop_frame (GstH265Encoder * encoder)
{
  GstH265EncoderPrivate *priv = gst_h265_encoder_get_instance_private (encoder);
  if (priv->gop.cur_frame_index == priv->gop.idr_period) {
    priv->gop.cur_frame_index = 0;
    priv->gop.cur_frame_num = 0;
  }
  H265GOPFrame frame = g_array_index (priv->gop.frame_types, H265GOPFrame,
      priv->gop.cur_frame_index);
  frame.poc = priv->gop.cur_frame_index % priv->gop.max_pic_order_cnt;
  frame.index = priv->gop.cur_frame_index;
  priv->gop.cur_frame_index++;
  return frame;
}

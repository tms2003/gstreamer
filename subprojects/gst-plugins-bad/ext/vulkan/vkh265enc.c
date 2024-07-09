/* GStreamer
 * Copyright (C) 2024 Igalia, S.L.
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

/**
 * SECTION:element-vkh265enc
 * @title: vkh265enc
 * @short_description: A Vulkan based H265 video encoder
 *
 * vkh265enc encodes raw video surfaces into H.265 bitstreams using
 * Vulkan video extensions.
 *
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vulkanupload ! vulkanh265enc ! h265parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.26
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkh265enc.h"

#include <gst/codecparsers/gsth265bitwriter.h>

#include "gstvulkanelements.h"

#include <gst/vulkan/gstvkdevice.h>
#include <gst/vulkan/gstvkencoder-private.h>

#include <math.h>

static GstStaticPadTemplate gst_vulkan_h265_encoder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, "NV12")));

static GstStaticPadTemplate gst_vulkan_h265_encoder_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "profile = { (string) main, (string) main-10, (string) main-still-picture } ,"
        "stream-format = { (string) byte-stream }, "
        "alignment = (string) au"));

typedef struct _GstVulkanH265EncoderFrame GstVulkanH265EncoderFrame;

#define DEFAULT_H265_MB_SIZE_ALIGNMENT 16
#define DEFAULT_H265_AVERAGE_BIRATE 0
#define DEFAULT_H265_MIN_QP 0
#define DEFAULT_H265_MAX_QP 51
#define DEFAULT_H265_CONSTANT_QP 26

#define MAX_H265_PROFILE_TIER_LEVEL_SIZE 684
#define MAX_H265_VPS_HDR_SIZE 13781
#define MAX_H265_SPS_HDR_SIZE 615
#define MAX_H265_SHORT_TERM_REFPICSET_SIZE 55
#define MAX_H265_VUI_PARAMS_SIZE 267
#define MAX_H265_HRD_PARAMS_SIZE 8196
#define MAX_H265_PPS_HDR_SIZE 274
#define MAX_H265_SLICE_HDR_SIZE 33660

#ifndef STD_VIDEO_H265_NO_REFERENCE_PICTURE
# define STD_VIDEO_H265_NO_REFERENCE_PICTURE 0xFF
#endif

enum
{
  PROP_0,
  PROP_RATE_CONTROL,
  PROP_ENCODE_USAGE,
  PROP_ENCODE_CONTENT,
  PROP_TUNING_MODE,
  PROP_QP_MIN,
  PROP_QP_MAX,
  PROP_AUD,
  PROP_AVERAGE_BITRATE,
  PROP_QUALITY_LEVEL,
  PROP_MAX
};

static GParamSpec *properties[PROP_MAX];

#define GST_TYPE_VULKAN_H265_RATE_CONTROL (gst_vulkan_h265_encoder_rate_control_get_type ())
static GType
gst_vulkan_h265_encoder_rate_control_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR, "default", "default"},
      {VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR,
            "Rate control is disabled",
          "disabled"},
      {VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR,
            "Constant bitrate mode rate control mode",
          "cbr"},
      {VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR,
            "Variable bitrate mode rate control mode",
          "vbr"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVulkanH265EncRateControl", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H265_ENCODE_USAGE (gst_vulkan_h265_enc_usage_get_type ())
static GType
gst_vulkan_h265_enc_usage_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR, "default", "default"},
      {VK_VIDEO_ENCODE_USAGE_TRANSCODING_BIT_KHR, "Encode usage transcoding",
          "transcoding"},
      {VK_VIDEO_ENCODE_USAGE_STREAMING_BIT_KHR, "Encode usage streaming",
          "streaming"},
      {VK_VIDEO_ENCODE_USAGE_RECORDING_BIT_KHR, "Encode usage recording",
          "recording"},
      {VK_VIDEO_ENCODE_USAGE_CONFERENCING_BIT_KHR, "Encode usage conferencing",
          "conferencing"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVulkanH265EncUsage", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H265_ENCODE_CONTENT (gst_vulkan_h265_enc_content_get_type ())
static GType
gst_vulkan_h265_enc_content_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR, "default", "default"},
      {VK_VIDEO_ENCODE_CONTENT_CAMERA_BIT_KHR, "Encode content camera",
          "camera"},
      {VK_VIDEO_ENCODE_CONTENT_DESKTOP_BIT_KHR, "Encode content desktop",
          "desktop"},
      {VK_VIDEO_ENCODE_CONTENT_RENDERED_BIT_KHR, "Encode content rendered",
          "rendered"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVulkanH265EncContent", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H265_ENCODE_TUNING_MODE (gst_vulkan_h265_enc_tuning_mode_get_type ())
static GType
gst_vulkan_h265_enc_tuning_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR, "default", "default"},
      {VK_VIDEO_ENCODE_TUNING_MODE_HIGH_QUALITY_KHR, "Tuning mode high quality",
          "high-quality"},
      {VK_VIDEO_ENCODE_TUNING_MODE_LOW_LATENCY_KHR, "Tuning mode low latency",
          "low-latency"},
      {VK_VIDEO_ENCODE_TUNING_MODE_ULTRA_LOW_LATENCY_KHR,
          "Tuning mode ultra low latency", "ultra-low-latency"},
      {VK_VIDEO_ENCODE_TUNING_MODE_LOSSLESS_KHR, "Tuning mode lossless",
          "lossless"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVulkanH265EncTuningMode", values);
  }
  return qtype;
}

typedef struct _VkH265Params
{
  StdVideoH265VideoParameterSet vps;
  StdVideoH265SequenceParameterSet sps;
  StdVideoH265PictureParameterSet pps;

  StdVideoH265SequenceParameterSetVui vui;
  StdVideoH265DecPicBufMgr pic_buf_mgr;
  StdVideoH265ProfileTierLevel profile_tier_level;
} VkH265Params;

struct _GstVulkanH265Encoder
{
  /*< private > */
  GstH265Encoder parent;

  gint width;
  gint height;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *encode_queue;
  GstVulkanEncoder *encoder;

  GstVulkanVideoProfile profile;
  VkVideoEncodeH265CapabilitiesKHR caps;

  VkH265Params session_params;

  /* H265 fields */
  guint8 level_idc;

  struct
  {
    guint32 rate_ctrl;
    guint video_usage_hints;
    guint video_content_hints;
    guint tuning_mode;
    guint32 min_qp;
    guint32 max_qp;
    gboolean aud;
    guint32 quality_level;
    guint32 average_bitrate;
  } prop;
};

struct _GstVulkanH265EncoderFrame
{
  GstVulkanEncodePicture *picture;

  StdVideoEncodeH265WeightTable slice_wt;
  StdVideoEncodeH265SliceSegmentHeader slice_hdr;
  VkVideoEncodeH265NaluSliceSegmentInfoKHR slice_info;
  VkVideoEncodeH265RateControlInfoKHR rc_info;
  VkVideoEncodeH265RateControlLayerInfoKHR rc_layer_info;
  VkVideoEncodeH265PictureInfoKHR enc_pic_info;
  VkVideoEncodeH265DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeH265QualityLevelPropertiesKHR quality_level;

  StdVideoEncodeH265PictureInfo pic_info;
  StdVideoEncodeH265ReferenceInfo ref_info;
  StdVideoEncodeH265ReferenceListsInfo ref_list_info;
  StdVideoH265ShortTermRefPicSet short_term_ref_pic_set;
};

GST_DEBUG_CATEGORY_STATIC (gst_vulkan_h265_encoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_h265_encoder_debug

#define gst_vulkan_h265_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanH265Encoder, gst_vulkan_h265_encoder,
    GST_TYPE_H265_ENCODER,
    GST_DEBUG_CATEGORY_INIT (gst_vulkan_h265_encoder_debug, "vulkanh265enc", 0,
        "Vulkan H.265 enccoder"));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vulkanh265enc, "vulkanh265enc",
    GST_RANK_NONE, GST_TYPE_VULKAN_H265_ENCODER, vulkan_element_init (plugin));

static GstVulkanH265EncoderFrame *
gst_vulkan_h265_encoder_frame_new (void)
{
  GstVulkanH265EncoderFrame *frame;

  frame = g_new (GstVulkanH265EncoderFrame, 1);
  frame->picture = NULL;

  return frame;
}

static void
gst_vulkan_h265_encoder_frame_free (gpointer pframe)
{
  GstVulkanH265EncoderFrame *frame = pframe;
  g_clear_pointer (&frame->picture, gst_vulkan_encode_picture_free);
  g_free (frame);
}

static inline GstVulkanH265EncoderFrame *
_vk_enc_frame (GstH265EncodeFrame * frame)
{
  GstVulkanH265EncoderFrame *enc_frame =
      gst_h265_encode_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}

static StdVideoH265ChromaFormatIdc
gst_vulkan_h265_chroma_from_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return STD_VIDEO_H265_CHROMA_FORMAT_IDC_MONOCHROME;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return STD_VIDEO_H265_CHROMA_FORMAT_IDC_420;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_NV16_10LE32:
      return STD_VIDEO_H265_CHROMA_FORMAT_IDC_422;
    default:
      return STD_VIDEO_H265_CHROMA_FORMAT_IDC_INVALID;
  }

  return STD_VIDEO_H265_CHROMA_FORMAT_IDC_INVALID;
}

static StdVideoH265SliceType
gst_vulkan_h265_slice_type (GstH265SliceType type)
{
  switch (type) {
    case GST_H265_I_SLICE:
      return STD_VIDEO_H265_SLICE_TYPE_I;
    case GST_H265_P_SLICE:
      return STD_VIDEO_H265_SLICE_TYPE_P;
    case GST_H265_B_SLICE:
      return STD_VIDEO_H265_SLICE_TYPE_B;
    default:
      GST_WARNING ("Unsupported slice type '%d'", type);
      return STD_VIDEO_H265_SLICE_TYPE_INVALID;
  }
}

static StdVideoH265PictureType
gst_vulkan_h265_picture_type (GstH265SliceType type, gboolean key_type)
{
  switch (type) {
    case GST_H265_I_SLICE:
      if (key_type)
        return STD_VIDEO_H265_PICTURE_TYPE_IDR;
      else
        return STD_VIDEO_H265_PICTURE_TYPE_I;
    case GST_H265_P_SLICE:
      return STD_VIDEO_H265_PICTURE_TYPE_P;
    case GST_H265_B_SLICE:
      return STD_VIDEO_H265_PICTURE_TYPE_B;
    default:
      GST_WARNING ("Unsupported slice type '%d'", type);
      return STD_VIDEO_H265_PICTURE_TYPE_INVALID;
  }
}

static StdVideoH265ProfileIdc
gst_vulkan_h265_profile_type (GstH265Profile profile)
{
  switch (profile) {
    case GST_H265_PROFILE_IDC_MAIN:
      return STD_VIDEO_H265_PROFILE_IDC_MAIN;
    case GST_H265_PROFILE_IDC_MAIN_10:
      return STD_VIDEO_H265_PROFILE_IDC_MAIN_10;
    case GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE:
      return STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE;
    default:
      GST_WARNING ("Unsupported profile type '%d'", profile);
      return STD_VIDEO_H265_PROFILE_IDC_INVALID;
  }
}

static StdVideoH265LevelIdc
gst_vulkan_h265_level_idc (int level_idc)
{
  switch (level_idc) {
    case GST_H265_LEVEL_L1:
      return STD_VIDEO_H265_LEVEL_IDC_1_0;
    case GST_H265_LEVEL_L2:
      return STD_VIDEO_H265_LEVEL_IDC_2_0;
    case GST_H265_LEVEL_L3:
      return STD_VIDEO_H265_LEVEL_IDC_3_0;
    case GST_H265_LEVEL_L3_1:
      return STD_VIDEO_H265_LEVEL_IDC_3_1;
    case GST_H265_LEVEL_L4:
      return STD_VIDEO_H265_LEVEL_IDC_4_0;
    case GST_H265_LEVEL_L4_1:
      return STD_VIDEO_H265_LEVEL_IDC_4_1;
    case GST_H265_LEVEL_L5:
      return STD_VIDEO_H265_LEVEL_IDC_5_0;
    case GST_H265_LEVEL_L5_1:
      return STD_VIDEO_H265_LEVEL_IDC_5_1;
    case GST_H265_LEVEL_L5_2:
      return STD_VIDEO_H265_LEVEL_IDC_5_2;
    case GST_H265_LEVEL_L6:
      return STD_VIDEO_H265_LEVEL_IDC_6_0;
    case GST_H265_LEVEL_L6_1:
      return STD_VIDEO_H265_LEVEL_IDC_6_1;
    case GST_H265_LEVEL_L6_2:
      return STD_VIDEO_H265_LEVEL_IDC_6_2;
    default:
      GST_WARNING ("Unsupported level IDC '%d'", level_idc);
      return STD_VIDEO_H265_LEVEL_IDC_INVALID;
  }
}

static void
gst_vulkan_h265_encoder_init_std_vps (GstVulkanH265Encoder * self, gint vps_id)
{
  self->session_params.profile_tier_level = (StdVideoH265ProfileTierLevel) {
    /* *INDENT-OFF* */
    .flags = {
      .general_tier_flag = 0u,
      .general_progressive_source_flag = 1u,
      .general_interlaced_source_flag = 0u,
      .general_non_packed_constraint_flag = 0u,
      .general_frame_only_constraint_flag = 1u,
    },
    .general_profile_idc =  self->profile.codec.h265enc.stdProfileIdc,
    .general_level_idc = gst_vulkan_h265_level_idc (self->level_idc),
    /* *INDENT-ON* */
  };

  self->session_params.pic_buf_mgr = (StdVideoH265DecPicBufMgr) {
    /* *INDENT-OFF* */
    .max_dec_pic_buffering_minus1[0] = 4,
    .max_latency_increase_plus1[0] = 5,
    .max_num_reorder_pics[0] = 2,
    /* *INDENT-ON* */
  };

  self->session_params.vps = (StdVideoH265VideoParameterSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265VpsFlags) {
      .vps_poc_proportional_to_timing_flag = 0u,
      .vps_sub_layer_ordering_info_present_flag = 1u,
      .vps_temporal_id_nesting_flag = 0u,
      .vps_timing_info_present_flag = 0u,
    },
    .vps_video_parameter_set_id = vps_id,
    .vps_max_sub_layers_minus1 = 0,
    .vps_num_units_in_tick = 0,
    .vps_time_scale = 0,
    .vps_num_ticks_poc_diff_one_minus1 = 0u,
    .pDecPicBufMgr = &self->session_params.pic_buf_mgr,
    .pHrdParameters = NULL,
    .pProfileTierLevel = &self->session_params.profile_tier_level,
    /* *INDENT-ON* */
  };
}

static void
gst_vulkan_h265_encoder_init_std_sps (GstVulkanH265Encoder * self,
    GstVulkanVideoCapabilities * enc_caps, guint vps_id, guint sps_id)
{
  GstH265Encoder *base = GST_H265_ENCODER (self);
  VkVideoChromaSubsamplingFlagBitsKHR chroma_format;
  VkVideoComponentBitDepthFlagsKHR bit_depth_luma, bit_depth_chroma;
  gint min_ctb_size = 64, max_ctb_size = 16;
  gint max_tb_size = 0, min_tb_size = 0;
  gint max_transform_hierarchy;
  GstVideoCodecState *input_state = gst_h265_encoder_get_input_state (base);

  if (enc_caps->codec.
      h265enc.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR) {
    max_ctb_size = 64;
  } else if (enc_caps->codec.
      h265enc.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR) {
    max_ctb_size = 32;
  }

  if (enc_caps->codec.
      h265enc.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_16_BIT_KHR) {
    min_ctb_size = 16;
  } else if (enc_caps->codec.
      h265enc.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR) {
    min_ctb_size = 32;
  }

  if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
    min_tb_size = 4;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    min_tb_size = 8;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    min_tb_size = 16;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    min_tb_size = 32;

  if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    max_tb_size = 32;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    max_tb_size = 16;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    max_tb_size = 8;
  else if (enc_caps->codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
    max_tb_size = 4;

  max_transform_hierarchy =
      gst_util_ceil_log2 (max_ctb_size) - gst_util_ceil_log2 (min_tb_size);

  const uint32_t mbAlignedWidth = GST_ROUND_UP_N (self->width, min_ctb_size);
  const uint32_t mbAlignedHeight = GST_ROUND_UP_N (self->height, min_ctb_size);

  // Set the VUI parameters
  self->session_params.vui = (StdVideoH265SequenceParameterSetVui) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265SpsVuiFlags) {
      .aspect_ratio_info_present_flag = 0u,
      .overscan_info_present_flag = 0u,
      .overscan_appropriate_flag = 0u,
      .video_signal_type_present_flag = 1u,
      .video_full_range_flag = 0u,
      .colour_description_present_flag = 0u,
      .chroma_loc_info_present_flag = 0u,
      .neutral_chroma_indication_flag = 0u,
      .field_seq_flag = 0u,
      .frame_field_info_present_flag = 0u,
      .default_display_window_flag = 0u,
      .vui_timing_info_present_flag = 1u,
      .vui_poc_proportional_to_timing_flag = 0u,
      .vui_hrd_parameters_present_flag = 0u,
      .bitstream_restriction_flag = 0u,
      .tiles_fixed_structure_flag = 0u,
      .motion_vectors_over_pic_boundaries_flag = 0u,
      .restricted_ref_pic_lists_flag = 0u,
    },
    .aspect_ratio_idc = STD_VIDEO_H265_ASPECT_RATIO_IDC_UNSPECIFIED,
    .sar_width = GST_VIDEO_INFO_PAR_N (&input_state->info),
    .sar_height = GST_VIDEO_INFO_PAR_D (&input_state->info),
    .video_format = 1,     // PAL Table E.2
    .colour_primaries = 0,
    .transfer_characteristics = 0,
    .matrix_coeffs = 0,
    .chroma_sample_loc_type_top_field = 0,
    .chroma_sample_loc_type_bottom_field = 0,
    .reserved1 = 0,
    .reserved2 = 0,
    .def_disp_win_left_offset = 0,
    .def_disp_win_right_offset = 0,
    .def_disp_win_top_offset = 0,
    .def_disp_win_bottom_offset = 0,
    .vui_num_units_in_tick =   GST_VIDEO_INFO_FPS_N (&input_state->info) ? GST_VIDEO_INFO_FPS_D (&input_state->info) : 0,
    .vui_time_scale = GST_VIDEO_INFO_FPS_N (&input_state->info) * 2,
    .vui_num_ticks_poc_diff_one_minus1 = 0u,
    .min_spatial_segmentation_idc = 0,
    .reserved3 = 0,
    .max_bytes_per_pic_denom = 0,
    .max_bits_per_min_cu_denom = 0,
    .log2_max_mv_length_horizontal = 0,
    .log2_max_mv_length_vertical = 0,
    .pHrdParameters = NULL,
    /* *INDENT-ON* */
  };

  gst_vulkan_video_get_chroma_info_from_format (input_state->info.finfo->format,
      &chroma_format, &bit_depth_luma, &bit_depth_chroma);

  self->session_params.sps = (StdVideoH265SequenceParameterSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265SpsFlags) {
      .sps_temporal_id_nesting_flag = 0u,
      .separate_colour_plane_flag = 0u,
      .conformance_window_flag = 1u,
      .sps_sub_layer_ordering_info_present_flag = 1u,
      .scaling_list_enabled_flag = 0u,
      .sps_scaling_list_data_present_flag = 0u,
      .amp_enabled_flag = 0u,
      .sample_adaptive_offset_enabled_flag = 1u,
      .pcm_enabled_flag = 0u,
      .pcm_loop_filter_disabled_flag = 0u,
      .long_term_ref_pics_present_flag = 0u,
      .sps_temporal_mvp_enabled_flag = 1u,
      .strong_intra_smoothing_enabled_flag = 1u,
      .vui_parameters_present_flag = 1u,
      .sps_extension_present_flag = 0u,
      .sps_range_extension_flag = 0u,
      .transform_skip_rotation_enabled_flag = 0u,
      .transform_skip_context_enabled_flag = 0u,
      .implicit_rdpcm_enabled_flag = 0u,
      .explicit_rdpcm_enabled_flag = 0u,
      .extended_precision_processing_flag = 0u,
      .intra_smoothing_disabled_flag = 0u,
      .high_precision_offsets_enabled_flag = 0u,
      .persistent_rice_adaptation_enabled_flag = 0u,
      .cabac_bypass_alignment_enabled_flag = 0u,
      .sps_scc_extension_flag = 0u,
      .sps_curr_pic_ref_enabled_flag = 0u,
      .palette_mode_enabled_flag = 0u,
      .sps_palette_predictor_initializers_present_flag = 0u,
      .intra_boundary_filtering_disabled_flag = 0u,
    },
    .chroma_format_idc =
        gst_vulkan_h265_chroma_from_format (input_state->info.
        finfo->format),
    .pic_width_in_luma_samples = mbAlignedWidth,
    .pic_height_in_luma_samples = mbAlignedHeight,
    .sps_video_parameter_set_id = vps_id,
    .sps_max_sub_layers_minus1 = 0,
    .sps_seq_parameter_set_id = sps_id,
    .bit_depth_luma_minus8 = 0,
    .bit_depth_chroma_minus8 = 0,
    // This allows for picture order count values in the range [0, 255].
    .log2_max_pic_order_cnt_lsb_minus4 = 4,
    .log2_min_luma_coding_block_size_minus3 = 0,
    .log2_diff_max_min_luma_coding_block_size = gst_util_ceil_log2 (max_ctb_size) - 3,
    .log2_min_luma_transform_block_size_minus2 = gst_util_ceil_log2 (min_tb_size) - 2,
    .log2_diff_max_min_luma_transform_block_size = gst_util_ceil_log2 (max_tb_size) - gst_util_ceil_log2 (min_tb_size),
    .max_transform_hierarchy_depth_inter = max_transform_hierarchy,
    .max_transform_hierarchy_depth_intra = max_transform_hierarchy,
    .num_short_term_ref_pic_sets = 0,
    .num_long_term_ref_pics_sps = 0,
    .pcm_sample_bit_depth_luma_minus1 = 0,
    .pcm_sample_bit_depth_chroma_minus1 = 0,
    .log2_min_pcm_luma_coding_block_size_minus3 = 0,
    .log2_diff_max_min_pcm_luma_coding_block_size = 0,
    .palette_max_size = 0,
    .delta_palette_max_predictor_size = 0,
    .motion_vector_resolution_control_idc = 0,
    .sps_num_palette_predictor_initializers_minus1 = 0,
    .conf_win_left_offset = 0u,
    .conf_win_right_offset = (mbAlignedWidth - self->width) / 2,
    .conf_win_top_offset = 0u,
    .conf_win_bottom_offset = (mbAlignedHeight - self->height) / 2,
    .pProfileTierLevel = &self->session_params.profile_tier_level,
    .pDecPicBufMgr = &self->session_params.pic_buf_mgr,
    .pScalingLists = NULL,
    .pShortTermRefPicSet = NULL,
    .pLongTermRefPicsSps = NULL,
    .pSequenceParameterSetVui = &self->session_params.vui,
    .pPredictorPaletteEntries = NULL,
    /* *INDENT-ON* */
  };
  gst_video_codec_state_unref (input_state);
}

static void
gst_vulkan_h265_encoder_init_std_pps (GstVulkanH265Encoder * self,
    GstVulkanVideoCapabilities * enc_caps, guint vps_id, guint sps_id,
    guint pps_id)
{

  self->session_params.pps = (StdVideoH265PictureParameterSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265PpsFlags) {
      .dependent_slice_segments_enabled_flag = 0u,
      .output_flag_present_flag = 0u,
      .sign_data_hiding_enabled_flag = 0u,
      .cabac_init_present_flag = 0u,
      .constrained_intra_pred_flag = 0u,
      .transform_skip_enabled_flag = enc_caps->codec.h265enc.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR ? 1 : 0,
      .cu_qp_delta_enabled_flag = 1u,
      .pps_slice_chroma_qp_offsets_present_flag = 0u,
      .weighted_pred_flag = enc_caps->codec.h265enc.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ? 1 : 0,
      .weighted_bipred_flag = 0u,
      .transquant_bypass_enabled_flag = 0u,
      .tiles_enabled_flag = 0u,
      .entropy_coding_sync_enabled_flag = (enc_caps->codec.h265enc.maxTiles.width > 1 || enc_caps->codec.h265enc.maxTiles.height > 1) ? 1 : 0,
      .uniform_spacing_flag = 0u,
      .loop_filter_across_tiles_enabled_flag = 0u,
      .pps_loop_filter_across_slices_enabled_flag = 1u,
      .deblocking_filter_control_present_flag = 0u,
      .deblocking_filter_override_enabled_flag = 0u,
      .pps_deblocking_filter_disabled_flag = 0u,
      .pps_scaling_list_data_present_flag = 0u,
      .lists_modification_present_flag = 0u,
      .slice_segment_header_extension_present_flag = 0u,
      .pps_extension_present_flag = 0u,
      .cross_component_prediction_enabled_flag = 0u,
      .chroma_qp_offset_list_enabled_flag = 0u,
      .pps_curr_pic_ref_enabled_flag = 0u,
      .residual_adaptive_colour_transform_enabled_flag = 0u,
      .pps_slice_act_qp_offsets_present_flag = 0u,
      .pps_palette_predictor_initializers_present_flag = 0u,
      .monochrome_palette_flag = 0u,
      .pps_range_extension_flag = 0u,
    },
    .sps_video_parameter_set_id = vps_id,
    .pps_seq_parameter_set_id = sps_id,
    .pps_pic_parameter_set_id = pps_id,
    .num_extra_slice_header_bits = 0,
    .num_ref_idx_l0_default_active_minus1 = 0,
    .num_ref_idx_l1_default_active_minus1 = 0,
    .init_qp_minus26 = 0,
    .diff_cu_qp_delta_depth = 1,
    .pps_cb_qp_offset = 0,
    .pps_cr_qp_offset = 0,
    .pps_beta_offset_div2 = 0,
    .pps_tc_offset_div2 = 0,
    .log2_parallel_merge_level_minus2 = 0,
    .log2_max_transform_skip_block_size_minus2 = 0,
    .diff_cu_chroma_qp_offset_depth = 0,
    .chroma_qp_offset_list_len_minus1 = 0,
    .cb_qp_offset_list ={0,},
    .cr_qp_offset_list = {0,},
    .log2_sao_offset_scale_luma = 0,
    .log2_sao_offset_scale_chroma = 0,
    .pps_act_y_qp_offset_plus5 = 0,
    .pps_act_cb_qp_offset_plus5 = 0,
    .pps_act_cr_qp_offset_plus3 = 0,
    .pps_num_palette_predictor_initializers = 0,
    .luma_bit_depth_entry_minus8 = 0,
    .chroma_bit_depth_entry_minus8 = 0,
    .num_tile_columns_minus1 = 0,
    .num_tile_rows_minus1 = 0,
    .reserved1 = 0,
    .reserved2 = 0,
    .column_width_minus1 = {0, },
    .row_height_minus1 = {0, },
    .pScalingLists = NULL,
    .pPredictorPaletteEntries = NULL,
    /* *INDENT-ON* */
  };
}

static gboolean
gst_vulkan_h265_encoder_get_session_params (GstVulkanH265Encoder * self,
    gint vps_id, gint sps_id, gint pps_id, void **packed_params, gsize * size)
{
  GError *err = NULL;
  GstVulkanEncoderParametersFeedback feedback = { 0, };
  GstVulkanEncoderParametersOverrides override_params = {
    .h265 = {
          .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
          .writeStdVPS = (vps_id >= 0),
          .writeStdSPS = (sps_id >= 0),
          .writeStdPPS = (pps_id >= 0),
          .stdVPSId = vps_id,
          .stdSPSId = sps_id,
          .stdPPSId = pps_id,
        }
  };

  gst_vulkan_encoder_video_session_parameters_overrides (self->encoder,
      &override_params, &feedback, size, (gpointer *) packed_params, &err);

  return (*size != 0);
}

static void
gst_vulkan_h265_encoder_reset (GstVulkanH265Encoder * self)
{
  if (!self->encoder) {
    GST_WARNING_OBJECT (self,
        "The encoder object has not been initialized correctly.");
    return;
  }

  g_object_set (self->encoder, "rate-control", self->prop.rate_ctrl,
      "average-bitrate", self->prop.average_bitrate,
      "quality-level", self->prop.quality_level, NULL);
}

static guint
_calculate_output_buffer_size (GstVulkanH265Encoder * self)
{
  guint codedbuf_size = 0;

  codedbuf_size = (self->width * self->height);

  /* Calculate the maximum sizes for common headers (in bits) */

  /* Account for VPS header */
  codedbuf_size += 4 /* start code */  + GST_ROUND_UP_8 (MAX_H265_VPS_HDR_SIZE +
      MAX_H265_PROFILE_TIER_LEVEL_SIZE + MAX_H265_HRD_PARAMS_SIZE) / 8;

  /* Account for SPS header */
  codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_H265_SPS_HDR_SIZE +
      MAX_H265_PROFILE_TIER_LEVEL_SIZE +
      64 * MAX_H265_SHORT_TERM_REFPICSET_SIZE + MAX_H265_VUI_PARAMS_SIZE +
      MAX_H265_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_H265_PPS_HDR_SIZE) / 8;

  GST_DEBUG_OBJECT (self, "Calculate codedbuf size: %u", codedbuf_size);
  return codedbuf_size;
}

static gboolean
gst_vulkan_h265_encoder_init_session (GstVulkanH265Encoder * self)
{
  GstH265Encoder *base = GST_H265_ENCODER (self);
  GError *err = NULL;
  GstVulkanEncoderParameters enc_params;
  GstVulkanVideoCapabilities enc_caps;
  VkVideoEncodeH265SessionParametersAddInfoKHR params_add;
  VkVideoChromaSubsamplingFlagBitsKHR chroma_format;
  VkVideoComponentBitDepthFlagsKHR bit_depth_luma, bit_depth_chroma;
  guint32 output_buffer_size = _calculate_output_buffer_size (self);
  GstVideoCodecState *input_state, *output_state;
  const gchar *profile;

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return FALSE;
  }

  input_state = gst_h265_encoder_get_input_state (base);
  output_state = gst_video_encoder_get_output_state (GST_VIDEO_ENCODER (self));
  profile =
      gst_structure_get_string (gst_caps_get_structure (output_state->caps, 0),
      "profile");

  if (!gst_vulkan_video_get_chroma_info_from_format (input_state->info.finfo->
          format, &chroma_format, &bit_depth_luma, &bit_depth_chroma)) {
    GST_WARNING_OBJECT (self,
        "unable to retrieve chroma info from input format");
    return FALSE;
  }

  gst_video_codec_state_unref (input_state);
  gst_video_codec_state_unref (output_state);

  gst_h265_encoder_set_profile (base, gst_h265_profile_from_string (profile));

  self->profile = (GstVulkanVideoProfile) {
    /* *INDENT-OFF* */
    .profile = (VkVideoProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &self->profile.usage.encode,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
      .chromaSubsampling = chroma_format,
      .chromaBitDepth = bit_depth_luma,
      .lumaBitDepth = bit_depth_chroma,
      },
      .usage.encode = (VkVideoEncodeUsageInfoKHR) {
        .pNext =  &self->profile.codec.h265enc,
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
        .videoUsageHints = self->prop.video_usage_hints,
        .videoContentHints = self->prop.video_content_hints,
        .tuningMode = self->prop.tuning_mode,
      },
      .codec.h265enc = (VkVideoEncodeH265ProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
        .pNext = NULL,
        .stdProfileIdc = gst_vulkan_h265_profile_type (gst_h265_encoder_get_profile (base)),
      }
    /* *INDENT-ON* */
  };

  if (!gst_vulkan_encoder_start (self->encoder, &self->profile,
          output_buffer_size, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to start vulkan encoder with error %s", err->message), (NULL));
    return FALSE;
  }

  self->level_idc = gst_h265_encoder_get_level_idc (base);

  if (!gst_vulkan_encoder_caps (self->encoder, &enc_caps))
    return FALSE;

  gst_vulkan_h265_encoder_init_std_vps (self, 0);
  gst_vulkan_h265_encoder_init_std_sps (self, &enc_caps, 0, 0);
  gst_vulkan_h265_encoder_init_std_pps (self, &enc_caps, 0, 0, 0);

  params_add = (VkVideoEncodeH265SessionParametersAddInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdVPSs = &self->session_params.vps,
    .stdVPSCount = 1,
    .pStdSPSs = &self->session_params.sps,
    .stdSPSCount = 1,
    .pStdPPSs = &self->session_params.pps,
    .stdPPSCount = 1,
    /* *INDENT-ON* */
  };
  enc_params.h265 = (VkVideoEncodeH265SessionParametersCreateInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdVPSCount = 1,
    .maxStdSPSCount = 1,
    .maxStdPPSCount = 1,
    .pParametersAddInfo = &params_add,
    /* *INDENT-ON* */
  };

  if (!gst_vulkan_encoder_update_video_session_parameters (self->encoder,
          &enc_params, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to update session with error %s", err->message), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_close (GstVideoEncoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  gst_clear_object (&self->encoder);
  gst_clear_object (&self->encode_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_stop (GstVideoEncoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  gst_vulkan_encoder_stop (self->encoder);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static gboolean
_query_context (GstVulkanH265Encoder * self, GstQuery * query)
{
  if (!self->encoder)
    return FALSE;

  if (gst_vulkan_handle_context_query (GST_ELEMENT (self), query, NULL,
          self->instance, self->device))
    return TRUE;

  if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (self), query,
          self->encode_queue))
    return TRUE;

  return FALSE;
}

static gboolean
gst_vulkan_h265_encoder_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H265_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h265_encoder_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H265_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h265_encoder_set_format (GstH265Encoder * h265enc,
    GstVideoCodecState * state)
{
  GstVideoCodecState *output_state;
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (h265enc);
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
  GstCaps *outcaps;

  self->width = state->info.width;
  self->height = state->info.height;

  outcaps = gst_pad_get_pad_template_caps (encoder->srcpad);
  outcaps = gst_caps_fixate (outcaps);

  output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self), outcaps,
      state);
  gst_video_codec_state_unref (output_state);

  GST_INFO_OBJECT (self, "output caps: %" GST_PTR_FORMAT, state->caps);

  gst_vulkan_h265_encoder_reset (self);

  if (GST_VIDEO_ENCODER_CLASS (parent_class)->negotiate (encoder)) {
    return gst_vulkan_h265_encoder_init_session (self);
  }

  return FALSE;
}

static gboolean
_add_vulkan_params_header (GstVulkanH265Encoder * self,
    GstVulkanH265EncoderFrame * frame)
{
  void *header = NULL;
  gsize header_size = 0;

  gst_vulkan_h265_encoder_get_session_params (self, 0, 0, 0, &header,
      &header_size);
  GST_LOG_OBJECT (self, "Adding params header of size %lu", header_size);
  g_ptr_array_add (frame->picture->packed_headers,
      gst_buffer_new_wrapped (header, header_size));

  return TRUE;
}

static gboolean
_add_aud (GstVulkanH265Encoder * self, GstH265EncodeFrame * frame)
{
  guint8 *aud_data;
  guint size = 8;
  guint8 primary_pic_type = 0;
  GstVulkanH265EncoderFrame *vk_frame = _vk_enc_frame (frame);

  switch (frame->type) {
    case GST_H265_I_SLICE:
      primary_pic_type = 0;
      break;
    case GST_H265_P_SLICE:
      primary_pic_type = 1;
      break;
    case GST_H265_B_SLICE:
      primary_pic_type = 2;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  aud_data = g_malloc0 (size);
  if (gst_h265_bit_writer_aud (primary_pic_type, TRUE, aud_data,
          &size) != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the AUD");
    return FALSE;
  }
  g_ptr_array_add (vk_frame->picture->packed_headers,
      gst_buffer_new_wrapped (aud_data, size));

  return TRUE;
}

static GstFlowReturn
gst_vulkan_h265_encoder_encode_frame (GstH265Encoder * base,
    GstH265EncodeFrame * h265_frame, GstH265EncodeFrame ** list0,
    guint list0_num, GstH265EncodeFrame ** list1, guint list1_num)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (base);
  GstVulkanVideoCapabilities enc_caps;
  GstVulkanEncodePicture *ref_pics[16] = { NULL, };
  gint i, ref_pics_num = 0;
  gint16 delta_poc_s0_minus1 = 0, delta_poc_s1_minus1 = 0;
  GstVideoCodecState *input_state;
  GstVulkanH265EncoderFrame *vk_frame = _vk_enc_frame (h265_frame);

  if (!gst_vulkan_encoder_caps (self->encoder, &enc_caps))
    return FALSE;

  if (self->prop.aud && !_add_aud (self, h265_frame)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return GST_FLOW_ERROR;
  }

  vk_frame->picture->pic_order_cnt = h265_frame->poc;
  vk_frame->picture->pic_num = h265_frame->frame_num;

  /* Repeat the SPS for IDR. */
  if (h265_frame->poc == 0 && !_add_vulkan_params_header (self, vk_frame)) {
    return GST_FLOW_ERROR;
  }

  vk_frame->slice_wt = (StdVideoEncodeH265WeightTable) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265WeightTableFlags) {
        .luma_weight_l0_flag = 0,
        .chroma_weight_l0_flag = 0,
        .luma_weight_l1_flag = 0,
        .chroma_weight_l1_flag = 0,
    },
    .luma_log2_weight_denom = 0,
    .delta_chroma_log2_weight_denom = 0,
    .delta_luma_weight_l0 = { 0 },
    .luma_offset_l0 = { 0 },
    .delta_chroma_weight_l0 = { { 0 } },
    .delta_chroma_offset_l0 = { { 0 } },
    .delta_luma_weight_l1 = { 0 },
    .luma_offset_l1 = { 0 },
    .delta_chroma_weight_l1 = { { 0 } },
    .delta_chroma_offset_l1 = { { 0 } },
    /* *INDENT-ON* */
  };

  vk_frame->slice_hdr = (StdVideoEncodeH265SliceSegmentHeader) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265SliceSegmentHeaderFlags) {
        .first_slice_segment_in_pic_flag = 1,
        .dependent_slice_segment_flag = 0,
        .slice_sao_luma_flag = 1,
        .slice_sao_chroma_flag = 1,
        .num_ref_idx_active_override_flag = 0,
        .mvd_l1_zero_flag = 0,
        .cabac_init_flag = 0,
        .cu_chroma_qp_offset_enabled_flag = 1,
        .deblocking_filter_override_flag = 1,
        .slice_deblocking_filter_disabled_flag = 0,
        .collocated_from_l0_flag = 0,
        .slice_loop_filter_across_slices_enabled_flag = 0,
    },
      .slice_type = gst_vulkan_h265_slice_type (h265_frame->type),
      .slice_segment_address = 0,
      .collocated_ref_idx = 0,
      .MaxNumMergeCand = 5,
      .slice_cb_qp_offset = 0,
      .slice_cr_qp_offset = 0,
      .slice_beta_offset_div2 = 0,
      .slice_tc_offset_div2 = 0,
      .slice_act_y_qp_offset = 0,
      .slice_act_cb_qp_offset = 0,
      .slice_act_cr_qp_offset = 0,
      .slice_qp_delta = 0,
      .pWeightTable = NULL,//&frame->slice_wt,
    /* *INDENT-ON* */
  };

  vk_frame->slice_info = (VkVideoEncodeH265NaluSliceSegmentInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR,
    .pNext = NULL,
    .pStdSliceSegmentHeader = &vk_frame->slice_hdr,
    .constantQp = DEFAULT_H265_CONSTANT_QP,
    /* *INDENT-ON* */
  };
  if (self->prop.rate_ctrl !=
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    vk_frame->slice_info.constantQp = 0;
  }

  if (list0_num) {
    delta_poc_s0_minus1 = vk_frame->picture->pic_order_cnt - list0[0]->poc - 1;
  }
  if (list1_num) {
    delta_poc_s1_minus1 = list1[0]->poc - vk_frame->picture->pic_order_cnt - 1;
  }

  vk_frame->short_term_ref_pic_set = (StdVideoH265ShortTermRefPicSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265ShortTermRefPicSetFlags) {
      .inter_ref_pic_set_prediction_flag = 0u,
      .delta_rps_sign = 0u,
    },
    .delta_idx_minus1 = 0,
    .use_delta_flag = 0,
    .abs_delta_rps_minus1 = 0,
    .used_by_curr_pic_flag = 0,
    .used_by_curr_pic_s0_flag  = list0_num ? 1 : 0,
    .used_by_curr_pic_s1_flag = list1_num ? 1 : 0,
    .num_negative_pics = list0_num,
    .num_positive_pics = list1_num,
    .delta_poc_s0_minus1 = {delta_poc_s0_minus1, 0},
    .delta_poc_s1_minus1 = {delta_poc_s1_minus1, 0},
    /* *INDENT-ON* */
  };

  vk_frame->pic_info = (StdVideoEncodeH265PictureInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265PictureInfoFlags) {
        .is_reference = h265_frame->is_ref,
        .IrapPicFlag = (gst_vulkan_h265_picture_type(h265_frame->type, h265_frame->is_ref) == STD_VIDEO_H265_PICTURE_TYPE_IDR),
        .used_for_long_term_reference = 0,
        .discardable_flag = 0,
        .cross_layer_bla_flag = 0,
        .pic_output_flag = (gst_vulkan_h265_picture_type(h265_frame->type, h265_frame->is_ref) == STD_VIDEO_H265_PICTURE_TYPE_IDR),
        //Can bother resolution change use case, to be set to 0
        .no_output_of_prior_pics_flag = 0,
        .short_term_ref_pic_set_sps_flag = 0,
        .slice_temporal_mvp_enabled_flag = 0,
    },
    .pic_type = gst_vulkan_h265_picture_type(h265_frame->type, h265_frame->is_ref),
    .sps_video_parameter_set_id = self->session_params.sps.sps_video_parameter_set_id,
    .pps_seq_parameter_set_id = self->session_params.sps.sps_seq_parameter_set_id,
    .pps_pic_parameter_set_id =  self->session_params.pps.pps_pic_parameter_set_id,
    .PicOrderCntVal = h265_frame->poc,
    .TemporalId = 0,
    .pRefLists = NULL,
    .pShortTermRefPicSet = &vk_frame->short_term_ref_pic_set,
    .pLongTermRefPics = NULL,
    /* *INDENT-ON* */
  };

  vk_frame->ref_list_info = (StdVideoEncodeH265ReferenceListsInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265ReferenceListsInfoFlags) {
      .ref_pic_list_modification_flag_l0 = 0,
      .ref_pic_list_modification_flag_l1 = 0,
    },
    .num_ref_idx_l0_active_minus1 = 0,
    .num_ref_idx_l1_active_minus1 = 0,
    .RefPicList0 = {0, STD_VIDEO_H265_NO_REFERENCE_PICTURE, },
    .RefPicList1 = {0,STD_VIDEO_H265_NO_REFERENCE_PICTURE, },
    .list_entry_l0 = {0, },
    .list_entry_l1 = {0, },
    /* *INDENT-ON* */
  };
  vk_frame->pic_info.pRefLists = &vk_frame->ref_list_info;

  memset (vk_frame->ref_list_info.RefPicList0,
      STD_VIDEO_H265_NO_REFERENCE_PICTURE, STD_VIDEO_H265_MAX_NUM_LIST_REF);
  memset (vk_frame->ref_list_info.RefPicList1,
      STD_VIDEO_H265_NO_REFERENCE_PICTURE, STD_VIDEO_H265_MAX_NUM_LIST_REF);

  vk_frame->rc_layer_info = (VkVideoEncodeH265RateControlLayerInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_KHR,
    .pNext = NULL,
    .useMinQp = TRUE,
    .minQp = (VkVideoEncodeH265QpKHR){ self->prop.min_qp, self->prop.min_qp, self->prop.min_qp },
    .useMaxQp = TRUE,
    .maxQp = (VkVideoEncodeH265QpKHR){ self->prop.max_qp, self->prop.max_qp, self->prop.max_qp },
    .useMaxFrameSize = 0,
    .maxFrameSize = (VkVideoEncodeH265FrameSizeKHR) {0, 0, 0},
    /* *INDENT-ON* */
  };

  vk_frame->rc_info = (VkVideoEncodeH265RateControlInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR,
    .pNext = NULL,
    .gopFrameCount = 0,
    .idrPeriod = 0,
    .consecutiveBFrameCount = 0,
    /* *INDENT-ON* */
  };

  vk_frame->quality_level = (VkVideoEncodeH265QualityLevelPropertiesKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR,
    .pNext = NULL,
    .preferredRateControlFlags = VK_VIDEO_ENCODE_H265_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .preferredGopFrameCount = 0,
    .preferredIdrPeriod = 0,
    .preferredConsecutiveBFrameCount = 0,
    .preferredConstantQp = (VkVideoEncodeH265QpKHR){ DEFAULT_H265_CONSTANT_QP, DEFAULT_H265_CONSTANT_QP, DEFAULT_H265_CONSTANT_QP},
    .preferredMaxL0ReferenceCount = 0,
    .preferredMaxL1ReferenceCount = 0,
    /* *INDENT-ON* */
  };

  vk_frame->enc_pic_info = (VkVideoEncodeH265PictureInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR,
    .pNext = NULL,
    .naluSliceSegmentEntryCount = 1,
    .pNaluSliceSegmentEntries = &vk_frame->slice_info,
    .pStdPictureInfo = &vk_frame->pic_info,
    /* *INDENT-ON* */
  };
  vk_frame->ref_info = (StdVideoEncodeH265ReferenceInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265ReferenceInfoFlags) {
      .used_for_long_term_reference =0,
    },
    .pic_type = gst_vulkan_h265_picture_type(h265_frame->type, h265_frame->is_ref),
    .PicOrderCntVal = h265_frame->poc, //display order
    .TemporalId = 0,
    /* *INDENT-ON* */
  };

  vk_frame->dpb_slot_info = (VkVideoEncodeH265DpbSlotInfoKHR) {
     /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR,
    .pNext = NULL,
    .pStdReferenceInfo = &vk_frame->ref_info,
     /* *INDENT-ON* */
  };

  input_state = gst_h265_encoder_get_input_state (base);;
  vk_frame->picture->codec_pic_info = &vk_frame->enc_pic_info;
  vk_frame->picture->codec_rc_info = &vk_frame->rc_info;
  vk_frame->picture->codec_rc_layer_info = &vk_frame->rc_layer_info;
  vk_frame->picture->codec_dpb_slot_info = &vk_frame->dpb_slot_info;
  vk_frame->picture->codec_quality_level = &vk_frame->quality_level;
  vk_frame->picture->fps_n = GST_VIDEO_INFO_FPS_N (&input_state->info);
  vk_frame->picture->fps_d = GST_VIDEO_INFO_FPS_D (&input_state->info);
  gst_video_codec_state_unref (input_state);

  for (i = 0; i < list0_num; i++) {
    GstVulkanH265EncoderFrame *ref_frame = _vk_enc_frame (list0[i]);
    ref_pics[i] = ref_frame->picture;
    vk_frame->ref_list_info.RefPicList0[i] = ref_frame->picture->slotIndex;
    ref_pics_num++;
  }

  for (i = 0; i < list1_num; i++) {
    GstVulkanH265EncoderFrame *ref_frame = _vk_enc_frame (list1[i]);
    ref_pics[i + list0_num] = ref_frame->picture;
    vk_frame->ref_list_info.RefPicList1[i] = ref_frame->picture->slotIndex;
    ref_pics_num++;
  }

  vk_frame->picture->nb_refs = ref_pics_num;

  if (!gst_vulkan_encoder_encode (self->encoder, vk_frame->picture, ref_pics)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static gboolean
gst_vulkan_h265_encoder_flush (GstVideoEncoder * venc)
{
  /* begin from an IDR after flush. */
  gst_h265_encoder_reset (venc, TRUE);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (venc);
}

static void
gst_vulkan_h265_encoder_prepare_output (GstH265Encoder * base,
    GstVideoCodecFrame * frame)
{
  GstH265EncodeFrame *h265_frame;
  GstVulkanH265EncoderFrame *vk_frame;
  GstMapInfo info;

  h265_frame = gst_video_codec_frame_get_user_data (frame);
  vk_frame = _vk_enc_frame (h265_frame);

  frame->output_buffer = gst_buffer_ref (vk_frame->picture->out_buffer);

  gst_buffer_map (frame->output_buffer, &info, GST_MAP_READ);
  GST_MEMDUMP ("output buffer", info.data, info.size);
  gst_buffer_unmap (frame->output_buffer, &info);
}

static gboolean
gst_vulkan_h265_encoder_new_frame (GstH265Encoder * base,
    GstH265EncodeFrame * frame, guint input_frame_count)
{
  GstVulkanH265EncoderFrame *frame_in;
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (base);

  frame_in = gst_vulkan_h265_encoder_frame_new ();
  frame_in->picture = gst_vulkan_encode_picture_new (self->encoder,
      frame->frame->input_buffer, self->width, self->height,
      frame->is_ref, frame->type != GST_H265_I_SLICE);

  if (!frame_in->picture) {
    GST_ERROR_OBJECT (self, "Failed to create the encode picture");
    return GST_FLOW_ERROR;
  }

  gst_h265_encode_frame_set_user_data (frame, frame_in,
      gst_vulkan_h265_encoder_frame_free);

  return TRUE;
}

static void
gst_vulkan_h265_encoder_init (GstVulkanH265Encoder * self)
{
  gst_vulkan_buffer_memory_init_once ();
}

static gboolean
gst_vulkan_h265_encoder_open (GstVideoEncoder * encoder)
{
  int i;
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (self), NULL,
          &self->instance)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan instance"), (NULL));
    return FALSE;
  }

  for (i = 0; i < self->instance->n_physical_devices; i++) {
    self->device = gst_vulkan_device_new_with_index (self->instance, i);
    self->encode_queue =
        gst_vulkan_device_select_queue (self->device,
        VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
    if (self->encode_queue)
      break;
    gst_object_unref (self->device);
  }

  if (!self->encode_queue) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create/retrieve vulkan H.265 encoder queue"), (NULL));
    gst_clear_object (&self->instance);
    return FALSE;
  }

  self->encoder =
      gst_vulkan_encoder_create_from_queue (self->encode_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to instanciate the encoder"), (NULL));
  }
  return TRUE;
}

static void
gst_vulkan_h265_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (object);
  switch (prop_id) {
    case PROP_RATE_CONTROL:
      self->prop.rate_ctrl = g_value_get_enum (value);
      break;
    case PROP_ENCODE_USAGE:
      self->prop.video_usage_hints = g_value_get_uint (value);
      break;
    case PROP_ENCODE_CONTENT:
      self->prop.video_content_hints = g_value_get_uint (value);
      break;
    case PROP_TUNING_MODE:
      self->prop.tuning_mode = g_value_get_uint (value);
    case PROP_QP_MIN:
      self->prop.min_qp = g_value_get_uint (value);
      break;
    case PROP_QP_MAX:
      self->prop.max_qp = g_value_get_uint (value);
      break;
    case PROP_AVERAGE_BITRATE:
      self->prop.average_bitrate = g_value_get_uint (value);
      break;
    case PROP_QUALITY_LEVEL:
      self->prop.quality_level = g_value_get_uint (value);
      break;
    case PROP_AUD:
      self->prop.aud = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_h265_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (object);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->prop.rate_ctrl);
      break;
    case PROP_ENCODE_USAGE:
      g_value_set_enum (value, self->prop.video_usage_hints);
      break;
    case PROP_ENCODE_CONTENT:
      g_value_set_enum (value, self->prop.video_content_hints);
      break;
    case PROP_TUNING_MODE:
      g_value_set_enum (value, self->prop.tuning_mode);
      break;
    case PROP_QP_MIN:
      g_value_set_uint (value, self->prop.min_qp);
      break;
    case PROP_QP_MAX:
      g_value_set_uint (value, self->prop.max_qp);
      break;
    case PROP_AVERAGE_BITRATE:
      g_value_set_uint (value, self->prop.average_bitrate);
      break;
    case PROP_QUALITY_LEVEL:
      g_value_set_uint (value, self->prop.quality_level);
      break;
    case PROP_AUD:
      g_value_set_boolean (value, self->prop.aud);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vulkan_h265_encoder_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query)
{
  gboolean need_pool;
  GstCaps *caps, *profile_caps;
  GstVideoInfo info;
  guint size;
  GstBufferPool *pool = NULL;
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (venc);

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  /* the normal size of a frame */
  size = info.size;

  if (need_pool) {
    GstStructure *config;
    GstVulkanVideoCapabilities enc_caps;

    pool = gst_vulkan_image_buffer_pool_new (self->device);

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    profile_caps = gst_vulkan_encoder_profile_caps (self->encoder);
    gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);
    gst_caps_unref (profile_caps);
    gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

    gst_vulkan_encoder_caps (self->encoder, &enc_caps);
    if ((enc_caps.
            caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)
        == 0) {
      gst_structure_set (config, "num-layers", G_TYPE_UINT,
          enc_caps.caps.maxDpbSlots, NULL);
    }

    if (!gst_buffer_pool_set_config (pool, config)) {
      g_object_unref (pool);
      return FALSE;
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 1, 0);
  if (pool)
    g_object_unref (pool);

  if (!gst_vulkan_encoder_create_dpb_pool (self->encoder, caps)) {
    GST_ERROR_OBJECT (self, "Unable to create the dpb pool");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_max_num_reference (GstH265Encoder * base,
    guint32 * max_l0_reference_count, guint32 * max_l1_reference_count)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (base);
  GstVulkanVideoCapabilities enc_caps;

  if (!gst_vulkan_encoder_caps (self->encoder, &enc_caps))
    return FALSE;

  *max_l0_reference_count = enc_caps.codec.h265enc.maxPPictureL0ReferenceCount;
  *max_l1_reference_count = enc_caps.codec.h265enc.maxL1ReferenceCount;

  return TRUE;
}

static void
gst_vulkan_h265_encoder_class_init (GstVulkanH265EncoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstH265EncoderClass *h265encoder_class = GST_H265_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gint n_props = PROP_MAX;
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;
  gst_element_class_set_metadata (element_class, "Vulkan H.265 encoder",
      "Codec/Encoder/Video/Hardware", "A H.265 video encoder based on Vulkan",
      "Stéphane Cerveau <scerveau@igalia.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_h265_encoder_sink_template);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_h265_encoder_src_template);

  gobject_class->set_property = gst_vulkan_h265_encoder_set_property;
  gobject_class->get_property = gst_vulkan_h265_encoder_get_property;

  /**
   * GstVkH265Enc:rate-control:
   *
   * Choose the vulkan rate control to use.
   */
  properties[PROP_RATE_CONTROL] =
      g_param_spec_enum ("rate-control", "Vulkan rate control",
      "Choose the vulkan rate control", GST_TYPE_VULKAN_H265_RATE_CONTROL,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH265Enc:encode-usage:
   *
   * Choose the vulkan encode usage.
   */
  properties[PROP_ENCODE_USAGE] =
      g_param_spec_enum ("encode-usage", "Vulkan encode usage",
      "Choose the vulkan encode usage", GST_TYPE_VULKAN_H265_ENCODE_USAGE,
      VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH265Enc:encode-content:
   *
   * Choose the vulkan encode content.
   */
  properties[PROP_ENCODE_CONTENT] =
      g_param_spec_enum ("encode-content", "Vulkan encode content",
      "Choose the vulkan encode content", GST_TYPE_VULKAN_H265_ENCODE_CONTENT,
      VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH265Enc:tuning-mode:
   *
   * Choose the vulkan encode tuning.
   */
  properties[PROP_TUNING_MODE] =
      g_param_spec_enum ("tuning-mode", "Vulkan encode tuning",
      "Choose the vulkan encode tuning",
      GST_TYPE_VULKAN_H265_ENCODE_TUNING_MODE,
      VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

 /**
   * GstVkH265Enc:max-qp:
   *
   * The maximum quantizer value, 0 meaning lossless..
   */
  properties[PROP_QP_MAX] = g_param_spec_uint ("qp-max", "Maximum QP",
      "Maximum quantizer value for each frame", DEFAULT_H265_MIN_QP,
      DEFAULT_H265_MAX_QP, DEFAULT_H265_MAX_QP, param_flags);

  /**
   * GstVkH265Enc:min-qp:
   *
   * The minimum quantizer value, 0 meaning lossless.
   */
  properties[PROP_QP_MIN] = g_param_spec_uint ("qp-min", "Minimum QP",
      "Minimum quantizer value for each frame", DEFAULT_H265_MIN_QP,
      DEFAULT_H265_MAX_QP, 1, param_flags);

  /**
   * GstVkH265Enc:average-bitrate:
   *
   * The average bitrate in bps to be used by the encoder.
   *
   */
  properties[PROP_AVERAGE_BITRATE] =
      g_param_spec_uint ("average-bitrate", "Vulkan encode average bitrate",
      "Choose the vulkan encoding bitrate", 0, UINT_MAX,
      DEFAULT_H265_AVERAGE_BIRATE, param_flags);

  /**
   * GstVkH265Enc:quality-level:
   *
   * The quality level to be used by the encoder.
   *
   */
  properties[PROP_QUALITY_LEVEL] =
      g_param_spec_uint ("quality-level", "Vulkan encode quality level",
      "Choose the vulkan encoding quality level", 0, UINT_MAX, 0, param_flags);

  /**
   * GstVkH265Enc:aud:
   *
   * Insert the AU (Access Unit) delimeter for each frame.
   */
  properties[PROP_AUD] = g_param_spec_boolean ("aud", "Insert AUD",
      "Insert AU (Access Unit) delimeter for each frame", FALSE, param_flags);

  g_object_class_install_properties (gobject_class, n_props, properties);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_close);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_stop);
  encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_src_query);
  encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_sink_query);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_flush);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_propose_allocation);

  h265encoder_class->new_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_new_frame);
  h265encoder_class->encode_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_encode_frame);
  h265encoder_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_prepare_output);
  h265encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_set_format);
  h265encoder_class->max_num_reference =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_max_num_reference);
}

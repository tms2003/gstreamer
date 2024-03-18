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
 * SECTION:element-vkh264enc
 * @title: vkh264enc
 * @short_description: A Vulkan based H264 video encoder
 *
 * vkh264enc encodes raw video surfaces into H.264 bitstreams using
 * Vulkan video extensions.
 *
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vulkanupload ! vulkanh264enc ! h264parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.26
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkh264enc.h"

#include <gst/codecparsers/gsth264bitwriter.h>

#include "gstvulkanelements.h"

#include <gst/vulkan/gstvkencoder-private.h>


static GstStaticPadTemplate gst_vulkan_h264_encoder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, "NV12")));

static GstStaticPadTemplate gst_vulkan_h264_encoder_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "profile = { (string) main, (string) high, (string) baseline, (string) high-4:4:4 } ,"
        "stream-format = { (string) byte-stream }, "
        "alignment = (string) au"));

typedef struct _GstVulkanH264EncoderFrame GstVulkanH264EncoderFrame;

#define DEFAULT_H264_MB_SIZE_ALIGNMENT 16
#define DEFAULT_H264_QP_MIN 0
#define DEFAULT_H264_QP_MAX 51
#define DEFAULT_H264_CONSTANT_QP 26

#define MAX_H264_SPS_HDR_SIZE  16473
#define MAX_H264_VUI_PARAMS_SIZE  210
#define MAX_H264_HRD_PARAMS_SIZE  4103
#define MAX_H264_PPS_HDR_SIZE  101
#define MAX_H264_SLICE_HDR_SIZE  397 + 2572 + 6670 + 2402

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

#define GST_TYPE_VULKAN_H264_RATE_CONTROL (gst_vulkan_h264_enc_rate_control_get_type ())
static GType
gst_vulkan_h264_enc_rate_control_get_type (void)
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

    qtype = g_enum_register_static ("GstVulkanH264EncRateControl", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H264_ENCODE_USAGE (gst_vulkan_h264_enc_usage_get_type ())
static GType
gst_vulkan_h264_enc_usage_get_type (void)
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

    qtype = g_enum_register_static ("GstVulkanH264EncUsage", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H264_ENCODE_CONTENT (gst_vulkan_h264_enc_content_get_type ())
static GType
gst_vulkan_h264_enc_content_get_type (void)
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

    qtype = g_enum_register_static ("GstVulkanH264EncContent", values);
  }
  return qtype;
}

#define GST_TYPE_VULKAN_H264_ENCODE_TUNING_MODE (gst_vulkan_h264_enc_tuning_mode_get_type ())
static GType
gst_vulkan_h264_enc_tuning_mode_get_type (void)
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

    qtype = g_enum_register_static ("GstVulkanH264EncTuningMode", values);
  }
  return qtype;
}

typedef struct _VkH264Params
{
  StdVideoH264SequenceParameterSet sps;
  StdVideoH264PictureParameterSet pps;
  StdVideoH264SequenceParameterSetVui vui;
  StdVideoH264HrdParameters hrd;
} VkH264Params;

struct _GstVulkanH264Encoder
{
  /*< private > */
  GstH264Encoder parent;

  gint width;
  gint height;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *encode_queue;
  GstVulkanEncoder *encoder;

  GstVulkanVideoProfile profile;
  VkVideoEncodeH264CapabilitiesKHR caps;

  VkH264Params session_params;

  /* H264 fields */
  guint8 level_idc;
  struct
  {
    guint32 rate_ctrl;
    guint32 tuning_mode;
    guint32 video_usage_hints;
    guint32 video_content_hints;
    guint32 min_qp;
    guint32 max_qp;
    guint32 num_slices;
    gboolean aud;
    guint32 quality_level;
    guint32 average_bitrate;
  } prop;

};

struct _GstVulkanH264EncoderFrame
{
  GstVulkanEncodePicture *picture;

  StdVideoEncodeH264WeightTable slice_wt;
  StdVideoEncodeH264SliceHeader slice_hdr;
  VkVideoEncodeH264NaluSliceInfoKHR slice_info;
  VkVideoEncodeH264RateControlInfoKHR rc_info;
  VkVideoEncodeH264RateControlLayerInfoKHR rc_layer_info;
  VkVideoEncodeH264PictureInfoKHR enc_pic_info;
  VkVideoEncodeH264DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeH264QualityLevelPropertiesKHR quality_level;

  StdVideoEncodeH264PictureInfo pic_info;
  StdVideoEncodeH264ReferenceInfo ref_info;
  StdVideoEncodeH264ReferenceListsInfo ref_list_info;
};

GST_DEBUG_CATEGORY_STATIC (gst_vulkan_h264_encoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_h264_encoder_debug

#define gst_vulkan_h264_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanH264Encoder, gst_vulkan_h264_encoder,
    GST_TYPE_H264_ENCODER,
    GST_DEBUG_CATEGORY_INIT (gst_vulkan_h264_encoder_debug, "vulkanh264enc", 0,
        "Vulkan H.264 encoder"));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vulkanh264enc, "vulkanh264enc",
    GST_RANK_NONE, GST_TYPE_VULKAN_H264ENC, vulkan_element_init (plugin));

static GstVulkanH264EncoderFrame *
gst_vulkan_h264_encoder_frame_new (void)
{
  GstVulkanH264EncoderFrame *frame;

  frame = g_new (GstVulkanH264EncoderFrame, 1);
  frame->picture = NULL;

  return frame;
}

static void
gst_vulkan_h264_encoder_frame_free (gpointer pframe)
{
  GstVulkanH264EncoderFrame *frame = pframe;
  g_clear_pointer (&frame->picture, gst_vulkan_encode_picture_free);
  g_free (frame);
}

static inline GstVulkanH264EncoderFrame *
_vk_enc_frame (GstH264EncodeFrame * frame)
{
  GstVulkanH264EncoderFrame *enc_frame =
      gst_h264_encode_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}

static StdVideoH264ChromaFormatIdc
gst_vulkan_h264_chromat_from_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return STD_VIDEO_H264_CHROMA_FORMAT_IDC_MONOCHROME;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_NV16_10LE32:
      return STD_VIDEO_H264_CHROMA_FORMAT_IDC_422;
    default:
      return STD_VIDEO_H264_CHROMA_FORMAT_IDC_INVALID;
  }

  return STD_VIDEO_H264_CHROMA_FORMAT_IDC_INVALID;
}

static StdVideoH264PictureType
gst_vulkan_h264_picture_type (GstH264SliceType type, gboolean key_type)
{
  switch (type) {
    case GST_H264_I_SLICE:
      if (key_type)
        return STD_VIDEO_H264_PICTURE_TYPE_IDR;
      else
        return STD_VIDEO_H264_PICTURE_TYPE_I;
    case GST_H264_P_SLICE:
      return STD_VIDEO_H264_PICTURE_TYPE_P;
    case GST_H264_B_SLICE:
      return STD_VIDEO_H264_PICTURE_TYPE_B;
    default:
      GST_WARNING ("Unsupported picture type '%d'", type);
      return STD_VIDEO_H264_PICTURE_TYPE_INVALID;
  }
}

static StdVideoH264SliceType
gst_vulkan_h264_slice_type (GstH264SliceType type)
{
  switch (type) {
    case GST_H264_I_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_I;
    case GST_H264_P_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_P;
    case GST_H264_B_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_B;
    default:
      GST_WARNING ("Unsupported picture type '%d'", type);
      return STD_VIDEO_H264_SLICE_TYPE_INVALID;
  }
}

static StdVideoH264ProfileIdc
gst_vulkan_h264_profile_type (GstH264Profile profile)
{
  switch (profile) {
    case GST_H264_PROFILE_BASELINE:
      return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    case GST_H264_PROFILE_MAIN:
      return STD_VIDEO_H264_PROFILE_IDC_MAIN;
    case GST_H264_PROFILE_HIGH:
      return STD_VIDEO_H264_PROFILE_IDC_HIGH;
    default:
      GST_WARNING ("Unsupported profile type '%d'", profile);
      return STD_VIDEO_H264_PROFILE_IDC_INVALID;
  }
}

static StdVideoH264LevelIdc
gst_vulkan_h264_level_idc (int level_idc)
{
  switch (level_idc) {
    case 10:
      return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case 11:
      return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case 12:
      return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case 13:
      return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case 20:
      return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case 21:
      return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case 22:
      return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case 30:
      return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case 31:
      return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case 32:
      return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case 40:
      return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case 41:
      return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case 42:
      return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case 50:
      return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case 51:
      return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case 52:
      return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case 60:
      return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case 61:
      return STD_VIDEO_H264_LEVEL_IDC_6_1;
    default:
    case 62:
      return STD_VIDEO_H264_LEVEL_IDC_6_2;
  }
}

static void
gst_vulkan_h264_encoder_init_std_sps (GstVulkanH264Encoder * self, gint sps_id)
{
  GstH264Encoder *base = GST_H264_ENCODER (self);
  VkVideoChromaSubsamplingFlagBitsKHR chroma_format;
  VkVideoComponentBitDepthFlagsKHR bit_depth_luma, bit_depth_chroma;
  StdVideoH264SequenceParameterSet *vksps = &self->session_params.sps;
  GstVideoCodecState *input_state = gst_h264_encoder_get_input_state (base);
  const uint32_t mbAlignedWidth =
      GST_ROUND_UP_N (self->width, DEFAULT_H264_MB_SIZE_ALIGNMENT);
  const uint32_t mbAlignedHeight =
      GST_ROUND_UP_N (self->height, DEFAULT_H264_MB_SIZE_ALIGNMENT);

  gst_vulkan_video_get_chroma_info_from_format (input_state->info.finfo->format,
      &chroma_format, &bit_depth_luma, &bit_depth_chroma);
  self->session_params.hrd = (StdVideoH264HrdParameters) {
     /* *INDENT-OFF* */
    .cpb_cnt_minus1 = 0,
    .bit_rate_scale = 4,
    .cpb_size_scale = 0,
    .reserved1 = 0,
    .bit_rate_value_minus1 = {2928,},
    .cpb_size_value_minus1 = {74999,},
    .cbr_flag = {0,},
    .initial_cpb_removal_delay_length_minus1 = 23u,
    .cpb_removal_delay_length_minus1 = 23u,
    .dpb_output_delay_length_minus1 = 23u,
    .time_offset_length = 24u,
     /* *INDENT-ON* */
  };
  self->session_params.vui = (StdVideoH264SequenceParameterSetVui) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH264SpsVuiFlags) {
      .aspect_ratio_info_present_flag = 1u,
      .overscan_info_present_flag = 0u,
      .overscan_appropriate_flag = 0u,
      .video_signal_type_present_flag = 0u,
      .video_full_range_flag = 0u,
      .color_description_present_flag = 0u,
      .chroma_loc_info_present_flag = 0u,
      .timing_info_present_flag = 1u,
      .fixed_frame_rate_flag = GST_VIDEO_INFO_FPS_N (&input_state->info),
      .bitstream_restriction_flag = 0u,
      .nal_hrd_parameters_present_flag = 0u,
      .vcl_hrd_parameters_present_flag = 0u,
    },
    .aspect_ratio_idc = STD_VIDEO_H264_ASPECT_RATIO_IDC_UNSPECIFIED,
    .sar_width = GST_VIDEO_INFO_PAR_N (&input_state->info),
    .sar_height = GST_VIDEO_INFO_PAR_D (&input_state->info),
    // PAL Table E.2
    .video_format = 1,
    .colour_primaries = 0,
    .transfer_characteristics = 0,
    .matrix_coefficients = 0,
    .num_units_in_tick = GST_VIDEO_INFO_FPS_N (&input_state->info) ? GST_VIDEO_INFO_FPS_D (&input_state->info) : 0,
    .time_scale = GST_VIDEO_INFO_FPS_N (&input_state->info) * 2,
    .max_num_reorder_frames = 0,
    .max_dec_frame_buffering = 0,
    .chroma_sample_loc_type_top_field = 0,
    .chroma_sample_loc_type_bottom_field = 0,
    .pHrdParameters = &self->session_params.hrd,
    /* *INDENT-ON* */
  };

  self->session_params.sps = (StdVideoH264SequenceParameterSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH264SpsFlags){
      .direct_8x8_inference_flag = 1u,
      .constraint_set0_flag = 0u,
      .constraint_set1_flag = 0u,
      .constraint_set2_flag = 0u,
      .constraint_set3_flag = 0u,
      .constraint_set4_flag = 0u,
      .constraint_set5_flag = 0u,
      .mb_adaptive_frame_field_flag = 0u,
      .frame_mbs_only_flag = 1u,
      .delta_pic_order_always_zero_flag = 0u,
      .separate_colour_plane_flag = 0u,
      .gaps_in_frame_num_value_allowed_flag = 0u,
      .qpprime_y_zero_transform_bypass_flag = 0u,
      .frame_cropping_flag = 0u,
      .seq_scaling_matrix_present_flag = 0u,
      .vui_parameters_present_flag = 1u,
    },
    .profile_idc = self->profile.codec.h264enc.stdProfileIdc,
    .level_idc = gst_vulkan_h264_level_idc (self->level_idc),
    .chroma_format_idc =
        gst_vulkan_h264_chromat_from_format (input_state->info.finfo->
        format),
    .seq_parameter_set_id = 0,
    .bit_depth_luma_minus8 = 0, //TODO: be configurable
    .bit_depth_chroma_minus8 = 0, //TODO: be configurable
    .log2_max_frame_num_minus4 = 0,
    .pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_2,
    .offset_for_non_ref_pic = 0,
    .offset_for_top_to_bottom_field = 0,
    .log2_max_pic_order_cnt_lsb_minus4 = 4,
    .num_ref_frames_in_pic_order_cnt_cycle = 0,
    .max_num_ref_frames = 3,
    .pic_width_in_mbs_minus1 = mbAlignedWidth / DEFAULT_H264_MB_SIZE_ALIGNMENT - 1,
    .pic_height_in_map_units_minus1 = mbAlignedHeight / DEFAULT_H264_MB_SIZE_ALIGNMENT - 1,
    .frame_crop_left_offset = 0u,
    .frame_crop_right_offset = mbAlignedWidth - self->width,
    .frame_crop_top_offset = 0u,
    .frame_crop_bottom_offset = mbAlignedHeight - self->height,

    // This allows for picture order count values in the range [0, 255].
    .pOffsetForRefFrame = NULL,
    .pScalingLists = NULL,
    .pSequenceParameterSetVui = &self->session_params.vui,
    /* *INDENT-ON* */
  };
  vksps->flags.frame_cropping_flag = (vksps->frame_crop_right_offset
      || vksps->frame_crop_bottom_offset);

  if (self->session_params.sps.frame_crop_right_offset
      || self->session_params.sps.frame_crop_bottom_offset) {
    if (vksps->chroma_format_idc == STD_VIDEO_H264_CHROMA_FORMAT_IDC_420) {
      self->session_params.sps.frame_crop_right_offset >>= 1;
      self->session_params.sps.frame_crop_bottom_offset >>= 1;
    }
  }
  gst_video_codec_state_unref (input_state);
}

static void
gst_vulkan_h264_encoder_init_std_pps (GstVulkanH264Encoder * self, gint sps_id,
    gint pps_id)
{
  self->session_params.pps = (StdVideoH264PictureParameterSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH264PpsFlags) {
      .transform_8x8_mode_flag = 0u,
      .redundant_pic_cnt_present_flag = 0u,
      .constrained_intra_pred_flag = 0u,
      .deblocking_filter_control_present_flag = 1u,
      .weighted_pred_flag = 0u,
      .bottom_field_pic_order_in_frame_present_flag = 0u,
      .entropy_coding_mode_flag = 1u,
      .pic_scaling_matrix_present_flag = 0u,
    },
    .seq_parameter_set_id = sps_id,
    .pic_parameter_set_id = pps_id,
    .num_ref_idx_l0_default_active_minus1 = 0,
    .num_ref_idx_l1_default_active_minus1 = 0,
    .weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT,
    .pic_init_qp_minus26 = 0,
    .pic_init_qs_minus26 = 0,
    .chroma_qp_index_offset = 0,
    .second_chroma_qp_index_offset = 0,
    .pScalingLists = NULL,
    /* *INDENT-ON* */
  };
}

static gboolean
gst_vulkan_h264_encoder_get_session_params (GstVulkanH264Encoder * self,
    gint sps_id, gint pps_id, void **packed_params, gsize * size)
{
  GError *err = NULL;
  GstVulkanEncoderParametersFeedback feedback = { 0, };
  GstVulkanEncoderParametersOverrides override_params = {
    .h264 = {
          .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
          .writeStdSPS = (sps_id >= 0),
          .writeStdPPS = (pps_id >= 0),
          .stdSPSId = sps_id,
          .stdPPSId = pps_id,
        }
  };
  gst_vulkan_encoder_video_session_parameters_overrides (self->encoder,
      &override_params, &feedback, size, (gpointer *) packed_params, &err);

  return (*size != 0);
}

static void
gst_vulkan_h264_encoder_reset (GstVulkanH264Encoder * self)
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
_calculate_output_buffer_size (GstVulkanH264Encoder * self)
{
  guint codedbuf_size = 0;

  codedbuf_size = (self->width * self->height);

  /* Account for SPS header */
  /* XXX: exclude scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 /* start code */  + GST_ROUND_UP_8 (MAX_H264_SPS_HDR_SIZE +
      MAX_H264_VUI_PARAMS_SIZE + 2 * MAX_H264_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  /* XXX: exclude slice groups, scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_H264_PPS_HDR_SIZE) / 8;

  /* Add 5% for safety */
  codedbuf_size = (guint) ((gfloat) codedbuf_size * 1.05);

  GST_DEBUG_OBJECT (self, "Calculate codedbuf size: %u", codedbuf_size);
  return codedbuf_size;
}

static gboolean
gst_vulkan_h264_encoder_init_session (GstVulkanH264Encoder * self)
{
  GstH264Encoder *base = GST_H264_ENCODER (self);
  GError *err = NULL;
  GstVulkanEncoderParameters enc_params;
  VkVideoEncodeH264SessionParametersAddInfoKHR params_add;
  VkVideoEncodeQualityLevelInfoKHR quality_level_info;
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

  output_state = gst_video_encoder_get_output_state (GST_VIDEO_ENCODER (self));
  input_state = gst_h264_encoder_get_input_state (base);
  profile =
      gst_structure_get_string (gst_caps_get_structure (output_state->caps, 0),
      "profile");
  if (!gst_vulkan_video_get_chroma_info_from_format (input_state->info.
          finfo->format, &chroma_format, &bit_depth_luma, &bit_depth_chroma)) {
    GST_WARNING_OBJECT (self,
        "unable to retrieve chroma info from input format");
    return FALSE;
  }

  gst_video_codec_state_unref (input_state);
  gst_video_codec_state_unref (output_state);

  gst_h264_encoder_set_profile (base, gst_h264_profile_from_string (profile));

  self->profile = (GstVulkanVideoProfile) {
    /* *INDENT-OFF* */
    .profile = (VkVideoProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &self->profile.usage.encode,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
      .chromaSubsampling = chroma_format,
      .chromaBitDepth = bit_depth_luma,
      .lumaBitDepth = bit_depth_chroma,
    },
    .usage.encode = (VkVideoEncodeUsageInfoKHR) {
        .pNext =  &self->profile.codec.h264enc,
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
        .videoUsageHints = self->prop.video_usage_hints,
        .videoContentHints = self->prop.video_content_hints,
        .tuningMode = self->prop.tuning_mode,
    },
    .codec.h264enc = (VkVideoEncodeH264ProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
      .pNext = NULL,
      .stdProfileIdc = gst_vulkan_h264_profile_type (gst_h264_encoder_get_profile(base)),
    },
    /* *INDENT-ON* */
  };

  self->level_idc = gst_h264_encoder_get_level_limit (base);
  self->caps = (VkVideoEncodeH264CapabilitiesKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR
    /* *INDENT-ON* */
  };

  gst_vulkan_h264_encoder_init_std_sps (self, 0);
  gst_vulkan_h264_encoder_init_std_pps (self, 0, 0);

  params_add = (VkVideoEncodeH264SessionParametersAddInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdSPSs = &self->session_params.sps,
    .stdSPSCount = 1,
    .pStdPPSs = &self->session_params.pps,
    .stdPPSCount = 1,
    /* *INDENT-ON* */
  };
  enc_params.h264 = (VkVideoEncodeH264SessionParametersCreateInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdSPSCount = 1,
    .maxStdPPSCount = 1,
    .pParametersAddInfo = &params_add,
    /* *INDENT-ON* */
  };

  if (self->prop.quality_level) {
    quality_level_info = (VkVideoEncodeQualityLevelInfoKHR) {
      /* *INDENT-OFF* */
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR,
      .pNext = NULL,
      .qualityLevel = self->prop.quality_level,
      /* *INDENT-ON* */
    };
    enc_params.h264.pNext = &quality_level_info;
  }

  if (!gst_vulkan_encoder_start (self->encoder, &self->profile,
          output_buffer_size, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to start vulkan encoder with error %s", err->message), (NULL));
    return FALSE;
  }

  if (!gst_vulkan_encoder_update_video_session_parameters (self->encoder,
          &enc_params, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to update session parameters with error %s", err->message),
        (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_close (GstVideoEncoder * encoder)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);

  gst_clear_object (&self->encoder);
  gst_clear_object (&self->encode_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_stop (GstVideoEncoder * encoder)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);

  gst_vulkan_encoder_stop (self->encoder);
  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static gboolean
_query_context (GstVulkanH264Encoder * self, GstQuery * query)
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
gst_vulkan_h264_encoder_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H264_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h264_encoder_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H264_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h264_encoder_set_format (GstH264Encoder * h264enc,
    GstVideoCodecState * state)
{
  GstVideoCodecState *output_state;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (h264enc);
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

  gst_vulkan_h264_encoder_reset (self);

  if (GST_VIDEO_ENCODER_CLASS (parent_class)->negotiate (encoder)) {
    return gst_vulkan_h264_encoder_init_session (self);
  }

  return FALSE;
}

static gboolean
_add_vulkan_params_header (GstVulkanH264Encoder * self,
    GstVulkanH264EncoderFrame * frame)
{
  void *header = NULL;
  gsize header_size = 0;

  gst_vulkan_h264_encoder_get_session_params (self, 0, 0, &header,
      &header_size);
  GST_LOG_OBJECT (self, "Adding params header of size %lu", header_size);
  g_ptr_array_add (frame->picture->packed_headers,
      gst_buffer_new_wrapped (header, header_size));

  return TRUE;
}


static gboolean
_add_aud (GstVulkanH264Encoder * self, GstH264EncodeFrame * frame)
{
  guint8 *aud_data;
  guint size = 6;
  guint8 primary_pic_type = 0;
  GstVulkanH264EncoderFrame *vk_frame = _vk_enc_frame (frame);

  switch (frame->type) {
    case GST_H264_I_SLICE:
      primary_pic_type = 0;
      break;
    case GST_H264_P_SLICE:
      primary_pic_type = 1;
      break;
    case GST_H264_B_SLICE:
      primary_pic_type = 2;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  aud_data = g_malloc0 (size);
  if (gst_h264_bit_writer_aud (primary_pic_type, TRUE, aud_data,
          &size) != GST_H264_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the AUD");
    return FALSE;
  }

  g_ptr_array_add (vk_frame->picture->packed_headers,
      gst_buffer_new_wrapped (aud_data, size));

  return TRUE;
}

static GstFlowReturn
gst_vulkan_h264_encoder_encode_frame (GstH264Encoder * base,
    GstH264EncodeFrame * h264_frame, GstH264EncodeFrame ** list0,
    guint list0_num, GstH264EncodeFrame ** list1, guint list1_num)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);
  GstVulkanVideoCapabilities enc_caps;
  GstVulkanEncodePicture *ref_pics[16] = { NULL, };
  gint i, ref_pics_num = 0;
  GstVideoCodecState *input_state;
  GstVulkanH264EncoderFrame *vk_frame = _vk_enc_frame (h264_frame);

  if (!gst_vulkan_encoder_caps (self->encoder, &enc_caps))
    return FALSE;

  input_state = gst_h264_encoder_get_input_state (base);;

  if (self->prop.aud && !_add_aud (self, h264_frame)) {
    GST_ERROR_OBJECT (self, "Encode AUD error");
    return GST_FLOW_ERROR;
  }

  /* Repeat the SPS for IDR. */
  if (h264_frame->poc == 0 && !_add_vulkan_params_header (self, vk_frame)) {
    GST_ERROR_OBJECT (self, "Encode params header error");
    return GST_FLOW_ERROR;
  }

  vk_frame->slice_wt = (StdVideoEncodeH264WeightTable) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264WeightTableFlags) {
        .luma_weight_l0_flag = 0,
        .chroma_weight_l0_flag = 0,
        .luma_weight_l1_flag = 0,
        .chroma_weight_l1_flag = 0,
    },
    .luma_log2_weight_denom = 0,
    .chroma_log2_weight_denom = 0,
    .luma_weight_l0 = { 0 },
    .luma_offset_l0 = { 0 },
    .chroma_weight_l0 = { { 0 } },
    .chroma_offset_l0 = { { 0 } },
    .luma_weight_l1 = { 0 },
    .luma_offset_l1 = { 0 },
    .chroma_weight_l1 = { { 0 } },
    .chroma_offset_l1 = { { 0 } },
    /* *INDENT-ON* */
  };

  vk_frame->slice_hdr = (StdVideoEncodeH264SliceHeader) {
    /* *INDENT-OFF* */
      .flags = (StdVideoEncodeH264SliceHeaderFlags) {
        .direct_spatial_mv_pred_flag = 0,
        .num_ref_idx_active_override_flag = (gst_vulkan_h264_slice_type (h264_frame->type) != STD_VIDEO_H264_SLICE_TYPE_I && h264_frame->is_ref),
    },
    .first_mb_in_slice = 0,
    .slice_type = gst_vulkan_h264_slice_type(h264_frame->type),
    .cabac_init_idc = STD_VIDEO_H264_CABAC_INIT_IDC_0,
    .disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED,
    .slice_alpha_c0_offset_div2 = 0,
    .slice_beta_offset_div2 = 0,
    .pWeightTable = &vk_frame->slice_wt,
    /* *INDENT-ON* */
  };

  vk_frame->slice_info = (VkVideoEncodeH264NaluSliceInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR,
    .pNext = NULL,
    .pStdSliceHeader = &vk_frame->slice_hdr,
    .constantQp = DEFAULT_H264_CONSTANT_QP,
    /* *INDENT-ON* */
  };
  if (self->prop.rate_ctrl !=
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    vk_frame->slice_info.constantQp = 0;
  }

  vk_frame->pic_info = (StdVideoEncodeH264PictureInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264PictureInfoFlags) {
        .IdrPicFlag = (gst_vulkan_h264_picture_type(h264_frame->type, h264_frame->is_ref) == STD_VIDEO_H264_PICTURE_TYPE_IDR),
        .is_reference = h264_frame->is_ref,
        .no_output_of_prior_pics_flag = 0,
        .long_term_reference_flag = 0,
        .adaptive_ref_pic_marking_mode_flag = 0,
    },
    .seq_parameter_set_id = self->session_params.sps.seq_parameter_set_id,
    .pic_parameter_set_id = self->session_params.pps.pic_parameter_set_id,
    .primary_pic_type = gst_vulkan_h264_picture_type(h264_frame->type, h264_frame->is_ref),
    .frame_num = h264_frame->frame_num,
    .PicOrderCnt = h264_frame->poc,
    /* *INDENT-ON* */
  };

  vk_frame->ref_list_info = (StdVideoEncodeH264ReferenceListsInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264ReferenceListsInfoFlags) {
      .ref_pic_list_modification_flag_l0 = 0,
      .ref_pic_list_modification_flag_l1 = 0,
    },
    .num_ref_idx_l0_active_minus1 = 0,
    .num_ref_idx_l1_active_minus1 = 0,
    .RefPicList0 = {STD_VIDEO_H264_NO_REFERENCE_PICTURE, },
    .RefPicList1 = {STD_VIDEO_H264_NO_REFERENCE_PICTURE, },
    .refList0ModOpCount = 0,
    .refList1ModOpCount = 0,
    .refPicMarkingOpCount = 0,
    .reserved1 = {0, },
    .pRefList0ModOperations = NULL,
    .pRefList1ModOperations = NULL,
    .pRefPicMarkingOperations = NULL,
    /* *INDENT-ON* */
  };
  vk_frame->pic_info.pRefLists = &vk_frame->ref_list_info;

  memset (vk_frame->ref_list_info.RefPicList0,
      STD_VIDEO_H264_NO_REFERENCE_PICTURE, STD_VIDEO_H264_MAX_NUM_LIST_REF);
  memset (vk_frame->ref_list_info.RefPicList1,
      STD_VIDEO_H264_NO_REFERENCE_PICTURE, STD_VIDEO_H264_MAX_NUM_LIST_REF);

  vk_frame->rc_info = (VkVideoEncodeH264RateControlInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
    .pNext = NULL,
    .gopFrameCount = 0,
    .idrPeriod = 0,
    .consecutiveBFrameCount = 0,
    .temporalLayerCount = 1,
    /* *INDENT-ON* */
  };

  vk_frame->rc_layer_info = (VkVideoEncodeH264RateControlLayerInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_KHR,
    .pNext = NULL,
    .useMinQp = TRUE,
    .minQp = (VkVideoEncodeH264QpKHR){ self->prop.min_qp, self->prop.min_qp, self->prop.min_qp },
    .useMaxQp = TRUE,
    .maxQp = (VkVideoEncodeH264QpKHR){ self->prop.max_qp, self->prop.max_qp, self->prop.max_qp },
    .useMaxFrameSize = TRUE,
    .maxFrameSize = (VkVideoEncodeH264FrameSizeKHR) {0, 0, 0},
    /* *INDENT-ON* */
  };

  vk_frame->quality_level = (VkVideoEncodeH264QualityLevelPropertiesKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR,
    .pNext = NULL,
    .preferredRateControlFlags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .preferredGopFrameCount = 0,
    .preferredIdrPeriod = 0,
    .preferredConsecutiveBFrameCount = 0,
    .preferredConstantQp = (VkVideoEncodeH264QpKHR){ DEFAULT_H264_CONSTANT_QP, DEFAULT_H264_CONSTANT_QP, DEFAULT_H264_CONSTANT_QP },
    .preferredMaxL0ReferenceCount = 0,
    .preferredMaxL1ReferenceCount = 0,
    .preferredStdEntropyCodingModeFlag = 0,
    /* *INDENT-ON* */
  };

  vk_frame->enc_pic_info = (VkVideoEncodeH264PictureInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR,
    .pNext = NULL,
    .naluSliceEntryCount = 1,
    .pNaluSliceEntries = &vk_frame->slice_info,
    .pStdPictureInfo = &vk_frame->pic_info,
    .generatePrefixNalu = (enc_caps.codec.h264enc.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_GENERATE_PREFIX_NALU_BIT_KHR),
    /* *INDENT-ON* */
  };

  vk_frame->ref_info = (StdVideoEncodeH264ReferenceInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264ReferenceInfoFlags) {
      .used_for_long_term_reference =0,
    },
    .primary_pic_type = gst_vulkan_h264_picture_type(h264_frame->type, h264_frame->is_ref),
    .FrameNum = h264_frame->frame_num, //decode order
    .PicOrderCnt = h264_frame->poc, //display order
    .long_term_pic_num = 0,
    .long_term_frame_idx = 0,
    .temporal_id = 0,
    /* *INDENT-ON* */
  };

  vk_frame->dpb_slot_info = (VkVideoEncodeH264DpbSlotInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR,
    .pNext = NULL,
    .pStdReferenceInfo = &vk_frame->ref_info,
    /* *INDENT-ON* */
  };

  vk_frame->picture->codec_pic_info = &vk_frame->enc_pic_info;
  vk_frame->picture->codec_rc_info = &vk_frame->rc_info;
  vk_frame->picture->codec_rc_layer_info = &vk_frame->rc_layer_info;
  vk_frame->picture->codec_dpb_slot_info = &vk_frame->dpb_slot_info;
  vk_frame->picture->codec_quality_level = &vk_frame->quality_level;
  vk_frame->picture->fps_n = GST_VIDEO_INFO_FPS_N (&input_state->info);
  vk_frame->picture->fps_d = GST_VIDEO_INFO_FPS_D (&input_state->info);

  gst_video_codec_state_unref (input_state);

  for (i = 0; i < list0_num; i++) {
    GstVulkanH264EncoderFrame *ref_frame = _vk_enc_frame (list0[i]);
    ref_pics[i] = ref_frame->picture;
    vk_frame->ref_list_info.RefPicList0[i] = ref_frame->picture->slotIndex;
    ref_pics_num++;
  }

  for (i = 0; i < list1_num; i++) {
    GstVulkanH264EncoderFrame *ref_frame = _vk_enc_frame (list1[i]);
    ref_pics[i + list0_num] = ref_frame->picture;
    vk_frame->ref_list_info.RefPicList1[i] = ref_frame->picture->slotIndex;
    ref_pics_num++;
  }
  // if(ref_pics_num) {
  //   frame->ref_list_info.num_ref_idx_l0_active_minus1 = ref_pics_num - 1;
  //   if (list1_num)
  //     frame->ref_list_info.num_ref_idx_l1_active_minus1 = ref_pics_num - 1;
  // }
  vk_frame->picture->nb_refs = ref_pics_num;

  if (!gst_vulkan_encoder_encode (self->encoder, vk_frame->picture, ref_pics)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static gboolean
gst_vulkan_h264_encoder_flush (GstVideoEncoder * venc)
{
  /* begin from an IDR after flush. */
  gst_h264_encoder_reset (venc, TRUE);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (venc);
}

static void
gst_vulkan_h264_encoder_prepare_output (GstH264Encoder * base,
    GstVideoCodecFrame * frame)
{
  GstH264EncodeFrame *h264_frame;
  GstVulkanH264EncoderFrame *vk_frame;
  GstMapInfo info;

  h264_frame = gst_video_codec_frame_get_user_data (frame);
  vk_frame = _vk_enc_frame (h264_frame);

  frame->output_buffer = gst_buffer_ref (vk_frame->picture->out_buffer);

  gst_buffer_map (frame->output_buffer, &info, GST_MAP_READ);
  GST_MEMDUMP ("output buffer", info.data, info.size);
  gst_buffer_unmap (frame->output_buffer, &info);
}

static gboolean
gst_vulkan_h264_encoder_new_frame (GstH264Encoder * base,
    GstH264EncodeFrame * frame, guint input_frame_count)
{
  GstVulkanH264EncoderFrame *frame_in;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);

  frame_in = gst_vulkan_h264_encoder_frame_new ();

  frame_in->picture = gst_vulkan_encode_picture_new (self->encoder,
      frame->frame->input_buffer, self->width, self->height, frame->is_ref, 0);

  if (!frame_in->picture) {
    GST_ERROR_OBJECT (self, "Failed to create the encode picture");
    return GST_FLOW_ERROR;
  }

  frame_in->picture->pic_order_cnt = frame->poc;
  frame_in->picture->pic_num = frame->frame_num;

  gst_h264_encode_frame_set_user_data (frame, frame_in,
      gst_vulkan_h264_encoder_frame_free);

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_open (GstVideoEncoder * base)
{
  int i;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);

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
        ("Failed to create/retrieve vulkan H.264 encoder queue"), (NULL));
    gst_clear_object (&self->instance);
    return FALSE;
  }

  self->encoder =
      gst_vulkan_encoder_create_from_queue (self->encode_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan encoder"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static void
gst_vulkan_h264_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (object);
  switch (prop_id) {
    case PROP_RATE_CONTROL:
      self->prop.rate_ctrl = g_value_get_enum (value);
      g_object_set_property (G_OBJECT (self->encoder), "rate-control", value);
      break;
    case PROP_ENCODE_USAGE:
      self->prop.video_usage_hints = g_value_get_uint (value);
      g_object_set_property (G_OBJECT (self->encoder), "encode-usage", value);
      break;
    case PROP_ENCODE_CONTENT:
      self->prop.video_content_hints = g_value_get_uint (value);
      g_object_set_property (G_OBJECT (self->encoder), "encode-content", value);
      break;
    case PROP_TUNING_MODE:
      self->prop.tuning_mode = g_value_get_uint (value);
      g_object_set_property (G_OBJECT (self->encoder), "tuning-mode", value);
      break;
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
gst_vulkan_h264_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (object);

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
gst_vulkan_h264_encoder_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query)
{
  gboolean need_pool;
  GstCaps *caps, *profile_caps;
  GstVideoInfo info;
  guint size;
  GstBufferPool *pool = NULL;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (venc);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return FALSE;
  }

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
    if ((enc_caps.caps.
            flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)
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
    gst_object_unref (pool);

  if (!gst_vulkan_encoder_create_dpb_pool (self->encoder, caps)) {
    GST_ERROR_OBJECT (self, "Unable to create the dpb pool");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_max_num_reference (GstH264Encoder * base,
    guint32 * max_l0_reference_count, guint32 * max_l1_reference_count)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);
  GstVulkanVideoCapabilities enc_caps;

  if (!self->encoder || !gst_vulkan_encoder_caps (self->encoder, &enc_caps))
    return FALSE;

  *max_l0_reference_count = enc_caps.codec.h264enc.maxPPictureL0ReferenceCount;
  *max_l1_reference_count = enc_caps.codec.h264enc.maxL1ReferenceCount;

  return TRUE;
}

static void
gst_vulkan_h264_encoder_init (GstVulkanH264Encoder * self)
{
  gst_vulkan_buffer_memory_init_once ();
}

static void
gst_vulkan_h264_encoder_class_init (GstVulkanH264EncoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstH264EncoderClass *h264encoder_class = GST_H264_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gint n_props = PROP_MAX;
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;
  gst_element_class_set_metadata (element_class, "Vulkan H.264 encoder",
      "Codec/Encoder/Video/Hardware", "A H.264 video encoder based on Vulkan",
      "Stéphane Cerveau <scerveau@igalia.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_h264_encoder_sink_template);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_h264_encoder_src_template);

  gobject_class->set_property = gst_vulkan_h264_encoder_set_property;
  gobject_class->get_property = gst_vulkan_h264_encoder_get_property;

  /**
   * GstVkH264Enc:rate-control:
   *
   * Choose the rate control to use.
   */
  properties[PROP_RATE_CONTROL] =
      g_param_spec_enum ("rate-control", "Vulkan rate control",
      "Choose the vulkan rate control", GST_TYPE_VULKAN_H264_RATE_CONTROL,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH264Enc:encode-usage:
   *
   * Choose the vulkan encode usage.
   */
  properties[PROP_ENCODE_USAGE] =
      g_param_spec_enum ("encode-usage", "Vulkan encode usage",
      "Choose the vulkan encode usage", GST_TYPE_VULKAN_H264_ENCODE_USAGE,
      VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH264Enc:encode-content:
   *
   * Choose the vulkan encode content.
   */
  properties[PROP_ENCODE_CONTENT] =
      g_param_spec_enum ("encode-content", "Vulkan encode content",
      "Choose the vulkan encode content", GST_TYPE_VULKAN_H264_ENCODE_CONTENT,
      VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH264Enc:tuning-mode:
   *
   * Choose the vulkan encode tuning.
   */
  properties[PROP_TUNING_MODE] =
      g_param_spec_enum ("tuning-mode", "Vulkan encode tuning",
      "Choose the vulkan encode tuning mode",
      GST_TYPE_VULKAN_H264_ENCODE_TUNING_MODE,
      VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);

  /**
   * GstVkH264Enc:max-qp:
   *
   * The maximum quantizer value, 0 meaning lossless.
   */
  properties[PROP_QP_MAX] = g_param_spec_uint ("qp-max", "Maximum QP",
      "Maximum quantizer value for each frame", DEFAULT_H264_QP_MIN,
      DEFAULT_H264_QP_MAX, DEFAULT_H264_QP_MAX, param_flags);

  /**
   * GstVkH264Enc:min-qp:
   *
   * The minimum quantizer value, 0 meaning lossless.
   */
  properties[PROP_QP_MIN] = g_param_spec_uint ("qp-min", "Minimum QP",
      "Minimum quantizer value for each frame", DEFAULT_H264_QP_MIN,
      DEFAULT_H264_QP_MAX, DEFAULT_H264_QP_MIN, param_flags);

  /**
   * GstVkH264Enc:average-bitrate:
   *
   * The average bitrate in bps to be used by the encoder.
   *
   */
  properties[PROP_AVERAGE_BITRATE] =
      g_param_spec_uint ("average-bitrate", "Vulkan encode average bitrate",
      "Choose the vulkan encoding bitrate", 0, UINT_MAX, 0, param_flags);

  /**
   * GstVkH264Enc:quality-level:
   *
   * The quality level to be used by the encoder.
   *
   */
  properties[PROP_QUALITY_LEVEL] =
      g_param_spec_uint ("quality-level", "Vulkan encode quality level",
      "Choose the vulkan encoding quality level", 0, UINT_MAX, 0, param_flags);

  /**
   * GstVkH264Enc:aud:
   *
   * Insert the AU (Access Unit) delimeter before each frame.
   */
  properties[PROP_AUD] = g_param_spec_boolean ("aud", "Insert AUD",
      "Insert AU (Access Unit) delimeter before each frame", FALSE,
      param_flags);

  g_object_class_install_properties (gobject_class, n_props, properties);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_close);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_stop);
  encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_src_query);
  encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_sink_query);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_flush);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_propose_allocation);

  h264encoder_class->new_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_new_frame);
  h264encoder_class->encode_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_encode_frame);
  h264encoder_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_prepare_output);
  h264encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_set_format);
  h264encoder_class->max_num_reference =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_max_num_reference);
}

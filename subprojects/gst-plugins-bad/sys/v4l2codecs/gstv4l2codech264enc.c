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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstv4l2codecallocator.h"
#include "gstv4l2codech264enc.h"
#include "gstv4l2codecpool.h"
#include "gstv4l2format.h"
#include <gst/codecparsers/gsth264bitwriter.h>

#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

#define V4L2_MIN_KERNEL_VER_MAJOR 5
#define V4L2_MIN_KERNEL_VER_MINOR 17
#define V4L2_MIN_KERNEL_VERSION KERNEL_VERSION(V4L2_MIN_KERNEL_VER_MAJOR, V4L2_MIN_KERNEL_VER_MINOR, 0)

GST_DEBUG_CATEGORY_STATIC (v4l2_h264enc_debug);
#define GST_CAT_DEFAULT v4l2_h264enc_debug

enum
{
  PROP_0,
  PROP_LAST = PROP_0
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_V4L2_DEFAULT_VIDEO_FORMATS)));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "profile = (string) { main, constrained-baseline, baseline}")
    );


#define H264ENC_DEFAULT_KEYFRAME_INTERVAL	30

#define H264_MAX_QUALITY				63
#define H264_MIN_QUALITY				0

#define H264_DEFAULT_BITRATE			100000

/* Scale factor for bitrate (HRD bit_rate_scale: min = 6) */
#define SX_BITRATE 6
/* Scale factor for cpb_size (HRD cpb_size_scale: min = 4) */
#define SX_CPB_SIZE 4
/* Maximum sizes for common headers (in bits) */
#define MAX_SPS_HDR_SIZE  16473
#define MAX_VUI_PARAMS_SIZE  210
#define MAX_HRD_PARAMS_SIZE  4103
#define MAX_PPS_HDR_SIZE  101
#define MAX_SLICE_HDR_SIZE  397 + 2572 + 6670 + 2402

#define MAX_GOP_SIZE  1024

#define SPS_SIZE 4 + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE + MAX_VUI_PARAMS_SIZE + \
    2 * MAX_HRD_PARAMS_SIZE) / 8
#define PPS_SIZE 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8

struct _GstV4l2CodecH264Enc
{
  GstH264Encoder parent;
  GstV4l2Encoder *encoder;
  GstVideoCodecState *output_state;
  GstVideoInfo vinfo;
  gint width;
  gint height;
  gint width_in_macroblocks;
  gint height_in_macroblocks;
  guint qp_max, qp_min;
  guint64 targeted_bitrate;
  gboolean cabac;
  guint cabac_init_idc;
  gchar *profile_name;

  GstV4l2CodecAllocator *sink_allocator;
  GstV4l2CodecAllocator *src_allocator;
  GstV4l2CodecPool *sink_pool;
  GstV4l2CodecPool *src_pool;

  gboolean first_frame;
  guint64 reference_timestamp;

  struct v4l2_ctrl_h264_encode_params encode_params;
  struct v4l2_ctrl_h264_encode_rc encode_rc;
  GstH264SPS sps;
  GstH264PPS pps;
};

G_DEFINE_ABSTRACT_TYPE (GstV4l2CodecH264Enc, gst_v4l2_codec_h264_enc,
    GST_TYPE_H264_ENCODER);

#define parent_class gst_v4l2_codec_h264_enc_parent_class

static gboolean
gst_v4l2_codec_h264_enc_open (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  guint version;

  if (!gst_v4l2_encoder_open (self->encoder)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Failed to open H264 encoder"),
        ("gst_v4l2_encoder_open() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  version = gst_v4l2_encoder_get_version (self->encoder);
  if (version < V4L2_MIN_KERNEL_VERSION)
    GST_WARNING_OBJECT (self,
        "V4L2 API v%u.%u too old, at least v%u.%u required",
        (version >> 16) & 0xff, (version >> 8) & 0xff,
        V4L2_MIN_KERNEL_VER_MAJOR, V4L2_MIN_KERNEL_VER_MINOR);

  GST_DEBUG_OBJECT (self, "open h264 encoder");

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_enc_api_check (GstV4l2Encoder * encoder)
{
  guint i, ret_size;
  /* *INDENT-OFF* */
  #define SET_ID(cid) .id = (cid), .name = #cid
  struct
  {
    const gchar *name;
    unsigned int id;
    unsigned int size;
    gboolean optional;
  } controls[] = {
    {
      SET_ID (V4L2_CID_STATELESS_H264_ENCODE_PARAMS),
      .size = sizeof(struct v4l2_ctrl_h264_encode_params),
    }, {
      SET_ID (V4L2_CID_STATELESS_H264_ENCODE_RC),
      .size = sizeof(struct v4l2_ctrl_h264_encode_rc),
    }
  };
  #undef SET_ID
  /* *INDENT-ON* */

  /*
   * Compatibility check: make sure the pointer controls are
   * the right size.
   */
  for (i = 0; i < G_N_ELEMENTS (controls); i++) {
    gboolean control_found;

    control_found = gst_v4l2_encoder_query_control_size (encoder,
        controls[i].id, &ret_size);

    if (!controls[i].optional && !control_found) {
      GST_WARNING ("Driver is missing %s support.", controls[i].name);
      return FALSE;
    }

    if (control_found && ret_size != controls[i].size) {
      GST_WARNING ("%s control size mismatch: got %d bytes but %d expected.",
          controls[i].name, ret_size, controls[i].size);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_enc_close (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  gst_v4l2_encoder_close (self->encoder);
  return TRUE;
}

static void
gst_v4l2_codec_h264_enc_reset_allocation (GstV4l2CodecH264Enc * self)
{
  if (self->sink_allocator) {
    gst_v4l2_codec_allocator_detach (self->sink_allocator);
    g_clear_object (&self->sink_allocator);
    g_clear_object (&self->sink_pool);
  }

  if (self->src_allocator) {
    gst_v4l2_codec_allocator_detach (self->src_allocator);
    g_clear_object (&self->src_allocator);
    g_clear_object (&self->src_pool);
  }
}

static gboolean
gst_v4l2_codec_h264_enc_start (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  GST_DEBUG_OBJECT (self, "start");

  return GST_VIDEO_ENCODER_CLASS (parent_class)->start (encoder);
}

static gboolean
gst_v4l2_codec_h264_enc_stop (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  GST_DEBUG_OBJECT (self, "stop");

  gst_v4l2_encoder_streamoff (self->encoder, GST_PAD_SINK);
  gst_v4l2_encoder_streamoff (self->encoder, GST_PAD_SRC);

  gst_v4l2_codec_h264_enc_reset_allocation (self);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  g_clear_pointer (&self->profile_name, g_free);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static GstCaps *
gst_v4l2_codec_h264_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  GstCaps *caps, *result;

  caps = gst_v4l2_encoder_list_sink_formats (self->encoder);
  GST_DEBUG_OBJECT (self, "Supported input formats: %" GST_PTR_FORMAT, caps);

  result = gst_video_encoder_proxy_getcaps (encoder, caps, filter);

  if (caps)
    gst_caps_unref (caps);

  GST_DEBUG_OBJECT (self, "Returning sink caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_v4l2_codec_h264_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  GstV4l2CodecPool *pool = NULL;
  gboolean need_pool;

  gst_query_parse_allocation (query, NULL, &need_pool);

  if (need_pool)
    pool = gst_v4l2_codec_pool_new (self->sink_allocator, &self->vinfo);

  gst_query_add_allocation_pool (query, GST_BUFFER_POOL (pool),
      self->vinfo.size, 2, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static gboolean
gst_v4l2_codec_h264_enc_buffers_allocation (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  GST_DEBUG_OBJECT (self, "buffers allocation");

  g_clear_object (&self->sink_pool);
  g_clear_object (&self->src_pool);
  g_clear_object (&self->src_allocator);

  self->sink_allocator = gst_v4l2_codec_encoder_allocator_new (self->encoder,
      GST_PAD_SINK, 4);
  if (!self->sink_allocator) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Not enough memory to allocate sink buffers."), (NULL));
    return FALSE;
  }

  self->sink_pool =
      gst_v4l2_codec_pool_new (self->sink_allocator, &self->vinfo);

  self->src_allocator = gst_v4l2_codec_encoder_allocator_new (self->encoder,
      GST_PAD_SRC, 4);
  if (!self->src_allocator) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Not enough memory to allocate source buffers."), (NULL));
    g_clear_object (&self->sink_allocator);
    return FALSE;
  }

  self->src_pool = gst_v4l2_codec_pool_new (self->src_allocator, &self->vinfo);

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_enc_set_codec_data (GstVideoEncoder * encoder,
    GstBuffer * codec_data, guint * data_size)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  GstMapInfo info;
  guint8 *data;
  guint size;

  gst_buffer_map (codec_data, &info, GST_MAP_WRITE);
  data = (guint8 *) info.data;
  memset (data, 0, info.size);

  size = SPS_SIZE;

  if (gst_h264_bit_writer_sps (&self->sps, TRUE, data,
          &size) != GST_H264_BIT_WRITER_OK) {
    gst_buffer_unmap (codec_data, &info);
    return FALSE;
  }

  data += size;
  *data_size = size;

  size = PPS_SIZE;
  if (gst_h264_bit_writer_pps (&self->pps, TRUE, data,
          &size) != GST_H264_BIT_WRITER_OK) {
    gst_buffer_unmap (codec_data, &info);
    return FALSE;
  }

  *data_size += size;

  gst_buffer_unmap (codec_data, &info);

  return TRUE;
}

static guint8
get_sps_aspect_ratio_idc (guint par_n, guint par_d)
{
  if (par_n == 1 && par_d == 1)
    return 1;
  else if (par_n == 12 && par_d == 11)
    return 2;
  else if (par_n == 10 && par_d == 11)
    return 3;
  else if (par_n == 16 && par_d == 11)
    return 4;
  else if (par_n == 40 && par_d == 33)
    return 5;
  else if (par_n == 24 && par_d == 11)
    return 6;
  else if (par_n == 20 && par_d == 11)
    return 7;
  else if (par_n == 32 && par_d == 11)
    return 8;
  else if (par_n == 80 && par_d == 33)
    return 9;
  else if (par_n == 18 && par_d == 11)
    return 10;
  else if (par_n == 15 && par_d == 11)
    return 11;
  else if (par_n == 64 && par_d == 33)
    return 12;
  else if (par_n == 160 && par_d == 99)
    return 13;
  else if (par_n == 4 && par_d == 3)
    return 14;
  else if (par_n == 3 && par_d == 2)
    return 15;
  else if (par_n == 2 && par_d == 1)
    return 16;
  else
    return 255;                 // Extended_SAR for custom ratios
}

/* Begin of code taken from VA plugin */
typedef struct
{
  const char *level;
  int idc;
  int max_macroblocks_per_second;
  int max_frame_size_in_macroblocks;
  int max_dpb_macroblocks;
  int maximum_bitrate;
  int max_cpb_size;
  int minimum_compression_ratio;
} GstVaH264LevelLimits;

static const GstVaH264LevelLimits _h264_level_limits[] = {
    /* *INDENT-OFF* */
    /*level, idc, max_macroblocks_per_second, max_frame_size_in_macroblocks,
          max_dpb_macroblocks, max_bitrate, max_cpb_size, minimum_compression_ratio */
    {"1",   GST_H264_LEVEL_L1, 1485, 99, 396, 64, 175, 2},
    {"1b",  GST_H264_LEVEL_L1B, 1485, 99, 396, 128, 350, 2},
    {"1.1", GST_H264_LEVEL_L1_1, 3000, 396, 900, 192, 500, 2},
    {"1.2", GST_H264_LEVEL_L1_2, 6000, 396, 2376, 384, 1000, 2},
    {"1.3", GST_H264_LEVEL_L1_3, 11880, 396, 2376, 768, 2000, 2},
    {"2",   GST_H264_LEVEL_L2, 11880, 396, 2376, 2000, 2000, 2},
    {"2.1", GST_H264_LEVEL_L2_1, 19800, 792, 4752, 4000, 4000, 2},
    {"2.2", GST_H264_LEVEL_L2_2, 20250, 1620, 8100, 4000, 4000, 2},
    {"3",   GST_H264_LEVEL_L3, 40500, 1620, 8100, 10000, 10000, 2},
    {"3.1", GST_H264_LEVEL_L3_1, 108000, 3600, 18000, 14000, 14000, 4},
    {"3.2", GST_H264_LEVEL_L3_2, 216000, 5120, 20480, 20000, 20000, 4},
    {"4",   GST_H264_LEVEL_L4, 245760, 8192, 32768, 20000, 25000, 4},
    {"4.1", GST_H264_LEVEL_L4_1, 245760, 8192, 32768, 50000, 62500, 2},
    {"4.2", GST_H264_LEVEL_L4_2, 522240, 8704, 34816, 50000, 62500, 2},
    {"5",   GST_H264_LEVEL_L5, 589824, 22080, 110400, 135000, 135000, 2},
    {"5.1", GST_H264_LEVEL_L5_1, 983040, 36864, 184320, 240000, 240000, 2},
    {"5.2", GST_H264_LEVEL_L5_2, 2073600, 36864, 184320, 240000, 240000, 2},
    {"6",   GST_H264_LEVEL_L6,   4177920, 139264, 696320, 240000, 240000, 2},
    {"6.1", GST_H264_LEVEL_L6_1, 8355840, 139264, 696320, 480000, 480000, 2},
    {"6.2", GST_H264_LEVEL_L6_2, 16711680, 139264, 696320, 800000, 800000, 2},
    /* *INDENT-ON* */
};

/* Enf of code taken from VA Plugin */

static void
gst_v4l2_codec_h264_enc_init_sps_pps (GstV4l2CodecH264Enc * self,
    GstVideoCodecState * state)
{
  gint maximum_bitrate, frame_size_in_macroblocks,
      macroblocks_per_second, dpb_macroblocks, cpb_size, compression_ratio,
      minimum_level_index;

  memset (&self->sps, 0, sizeof (self->sps));
  memset (&self->sps, 0, sizeof (self->pps));

  /* SPS */
  if (g_str_equal (self->profile_name, "baseline")) {
    self->sps.profile_idc = GST_H264_PROFILE_BASELINE;
    self->sps.constraint_set0_flag = 1;
    self->sps.constraint_set1_flag = 0;
  } else if (g_str_equal (self->profile_name, "constrained-baseline")) {
    self->sps.profile_idc = GST_H264_PROFILE_BASELINE;
    self->sps.constraint_set0_flag = 1;
    self->sps.constraint_set1_flag = 1;
  } else if (g_str_equal (self->profile_name, "main")) {
    self->sps.profile_idc = GST_H264_PROFILE_MAIN;
  } else if (g_str_equal (self->profile_name, "high")) {
    self->sps.profile_idc = GST_H264_PROFILE_HIGH;
  }

  self->sps.chroma_format_idc = 1;      /* YUV 4:2:0 */

  self->sps.pic_width_in_mbs_minus1 = self->width_in_macroblocks - 1;
  self->sps.pic_height_in_map_units_minus1 = self->height_in_macroblocks - 1;

  self->sps.num_ref_frames = 1; // FIXME: rename according to spec, max_num_ref_frames
  self->sps.num_ref_frames_in_pic_order_cnt_cycle = 2;

  // XXX: fixed by hardware
  self->sps.pic_order_cnt_type = 2;

  // XXX: fixed by hardware FOSHO
  self->sps.log2_max_frame_num_minus4 = 12;
  self->sps.log2_max_pic_order_cnt_lsb_minus4 = 0;

  // XXX: fixed by hardware (at least constant in MPP)
  self->sps.direct_8x8_inference_flag = 1;
  self->sps.frame_mbs_only_flag = 1;

  // Calculate lowest acceptable level
  gint n_levels = G_N_ELEMENTS (_h264_level_limits);
  GValue bitrate = G_VALUE_INIT;
  g_object_get_property (G_OBJECT (self), "bitrate", &bitrate);
  self->targeted_bitrate = g_value_get_uint64 (&bitrate);
  maximum_bitrate = self->targeted_bitrate;
  frame_size_in_macroblocks = self->width * self->height / 256;
  macroblocks_per_second = frame_size_in_macroblocks * state->info.fps_n / state->info.fps_d;   // Assuming each macroblock is 16x16
  dpb_macroblocks = frame_size_in_macroblocks;
  cpb_size = maximum_bitrate;
  compression_ratio = state->info.fps_n / state->info.fps_d;
  minimum_level_index = 0;
  for (; minimum_level_index < n_levels; ++minimum_level_index) {
    const GstVaH264LevelLimits *level =
        &_h264_level_limits[minimum_level_index];
    if (macroblocks_per_second <= level->max_macroblocks_per_second
        && frame_size_in_macroblocks <= level->max_frame_size_in_macroblocks
        && dpb_macroblocks <= level->max_dpb_macroblocks
        && maximum_bitrate <= level->maximum_bitrate
        && cpb_size <= level->max_cpb_size
        && compression_ratio >= level->minimum_compression_ratio) {
      break;
    }
  }
  self->sps.level_idc = _h264_level_limits[minimum_level_index].idc;
  if (self->sps.level_idc == GST_H264_LEVEL_L1B) {
    self->sps.constraint_set3_flag = 1;
  }
  GST_DEBUG_OBJECT (self, "width:%d height:%d targeted_bitrate:%ld",
      self->height, self->height, self->targeted_bitrate);
  GST_DEBUG_OBJECT (self,
      "macroblocks_per_second=%d,frame_size_in_macroblocks=%d,dpb_macroblocks=%d,maximum_bitrate=%d,cpb_size=%d,compression_ratio=%d",
      macroblocks_per_second, frame_size_in_macroblocks, dpb_macroblocks,
      maximum_bitrate, cpb_size, compression_ratio);
  GST_DEBUG_OBJECT (self, "level_idc: %d", self->sps.level_idc);

  /* Crop unaligned videos */
  if (self->width & 15 || self->height & 15) {
    static const guint chroma_subsampling_width[] = { 1, 2, 2, 1 };
    static const guint chroma_subsampling_height[] = { 1, 2, 1, 1 };
    const guint crop_unit_x =
        chroma_subsampling_width[self->sps.chroma_format_idc];
    const guint crop_unit_y =
        chroma_subsampling_height[self->sps.chroma_format_idc] * (2 -
        self->sps.frame_mbs_only_flag);

    self->sps.frame_cropping_flag = 1;
    self->sps.frame_crop_left_offset = 0;
    self->sps.frame_crop_right_offset = (16 * self->width_in_macroblocks -
        self->width) / crop_unit_x;
    self->sps.frame_crop_top_offset = 0;
    self->sps.frame_crop_bottom_offset = (16 * self->height_in_macroblocks -
        self->height) / crop_unit_y;
  }
  // set colorimetry
  self->sps.vui_parameters_present_flag = 1;
  if (state->info.colorimetry.range != GST_VIDEO_COLOR_RANGE_UNKNOWN &&
      state->info.colorimetry.matrix != GST_VIDEO_COLOR_MATRIX_UNKNOWN &&
      state->info.colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
      state->info.colorimetry.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN) {

    self->sps.vui_parameters.video_signal_type_present_flag = 1;
    self->sps.vui_parameters.video_format = 5;
    self->sps.vui_parameters.colour_description_present_flag = 1;
    self->sps.vui_parameters.colour_primaries =
        gst_video_color_primaries_to_iso (state->info.colorimetry.primaries);
    self->sps.vui_parameters.transfer_characteristics =
        gst_video_transfer_function_to_iso (state->info.colorimetry.transfer);
    self->sps.vui_parameters.matrix_coefficients =
        gst_video_color_matrix_to_iso (state->info.colorimetry.matrix);
    if (state->info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255) {
      self->sps.vui_parameters.video_full_range_flag = 1;
    }
  }
  // set aspect ratio
  self->sps.vui_parameters.aspect_ratio_info_present_flag = 1;
  self->sps.vui_parameters.aspect_ratio_idc =
      get_sps_aspect_ratio_idc (state->info.par_n, state->info.par_d);
  if (self->sps.vui_parameters.aspect_ratio_idc == 255) {
    self->sps.vui_parameters.sar_width = state->info.par_n;
    self->sps.vui_parameters.sar_height = state->info.par_d;
  }
  // set Frame rate
  self->sps.vui_parameters.timing_info_present_flag = 1;
  self->sps.vui_parameters.fixed_frame_rate_flag = 1;   // Only supports fixed frame rate for now
  self->sps.vui_parameters.num_units_in_tick = state->info.fps_d;
  self->sps.vui_parameters.time_scale = state->info.fps_n * 2;

  /* PPS */
  self->pps.id = 0;
  self->pps.sequence = &self->sps;

  self->pps.deblocking_filter_control_present_flag = 1; // TODO: Set it properly

  /* XXX: fixed by hardware */
  self->pps.weighted_bipred_idc = 0;

  /* Rate Control */
  self->pps.chroma_qp_index_offset = 4;
  self->pps.pic_init_qp_minus26 = -13;
  self->pps.second_chroma_qp_index_offset = self->pps.chroma_qp_index_offset;
  self->pps.deblocking_filter_control_present_flag = 1;
  self->pps.entropy_coding_mode_flag = self->cabac;
}

static gchar *
gst_v4l2_codec_h264_enc_decide_profile (GstV4l2CodecH264Enc * self)
{
  GstCaps *allowed_caps = NULL;
  const gchar *profile_name;
  GstStructure *structure;
  gchar *ret;

  g_object_get (self, "cabac", &self->cabac, "cabac-init-idc",
      &self->cabac_init_idc, NULL);

  /* First, check whether the downstream requires a specified profile. */
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));
  if (!allowed_caps)
    allowed_caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SRC_PAD (self), NULL);

  allowed_caps = gst_caps_make_writable (allowed_caps);
  allowed_caps = gst_caps_fixate (allowed_caps);
  structure = gst_caps_get_structure (allowed_caps, 0);
  profile_name = gst_structure_get_string (structure, "profile");

  if (self->cabac) {
    if (!g_strstr_len (profile_name, -1, "main")
        && !g_strstr_len (profile_name, -1, "high")) {
      GST_WARNING_OBJECT (self,
          "CABAC is not support by user selected profile '%s'"
          ", disabling this features", profile_name);
      self->cabac = FALSE;
    }
  }

  ret = g_strdup (profile_name);
  gst_caps_unref (allowed_caps);

  return ret;
}

static gboolean
gst_v4l2_codec_h264_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  GstCaps *caps;

  GST_DEBUG_OBJECT (self, "Set format");

  gst_v4l2_encoder_streamoff (self->encoder, GST_PAD_SINK);
  gst_v4l2_encoder_streamoff (self->encoder, GST_PAD_SRC);

  gst_v4l2_codec_h264_enc_reset_allocation (self);

  if (!gst_v4l2_encoder_set_src_fmt (self->encoder, &state->info,
          V4L2_PIX_FMT_H264_SLICE)) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, ("Unsupported pixel format"),
        ("No support for %ux%u format H264", state->info.width,
            state->info.height));
    return FALSE;
  }

  if (!gst_v4l2_encoder_select_sink_format (self->encoder, &state->info,
          &self->vinfo)) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Failed to configure H264 encoder"),
        ("gst_v4l2_encoder_select_sink_format() failed: %s",
            g_strerror (errno)));
    gst_v4l2_encoder_close (self->encoder);
    return FALSE;
  }

  self->width = state->info.width;
  self->height = state->info.height;
  gst_v4l2_codec_h264_enc_buffers_allocation (encoder);
  self->width_in_macroblocks = (self->width + 15) / 16;
  self->height_in_macroblocks = (self->height + 15) / 16;
  self->first_frame = TRUE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  g_free (self->profile_name);
  self->profile_name = gst_v4l2_codec_h264_enc_decide_profile (self);

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au",
      "profile", G_TYPE_STRING, self->profile_name, NULL);

  self->output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      caps, state);

  if (gst_video_encoder_negotiate (encoder)) {
    if (!gst_v4l2_encoder_streamon (self->encoder, GST_PAD_SINK)) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Could not enable the encoder driver."),
          ("VIDIOC_STREAMON(SINK) failed: %s", g_strerror (errno)));
      return FALSE;
    }

    if (!gst_v4l2_encoder_streamon (self->encoder, GST_PAD_SRC)) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Could not enable the encoder driver."),
          ("VIDIOC_STREAMON(SRC) failed: %s", g_strerror (errno)));
      return FALSE;
    }

    gst_v4l2_codec_h264_enc_get_qp_range (self->encoder, &self->qp_min,
        &self->qp_max);

    gst_v4l2_codec_h264_enc_init_sps_pps (self, state);

    return TRUE;
  }

  return FALSE;
}

static void
gst_v4l2_codec_h264_enc_set_flushing (GstV4l2CodecH264Enc * self,
    gboolean flushing)
{
  if (self->sink_allocator)
    gst_v4l2_codec_allocator_set_flushing (self->sink_allocator, flushing);
  if (self->src_allocator)
    gst_v4l2_codec_allocator_set_flushing (self->src_allocator, flushing);
}

static gboolean
gst_v4l2_codec_h264_enc_flush (GstVideoEncoder * encoder)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Flushing encoder state.");

  gst_v4l2_encoder_flush (self->encoder);
  gst_v4l2_codec_h264_enc_set_flushing (self, FALSE);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (encoder);
}

static gboolean
gst_v4l2_codec_h264_enc_sink_event (GstVideoEncoder * encoder, GstEvent * event)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (self, "flush start");
      gst_v4l2_codec_h264_enc_set_flushing (self, TRUE);
      break;
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_event (encoder, event);
}

static GstStateChangeReturn
gst_v4l2_codec_h264_enc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (element);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    gst_v4l2_codec_h264_enc_set_flushing (self, TRUE);

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_v4l2_codec_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (object);
  GObject *enc = G_OBJECT (self->encoder);

  switch (prop_id) {
    default:
      gst_v4l2_encoder_set_property (enc, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static void
gst_v4l2_codec_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (object);
  GObject *enc = G_OBJECT (self->encoder);

  switch (prop_id) {
    default:
      gst_v4l2_encoder_get_property (enc, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static gboolean
gst_v4l2_codec_h264_enc_copy_input_buffer (GstV4l2CodecH264Enc * self,
    GstVideoCodecFrame * frame)
{
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstVideoInfo src_vinfo;
  GstBuffer *buffer;
  GstFlowReturn flow_ret;

  gst_video_info_set_format (&src_vinfo, GST_VIDEO_INFO_FORMAT (&self->vinfo),
      self->width, self->height);

  flow_ret = gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (self->sink_pool),
      &buffer, NULL);
  if (flow_ret != GST_FLOW_OK) {
    if (flow_ret == GST_FLOW_FLUSHING)
      GST_DEBUG_OBJECT (self, "Frame encoding aborted, we are flushing.");
    else
      GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
          ("No more picture buffer available."), (NULL));
    return FALSE;
  }

  if (!buffer)
    goto fail;

  if (!gst_video_frame_map (&src_frame, &src_vinfo,
          frame->input_buffer, GST_MAP_READ))
    goto fail;

  if (!gst_video_frame_map (&dest_frame, &self->vinfo, buffer, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  if (!gst_video_frame_copy (&dest_frame, &src_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);
  gst_buffer_replace (&frame->input_buffer, buffer);
  gst_buffer_unref (buffer);

  return TRUE;

fail:
  GST_ERROR_OBJECT (self, "Failed copy input buffer.");
  return FALSE;
}

static gboolean
gst_v4l2_codec_h264_enc_ensure_output_bitstream (GstV4l2CodecH264Enc * self,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn flow_ret;

  flow_ret = gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (self->src_pool),
      &frame->output_buffer, NULL);
  if (flow_ret != GST_FLOW_OK) {
    if (flow_ret == GST_FLOW_FLUSHING)
      GST_DEBUG_OBJECT (self, "Frame encoding aborted, we are flushing.");
    else
      GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
          ("No more encoded buffer available."), (NULL));
    return FALSE;
  }

  if (!frame->output_buffer)
    return FALSE;

  return TRUE;
}

static void
gst_v4l2_codec_h264_enc_fill_encode_params (GstH264Encoder * encoder,
    GstH264Frame * h264_frame)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  /* FIXME
   * Completely rewrite encode_parms each run, this is error prone otherwise,
   * keep state outside of the structure
   */
  self->encode_params.flags = 0;

  switch (h264_frame->type) {
    case GstH264Keyframe:
      self->encode_params.slice_type = V4L2_H264_SLICE_TYPE_I;
      self->encode_params.nalu_type = V4L2_H264_NAL_CODED_SLICE_IDR_PIC;
      self->encode_params.idr_pic_id++;
      self->encode_params.frame_num = 0;
      self->encode_params.nalu_type = 5;
      self->encode_params.nal_reference_idc = 1;
      break;
    case GstH264Inter:
    default:
      self->encode_params.slice_type = V4L2_H264_SLICE_TYPE_P;
      self->encode_params.nalu_type = V4L2_H264_NAL_CODED_SLICE_NON_IDR_PIC;
      self->encode_params.reference_ts = self->reference_timestamp;
      self->encode_params.frame_num++;
      self->encode_params.frame_num %=
          (1 << (self->sps.log2_max_frame_num_minus4 + 4));
      self->encode_params.nalu_type = 1;
      self->encode_params.nal_reference_idc = 2;
      break;
  }

  if (self->cabac) {
    self->encode_params.flags |= V4L2_H264_ENCODE_FLAG_ENTROPY_CABAC;
  } else {
    self->encode_params.flags &= ~V4L2_H264_ENCODE_FLAG_ENTROPY_CABAC;
  }

  self->encode_params.pic_parameter_set_id = 0;
  self->encode_params.cabac_init_idc = self->cabac_init_idc;

  self->encode_params.pic_init_qp_minus26 = self->pps.pic_init_qp_minus26;
  self->encode_params.chroma_qp_index_offset = self->pps.chroma_qp_index_offset;
  self->encode_params.disable_deblocking_filter_idc = 0;
}

static void
gst_v4l2_codec_h264_enc_fill_encode_rc (GstH264Encoder * encoder,
    GstH264Frame * h264_frame)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);

  /* Rate Control */
  self->encode_rc.qp = self->pps.pic_init_qp_minus26 + 26;      //encoder->rc.qp;
  self->encode_rc.qp_min = self->qp_min;
  self->encode_rc.qp_max = self->qp_max;
}

static guint
gst_v4l2_codec_h264_enc_check_qp_range (GstV4l2CodecH264Enc * self,
    GstH264Frame * h264_frame)
{
  if (h264_frame->quality > self->qp_max)
    return self->qp_max;
  if (h264_frame->quality < self->qp_min)
    return self->qp_min;

  return h264_frame->quality;
}

static GstFlowReturn
gst_v4l2_codec_h264_enc_encode_frame (GstH264Encoder * encoder,
    GstH264Frame * h264_frame)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (encoder);
  GstVideoEncoder *venc = GST_VIDEO_ENCODER (encoder);
  GstV4l2Request *request = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstVideoCodecFrame *frame = h264_frame->frame;
  GstBuffer *codec_data = NULL;
  GstBuffer *resized_buffer;
  guint32 bytesused;
  guint data_size;
  guint32 flags;

  if (self->first_frame) {
    codec_data = gst_buffer_new_and_alloc (38 + SPS_SIZE + PPS_SIZE);
    if (!gst_v4l2_codec_h264_enc_set_codec_data (venc, codec_data, &data_size)) {
      GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
          ("Failed to create sps/pps buffer."), (NULL));
      gst_buffer_unref (codec_data);
      goto done;
    }
    gst_buffer_resize (codec_data, 0, data_size);
  }

  struct v4l2_ext_control control[] = {
    /* *INDENT-OFF* */
    {
      .id = V4L2_CID_STATELESS_H264_ENCODE_PARAMS,
      .ptr = &self->encode_params,
      .size = sizeof (self->encode_params),
    }, {
      .id = V4L2_CID_STATELESS_H264_ENCODE_RC,
      .ptr = &self->encode_rc,
      .size = sizeof (self->encode_rc),
    },
    /* *INDENT-ON* */
  };

  GST_DEBUG_OBJECT (self, "encode h264 frame with quality = %d",
      h264_frame->quality);

  if (!gst_v4l2_codec_h264_enc_ensure_output_bitstream (self, frame)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Failed to allocate output buffer."), (NULL));
    goto done;
  }

  request = gst_v4l2_encoder_alloc_request (self->encoder,
      frame->system_frame_number, frame->input_buffer, frame->output_buffer);

  if (!request) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Failed to allocate a media request object."), (NULL));
    goto done;
  }

  gst_v4l2_codec_h264_enc_fill_encode_params (encoder, h264_frame);

  gst_v4l2_codec_h264_enc_fill_encode_rc (encoder, h264_frame);

  if (!gst_v4l2_encoder_set_controls (self->encoder, request, control,
          G_N_ELEMENTS (control))) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not accept the control parameters."), (NULL));
    goto done;
  }

  if (!gst_v4l2_encoder_request_queue (request, 0)) {
    if (!gst_v4l2_codec_h264_enc_copy_input_buffer (self, frame)) {
      GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
          ("Failed to allocate/copy input buffer."), (NULL));
      goto done;
    }

    gst_v4l2_encoder_request_replace_pic_buf (request, frame->input_buffer);

    if (!gst_v4l2_encoder_request_queue (request, 0)) {
      GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
          ("Driver did not accept the encode request."), (NULL));
      goto done;
    }
  }

  if (!gst_v4l2_encoder_request_set_done (request, &bytesused, &flags)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not ack the request."), (NULL));
    goto done;
  }

  gst_v4l2_encoder_request_unref (request);

  resized_buffer = gst_buffer_copy_region (frame->output_buffer,
      GST_BUFFER_COPY_MEMORY | GST_BUFFER_COPY_DEEP, 0, bytesused);
  if (self->first_frame) {
    gst_buffer_append (codec_data, resized_buffer);
    gst_buffer_replace (&frame->output_buffer, codec_data);
    gst_buffer_unref (codec_data);
    self->first_frame = FALSE;
  } else {
    gst_buffer_replace (&frame->output_buffer, resized_buffer);
    gst_buffer_unref (resized_buffer);
  }

  /* save last reference frame */
  self->reference_timestamp = (guint64) frame->system_frame_number * 1000;

  return gst_video_encoder_finish_frame (venc, frame);

done:
  if (request)
    gst_v4l2_encoder_request_unref (request);

  return ret;
}

static void
gst_v4l2_codec_h264_enc_init (GstV4l2CodecH264Enc * self)
{
}

static void
gst_v4l2_codec_h264_enc_subinit (GstV4l2CodecH264Enc * self,
    GstV4l2CodecH264EncClass * klass)
{
  self->encoder = gst_v4l2_encoder_new (klass->device);
}

static void
gst_v4l2_codec_h264_enc_dispose (GObject * object)
{
  GstV4l2CodecH264Enc *self = GST_V4L2_CODEC_H264_ENC (object);

  g_clear_object (&self->encoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_codec_h264_enc_class_init (GstV4l2CodecH264EncClass * klass)
{
}

static void
gst_v4l2_codec_h264_enc_subclass_init (GstV4l2CodecH264EncClass * klass,
    GstV4l2CodecDevice * device)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstH264EncoderClass *h264encoder_class = GST_H264_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_v4l2_codec_h264_enc_set_property;
  gobject_class->get_property = gst_v4l2_codec_h264_enc_get_property;
  gobject_class->dispose = gst_v4l2_codec_h264_enc_dispose;

  gst_element_class_set_static_metadata (element_class,
      "V4L2 Stateless H264 Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "A V4L2 based H264 video encoder",
      "Benjamin Gaignard <benjamin.gaignard@collabora.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_change_state);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_close);
  encoder_class->start = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_stop);
  encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_set_format);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_flush);
  encoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_sink_event);
  encoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_getcaps);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_propose_allocation);
  h264encoder_class->encode_frame =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_enc_encode_frame);

  klass->device = device;
  gst_v4l2_encoder_install_properties (gobject_class, PROP_LAST, device);
}

void
gst_v4l2_codec_h264_enc_register (GstPlugin * plugin, GstV4l2Encoder * encoder,
    GstV4l2CodecDevice * device, guint rank)
{
  gchar *element_name;
  guint version;

  GST_DEBUG_CATEGORY_INIT (v4l2_h264enc_debug, "v4l2codecs-h264enc", 0,
      "V4L2 stateless H264 encoder");

  version = gst_v4l2_encoder_get_version (encoder);
  if (version < V4L2_MIN_KERNEL_VERSION)
    GST_WARNING ("V4L2 API v%u.%u too old, at least v%u.%u required",
        (version >> 16) & 0xff, (version >> 8) & 0xff,
        V4L2_MIN_KERNEL_VER_MAJOR, V4L2_MIN_KERNEL_VER_MINOR);

  if (!gst_v4l2_codec_h264_enc_api_check (encoder)) {
    GST_WARNING ("Not registering H264 encoder as it failed ABI check.");
    return;
  }

  gst_v4l2_encoder_register (plugin, GST_TYPE_V4L2_CODEC_H264_ENC,
      (GClassInitFunc) gst_v4l2_codec_h264_enc_subclass_init,
      gst_mini_object_ref (GST_MINI_OBJECT (device)),
      (GInstanceInitFunc) gst_v4l2_codec_h264_enc_subinit,
      "v4l2sl%sh264enc", device, rank, &element_name);
}

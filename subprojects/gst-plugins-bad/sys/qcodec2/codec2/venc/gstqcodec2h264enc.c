// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqcodec2h264enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_venc_debug);
#define GST_CAT_DEFAULT gst_qcodec2_venc_debug

/* class initialization */
G_DEFINE_TYPE (GstQcodec2H264Enc, gst_qcodec2_h264_enc, GST_TYPE_QCODEC2_VENC);

#define DEFAULT_AVC_PROFILE C2W_AVC_PROFILE_HIGH

static gboolean gst_qcodec2_h264_enc_set_format (GstQcodec2Venc * encoder,
    GstVideoCodecState * state);

#define GST_QC2_H264_ENC_SINK_TEMPLATE_CAP \
    GST_QC2VENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"NV12",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE("NV12",128,8192)

static GstStaticPadTemplate gst_qcodec2_h264_enc_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_QC2_H264_ENC_SINK_TEMPLATE_CAP));

static GstStaticPadTemplate gst_qcodec2_h264_enc_src_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"));

static void
gst_qcodec2_h264_enc_class_init (GstQcodec2H264EncClass * klass)
{
  GstQcodec2VencClass *videoenc_class = GST_QCODEC2_VENC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_h264_enc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_h264_enc_src_template));

  videoenc_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qcodec2_h264_enc_set_format);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video H.264 encoder", "Encoder/Video",
      "Video H.264 Encoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_h264_enc_init (GstQcodec2H264Enc * self)
{
}

static const ProfileMapping h264_profiles[] = {
  {"baseline", C2W_AVC_PROFILE_BASELINE},
  {"constrained-baseline", C2W_AVC_PROFILE_CONSTRAINT_BASELINE},
  {"main", C2W_AVC_PROFILE_MAIN},
  {"high", C2W_AVC_PROFILE_HIGH},
  {"constrained-high", C2W_AVC_PROFILE_CONSTRAINT_HIGH},
};

static C2W_PROFILE_T
gst_qcodec2_h264_get_profile_from_str (const gchar * profile)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (h264_profiles); i++) {
    if (g_str_equal (profile, h264_profiles[i].profile))
      return h264_profiles[i].e;
  }
  return C2W_PROFILE_UNSPECIFIED;
}

static const LevelMapping h264_levels[] = {
  {"1", C2W_AVC_LEVEL_1},
  {"1b", C2W_AVC_LEVEL_1b},
  {"1.1", C2W_AVC_LEVEL_11},
  {"1.2", C2W_AVC_LEVEL_12},
  {"1.3", C2W_AVC_LEVEL_13},
  {"2", C2W_AVC_LEVEL_2},
  {"2.1", C2W_AVC_LEVEL_21},
  {"2.2", C2W_AVC_LEVEL_22},
  {"3", C2W_AVC_LEVEL_3},
  {"3.1", C2W_AVC_LEVEL_31},
  {"3.2", C2W_AVC_LEVEL_32},
  {"4", C2W_AVC_LEVEL_4},
  {"4.1", C2W_AVC_LEVEL_41},
  {"4.2", C2W_AVC_LEVEL_42},
  {"5", C2W_AVC_LEVEL_5},
  {"5.1", C2W_AVC_LEVEL_51},
  {"5.2", C2W_AVC_LEVEL_52},
  {"6", C2W_AVC_LEVEL_6},
  {"6.1", C2W_AVC_LEVEL_61},
  {"6.2", C2W_AVC_LEVEL_62},
};

static C2W_LEVEL_T
gst_qcodec2_h264_get_level_from_str (const gchar * level)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (h264_levels); i++) {
    if (g_str_equal (level, h264_levels[i].level))
      return h264_levels[i].e;
  }
  return C2W_LEVEL_UNSPECIFIED;
}

static gboolean
gst_qcodec2_h264_enc_set_format (GstQcodec2Venc * encoder,
    GstVideoCodecState * state)
{
  GstQcodec2H264Enc *enc = GST_QCODEC2_H264_ENC (encoder);
  GPtrArray *config = NULL;
  ConfigParams profile_level;
  C2W_PROFILE_T profile = C2W_PROFILE_UNSPECIFIED;
  C2W_LEVEL_T level = C2W_LEVEL_UNSPECIFIED;
  GstCaps *output_caps;
  const gchar *profile_string, *level_string;

  /* Set profile and level */
  output_caps = encoder->output_state->caps;
  if (output_caps) {
    GST_INFO_OBJECT (enc, "output state caps: %" GST_PTR_FORMAT, output_caps);
    GstStructure *s;
    if (gst_caps_is_empty (output_caps)) {
      GST_ERROR_OBJECT (enc, "Empty caps");
      return FALSE;
    }
    s = gst_caps_get_structure (output_caps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      profile = gst_qcodec2_h264_get_profile_from_str (profile_string);
      if (profile == C2W_PROFILE_UNSPECIFIED)
        goto unsupported_profile;
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      level = gst_qcodec2_h264_get_level_from_str (level_string);
      if (level == C2W_LEVEL_UNSPECIFIED)
        goto unsupported_level;
    }
  }

  config = g_ptr_array_new ();

  if (config) {
    /* For profile and level settings, there are 4 cases here:
     * 1. If profile and level are all specified, the values will be set to driver.
     * 2. If profile is set but level is unspecified, the specified profile will be
     *    set to driver and the level will use a default value accordingly.
     * 3. If level is set but profile is unspecified, this case is not allowed in
     *    C2 HAL. Need to use the DEFAULT_AVC_PROFILE.
     * 4. If profile and level are all unspecified, the encoded stream will have
     *    default profile and level values accordingly. */
    if (profile != C2W_PROFILE_UNSPECIFIED || level != C2W_LEVEL_UNSPECIFIED) {
      if (profile == C2W_PROFILE_UNSPECIFIED && level != C2W_LEVEL_UNSPECIFIED) {
        profile = DEFAULT_AVC_PROFILE;
      }
      profile_level = make_profile_level_param (profile, level);
      g_ptr_array_add (config, &profile_level);
    }

    if (config->len && !c2componentInterface_config (encoder->comp_intf,
            config, BLOCK_MODE_MAY_BLOCK)) {
      GST_WARNING_OBJECT (encoder,
          "Failed to set encoder config for profile(%d)/level(%d)", profile,
          level);
    }

    g_ptr_array_free (config, TRUE);
  }

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (enc, "Unsupported profile %s", profile_string);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (enc, "Unsupported level %s", level_string);
  return FALSE;
}

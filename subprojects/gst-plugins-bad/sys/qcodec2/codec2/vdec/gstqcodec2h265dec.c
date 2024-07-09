// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqcodec2h265dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

static gboolean gst_qcodec2_h265_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQcodec2H265Dec, gst_qcodec2_h265_dec, GST_TYPE_QCODEC2_VDEC);

static GstStaticPadTemplate gst_qcodec2_h265_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H265_CAPS));

static void
gst_qcodec2_h265_dec_class_init (GstQcodec2H265DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQcodec2VdecClass *qcodec2vdec_class = GST_QCODEC2_VDEC_CLASS (klass);

  qcodec2vdec_class->set_format = gst_qcodec2_h265_dec_set_format;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_h265_dec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video H.265 decoder", "Decoder/Video",
      "Video H.265 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_h265_dec_init (GstQcodec2H265Dec * self)
{
}

static gboolean
gst_qcodec2_h265_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2Vdec *base_dec = decoder;
  GstQcodec2H265Dec *dec = GST_QCODEC2_H265_DEC (decoder);
  GstStructure *structure = NULL;
  GPtrArray *config = NULL;
  const gchar *profile_string = NULL;
  gboolean is_10bit = FALSE;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "H265 dec set format");

  structure = gst_caps_get_structure (state->caps, 0);
  profile_string = gst_structure_get_string (structure, "profile");
  if (!profile_string) {
    GST_DEBUG_OBJECT (dec, "no profile field in caps");
  } else {
    GST_DEBUG_OBJECT (dec, "profile:%s", profile_string);
    if (!g_strcmp0 (profile_string, "main-10")) {
      is_10bit = TRUE;
      GST_DEBUG_OBJECT (dec, "10bit output");
    }
  }

  if (is_10bit) {
    if (base_dec->is_ubwc)
      output_format = GST_VIDEO_FORMAT_NV12_10LE32;
    else
      output_format = GST_VIDEO_FORMAT_P010_10LE;
  }

  config = g_ptr_array_new ();
  if (config) {
    pixelformat =
        make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
            output_format), FALSE);
    GST_LOG_OBJECT (dec, "set c2 output format: %d for H265",
        pixelformat.pixelFormat.fmt);
    g_ptr_array_add (config, &pixelformat);
    if (!c2componentInterface_config (base_dec->comp_intf,
            config, BLOCK_MODE_MAY_BLOCK)) {
      GST_ERROR_OBJECT (dec, "Failed to set config");
      ret = FALSE;
    }
    g_ptr_array_free (config, TRUE);
  }

  base_dec->output_format = output_format;

  return ret;
}

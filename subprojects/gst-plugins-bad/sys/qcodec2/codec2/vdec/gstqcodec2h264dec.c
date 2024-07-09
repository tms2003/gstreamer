// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqcodec2h264dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

static void gst_qcodec2_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qcodec2_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_qcodec2_h264_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state);

enum
{
  PROP_0,
  PROP_DEINTERLACE,
};

/* class initialization */
G_DEFINE_TYPE (GstQcodec2H264Dec, gst_qcodec2_h264_dec, GST_TYPE_QCODEC2_VDEC);

static GstStaticPadTemplate gst_qcodec2_h264_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS));

static void
gst_qcodec2_h264_dec_class_init (GstQcodec2H264DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQcodec2VdecClass *videodec_class = GST_QCODEC2_VDEC_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_h264_dec_sink_template));

  gobject_class->set_property = gst_qcodec2_h264_dec_set_property;
  gobject_class->get_property = gst_qcodec2_h264_dec_get_property;

  videodec_class->set_format = gst_qcodec2_h264_dec_set_format;

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE,
      g_param_spec_boolean ("deinterlace", "enable deinterlace in Codec2",
          "enable deinterlace in Codec2 (TRUE=default, "
          "1: enable deinterlace, 0: disable deinterlace)",
          DEFAULT_DEINTERLACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video H.264 decoder", "Decoder/Video",
      "Video H.264 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_h264_dec_init (GstQcodec2H264Dec * self)
{
}

static void
gst_qcodec2_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (object);

  switch (prop_id) {
    case PROP_DEINTERLACE:
      base_dec->deinterlace = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qcodec2_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (object);

  switch (prop_id) {
    case PROP_DEINTERLACE:
      g_value_set_boolean (value, base_dec->deinterlace);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qcodec2_h264_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2H264Dec *dec = GST_QCODEC2_H264_DEC (decoder);
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (decoder);
  gboolean result = TRUE;
  ConfigParams deinterlace;
  ConfigParams pixel_format;
  GPtrArray *config = NULL;

  config = g_ptr_array_new ();

  if (config) {
    pixel_format =
        make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
            base_dec->output_format), FALSE);
    GST_LOG_OBJECT (dec, "set c2 output format: %d for H264",
        pixel_format.pixelFormat.fmt);
    g_ptr_array_add (config, &pixel_format);

#ifdef GST_SUPPORT_INTERLACE
    deinterlace = make_deinterlace_param (base_dec->deinterlace);
    GST_DEBUG_OBJECT (dec, "set deinterlace param");

    g_ptr_array_add (config, &deinterlace);
#endif

    if (!c2componentInterface_config (decoder->comp_intf,
            config, BLOCK_MODE_MAY_BLOCK)) {
      result = FALSE;
      GST_ERROR_OBJECT (dec, "Failed to set config");
      goto out;
    }
  }

out:
  if (config)
    g_ptr_array_free (config, TRUE);

  return result;
}

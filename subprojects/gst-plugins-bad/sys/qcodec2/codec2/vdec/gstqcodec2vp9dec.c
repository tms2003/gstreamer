// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqcodec2vp9dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

static gboolean gst_qcodec2_vp9_dec_handle_frame (GstQcodec2Vdec * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_qcodec2_vp9_dec_open (GstQcodec2Vdec * decoder);
static gboolean gst_qcodec2_vp9_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQcodec2VP9Dec, gst_qcodec2_vp9_dec, GST_TYPE_QCODEC2_VDEC);

static GstStaticPadTemplate gst_qcodec2_vp9_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VP9_CAPS));

static void
gst_qcodec2_vp9_dec_class_init (GstQcodec2VP9DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_vp9_dec_sink_template));
  GstQcodec2VdecClass *qcodec2vdec_class = GST_QCODEC2_VDEC_CLASS (klass);

  qcodec2vdec_class->handle_frame = gst_qcodec2_vp9_dec_handle_frame;
  qcodec2vdec_class->set_format = gst_qcodec2_vp9_dec_set_format;
  qcodec2vdec_class->open = gst_qcodec2_vp9_dec_open;

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video VP9 decoder", "Decoder/Video",
      "Video VP9 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_vp9_dec_init (GstQcodec2VP9Dec * self)
{
}

static gboolean
gst_qcodec2_vp9_dec_open (GstQcodec2Vdec * decoder)
{
  GstQcodec2Vdec *base_dec = decoder;
  GstQcodec2VP9Dec *self = GST_QCODEC2_VP9_DEC (decoder);
  /* start C2 component later since checking VP9 10bit format */
  base_dec->delay_start = TRUE;
  self->check_vp9_10bit = TRUE;

  return TRUE;
}

static gboolean
gst_qcodec2_vp9_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2Vdec *base_dec = decoder;
  GstQcodec2VP9Dec *dec = GST_QCODEC2_VP9_DEC (decoder);
  GstStructure *s = NULL;
  guint bit_depth_luma, bit_depth_chroma;
  GPtrArray *config = NULL;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "VP9 dec set format");

  /* check VP9 10bit case 1: bit-depth-luma in caps , it supported since GST 1.20
   * or it added in caps explicitly by upstream element in secure mode*/
  if (dec->check_vp9_10bit) {
    GST_DEBUG_OBJECT (dec, "check whether field bit-depth-luma in caps");
    s = gst_caps_get_structure (state->caps, 0);
    if (s && gst_structure_get_uint (s, "bit-depth-luma", &bit_depth_luma) &&
        gst_structure_get_uint (s, "bit-depth-chroma", &bit_depth_chroma)) {
      if (bit_depth_luma == 10 && bit_depth_chroma == 10) {
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
        GST_LOG_OBJECT (dec, "set c2 output format: %d for VP9",
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
      /* disable checking and delay_start since bit-depth-chroma parsed */
      dec->check_vp9_10bit = FALSE;
      base_dec->delay_start = FALSE;
    }
  }

  return ret;
}

static gboolean
gst_qcodec2_vp9_dec_handle_frame (GstQcodec2Vdec * decoder,
    GstVideoCodecFrame * frame)
{
  GstQcodec2Vdec *base_dec = decoder;
  GstQcodec2VP9Dec *dec = GST_QCODEC2_VP9_DEC (decoder);
  gboolean ret = TRUE;
  GstMapInfo mapinfo = { 0, };
  GstBuffer *buf = NULL;
  GstVp9Parser *vp9_parser = NULL;
  GstVp9FrameHdr *vp9_hdr = NULL;
  GPtrArray *config = NULL;
  guint8 *frame_data = NULL;
  gsize frame_size = 0;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;

  GST_DEBUG_OBJECT (dec, "VP9 dec handle frame");

  /* check VP9 10bit case 2: no field bit-depth-luma in caps, parse it here in non-secure mode */
  if (dec->check_vp9_10bit && !base_dec->secure) {
    GST_DEBUG_OBJECT (dec,
        "check VP9 10bit if without field bit-depth-luma in caps");
    vp9_parser = gst_vp9_parser_new ();
    vp9_hdr = g_slice_new0 (GstVp9FrameHdr);
    config = g_ptr_array_new ();
    buf = frame->input_buffer;
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    frame_data = mapinfo.data;
    frame_size = mapinfo.size;
    if (vp9_parser && vp9_hdr && config) {
      gst_vp9_parser_parse_frame_header (vp9_parser, vp9_hdr, frame_data,
          frame_size);
    } else {
      GST_ERROR_OBJECT (dec, "failed to new some structure");
      gst_buffer_unmap (buf, &mapinfo);
      ret = FALSE;
      goto done;
    }
    gst_buffer_unmap (buf, &mapinfo);

    if (vp9_parser->bit_depth == GST_VP9_BIT_DEPTH_10) {
      if (base_dec->is_ubwc)
        output_format = GST_VIDEO_FORMAT_NV12_10LE32;
      else
        output_format = GST_VIDEO_FORMAT_P010_10LE;

      base_dec->output_format = output_format;

      GST_LOG_OBJECT (dec,
          "output width: %d, height: %d, format: %d (%s) for VP9",
          base_dec->width, base_dec->height, output_format,
          gst_video_format_to_string (output_format));
    }

    if (config) {
      pixelformat =
          make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
              output_format), FALSE);
      GST_LOG_OBJECT (dec, "set c2 output format: %d for VP9",
          pixelformat.pixelFormat.fmt);
      g_ptr_array_add (config, &pixelformat);
      if (!c2componentInterface_config (base_dec->comp_intf,
              config, BLOCK_MODE_MAY_BLOCK)) {
        GST_ERROR_OBJECT (dec, "Failed to set config");
        ret = FALSE;
        goto done;
      }
    }

    dec->check_vp9_10bit = FALSE;
  }

  if (base_dec->delay_start) {
    if (!gst_qcodec2_vdec_start_comp_and_config_pool (base_dec)) {
      GST_ERROR_OBJECT (dec, "failed to start c2 comp or config pool");
      ret = FALSE;
      goto done;
    }
    base_dec->delay_start = FALSE;
  }

done:
  if (config)
    g_ptr_array_free (config, TRUE);
  if (vp9_hdr)
    g_slice_free (GstVp9FrameHdr, vp9_hdr);
  if (vp9_parser)
    gst_vp9_parser_free (vp9_parser);

  return ret;
}

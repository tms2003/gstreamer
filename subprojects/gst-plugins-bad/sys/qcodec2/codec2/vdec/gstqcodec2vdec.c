/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gstqcodec2vdec.h"
#include "gstqcodec2bufferpool.h"
#include <dlfcn.h>
#include <libdrm/drm_fourcc.h>
#include "gstqcodec2h264dec.h"
#include "gstqcodec2h265dec.h"
#include "gstqcodec2vp9dec.h"
#include "gstqcodec2mpeg2dec.h"

GST_DEBUG_CATEGORY (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

/* class initialization */
G_DEFINE_TYPE (GstQcodec2Vdec, gst_qcodec2_vdec, GST_TYPE_VIDEO_DECODER);

#define parent_class gst_qcodec2_vdec_parent_class
#define NANO_TO_MILLI(x)  ((x) / 1000)
#define EOS_WAITING_TIMEOUT 5
#define QCODEC2_MIN_OUTBUFFERS 6
#define QCODEC2_MAX_OUTBUFFERS 32
#define EXT_BUF_WAIT_TIMEOUT_MS 500

#define DEFAULT_OUTPUT_PICTURE_ORDER_MODE    (0xffffffff)
#define DEFAULT_LOW_LATENCY_MODE             (FALSE)
#define DEFAULT_SECURE_MODE                  (FALSE)

/* Function will be named gst_fbuf_modifier_qdata_quark() */
static G_DEFINE_QUARK (FBufModifierQuark, gst_fbuf_modifier_qdata);


enum
{
  PROP_0,
  PROP_SILENT,
  PROP_OUTPUT_PICTURE_ORDER,
  PROP_LOW_LATENCY,
  PROP_SECURE,
  PROP_DATA_COPY_FUNTION,
  PROP_DATA_COPY_FUNTION_PARAM,
  PROP_USE_EXTERNAL_POOL,
};

/* GstVideoDecoder base class method */
static gboolean gst_qcodec2_vdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_qcodec2_vdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qcodec2_vdec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_qcodec2_vdec_flush (GstVideoDecoder * decoder);
static gboolean gst_qcodec2_vdec_open (GstVideoDecoder * decoder);
static gboolean gst_qcodec2_vdec_close (GstVideoDecoder * decoder);
static gboolean gst_qcodec2_vdec_stop (GstVideoDecoder * decoder);
static gboolean gst_qcodec2_vdec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_qcodec2_vdec_src_event (GstVideoDecoder * decoder,
    GstEvent * event);
static void gst_qcodec2_vdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qcodec2_vdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_qcodec2_vdec_finalize (GObject * object);

static gboolean gst_qcodec2_vdec_create_component (GstVideoDecoder * decoder);
static gboolean gst_qcodec2_vdec_destroy_component (GstVideoDecoder * decoder);
static void handle_video_event (const void *handle, EVENT_TYPE type,
    void *data);

static GstFlowReturn gst_qcodec2_vdec_decode (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qcodec2_vdec_setup_output (GstVideoDecoder * decoder);
static GstBuffer *gst_qcodec2_vdec_wrap_output_buffer (GstVideoDecoder *
    decoder, BufferDescriptor * buffer);
static gboolean gst_qcodec2_vdec_caps_has_feature (const GstCaps * caps,
    const gchar * partten);

/* pad templates */
static GstStaticPadTemplate gst_vdec_src_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QCODEC2_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ NV12 }")
        ";" QCODEC2_VDEC_RAW_CAPS ("{ NV12 }")
        ";" QCODEC2_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ NV12_10LE32 }")
        ";" QCODEC2_VDEC_RAW_CAPS ("{ NV12_10LE32 }")
        ";" QCODEC2_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ P010_10LE }")
        ";" QCODEC2_VDEC_RAW_CAPS ("{ P010_10LE }")));

static gboolean
_unfixed_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  gchar *string = NULL;
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  for (gint i = 0; i < count; i++) {
    structure = gst_caps_get_structure (caps, i);
    string =
        gst_structure_has_field (structure,
        "compression") ? gst_structure_to_string (structure) : NULL;
    if (string && g_strrstr (string, compression)) {
      ret = TRUE;
    }
    g_free (string);

    if (ret == TRUE) {
      break;
    }
  }

  return ret;
}

static void
modifier_free (gpointer p_modifier)
{
  if (p_modifier) {
    g_slice_free (guint64, p_modifier);
    GST_DEBUG ("modifier_free(%p) val 0x%lx called", p_modifier,
        *(guint64 *) p_modifier);
  } else {
    GST_ERROR ("invalid modifier");
  }

  return;
}

static ConfigParams
make_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_RESOLUTION;
  param.isInput = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

ConfigParams
make_pixel_format_param (guint32 fmt, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_PIXELFORMAT;
  param.isInput = is_input;
  param.pixelFormat.fmt = fmt;

  return param;
}

static ConfigParams
make_interlace_param (INTERLACE_MODE_TYPE mode, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTERLACE_INFO;
  param.isInput = is_input;
  param.interlaceMode.type = mode;

  return param;
}

static ConfigParams
make_output_picture_order_param (guint output_picture_order_mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE;
  param.output_picture_order_mode = output_picture_order_mode;

  return param;
}

static ConfigParams
make_low_latency_param (gboolean low_latency_mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY;
  param.low_latency_mode = low_latency_mode;

  return param;
}

ConfigParams
make_deinterlace_param (gboolean deinterlace)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DEINTERLACE;
  param.deinterlace = deinterlace;

  return param;
}

static gchar *
get_c2_comp_name (GstVideoDecoder * decoder, GstStructure * s,
    gboolean low_latency)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  gchar *str = NULL;
  gchar *concat_str = NULL;
  gchar *str_low_latency = g_strdup (".low_latency");
  gchar *str_secure = g_strdup (".secure");
  gchar *str_suffix = NULL;
  gboolean supported = FALSE;
  gboolean secure = dec->secure;
  gint mpegversion = 0;

  if (gst_structure_has_name (s, "video/x-h264")) {
    str = g_strdup ("c2.qti.avc.decoder");
  } else if (gst_structure_has_name (s, "video/x-h265")) {
    str = g_strdup ("c2.qti.hevc.decoder");
  } else if (gst_structure_has_name (s, "video/x-vp8")) {
    str = g_strdup ("c2.qti.vp8.decoder");
  } else if (gst_structure_has_name (s, "video/x-vp9")) {
    str = g_strdup ("c2.qti.vp9.decoder");
  } else if (gst_structure_has_name (s, "video/mpeg")) {
    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      if (mpegversion == 2) {
        str = g_strdup ("c2.qti.mpeg2.decoder");
      }
    }
  }

  if (low_latency) {
    str_suffix = str_low_latency;
    GST_DEBUG_OBJECT (dec, "selected low latency component");
  }
  if (secure) {
    str_suffix = str_secure;
    GST_DEBUG_OBJECT (dec, "selected secure component");
  }

  if (low_latency || secure) {
    concat_str = g_strconcat (str, str_suffix, NULL);
    supported =
        c2componentStore_isComponentSupported (dec->comp_store, concat_str);

    if (supported) {
      if (str)
        g_free (str);
      str = concat_str;
    } else {
      g_free (concat_str);
    }
  }

  if (str_low_latency)
    g_free (str_low_latency);
  if (str_secure)
    g_free (str_secure);

  return str;
}

guint32
gst_to_c2_pixelformat (GstQcodec2Vdec * decoder, GstVideoFormat format)
{
  guint32 result = 0;
  GstQcodec2Vdec *dec = decoder;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (dec->is_ubwc) {
        result = PIXEL_FORMAT_NV12_UBWC;
      } else {
        result = PIXEL_FORMAT_NV12_LINEAR;
      }
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      result = PIXEL_FORMAT_TP10_UBWC;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      result = PIXEL_FORMAT_P010;
      break;
    default:
      result = PIXEL_FORMAT_NV12_UBWC;
      GST_WARNING_OBJECT (dec,
          "Invalid pixel format(%d), fallback to NV12 UBWC", format);
      break;
  }

  GST_DEBUG_OBJECT (dec, "to_c2_pixelformat (%s), c2 format: %d",
      gst_video_format_to_string (format), result);

  return result;
}

static gboolean
gst_qcodec2_vdec_create_component (GstVideoDecoder * decoder)
{
  gboolean ret = FALSE;
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "create component");

  if (dec->comp_store) {
    ret =
        c2componentStore_createComponent (dec->comp_store, dec->comp_name,
        &dec->comp, &dec->cb);
    if (ret == TRUE) {
      dec->comp_intf = c2component_intf (dec->comp);
      if (dec->comp_intf) {
        ret =
            c2component_setListener (dec->comp, decoder, handle_video_event,
            BLOCK_MODE_MAY_BLOCK);
        if (ret == TRUE) {
          ret =
              c2component_createBlockpool (dec->comp, BUFFER_POOL_BASIC_LINEAR);
          if (ret == FALSE) {
            GST_ERROR_OBJECT (dec, "Failed to create linear pool");
          }
        } else {
          GST_ERROR_OBJECT (dec, "Failed to set event handler");
        }
      } else {
        GST_ERROR_OBJECT (dec, "Failed to create interface");
      }
    } else {
      GST_ERROR_OBJECT (dec, "Failed to create component");
    }
  } else {
    GST_ERROR_OBJECT (dec, "Component store is Null");
  }

  return ret;
}

gboolean
gst_qcodec2_vdec_start_comp_and_config_pool (GstQcodec2Vdec * decoder)
{
  gboolean ret = TRUE;
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "start component and config pool");

  /* Start decoder */
  ret = c2component_start (dec->comp);
  if (ret == FALSE) {
    GST_ERROR_OBJECT (dec, "Failed to start component");
    return FALSE;
  }

  /* NOTICE: Config own graphic block pool should be called after c2 compoennt
   * started and before buffer queued. */
  ret = c2component_createBlockpool (dec->comp, BUFFER_POOL_BASIC_GRAPHIC);
  if (ret == FALSE) {
    GST_ERROR_OBJECT (dec, "Failed to create graphic pool");
    return FALSE;
  }

  /* let C2 component use graphic block pool created by client */
  ret = c2component_configBlockpool (dec->comp, BUFFER_POOL_BASIC_GRAPHIC);
  if (ret == FALSE) {
    GST_ERROR_OBJECT (dec,
        "Failed to let component use graphic pool created by client");
    return FALSE;
  }

  /* Set to use external pool */
  if (dec->use_external_buf) {
    ret = c2component_setUseExternalBuffer (dec->comp,
        BUFFER_POOL_BASIC_GRAPHIC, TRUE);
    if (ret == FALSE) {
      GST_ERROR_OBJECT (dec, "Failed to set component use external buffer");
      return FALSE;
    }
  }

  return ret;
}

static gboolean
gst_qcodec2_vdec_destroy_component (GstVideoDecoder * decoder)
{
  gboolean ret = TRUE;
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "destroy_component");

  if (dec->comp) {
    c2component_delete (dec->comp);
    dec->comp = NULL;
  }

  return ret;
}

static GstFlowReturn
gst_qcodec2_vdec_setup_output (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;

  GstCaps *templ_caps, *intersection = NULL;
  GstStructure *s;
  const gchar *format_str;

  /* Set decoder output format to NV12 by default */
  dec->output_state =
      gst_video_decoder_set_output_state (decoder,
      output_format, dec->width, dec->height, dec->input_state);

  /* state->caps should be NULL */
  if (dec->output_state->caps) {
    gst_caps_unref (dec->output_state->caps);
  }

  /* Fixate decoder output caps */
  templ_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  intersection =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder), templ_caps);
  gst_caps_unref (templ_caps);

  GST_DEBUG_OBJECT (dec, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (dec, "Empty caps");
    goto error_setup_output;
  }

  /* Secure mode only support UBWC output */
  dec->is_ubwc =
      _unfixed_caps_has_compression (intersection, "ubwc") | dec->secure;

  /* Fixate color format */
  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);
  GST_DEBUG_OBJECT (dec, "intersection caps: %" GST_PTR_FORMAT, intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  GST_DEBUG_OBJECT (dec, "Fixed color format:%s, UBWC:%d", format_str,
      dec->is_ubwc);

  if (!format_str || (output_format = gst_video_format_from_string (format_str))
      == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (dec, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (intersection);
    goto error_setup_output;
  }

  GST_DEBUG_OBJECT (dec,
      "Set decoder output state: color format: %d, width: %d, height: %d",
      output_format, dec->width, dec->height);

  /* Fill actual width/height into output caps */
  GValue g_width = { 0, };
  GValue g_height = { 0, };
  g_value_init (&g_width, G_TYPE_INT);
  g_value_set_int (&g_width, dec->width);

  g_value_init (&g_height, G_TYPE_INT);
  g_value_set_int (&g_height, dec->height);
  gst_caps_set_value (intersection, "width", &g_width);
  gst_caps_set_value (intersection, "height", &g_height);

  /* Check if fixed caps supports DMA buffer */
  if (gst_qcodec2_vdec_caps_has_feature (intersection,
          GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    dec->downstream_supports_dma = TRUE;
    GST_DEBUG_OBJECT (dec, "Downstream supports DMA buffer");
  }

  GST_INFO_OBJECT (dec, "DMA output feature is %s",
      (dec->downstream_supports_dma ? "enabled" : "disabled"));

  dec->output_state->caps = intersection;
  GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
      dec->output_state->caps);

  dec->output_format = output_format;

  GST_LOG_OBJECT (dec, "output width: %d, height: %d, format: %d(%s)",
      dec->width, dec->height, output_format,
      gst_video_format_to_string (output_format));


  GST_DEBUG_OBJECT (dec, "Complete setup output");

  return ret;

error_setup_output:
  return GST_FLOW_ERROR;
}

/* Dispatch any pending remaining data at EOS. Class can refuse to decode new data after. */
static GstFlowReturn
gst_qcodec2_vdec_finish (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  gint64 timeout;
  BufferDescriptor inBuf;

  GST_DEBUG_OBJECT (dec, "finish");

  memset (&inBuf, 0, sizeof (BufferDescriptor));
  inBuf.fd = -1;
  inBuf.data = NULL;
  inBuf.size = 0;
  inBuf.timestamp = 0;
  inBuf.index = dec->frame_index;
  inBuf.flag = FLAG_TYPE_END_OF_STREAM;
  inBuf.pool_type = BUFFER_POOL_BASIC_LINEAR;

  /* Setup EOS work */
  if (dec->comp) {
    /* Queue buffer to Codec2 */
    c2component_queue (dec->comp, &inBuf);
  }

  /* wait for all the pending buffers to return */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  g_mutex_lock (&dec->pending_lock);
  if (!dec->eos_reached) {
    GST_DEBUG_OBJECT (dec, "wait until EOS signal is triggered");

    timeout =
        g_get_monotonic_time () + (EOS_WAITING_TIMEOUT * G_TIME_SPAN_SECOND);
    if (!g_cond_wait_until (&dec->pending_cond, &dec->pending_lock, timeout)) {
      GST_ERROR_OBJECT (dec, "Timed out on wait, exiting!");
    }
  } else {
    GST_DEBUG_OBJECT (dec, "EOS reached on output, finish the decoding");
  }

  g_mutex_unlock (&dec->pending_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_qcodec2_vdec_flush (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (dec, "flush");

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  ret = c2component_flush (dec->comp, FLUSH_MODE_COMPONENT);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return ret;
}

/* Called to inform the caps describing input video data that decoder is about to receive.
  Might be called more than once, if changing input parameters require reconfiguration.*/
static gboolean
gst_qcodec2_vdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstQcodec2VdecClass *dec_class = GST_QCODEC2_VDEC_GET_CLASS (decoder);
  GstStructure *structure;
  const gchar *mode;
  gint retval = 0;
  gboolean ret = FALSE;
  gint width = 0;
  gint height = 0;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  INTERLACE_MODE_TYPE c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
  gchar *comp_name;
  GPtrArray *config = NULL;
  ConfigParams resolution;
  ConfigParams interlace;
  ConfigParams output_picture_order_mode;
  ConfigParams low_latency_mode;

  GST_DEBUG_OBJECT (dec, "set format caps:%" GST_PTR_FORMAT, state->caps);

  structure = gst_caps_get_structure (state->caps, 0);
  comp_name = get_c2_comp_name (decoder, structure, dec->low_latency_mode);
  if (!comp_name) {
    GST_ERROR_OBJECT (dec, "Failed to get relevant component name, caps:%"
        GST_PTR_FORMAT, state->caps);
    return FALSE;
  }

  retval = gst_structure_get_int (structure, "width", &width);
  retval &= gst_structure_get_int (structure, "height", &height);
  if (!retval) {
    goto error_res;
  }

  if (dec->input_setup) {
    /* Don't handle input format change here */
    goto done;
  }

  if ((mode = gst_structure_get_string (structure, "interlace-mode"))) {
    if (g_str_equal ("progressive", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
    } else if (g_str_equal ("interleaved", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("mixed", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("fields", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_FIELDS;
      c2interlace_mode = INTERLACE_MODE_FIELD_TOP_FIRST;
    }
  }

  dec->width = width;
  dec->height = height;
  dec->interlace_mode = interlace_mode;
  dec->comp_name = comp_name;

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
  }

  dec->input_state = gst_video_codec_state_ref (state);

  if (!gst_qcodec2_vdec_create_component (decoder)) {
    goto error_set_format;
  }

  config = g_ptr_array_new ();

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

#ifdef GST_SUPPORT_INTERLACE
  interlace = make_interlace_param (c2interlace_mode, FALSE);
  g_ptr_array_add (config, &interlace);
#endif

  if (dec->output_picture_order_mode != DEFAULT_OUTPUT_PICTURE_ORDER_MODE) {
    output_picture_order_mode =
        make_output_picture_order_param (dec->output_picture_order_mode);
    g_ptr_array_add (config, &output_picture_order_mode);
  }

  if (dec->low_latency_mode) {
    low_latency_mode = make_low_latency_param (dec->low_latency_mode);
    g_ptr_array_add (config, &low_latency_mode);
  }

  /* Negotiate with downstream and setup output */
  if (GST_FLOW_OK != gst_qcodec2_vdec_setup_output (decoder)) {
    g_ptr_array_free (config, TRUE);
    goto error_set_format;
  } else if (dec->use_external_buf) {
    if (!gst_video_decoder_negotiate (decoder)) {
      gst_video_codec_state_unref (dec->output_state);
      GST_ERROR_OBJECT (dec, "Failed to negotiate");
      goto error_set_format;
    }
    gst_pad_check_reconfigure (decoder->srcpad);

    dec->output_setup = TRUE;
  }

  if (!c2componentInterface_initReflectedParamUpdater (dec->comp_store,
          dec->comp_intf)) {
    GST_WARNING_OBJECT (dec, "Failed to init ReflectedParamUpdater");
  }

  if (!c2componentInterface_config (dec->comp_intf,
          config, BLOCK_MODE_MAY_BLOCK)) {
    GST_WARNING_OBJECT (dec, "Failed to set config");
  }

  g_ptr_array_free (config, TRUE);

  if (dec_class->set_format) {
    GST_DEBUG_OBJECT (dec, "Subclass set format");
    if (!dec_class->set_format (dec, state)) {
      GST_ERROR_OBJECT (dec, "Subclass failed to set format");
      goto error_set_format;
    }
  }

  if (!dec->delay_start) {
    ret = gst_qcodec2_vdec_start_comp_and_config_pool (dec);
    if (ret == FALSE) {
      GST_ERROR_OBJECT (dec, "failed to start component");
      goto error_set_format;
    }
  }

done:
  dec->input_setup = TRUE;
  return TRUE;

  /* Errors */
error_res:
  {
    GST_ERROR_OBJECT (dec, "Unable to get width/height value");
    return FALSE;
  }
error_set_format:
  {
    GST_ERROR_OBJECT (dec, "failed to setup input");
    return FALSE;
  }

  return TRUE;
}

/* Called when the element changes to GST_STATE_READY */
static gboolean
gst_qcodec2_vdec_open (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstQcodec2VdecClass *dec_class = GST_QCODEC2_VDEC_GET_CLASS (decoder);
  gboolean ret = TRUE;

  dec->input_setup = FALSE;
  dec->output_setup = FALSE;
  dec->eos_reached = FALSE;
  dec->frame_index = 0;
  dec->num_input_queued = 0;
  dec->num_output_done = 0;
  dec->downstream_supports_dma = FALSE;
  dec->comp = NULL;
  dec->comp_intf = NULL;
  dec->out_port_pool = NULL;
  dec->is_10bit = FALSE;
  dec->delay_start = FALSE;
  dec->buffer_table = NULL;
  dec->max_external_buf_cnt = QCODEC2_MIN_OUTBUFFERS;
  dec->acquired_external_buf = 0;

  memset (dec->queued_frame, 0, MAX_QUEUED_FRAME);
  memset (&dec->start_time, 0, sizeof (struct timeval));
  memset (&dec->first_frame_time, 0, sizeof (struct timeval));
  gettimeofday (&dec->start_time, NULL);

  GST_DEBUG_OBJECT (dec, "open");

  /* Create component store */
  dec->comp_store = c2componentStore_create ();

  if (dec_class->open) {
    GST_DEBUG_OBJECT (dec, "Subclass open");
    if (!dec_class->open (dec)) {
      GST_ERROR_OBJECT (dec, "Subclass failed to open");
      ret = FALSE;
    }
  }

  return ret;
}

static gboolean
gst_qcodec2_vdec_stop (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  /* Stop the component */
  if (dec->comp) {
    c2component_stop (dec->comp);
  }

  return TRUE;
}

/* Called when the element changes to GST_STATE_NULL */
static gboolean
gst_qcodec2_vdec_close (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");

  if (dec->out_port_pool) {
    GST_DEBUG_OBJECT (dec, "pool ref cnt:%d",
        GST_OBJECT_REFCOUNT (dec->out_port_pool));
    gst_object_unref (dec->out_port_pool);
  }

  if (!gst_qcodec2_vdec_destroy_component (decoder)) {
    GST_ERROR_OBJECT (dec, "Failed to delete component");
  }

  if (dec->comp_name) {
    g_free (dec->comp_name);
    dec->comp_name = NULL;
  }

  if (dec->comp_store) {
    c2componentStore_delete (dec->comp_store);
    dec->comp_store = NULL;
  }

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }

  if (dec->output_state) {
    gst_video_codec_state_unref (dec->output_state);
    dec->output_state = NULL;
  }

  if (dec->buffer_table) {
    g_hash_table_destroy (dec->buffer_table);
  }

  return TRUE;
}

static void
insert_external_buf_to_hashtable (GstVideoDecoder * decoder, gint fd,
    GstBuffer * buffer)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GHashTable *buf_table = dec->buffer_table;
  gint key = fd;
  GstBuffer *gst_buf = NULL;

  if (!buf_table) {
    GST_ERROR_OBJECT (dec, "Buffer hash table is NULL");
    return;
  }
  gst_buf = (GstBuffer *) g_hash_table_lookup (buf_table, &key);
  if (gst_buf) {
    GST_DEBUG_OBJECT (dec,
        "GstBuffer(%p) is already in hashtable, fd=%d", gst_buf, fd);
  } else {
    gint *buf_key = NULL;
    buf_key = g_malloc (sizeof (gint));
    *buf_key = key;
    g_hash_table_insert (buf_table, buf_key, buffer);
    GST_DEBUG_OBJECT (dec,
        "Insert buffer %p with buf_fd=%d to hashtable, table_size=%u",
        buffer, fd, g_hash_table_size (buf_table));
  }
}

static void
acquire_external_buf_callback (GstVideoDecoder * decoder)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstFlowReturn ret;
  GstBuffer *buffer = NULL;
  GstMemory *memory = NULL;
  gint fd = -1;
  gint64 timeout;
  gboolean acquired = FALSE;

  if (!dec->out_port_pool) {
    GST_ERROR_OBJECT (dec, "External pool is NULL");
    return;
  }
  while (!acquired) {
    g_mutex_lock (&dec->external_buf_lock);
    if (dec->acquired_external_buf < dec->max_external_buf_cnt) {
      GstBufferPoolAcquireParams params = { 0 };
      params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
      ret =
          gst_buffer_pool_acquire_buffer (dec->out_port_pool, &buffer, &params);
      if (buffer) {
        memory = gst_buffer_peek_memory (buffer, 0);
        if (memory) {
          fd = gst_dmabuf_memory_get_fd (memory);
          GST_DEBUG_OBJECT (dec,
              "Acquired external buffer fd: %d in buffer: %p from pool: %p", fd,
              buffer, dec->out_port_pool);

          /* Attach the fd to c2component */
          if (!c2component_attachExternalFd (dec->comp,
                  BUFFER_POOL_BASIC_GRAPHIC, fd)) {
            GST_ERROR_OBJECT (dec, "Failed to attach fd to Codec2");
          }
          /* Insert the corresponding gstbuffer to hashtable */
          insert_external_buf_to_hashtable (decoder, fd, buffer);

          acquired = TRUE;
          dec->acquired_external_buf++;
        }
      } else {
        GST_WARNING_OBJECT (dec,
            "Failed to acquire buffer from pool: %p with ret=%d",
            dec->out_port_pool, ret);
        g_mutex_unlock (&dec->external_buf_lock);
        break;
      }
    } else {
      GST_DEBUG_OBJECT (dec,
          "Waiting for external buffers, acquired_external_buf=%u, "
          "max_external_buf_cnt=%u", dec->acquired_external_buf,
          dec->max_external_buf_cnt);
      timeout =
          g_get_monotonic_time () +
          (EXT_BUF_WAIT_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
      if (!g_cond_wait_until (&dec->external_buf_cond, &dec->external_buf_lock,
              timeout)) {
        if (!dec->eos_reached) {
          dec->max_external_buf_cnt++;
          GST_WARNING_OBJECT (dec,
              "Timed out on wait for external buf! Updated "
              "max_external_buf_cnt to %u", dec->max_external_buf_cnt);
        }
        g_mutex_unlock (&dec->external_buf_lock);
        break;
      }
    }
    g_mutex_unlock (&dec->external_buf_lock);
  }
}

/* Called whenever a input frame from the upstream is sent to decoder */
static GstFlowReturn
gst_qcodec2_vdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstQcodec2VdecClass *dec_class = GST_QCODEC2_VDEC_GET_CLASS (decoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "handle_frame");

  g_return_val_if_fail (frame != NULL, GST_FLOW_ERROR);

  if (!dec->input_setup) {
    goto done;
  }

  GST_DEBUG_OBJECT (dec,
      "Frame number : %d, Distance from Sync : %d, Presentation timestamp : %"
      GST_TIME_FORMAT, frame->system_frame_number, frame->distance_from_sync,
      GST_TIME_ARGS (frame->pts));

  if (dec_class->handle_frame) {
    if (!dec_class->handle_frame (dec, frame)) {
      GST_ERROR_OBJECT (dec, "Subclass failed to handle format");
      ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  /* Decode frame */
  ret = gst_qcodec2_vdec_decode (decoder, frame);

done:
  gst_video_codec_frame_unref (frame);

  return ret;
}

static gboolean
gst_qcodec2_vdec_caps_has_feature (const GstCaps * caps, const gchar * partten)
{
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  if (count > 0) {
    for (gint i = 0; i < count; i++) {
      GstCapsFeatures *features = gst_caps_get_features (caps, i);
      if (gst_caps_features_is_any (features))
        continue;
      if (gst_caps_features_contains (features, partten)) {
        ret = TRUE;
        break;
      }
    }
  }

  return ret;
}

static gboolean
gst_qcodec2_vdec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstCaps *outcaps;
  GstStructure *config;
  guint size, min, max;
  gboolean update = FALSE;
  gboolean use_dmabuf = FALSE;
  gboolean use_peer_pool = FALSE;
  GstBufferPool *out_port_pool = NULL;
  GstBufferPoolInitParam param;
  memset (&param, 0, sizeof (GstBufferPoolInitParam));

  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "decide allocation");

  out_port_pool = dec->out_port_pool;

  GstAllocationParams params = { (GstMemoryFlags) 0 };
  GstBufferPool *pool = NULL;
  min = max = size = 0;

  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_DEBUG_OBJECT (dec, "allocation caps: %" GST_PTR_FORMAT, outcaps);
  GST_DEBUG_OBJECT (dec, "allocation params: %" GST_PTR_FORMAT, query);

  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    update = TRUE;
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (dec->use_external_buf) {
        use_peer_pool = TRUE;
        /* Create a hashtabe to store gstbuffer from external pool */
        if (!dec->buffer_table) {
          dec->buffer_table =
              g_hash_table_new_full (g_int_hash, g_int_equal, g_free, NULL);
        }
        GST_DEBUG_OBJECT (dec,
            "Use buffer pool from downstream, pool: %p, size: %u, "
            "min_buffers: %u, max_buffers: %u", pool, size, min, max);

        config = gst_buffer_pool_get_config (pool);

        min = MAX (min, QCODEC2_MIN_OUTBUFFERS);
        if (min > dec->max_external_buf_cnt) {
          dec->max_external_buf_cnt = min;
          GST_DEBUG_OBJECT (dec, "Updated the max_external_buf_cnt to %u",
              dec->max_external_buf_cnt);
        }
        max = MAX (MAX (min, max), QCODEC2_MAX_OUTBUFFERS);

        gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
        gst_buffer_pool_set_config (pool, config);
      } else {
        GST_DEBUG_OBJECT (dec, "ignore buffer pool from downstream");
        gst_object_unref (pool);
        pool = NULL;
      }
    } else if (dec->use_external_buf) {
      dec->use_external_buf = FALSE;
      GST_WARNING_OBJECT (dec, "Failed to parse downstream proposed pool, "
          "reset use_external_buf flag to false");
    }
  } else if (dec->use_external_buf) {
    dec->use_external_buf = FALSE;
    GST_WARNING_OBJECT (dec, "Downstream does not propose buffer pool, reset "
        "use_external_buf flag to false");
  }

  if (gst_qcodec2_vdec_caps_has_feature (outcaps,
          GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    use_dmabuf = TRUE;
    GST_INFO_OBJECT (dec, "downstream support DMA buffer");
  } else {
    GST_INFO_OBJECT (dec,
        "downstream don't support DMA buffer, use FD buffer instead");
    use_dmabuf = FALSE;
  }

  if (!use_peer_pool) {
    if (out_port_pool) {
      gst_object_unref (out_port_pool);
    }

    param.is_ubwc = dec->is_ubwc;
    param.info = dec->output_state->info;
    param.c2_comp = dec->comp;
    param.mode = use_dmabuf ? DMABUF_WRAP_MODE : FDBUF_WRAP_MODE;
    pool = gst_qcodec2_buffer_pool_new (&param);

    if (max)
      max = MAX (MAX (min, max), QCODEC2_MIN_OUTBUFFERS);

    min = MAX (min, QCODEC2_MIN_OUTBUFFERS);
    /* disable gst buffer pool's allocator, since actual buffer(underlying DMA/ION buffer)
     * is allocated inside of C2 allocator */
    size = 0;

    config = gst_buffer_pool_get_config (pool);
    if (gst_query_find_allocation_meta (query, GST_VIDEO_C2BUF_META_API_TYPE,
            NULL)) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_C2BUF_META);
      GST_DEBUG_OBJECT (dec, "add option video C2 buf meta");
    }

    GST_DEBUG_OBJECT (dec, "allocation: size:%u min:%u max:%u pool:%"
        GST_PTR_FORMAT, size, min, max, pool);

    gst_buffer_pool_config_set_params (config, outcaps, size, min, max);

    GST_DEBUG_OBJECT (dec, "setting own pool config to %" GST_PTR_FORMAT,
        config);

    /* configure own pool */
    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (dec, "configure our own buffer pool failed");
      goto cleanup;
    }

    /* For simplicity, simply read back the active configuration, so our base
     * class get the right information */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max);
    gst_structure_free (config);
  }

  GST_DEBUG_OBJECT (dec, "setting pool with size: %d, min: %d, max: %d",
      size, min, max);

  /* update pool info in the query */
  if (update) {
    GST_DEBUG_OBJECT (dec, "update buffer pool");
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  } else {
    GST_DEBUG_OBJECT (dec, "new buffer pool");
    gst_query_add_allocation_pool (query, pool, size, min, max);
  }

  dec->out_port_pool = pool;

  return TRUE;

cleanup:
  {
    if (pool) {
      gst_object_unref (pool);
    }
    return FALSE;
  }

}

static GstBuffer *
gst_qcodec2_vdec_wrap_output_buffer (GstVideoDecoder * decoder,
    BufferDescriptor * decode_buf)
{
  GstBuffer *out_buf = NULL;
  GstVideoCodecState *state;
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  guint output_size = decode_buf->size;
  GstBufferPoolAcquireParamsExt param_ext;
  guint64 *p_modifier = NULL;

  memset (&param_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));

  state = gst_video_decoder_get_output_state (decoder);
  if (!state) {
    GST_ERROR_OBJECT (dec, "Failed to get decoder output state");
    return NULL;
  }

  if (dec->use_external_buf) {
    GstBuffer *gst_buf = NULL;
    gint key = decode_buf->fd;
    gst_buf = (GstBuffer *) g_hash_table_lookup (dec->buffer_table, &key);
    if (gst_buf) {
      g_mutex_lock (&dec->external_buf_lock);
      dec->acquired_external_buf--;
      g_cond_signal (&dec->external_buf_cond);
      GST_DEBUG_OBJECT (dec,
          "Found an external gstbuf:%p, fd:%d, idx:%lu, size=%u. Updated "
          "acquired_external_buf to %u", gst_buf, decode_buf->fd,
          decode_buf->index, output_size, dec->acquired_external_buf);
      out_buf = gst_buf;
      g_mutex_unlock (&dec->external_buf_lock);
    }
  } else {
    param_ext.fd = decode_buf->fd;
    param_ext.meta_fd = decode_buf->meta_fd;
    param_ext.index = decode_buf->index;
    param_ext.size = decode_buf->size;
    param_ext.c2_buf = decode_buf->c2Buffer;
    gst_buffer_pool_acquire_buffer (dec->out_port_pool, &out_buf,
        (GstBufferPoolAcquireParams *) & param_ext);
  }

  if (!out_buf) {
    GST_ERROR_OBJECT (dec, "Fail to allocate output gst buffer");
    goto fail;
  }

  if (decode_buf->gbm_bo) {
    /* That gstreamer buf is probably already attached modifier, check it at first.
     * As modifier only store some usage info. like ubwc and security, common event
     * like resolution change won't change modifier. Therefore, if already attached
     * modifier, needn't update or re-attach it. */
    if (!gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (out_buf),
            gst_fbuf_modifier_qdata_quark ())) {
      p_modifier = g_slice_new (guint64);
      if (!dec->gbm_api_bo_get_modifier) {
        *p_modifier = DRM_FORMAT_MOD_INVALID;
      } else {
        *p_modifier = dec->gbm_api_bo_get_modifier (decode_buf->gbm_bo);
      }
      gst_mini_object_set_qdata (GST_MINI_OBJECT (out_buf),
          gst_fbuf_modifier_qdata_quark (), p_modifier,
          (GDestroyNotify) modifier_free);
      GST_DEBUG_OBJECT (dec,
          "Attach modifier quark %p, value:0x%lx on gstbuf %p", p_modifier,
          *p_modifier, out_buf);
    }
  }

done:
  gst_video_codec_state_unref (state);
  return out_buf;

fail:
  if (out_buf) {
    gst_buffer_unref (out_buf);
    out_buf = NULL;
  }
  goto done;
}

/* Push decoded frame to downstream element */
static GstFlowReturn
push_frame_downstream (GstVideoDecoder * decoder, BufferDescriptor * decode_buf)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstBuffer *outbuf;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecState *state;
  GstVideoInfo *vinfo;

  GST_DEBUG_OBJECT (dec, "push frame to downstream");

  state = gst_video_decoder_get_output_state (decoder);
  if (state) {
    vinfo = &state->info;
  } else {
    GST_ERROR_OBJECT (dec, "video codec state is NULL, unexpected!");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  GST_DEBUG_OBJECT (dec,
      "push_frame_downstream, buffer: %p, fd: %d, meta_fd: %d, timestamp: %lu",
      decode_buf->data, decode_buf->fd, decode_buf->meta_fd,
      decode_buf->timestamp);

  frame = gst_video_decoder_get_frame (decoder, decode_buf->index);
  if (frame == NULL) {
    GST_DEBUG_OBJECT (dec,
        "seek: can't get frame (%lu), which was released during FLUSH-STOP event",
        decode_buf->index);
    /* free old output buffer since of seeking */
    if (!c2component_freeOutBuffer (dec->comp, decode_buf->index)) {
      GST_ERROR_OBJECT (dec, "Failed to release the buffer (%lu)",
          decode_buf->index);
    }
    GST_DEBUG_OBJECT (dec, "seek: release old buffer since of seeking");
    ret = GST_FLOW_OK;
    goto out;
  }

  outbuf = gst_qcodec2_vdec_wrap_output_buffer (decoder, decode_buf);
  if (outbuf) {
    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (decode_buf->timestamp, GST_SECOND,
        C2_TICKS_PER_SECOND);

    if (decode_buf->interlaceMode == INTERLACE_MODE_FIELD_TOP_FIRST) {
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
      GST_DEBUG_OBJECT (dec, "interlaced top field");
    } else if (decode_buf->interlaceMode == INTERLACE_MODE_FIELD_BOTTOM_FIRST) {
      GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_DEBUG_OBJECT (dec, "interlaced bottom field");
    }

    if (state->info.fps_d != 0 && state->info.fps_n != 0) {
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }
    frame->output_buffer = outbuf;

    GST_DEBUG_OBJECT (dec,
        "out buffer: PTS: %lu, duration: %lu, fps_d: %d, fps_n: %d interlace:%d",
        GST_BUFFER_PTS (outbuf), GST_BUFFER_DURATION (outbuf), vinfo->fps_d,
        vinfo->fps_n, decode_buf->interlaceMode);
  }

  ret = gst_video_decoder_finish_frame (decoder, frame);
  if (ret == GST_FLOW_FLUSHING) {
    GST_DEBUG_OBJECT (dec, "seek: downstream is flushing");
  } else if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dec, "Failed(%d) to push frame downstream", ret);
  }

out:
  if (state)
    gst_video_codec_state_unref (state);
  return ret;
}

/* Handle event from Codec2 */
static void
handle_video_event (const void *handle, EVENT_TYPE type, void *data)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) handle;
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (dec, "handle_video_event");

  switch (type) {
    case EVENT_OUTPUTS_DONE:{
      BufferDescriptor *out_buf = (BufferDescriptor *) data;
      GstVideoCodecState *output_state = NULL;
      gboolean deinterlace = dec->deinterlace;
      GstVideoInterlaceMode interlace_mode =
          GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

      if (!(out_buf->flag & FLAG_TYPE_END_OF_STREAM)) {
        if (!dec->use_external_buf && (!dec->output_setup ||
                dec->width != out_buf->width ||
                dec->height != out_buf->height)) {
          if (dec->output_setup) {
            GST_DEBUG_OBJECT (dec,
                "resolution change, width height:%d %d -> %u %u", dec->width,
                dec->height, out_buf->width, out_buf->height);
          }

          dec->width = out_buf->width;
          dec->height = out_buf->height;
          if (deinterlace == TRUE) {
            interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
          } else {
            if (GST_IS_QCODEC2_MPEG2_DEC (dec)) {
              if (dec->interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)
                interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
              else
                interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
            } else if (GST_IS_QCODEC2_H264_DEC (dec)) {
              if (out_buf->interlaceMode != INTERLACE_MODE_PROGRESSIVE)
                interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
            }
          }

          output_state =
              gst_video_decoder_set_interlaced_output_state (decoder,
              dec->output_format, interlace_mode, dec->width, dec->height,
              dec->input_state);
          if (!output_state) {
            GST_ERROR_OBJECT (dec, "Failed to set output state");
            break;
          }

          output_state->caps = gst_video_info_to_caps (&output_state->info);

          GST_DEBUG_OBJECT (dec, "set interlace mode %s in caps",
              gst_video_interlace_mode_to_string (interlace_mode));

          if (dec->downstream_supports_dma) {
            gst_caps_set_features (output_state->caps, 0,
                gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_DMABUF));
            GST_DEBUG_OBJECT (dec, "set DMA feature in Caps");
          }
          if (dec->is_ubwc) {
            gst_caps_set_simple (output_state->caps, "compression",
                G_TYPE_STRING, "ubwc", NULL);
          } else {
            gst_caps_set_simple (output_state->caps, "compression",
                G_TYPE_STRING, "linear", NULL);
          }
          GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
              output_state->caps);

          if (dec->output_state) {
            gst_video_codec_state_unref (dec->output_state);
          }
          dec->output_state = output_state;
          if (!gst_video_decoder_negotiate (decoder)) {
            gst_video_codec_state_unref (dec->output_state);
            GST_ERROR_OBJECT (dec, "Failed to negotiate");
            break;
          }
          gst_pad_check_reconfigure (decoder->srcpad);

          dec->output_setup = TRUE;
        }
      }

      if (out_buf->size) {
        if (!dec->first_frame_time.tv_sec && !dec->first_frame_time.tv_usec) {
          gettimeofday (&dec->first_frame_time, NULL);
          int time_1st_cost_us =
              (dec->first_frame_time.tv_sec -
              dec->start_time.tv_sec) * 1000000 +
              (dec->first_frame_time.tv_usec - dec->start_time.tv_usec);
          GST_DEBUG_OBJECT (dec, "first frame latency:%d us", time_1st_cost_us);
        }
        dec->num_output_done++;
        GST_DEBUG_OBJECT (dec, "output done, count: %lu", dec->num_output_done);

        ret = push_frame_downstream (decoder, out_buf);
        if (ret == GST_FLOW_FLUSHING) {
          GST_DEBUG_OBJECT (dec,
              "seek: it's a successful case since of downstream flushing");
        } else if (ret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (dec, "Failed to push frame downstream");
        }
      } else if (out_buf->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (dec, "Decoder reached EOS");
        g_mutex_lock (&dec->pending_lock);
        dec->eos_reached = TRUE;
        g_cond_signal (&dec->pending_cond);
        g_mutex_unlock (&dec->pending_lock);
      }
      break;
    }
    case EVENT_TRIPPED:{
      GST_ERROR_OBJECT (dec, "Failed to apply configuration setting(%d)",
          *(gint32 *) data);
      break;
    }
    case EVENT_ERROR:{
      GST_ERROR_OBJECT (dec, "Something un-expected happened(%d)",
          *(gint32 *) data);
      GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Decoder posts an error"),
          (NULL));
      break;
    }
    case EVENT_UPDATE_MAX_BUF_CNT:{
      guint32 max_buf_cnt = *(guint32 *) data;
      GST_DEBUG_OBJECT (dec,
          "Receive update max buf count event, expected value "
          "is %u, current max buf count is %u", max_buf_cnt,
          dec->max_external_buf_cnt);
      if (dec->use_external_buf && max_buf_cnt) {
        g_mutex_lock (&dec->external_buf_lock);
        if (max_buf_cnt > dec->max_external_buf_cnt) {
          dec->max_external_buf_cnt = max_buf_cnt;
          g_cond_signal (&dec->external_buf_cond);
          GST_DEBUG_OBJECT (dec, "Updated max_external_buf_cnt to %u",
              dec->max_external_buf_cnt);
        }
        g_mutex_unlock (&dec->external_buf_lock);
      }
      break;
    }
    case EVENT_ACQUIRE_EXT_BUF:{
      BufferResolution *resolution = (BufferResolution *) data;
      GstVideoCodecState *output_state = NULL;

      if (dec->width != resolution->width || dec->height != resolution->height) {
        GST_DEBUG_OBJECT (dec,
            "resolution change for external buffer, width height:%d %d -> %u %u",
            dec->width, dec->height, resolution->width, resolution->height);
        dec->acquired_external_buf = 0;
        /* Destroy current buffer hash table as the fds/gstbuffers are outdated */
        if (dec->buffer_table) {
          g_hash_table_destroy (dec->buffer_table);
          dec->buffer_table = NULL;
          GST_DEBUG_OBJECT (dec, "Destroy outdated buffer hash table");
        }

        dec->width = resolution->width;
        dec->height = resolution->height;
        output_state =
            gst_video_decoder_set_output_state (decoder,
            dec->output_format, dec->width, dec->height, dec->input_state);
        if (!output_state) {
          GST_ERROR_OBJECT (dec, "Failed to set output state");
          break;
        }
        output_state->caps = gst_video_info_to_caps (&output_state->info);

        if (dec->downstream_supports_dma) {
          gst_caps_set_features (output_state->caps, 0,
              gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_DMABUF));
          GST_DEBUG_OBJECT (dec, "set DMA feature in Caps");
        }
        if (dec->is_ubwc) {
          gst_caps_set_simple (output_state->caps, "compression",
              G_TYPE_STRING, "ubwc", NULL);
        } else {
          gst_caps_set_simple (output_state->caps, "compression",
              G_TYPE_STRING, "linear", NULL);
        }
        GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
            output_state->caps);
        dec->output_state = output_state;
        if (!gst_video_decoder_negotiate (decoder)) {
          gst_video_codec_state_unref (dec->output_state);
          GST_ERROR_OBJECT (dec, "Failed to negotiate");
          break;
        }
      }

      acquire_external_buf_callback (decoder);
      break;
    }
    default:{
      GST_ERROR_OBJECT (dec, "Invalid Event(%d)", type);
      break;
    }
  }
}

/* Push frame to Codec2 */
static GstFlowReturn
gst_qcodec2_vdec_decode (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);
  GstMapInfo mapinfo = { 0, };
  GstBuffer *buf = NULL;
  GstMemory *mem = NULL;
  BufferDescriptor inBuf;
  gboolean status = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "decode");

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  buf = frame->input_buffer;
  mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_dmabuf_memory (mem)) {
    inBuf.fd = gst_dmabuf_memory_get_fd (mem);
    inBuf.data = NULL;
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
    GST_DEBUG_OBJECT (dec, "Input dma buffer with fd=%d, size=%d",
        inBuf.fd, inBuf.size);
  } else {
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    inBuf.fd = -1;
    inBuf.data = mapinfo.data;
    inBuf.size = mapinfo.size;
  }
  GST_INFO_OBJECT (dec, "frame->pts (%" G_GUINT64_FORMAT ")", frame->pts);

  /* Keep track of queued frame */
  dec->queued_frame[(dec->frame_index) % MAX_QUEUED_FRAME] =
      frame->system_frame_number;

  inBuf.pool_type = BUFFER_POOL_BASIC_LINEAR;
  inBuf.timestamp = NANO_TO_MILLI (frame->pts);
  inBuf.index = frame->system_frame_number;
  inBuf.secure = dec->secure;

  /* Queue buffer to Codec2 */
  status = c2component_queue (dec->comp, &inBuf);
  gst_buffer_unmap (buf, &mapinfo);
  if (!status) {
    GST_ERROR_OBJECT (dec, "failed to queue input frame to Codec2");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  g_mutex_lock (&(dec->pending_lock));
  dec->frame_index += 1;
  dec->num_input_queued++;
  g_mutex_unlock (&(dec->pending_lock));

out:
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return ret;
}

static void
gst_qcodec2_vdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (object);

  GST_DEBUG_OBJECT (dec, "qcodec2_vdec_set_property");

  switch (prop_id) {
    case PROP_SILENT:
      dec->silent = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_PICTURE_ORDER:
      dec->output_picture_order_mode = g_value_get_uint (value);
      break;
    case PROP_LOW_LATENCY:
      dec->low_latency_mode = g_value_get_boolean (value);
      break;
    case PROP_SECURE:
      dec->secure = g_value_get_boolean (value);
      break;
    case PROP_DATA_COPY_FUNTION:
      dec->cb.data_copy_func = g_value_get_pointer (value);
      break;
    case PROP_DATA_COPY_FUNTION_PARAM:
      dec->cb.data_copy_func_param = g_value_get_pointer (value);
      break;
    case PROP_USE_EXTERNAL_POOL:
      dec->use_external_buf = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qcodec2_vdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (object);

  GST_DEBUG_OBJECT (dec, "qcodec2_vdec_get_property");

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, dec->silent);
      break;
    case PROP_OUTPUT_PICTURE_ORDER:
      g_value_set_uint (value, dec->output_picture_order_mode);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, dec->low_latency_mode);
      break;
    case PROP_SECURE:
      g_value_set_boolean (value, dec->secure);
      break;
    case PROP_DATA_COPY_FUNTION:
      g_value_set_pointer (value, dec->cb.data_copy_func);
      break;
    case PROP_DATA_COPY_FUNTION_PARAM:
      g_value_set_pointer (value, dec->cb.data_copy_func_param);
      break;
    case PROP_USE_EXTERNAL_POOL:
      g_value_set_boolean (value, dec->use_external_buf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qcodec2_vdec_src_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (decoder);

  gboolean ret = TRUE;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);
      GST_DEBUG_OBJECT (dec,
          "seek: start time:%" GST_TIME_FORMAT " stop time:%" GST_TIME_FORMAT
          " rate:%f format:%u flags:%u start_type:%u stop_type:%u seqnum:%u",
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop), rate, format, flags,
          start_type, stop_type, seqnum);
      break;
    }
    default:
      break;
  }

  ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_event (decoder, event);

  return ret;
}

/* Called during object destruction process */
static void
gst_qcodec2_vdec_finalize (GObject * object)
{
  GstQcodec2Vdec *dec = GST_QCODEC2_VDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  g_mutex_clear (&dec->pending_lock);
  g_cond_clear (&dec->pending_cond);

  g_mutex_clear (&dec->external_buf_lock);
  g_cond_clear (&dec->external_buf_cond);

  if (dec->gbm_lib) {
    GST_INFO_OBJECT (dec, "dlclose gbm lib:%p", dec->gbm_lib);
    dlclose (dec->gbm_lib);
  }

  /* Lastly chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_qcodec2_vdec_class_init (GstQcodec2VdecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_vdec_src_template));

  /* Set GObject class property */
  gobject_class->set_property = gst_qcodec2_vdec_set_property;
  gobject_class->get_property = gst_qcodec2_vdec_get_property;
  gobject_class->finalize = gst_qcodec2_vdec_finalize;

  /* Add property to this class */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_OUTPUT_PICTURE_ORDER, g_param_spec_uint ("output-picture-order-mode",
          "output picture order mode",
          "output picture order (0xffffffff=component default, 1: display order, 2: decoder order)",
          0, G_MAXUINT, DEFAULT_OUTPUT_PICTURE_ORDER_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency-mode", "Low latency mode",
          "If enabled, decoder should be in low latency mode",
          DEFAULT_LOW_LATENCY_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SECURE,
      g_param_spec_boolean ("secure", "secure mode",
          "If enabled, decoder should be in secure mode. Secure mode only support UBWC output "
          "For any secure cases, output is forced to set UBWC",
          DEFAULT_SECURE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DATA_COPY_FUNTION, g_param_spec_pointer ("data-copy-func",
          "set input date copy callback function",
          "set input data copy callback function, app could implement this callback function "
          "to copy data from dec plugin's sinkpad buf to codec2's input buf. Function prototype "
          "is: int datacopy(int dstbuf_fd, void* srcbuf, int datalen, void* param), returning "
          "zero means copy succeed. If this callback is NULL, plugin implement it internally",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DATA_COPY_FUNTION_PARAM,
      g_param_spec_pointer ("data-copy-func-param",
          "set input parameter of date copy callback function",
          "work with data-copy-func callback function, app could set input parameter for "
          "that function, this property will be passed as the 4th parameter of that function",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_USE_EXTERNAL_POOL, g_param_spec_boolean ("use-external-pool",
          "if allow using external pool",
          "If enabled, decoder will use external buffer pool if supported by downstream.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_finish);
  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_close);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_stop);
  video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_flush);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_decide_allocation);
  video_decoder_class->src_event =
      GST_DEBUG_FUNCPTR (gst_qcodec2_vdec_src_event);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video decoder", "Decoder/Video",
      "Video Decoder based on Codec2.0", "QTI");
}

/* Invoked during object instantiation (equivalent C++ constructor).
 * Initialize only those variables that do not change during state change.
 * For other variables, place initialization into function open.*/
static void
gst_qcodec2_vdec_init (GstQcodec2Vdec * dec)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) dec;

  gst_video_decoder_set_packetized (decoder, TRUE);

  dec->output_picture_order_mode = DEFAULT_OUTPUT_PICTURE_ORDER_MODE;
  dec->low_latency_mode = DEFAULT_LOW_LATENCY_MODE;
  dec->cb.data_copy_func = NULL;
  dec->cb.data_copy_func_param = NULL;
  dec->deinterlace = DEFAULT_DEINTERLACE;
  dec->delay_start = FALSE;
  dec->use_external_buf = FALSE;

  g_cond_init (&dec->pending_cond);
  g_mutex_init (&dec->pending_lock);

  g_mutex_init (&dec->external_buf_lock);
  g_cond_init (&dec->external_buf_cond);

  dec->silent = FALSE;
  dec->gbm_lib = dlopen ("libgbm.so", RTLD_NOW);
  GST_INFO_OBJECT (dec, "open gbm lib:%p", dec->gbm_lib);
  if (dec->gbm_lib == NULL) {
    GST_ERROR ("dlopen libgbm.so failed");
    return;
  }

  dec->gbm_api_bo_get_modifier = dlsym (dec->gbm_lib, "gbm_bo_get_modifier");
  if (!dec->gbm_api_bo_get_modifier) {
    GST_ERROR_OBJECT (dec, "Failed as a gbm API is null");
    dlclose (dec->gbm_lib);
    dec->gbm_lib = NULL;
    return;
  }

}

gboolean
gst_qcodec2_vdec_plugin_init (GstPlugin * plugin)
{
  /* debug category for filtering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_qcodec2_vdec_debug, "qcodec2vdec",
      0, "GST QTI codec2.0 video decoder");

  static gsize res = FALSE;
  static const gchar *tags[] = { NULL };
  if (g_once_init_enter (&res)) {
    gst_meta_register_custom ("GstQVDMeta", tags, NULL, NULL, NULL);
    g_once_init_leave (&res, TRUE);
  }

  if (!gst_element_register (plugin, "qcodec2h264dec",
          GST_RANK_PRIMARY + 10, GST_TYPE_QCODEC2_H264_DEC)) {
    GST_ERROR ("failed to register element qcodec2h264dec");
    return FALSE;
  }
  if (!gst_element_register (plugin, "qcodec2h265dec",
          GST_RANK_PRIMARY + 10, GST_TYPE_QCODEC2_H265_DEC)) {
    GST_ERROR ("failed to register element qcodec2h265dec");
    return FALSE;
  }
  if (!gst_element_register (plugin, "qcodec2vp9dec",
          GST_RANK_PRIMARY + 10, GST_TYPE_QCODEC2_VP9_DEC)) {
    GST_ERROR ("failed to register element qcodec2vp9dec");
    return FALSE;
  }
  if (!gst_element_register (plugin, "qcodec2mpeg2dec",
          GST_RANK_PRIMARY + 10, GST_TYPE_QCODEC2_MPEG2_DEC)) {
    GST_ERROR ("failed to register element qcodec2mpeg2dec");
    return FALSE;
  }

  return TRUE;
}

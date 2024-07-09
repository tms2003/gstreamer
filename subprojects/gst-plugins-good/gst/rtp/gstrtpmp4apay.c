/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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
#  include "config.h"
#endif

#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>

#include "gstrtpelements.h"
#include "gstrtpmp4apay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpmp4apay_debug);
#define GST_CAT_DEFAULT (rtpmp4apay_debug)

/* FIXME: add framed=(boolean)true once our encoders have this field set
 * on their output caps */
static GstStaticPadTemplate gst_rtp_mp4a_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion=(int)4, "
        "stream-format = (string) { raw, latm-mcp0, latm-mcp1 }")
    );

static GstStaticPadTemplate gst_rtp_mp4a_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"MP4A-LATM\""
        /* All optional parameters
         *
         * "cpresent = (string) \"0\""
         * "config="
         */
    )
    );

static void gst_rtp_mp4a_pay_finalize (GObject * object);

static gboolean gst_rtp_mp4a_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_mp4a_pay_handle_buffer (GstRTPBasePayload *
    payload, GstBuffer * buffer);

#define gst_rtp_mp4a_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpMP4APay, gst_rtp_mp4a_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpmp4apay, "rtpmp4apay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_MP4A_PAY, rtp_element_init (plugin));

static void
gst_rtp_mp4a_pay_class_init (GstRtpMP4APayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mp4a_pay_finalize;

  gstrtpbasepayload_class->set_caps = gst_rtp_mp4a_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_mp4a_pay_handle_buffer;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_mp4a_pay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_mp4a_pay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP MPEG4 audio payloader", "Codec/Payloader/Network/RTP",
      "Payload MPEG4 audio as RTP packets (RFC 3016)",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (rtpmp4apay_debug, "rtpmp4apay", 0,
      "MP4A-LATM RTP Payloader");
}

static void
gst_rtp_mp4a_pay_init (GstRtpMP4APay * rtpmp4apay)
{
  rtpmp4apay->rate = 90000;
  rtpmp4apay->profile = g_strdup ("1");
  rtpmp4apay->parse_latm = FALSE;
}

static void
gst_rtp_mp4a_pay_finalize (GObject * object)
{
  GstRtpMP4APay *rtpmp4apay;

  rtpmp4apay = GST_RTP_MP4A_PAY (object);

  g_free (rtpmp4apay->params);
  rtpmp4apay->params = NULL;

  if (rtpmp4apay->config)
    gst_buffer_unref (rtpmp4apay->config);
  rtpmp4apay->config = NULL;

  g_free (rtpmp4apay->profile);
  rtpmp4apay->profile = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const unsigned int sampling_table[16] = {
  96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
  16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

static gboolean
gst_rtp_mp4a_pay_get_audio_object_type (GstRtpMP4APay * rtpmp4apay,
    GstBitReader * br, guint8 * audio_object_type)
{
  if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 5))
    return FALSE;

  if (*audio_object_type == 31) {
    if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 6))
      return FALSE;
    *audio_object_type += 32;
  }

  GST_LOG_OBJECT (rtpmp4apay, "audio object type %u", *audio_object_type);

  return TRUE;
}

static gboolean
gst_rtp_mp4a_pay_get_audio_sample_rate (GstRtpMP4APay * rtpmp4apay,
    GstBitReader * br, gint * sample_rate)
{
  guint8 sampling_frequency_index;

  if (!gst_bit_reader_get_bits_uint8 (br, &sampling_frequency_index, 4))
    return FALSE;

  GST_LOG_OBJECT (rtpmp4apay, "sampling_frequency_index: %u",
      sampling_frequency_index);

  if (sampling_frequency_index == 0xf) {
    guint32 sampling_rate;
    if (!gst_bit_reader_get_bits_uint32 (br, &sampling_rate, 24))
      return FALSE;
    *sample_rate = sampling_rate;
  } else {
    *sample_rate = sampling_table[sampling_frequency_index];
    if (!*sample_rate)
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rtp_mp4a_pay_parse_audio_config (GstRtpMP4APay * rtpmp4apay,
    GstBitReader * br)
{
  guint8 objectType;
  guint8 channelCfg;

  /* any object type is fine, we need to copy it to the profile-level-id field. */
  if (!gst_rtp_mp4a_pay_get_audio_object_type (rtpmp4apay, br, &objectType))
    return FALSE;

  if (!gst_rtp_mp4a_pay_get_audio_sample_rate (rtpmp4apay, br,
          &rtpmp4apay->rate))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (br, &channelCfg, 4))
    return FALSE;
  if (channelCfg > 7)
    goto wrong_channels;

  /* extra rtp params contain the number of channels */
  g_free (rtpmp4apay->params);
  rtpmp4apay->params = g_strdup_printf ("%d", channelCfg);
  /* audio stream type */
  rtpmp4apay->streamtype = "5";
  /* profile */
  g_free (rtpmp4apay->profile);
  rtpmp4apay->profile = g_strdup_printf ("%d", objectType);

  GST_LOG_OBJECT (rtpmp4apay,
      "objectType: %d, sampling rate: %d, channelCfg: %d",
      objectType, rtpmp4apay->rate, channelCfg);

  return TRUE;

  /* ERROR */
wrong_channels:
  {
    GST_ELEMENT_ERROR (rtpmp4apay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("unsupported number of channels %d, must < 8", channelCfg));
    return FALSE;
  }
}

static gboolean
gst_rtp_mp4a_pay_latm_get_value (GstRtpMP4APay * rtpmp4apay, GstBitReader * br,
    guint32 * value)
{
  guint8 bytes, i, byte;

  *value = 0;

  if (!gst_bit_reader_get_bits_uint8 (br, &bytes, 2))
    return FALSE;

  for (i = 0; i <= bytes; ++i) {
    *value <<= 8;
    if (!gst_bit_reader_get_bits_uint8 (br, &byte, 8))
      return FALSE;
    *value += byte;
  }

  return TRUE;
}

static gboolean
gst_rtp_mp4a_pay_parse_streammux_config (GstRtpMP4APay * rtpmp4apay,
    GstBitReader * br)
{
  guint8 v, vA;

  GST_LOG_OBJECT (rtpmp4apay, "Trying to parse StreamMux config");

  /* audioMuxVersion */
  if (!gst_bit_reader_get_bits_uint8 (br, &v, 1))
    return FALSE;

  if (v) {
    /* audioMuxVersionA */
    if (!gst_bit_reader_get_bits_uint8 (br, &vA, 1))
      return FALSE;
  } else
    vA = 0;

  GST_DEBUG_OBJECT (rtpmp4apay, "v %d, vA %d", v, vA);

  if (vA == 0) {
    guint8 same_time_framing, subframes, num_program, prog;
    if (v == 1) {
      guint32 value;
      /* taraBufferFullness */
      if (!gst_rtp_mp4a_pay_latm_get_value (rtpmp4apay, br, &value))
        return FALSE;
    }

    if (!gst_bit_reader_get_bits_uint8 (br, &same_time_framing, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &subframes, 6))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &num_program, 4))
      return FALSE;

    /*
     * For LATM, maximum number of program and layer is 1. Getting a value of
     * 0 for either program or layer implies a value of 1.
     */
    num_program += 1;
    if (num_program > 1) {
      GST_ERROR_OBJECT (rtpmp4apay, "LATM Unsupported format");
      return FALSE;
    }

    GST_INFO_OBJECT (rtpmp4apay,
        "same_time_framing %d, subframes %d, num_program %d", same_time_framing,
        subframes, num_program);

    for (prog = 0; prog < num_program; ++prog) {
      guint8 num_layer, layer;

      if (!gst_bit_reader_get_bits_uint8 (br, &num_layer, 3))
        return FALSE;

      num_layer += 1;
      if (num_layer > 1) {
        GST_ERROR_OBJECT (rtpmp4apay, "LATM Unsupported format");
        return FALSE;
      }

      GST_INFO_OBJECT (rtpmp4apay, "Program %d: %d layers", prog, num_layer);

      for (layer = 0; layer < num_layer; layer++) {
        guint8 use_same_config;

        if (prog == 0 && layer == 0) {
          use_same_config = 0;
        } else {
          if (!gst_bit_reader_get_bits_uint8 (br, &use_same_config, 1))
            return FALSE;
        }

        if (!use_same_config) {
          if (v == 0) {
            if (!gst_rtp_mp4a_pay_parse_audio_config (rtpmp4apay, br)) {
              GST_ERROR_OBJECT (rtpmp4apay,
                  "Failed to read audio specific config");
              return FALSE;
            }
          } else {
            guint32 asc_len;
            if (!gst_rtp_mp4a_pay_latm_get_value (rtpmp4apay, br, &asc_len))
              return FALSE;
            if (!gst_rtp_mp4a_pay_parse_audio_config (rtpmp4apay, br)) {
              GST_ERROR_OBJECT (rtpmp4apay,
                  "Failed to read audio specific config");
              return FALSE;
            }
            if (!gst_bit_reader_skip (br, asc_len))
              return FALSE;
          }
        }
      }
    }
  } else {
    GST_ERROR_OBJECT (rtpmp4apay,
        "audioMuxVersionA > 0 reserved for future extensions");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rtp_mp4a_pay_new_caps (GstRtpMP4APay * rtpmp4apay)
{
  gchar *config;
  GValue v = { 0 };
  gboolean res;

  g_value_init (&v, GST_TYPE_BUFFER);
  gst_value_set_buffer (&v, rtpmp4apay->config);
  config = gst_value_serialize (&v);

  res = gst_rtp_base_payload_set_outcaps (GST_RTP_BASE_PAYLOAD (rtpmp4apay),
      "cpresent", G_TYPE_STRING, "0", "config", G_TYPE_STRING, config, NULL);

  g_value_unset (&v);
  g_free (config);

  return res;
}

static gboolean
gst_rtp_mp4a_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstRtpMP4APay *rtpmp4apay;
  GstStructure *structure;
  const GValue *codec_data;
  gboolean res, framed = TRUE;
  const gchar *stream_format;
  gboolean mux_config_present = FALSE;

  rtpmp4apay = GST_RTP_MP4A_PAY (payload);

  structure = gst_caps_get_structure (caps, 0);

  /* this is already handled by the template caps, but it is better
   * to leave here to have meaningful warning messages when linking
   * fails */
  stream_format = gst_structure_get_string (structure, "stream-format");
  if (stream_format) {
    if (!(strcmp (stream_format, "raw") != 0
            || strcmp (stream_format, "latm-mcp0") != 0
            || strcmp (stream_format, "latm-mcp1") != 0)) {
      GST_WARNING_OBJECT (rtpmp4apay,
          "AAC's stream-format must be 'raw' or 'latm-mcp0' or 'latm-mcp1', "
          "%s is not supported", stream_format);
      return FALSE;
    }
  } else {
    GST_WARNING_OBJECT (rtpmp4apay, "AAC's stream-format not specified, "
        "assuming 'raw'");
  }

  /* For LATM MCP0, the StreamMuxConfig will be in codec_data */
  if (strcmp (stream_format, "latm-mcp0") == 0)
    mux_config_present = TRUE;
  else if (strcmp (stream_format, "latm-mcp1") == 0) {
    /*
     * For LATM MCP1, we will have to parse the incoming bitstream to figure out
     * the required parameters like sample rate. Set the parse_latm flag to
     * signal this and parse the incoming bitstream in handle_buffer when this
     * is set.
     */
    rtpmp4apay->parse_latm = TRUE;
    return TRUE;
  }

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data) {
    GST_LOG_OBJECT (rtpmp4apay, "got codec_data");
    if (G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
      GstBuffer *buffer, *cbuffer;
      GstBitReader br;
      GstMapInfo map;
      GstMapInfo cmap;
      guint i;

      buffer = gst_value_get_buffer (codec_data);
      GST_LOG_OBJECT (rtpmp4apay, "configuring codec_data");

      if (buffer && gst_buffer_get_size (buffer) < 2) {
        GST_ERROR_OBJECT (rtpmp4apay, "Malformed codec_data. Too short.");
        return FALSE;
      }

      if (!gst_buffer_map (buffer, &map, GST_MAP_READ))
        return FALSE;
      gst_bit_reader_init (&br, map.data, map.size);

      if (mux_config_present)
        res = gst_rtp_mp4a_pay_parse_streammux_config (rtpmp4apay, &br);
      else
        res = gst_rtp_mp4a_pay_parse_audio_config (rtpmp4apay, &br);

      if (!res)
        goto config_failed;

      /*
       * If muxConfigPresent = 1, then codec_data will have StreamMuxConfig.
       * We do not need to construct a StreamMuxConfig again, just copy it to
       * the 'config' buffer.
       */
      if (mux_config_present) {
        cbuffer = gst_buffer_new_and_alloc (map.size);
        gst_buffer_map (cbuffer, &cmap, GST_MAP_WRITE);

        memcpy (cmap.data, map.data, map.size);
        gst_buffer_unmap (cbuffer, &cmap);
        gst_buffer_unmap (buffer, &map);
        goto streammux_present;
      }

      /* make the StreamMuxConfig, we need 15 bits for the header */
      cbuffer = gst_buffer_new_and_alloc (map.size + 2);
      gst_buffer_map (cbuffer, &cmap, GST_MAP_WRITE);

      memset (cmap.data, 0, map.size + 2);

      /* Create StreamMuxConfig according to ISO/IEC 14496-3:
       *
       * audioMuxVersion           == 0 (1 bit)
       * allStreamsSameTimeFraming == 1 (1 bit)
       * numSubFrames              == numSubFrames (6 bits)
       * numProgram                == 0 (4 bits)
       * numLayer                  == 0 (3 bits)
       */
      cmap.data[0] = 0x40;
      cmap.data[1] = 0x00;

      /* append the config bits, shifting them 1 bit left */
      for (i = 0; i < map.size; i++) {
        cmap.data[i + 1] |= ((map.data[i] & 0x80) >> 7);
        cmap.data[i + 2] |= ((map.data[i] & 0x7f) << 1);
      }

      gst_buffer_unmap (cbuffer, &cmap);
      gst_buffer_unmap (buffer, &map);

    streammux_present:
      /* now we can configure the buffer */
      if (rtpmp4apay->config)
        gst_buffer_unref (rtpmp4apay->config);
      rtpmp4apay->config = cbuffer;
    }
  }

  if (gst_structure_get_boolean (structure, "framed", &framed) && !framed) {
    GST_WARNING_OBJECT (payload, "Need framed AAC data as input!");
  }

  gst_rtp_base_payload_set_options (payload, "audio", TRUE, "MP4A-LATM",
      rtpmp4apay->rate);

  res = gst_rtp_mp4a_pay_new_caps (rtpmp4apay);

  return res;

  /* ERRORS */
config_failed:
  {
    GST_DEBUG_OBJECT (rtpmp4apay, "failed to parse config");
    return FALSE;
  }
}

#define RTP_HEADER_LEN 12

/* we expect buffers as exactly one complete AU
 */
static GstFlowReturn
gst_rtp_mp4a_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpMP4APay *rtpmp4apay;
  GstFlowReturn ret;
  GstBufferList *list;
  guint mtu;
  guint offset;
  gsize size;
  gboolean fragmented;
  GstClockTime timestamp;

  ret = GST_FLOW_OK;

  rtpmp4apay = GST_RTP_MP4A_PAY (basepayload);

  offset = 0;
  size = gst_buffer_get_size (buffer);

  timestamp = GST_BUFFER_PTS (buffer);

  fragmented = FALSE;
  mtu = GST_RTP_BASE_PAYLOAD_MTU (rtpmp4apay);

  list = gst_buffer_list_new_sized (size / (mtu - RTP_HEADER_LEN) + 1);

  /*
   * If this is set, incoming bitstream has stream format as latm-mcp1 and the
   * bitstream needs to be parsed to retrieve the required parameters from
   * StreamMuxConfig which is included in the bitstream.
   */
  if (rtpmp4apay->parse_latm) {
    GstMapInfo map;
    GstBitReader br;
    guint8 u8 = 0;

    GST_INFO_OBJECT (rtpmp4apay,
        "Trying to parse StreamMuxConfig in incoming buffer");

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    gst_bit_reader_init (&br, map.data, map.size);

    /* First bit is "use last config" */
    gst_bit_reader_get_bits_uint8 (&br, &u8, 1);

    if (u8) {
      GST_LOG_OBJECT (rtpmp4apay, "Frame uses previous config");
      goto use_prev_smc;
    } else {
      if (!gst_rtp_mp4a_pay_parse_streammux_config (rtpmp4apay, &br)) {
        GST_ERROR_OBJECT (rtpmp4apay,
            "Could not read StreamMuxConfig in incoming buffer");
        gst_buffer_unmap (buffer, &map);
        return GST_FLOW_ERROR;
      }
    }

    gst_buffer_unmap (buffer, &map);

    gst_rtp_base_payload_set_options (basepayload, "audio", TRUE, "MP4A-LATM",
        rtpmp4apay->rate);

    if (!gst_rtp_base_payload_set_outcaps (GST_RTP_BASE_PAYLOAD (rtpmp4apay),
            NULL)) {
      GST_ERROR_OBJECT (rtpmp4apay,
          "Could not set caps after parsing StreamMuxConfig");
      gst_buffer_list_unref (list);
      return GST_FLOW_ERROR;
    }
  }

use_prev_smc:
  while (size > 0) {
    guint towrite;
    GstBuffer *outbuf;
    guint payload_len;
    guint packet_len;
    guint header_len;
    GstBuffer *paybuf;
    GstRTPBuffer rtp = { NULL };

    header_len = 0;
    if (!fragmented) {
      guint count;
      /* first packet calculate space for the packet including the header */
      count = size;
      while (count >= 0xff) {
        header_len++;
        count -= 0xff;
      }
      header_len++;
    }

    packet_len = gst_rtp_buffer_calc_packet_len (header_len + size, 0, 0);
    towrite = MIN (packet_len, mtu);
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);
    payload_len -= header_len;

    GST_DEBUG_OBJECT (rtpmp4apay,
        "avail %" G_GSIZE_FORMAT
        ", header_len %d, packet_len %d, payload_len %d", size, header_len,
        packet_len, payload_len);

    /* create buffer to hold the payload. */
    outbuf = gst_rtp_base_payload_allocate_output_buffer (basepayload,
        header_len, 0, 0);

    /* copy payload */
    gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

    if (!fragmented) {
      guint8 *payload = gst_rtp_buffer_get_payload (&rtp);
      guint count;

      /* first packet write the header */
      count = size;
      while (count >= 0xff) {
        *payload++ = 0xff;
        count -= 0xff;
      }
      *payload++ = count;
    }

    /* marker only if the packet is complete */
    gst_rtp_buffer_set_marker (&rtp, size == payload_len);
    if (size == payload_len)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_MARKER);

    gst_rtp_buffer_unmap (&rtp);

    /* create a new buf to hold the payload */
    paybuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
        offset, payload_len);

    /* join memory parts */
    gst_rtp_copy_audio_meta (rtpmp4apay, outbuf, paybuf);
    outbuf = gst_buffer_append (outbuf, paybuf);
    gst_buffer_list_add (list, outbuf);
    offset += payload_len;
    size -= payload_len;

    /* copy incoming timestamp (if any) to outgoing buffers */
    GST_BUFFER_PTS (outbuf) = timestamp;

    fragmented = TRUE;
  }

  ret =
      gst_rtp_base_payload_push_list (GST_RTP_BASE_PAYLOAD (rtpmp4apay), list);

  gst_buffer_unref (buffer);

  return ret;
}

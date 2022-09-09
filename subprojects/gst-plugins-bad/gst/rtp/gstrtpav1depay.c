/* GStreamer
 * Copyright (c) 2022 Cisco Systems, Inc. and its affiliates 
 * All rights reserved.
 * Author: 2022 Alistair Hampton <alhampto@cisco.com>
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
 * SECTION:element-rtpav1pay
 *
 * This element depayloads AV1 RTP payloaded packets.
 * Output format is AV1 video.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpav1depay.h"
#include "gstrtpav1common.h"

GST_DEBUG_CATEGORY_STATIC (rtpav1depay_debug);
#define GST_CAT_DEFAULT (rtpav1depay_debug)

static GstStaticPadTemplate gst_rtp_av1_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"AV1\""));

static GstStaticPadTemplate gst_rtp_av1_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1," "alignment = (string) obu"));

#define gst_rtp_av1_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpAv1Depay, gst_rtp_av1_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE (rtpav1depay, "rtpav1depay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_AV1_DEPAY);

static void gst_rtp_av1_depay_finalize (GObject * object);

static void gst_rtp_av1_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_av1_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstBuffer *gst_rtp_av1_depay_process (GstRTPBaseDepayload * base,
    GstRTPBuffer * rtp_buffer);

static gboolean gst_rtp_av1_depay_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void
gst_rtp_av1_depay_class_init (GstRtpAv1DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gobject_class->set_property = gst_rtp_av1_depay_set_property;
  gobject_class->get_property = gst_rtp_av1_depay_get_property;
  gobject_class->finalize = gst_rtp_av1_depay_finalize;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_av1_depay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_av1_depay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP AV1 depayloader", "Codec/Depayloader/Network/RTP",
      "Depayload-encode RTP packets into AV1 video",
      "Alistair Hampton <alhampto@cisco.com>");

  // Caps functions are not specified so that the static template is used
  gstrtpbasedepayload_class->process_rtp_packet = gst_rtp_av1_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpav1depay_debug, "rtpav1depay", 0,
      "AV1 RTP Depayloader");

  GST_DEBUG ("class init called \n");
}

static void
gst_rtp_av1_depay_init (GstRtpAv1Depay * rtpav1depay)
{
  GST_DEBUG ("init called \n");
  rtpav1depay->prev_fragment = NULL;
}

static void
gst_rtp_av1_depay_finalize (GObject * object)
{
  GstRtpAv1Depay *rtpav1depay;

  GST_DEBUG ("finalize called \n");
  rtpav1depay = GST_RTP_AV1_DEPAY (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_av1_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GST_DEBUG ("set property called \n");
}

static void
gst_rtp_av1_depay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GST_DEBUG ("get property called \n");
}

static GstBuffer *
gst_rtp_av1_depay_process (GstRTPBaseDepayload * base,
    GstRTPBuffer * rtp_buffer)
{
  GstRtpAv1Depay *rtpav1depay;
  GstBuffer *outbuf;
  GstBuffer *fragment;
  guchar *payload;
  guint payload_len;
  guint8 header;
  guint8 header_zbit;
  guint8 header_ybit;
  guint8 header_wbits;
  guint32 obu_element_size;
  guint8 leb128_len;
  gsize parsed_bytes;
  gsize no_obus = 0;
  gsize fill_size;

  payload = gst_rtp_buffer_get_payload (rtp_buffer);
  payload_len = gst_rtp_buffer_get_payload_len (rtp_buffer);
  rtpav1depay = GST_RTP_AV1_DEPAY (base);
  fragment = rtpav1depay->prev_fragment;
  parsed_bytes = 0;

  // read av1 aggregate header
  header = *payload;
  header_zbit = (header >> 7) & 1;
  header_ybit = (header >> 6) & 1;
  header_wbits = (header >> 4) & 3;
  GST_DEBUG ("process packet: zbit=%d, ybit=%d, wbits=%d, size=%d", header_zbit,
      header_ybit, header_wbits, payload_len);

  payload += 1;
  parsed_bytes += 1;
  while (parsed_bytes < payload_len) {
    // read the size of the obu element
    if (header_wbits == 0 || (no_obus != header_wbits - 1)) {
      leb128_len = 0;
      obu_element_size =
          gst_rtp_av1_read_leb128 (payload, &leb128_len,
          payload_len - parsed_bytes);

      payload += leb128_len;
      parsed_bytes += leb128_len;
    } else {
      obu_element_size = payload_len - parsed_bytes;
    }

    GST_DEBUG ("process packet obu: size=%d", obu_element_size);

    outbuf = gst_buffer_new_and_alloc (obu_element_size);
    if (outbuf == NULL)
      return NULL;
    // copy OBU data to output
    fill_size = gst_buffer_fill (outbuf, 0, payload, obu_element_size);
    if (fill_size != obu_element_size) {
      gst_buffer_unref (outbuf);
      return NULL;
    }

    if (header_zbit && no_obus == 0) {
      if (fragment == NULL)
        goto next_obu;

      outbuf = gst_buffer_append (fragment, outbuf);
    }

    if (header_ybit && (parsed_bytes + obu_element_size >= payload_len)) {
      rtpav1depay->prev_fragment = fragment = gst_buffer_ref (outbuf);
    } else {
      GstMapInfo map = { NULL };
      guint8 obu_type = 0;

      gst_buffer_map (outbuf, &map, GST_MAP_READ);
      obu_type = (*map.data >> 3) & 0xF;
      gst_buffer_unmap (outbuf, &map);

      // Reserved and ignored OBU types
      switch (obu_type) {
        case 0:                // Reserved
        case 2:                // OBU_TEMPORAL_DELIMITER
        case 8:                // OBU_TILE_LIST
        case 9:                // Reserved
        case 10:               // Reserved
        case 11:               // Reserved
        case 12:               // Reserved
        case 13:               // Reserved
        case 14:               // Reserved
          GST_DEBUG ("Ignoring OBU TYPE=%d", obu_type);
          gst_buffer_unref (outbuf);
          goto next_obu;
        default:
          break;
      }
      gst_rtp_base_depayload_push (base, outbuf);
      rtpav1depay->prev_fragment = fragment = NULL;
    }

  next_obu:
    payload += obu_element_size;
    parsed_bytes += obu_element_size;
    no_obus++;
  }

  return NULL;
}

static gboolean
gst_rtp_av1_depay_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret;

  ret = gst_pad_query_default (pad, parent, query);
  GST_DEBUG ("src query called, %d, %d\n", GST_QUERY_TYPE (query), ret);

  return ret;
}

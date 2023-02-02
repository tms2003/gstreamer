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
 * This element payloads AV1 video into RTP packets.
 * Output format described in aomedia RTP Payload Format For AV1 (v1.0).
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpav1pay.h"
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/gsterror.h>

GST_DEBUG_CATEGORY_STATIC (rtpav1pay_debug);
#define GST_CAT_DEFAULT (rtpav1pay_debug)

static GstStaticPadTemplate gst_rtp_av1_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1," "alignment = (string) tu"));

static GstStaticPadTemplate gst_rtp_av1_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"AV1\""));

#define gst_rtp_av1_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpAv1Pay, gst_rtp_av1_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE (rtpav1pay, "rtpav1pay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_AV1_PAY);

#define DEFAULT_AGGREGATE_MODE GST_RTP_AV1_AGGREGATE_NONE
#define GST_TYPE_RTP_AV1_AGGREGATE_MODE \
  (gst_rtp_av1_aggregate_mode_get_type())

enum
{
  PROP_0,
  PROP_AGGREGATE_MODE,
};

static void gst_rtp_av1_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_av1_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GType gst_rtp_av1_aggregate_mode_get_type ();
static void gst_rtp_av1_pay_finalize (GObject * object);

static GstFlowReturn gst_rtp_av1_pay_handle_buffer (GstRTPBasePayload * pad,
    GstBuffer * buffer);

static gboolean gst_rtp_av1_pay_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_rtp_av1_pay_reset_bundle (GstRtpAv1Pay * rtpav1pay);

static void
gst_rtp_av1_pay_class_init (GstRtpAv1PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->set_property = gst_rtp_av1_pay_set_property;
  gobject_class->get_property = gst_rtp_av1_pay_get_property;
  gobject_class->finalize = gst_rtp_av1_pay_finalize;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_av1_pay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_av1_pay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "RTP AV1 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode AV1 video into RTP packets",
      "Alistair Hampton <alhampto@cisco.com>");

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_AGGREGATE_MODE,
      g_param_spec_enum ("aggregate-mode",
          "Aggregate packets together",
          "Bundle OBUs of a single TU into a single packet",
          GST_TYPE_RTP_AV1_AGGREGATE_MODE,
          DEFAULT_AGGREGATE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstrtpbasepayload_class->handle_buffer = gst_rtp_av1_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpav1pay_debug, "rtpav1pay", 0,
      "AV1 RTP Payloader");
}

static void
gst_rtp_av1_pay_init (GstRtpAv1Pay * rtpav1pay)
{
  GST_DEBUG ("init called \n");
  rtpav1pay->first_packet = TRUE;
  rtpav1pay->fragment_cont = FALSE;
  gst_pad_set_query_function (GST_RTP_BASE_PAYLOAD_SRCPAD (rtpav1pay),
      gst_rtp_av1_pay_src_query);
  GST_RTP_BASE_PAYLOAD (rtpav1pay)->clock_rate = 90000;
}

static GType
gst_rtp_av1_aggregate_mode_get_type ()
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {GST_RTP_AV1_AGGREGATE_NONE, "No aggregation of OBUs", "none"},
    {GST_RTP_AV1_AGGREGATE_TU,
        "Aggregate temporal units into a single RTP packet", "tu"},
    {0, NULL, NULL},
  };

  if (!type)
    type = g_enum_register_static ("GstRtpAv1AggregateMode", values);

  return type;
}

static void
gst_rtp_av1_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpAv1Pay *rtpav1pay = GST_RTP_AV1_PAY (object);

  switch (prop_id) {
    case PROP_AGGREGATE_MODE:
      rtpav1pay->aggregate_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_av1_pay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpAv1Pay *rtpav1pay = GST_RTP_AV1_PAY (object);

  switch (prop_id) {
    case PROP_AGGREGATE_MODE:
      g_value_set_enum (value, rtpav1pay->aggregate_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_av1_pay_finalize (GObject * object)
{
  GstRtpAv1Pay *rtpav1pay = GST_RTP_AV1_PAY (object);

  gst_rtp_av1_pay_reset_bundle (rtpav1pay);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_rtp_av1_pay_payload_tu (GstRTPBasePayload * basepayload, GstBuffer * buffer,
    GstClockTime pts, GstClockTime dts);
static GstFlowReturn
gst_rtp_av1_pay_payload_obu_element (GstRTPBasePayload * basepayload,
    GstBuffer * buffer, guint8 obu_type, GstClockTime pts, GstClockTime dts,
    gboolean tu_end);
static GstFlowReturn
gst_rtp_av1_pay_payload_push (GstRTPBasePayload * basepayload,
    GstBuffer * buffer, GstClockTime pts, GstClockTime dts, gboolean tu_end,
    guint8 z_bit, guint8 y_bit, guint8 w_bits);

static GstFlowReturn
gst_rtp_av1_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstClockTime pts, dts;

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);

  return gst_rtp_av1_pay_payload_tu (basepayload, buffer, pts, dts);
}

static gboolean
gst_rtp_av1_pay_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = gst_pad_query_default (pad, parent, query);

  return ret;
}

static void
gst_rtp_av1_pay_reset_bundle (GstRtpAv1Pay * rtpav1pay)
{
  g_clear_pointer (&rtpav1pay->bundle, gst_buffer_list_unref);
  rtpav1pay->max_bundle_size = 0;
}

static GstFlowReturn
gst_rtp_av1_pay_payload_push (GstRTPBasePayload * basepayload,
    GstBuffer * buffer, GstClockTime pts, GstClockTime dts, gboolean tu_end,
    guint8 z_bit, guint8 y_bit, guint8 w_bits)
{
  GstRtpAv1Pay *rtpav1pay;
  GstBuffer *outbuf;
  GstBuffer *header_buffer;
  GstRTPBuffer rtp = { NULL };
  gboolean map_ret;
  gsize fill_size;
  guint8 header;
  gboolean first;

  rtpav1pay = GST_RTP_AV1_PAY (basepayload);
  outbuf = gst_rtp_base_payload_allocate_output_buffer (basepayload, 0, 0, 0);
  first = rtpav1pay->first_packet;
  if (outbuf == NULL)
    goto error;

  map_ret = gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);
  if (!map_ret)
    goto error2;

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;

  // Set marker to indicate end of temporal unit
  gst_rtp_buffer_set_marker (&rtp, tu_end);
  if (tu_end)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_MARKER);

  gst_rtp_buffer_unmap (&rtp);

  header = 0;
  header |= (z_bit & 1) << 7;   // Z = z_bit
  header |= (y_bit & 1) << 6;   // Y = y_bit
  header |= (w_bits & 3) << 4;  // W = w_bits
  header |= (first & 1) << 3;   // N = first_packet
  header_buffer = gst_buffer_new_and_alloc (1);
  if (header_buffer == NULL)
    goto error2;

  // Prepend the AV1 aggregation header
  fill_size = gst_buffer_fill (header_buffer, 0, &header, 1);
  if (fill_size != 1)
    goto error3;
  buffer = gst_buffer_append (header_buffer, buffer);

  //gst_rtp_copy_video_meta (rtpav1pay, outbuf, buffer);
  outbuf = gst_buffer_append (outbuf, buffer);

  if (first)
    rtpav1pay->first_packet = FALSE;

  return gst_rtp_base_payload_push (basepayload, outbuf);

error3:
  gst_buffer_unref (header_buffer);
error2:
  gst_buffer_unref (outbuf);
error:
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_rtp_av1_pay_payload_obu_element (GstRTPBasePayload * basepayload,
    GstBuffer * buffer, guint8 obu_type, GstClockTime pts, GstClockTime dts,
    gboolean tu_end)
{
  GstRtpAv1Pay *rtpav1pay;
  GstBufferList *bundle;
  gsize max_bundle_size;
  gsize buffer_size;
  guint8 leb128_size;
  guint mtu;

  // Reserved and ignored OBU types
  switch (obu_type) {
    case 0:                    // Reserved
    case 2:                    // OBU_TEMPORAL_DELIMITER
    case 8:                    // OBU_TILE_LIST
    case 9:                    // Reserved
    case 10:                   // Reserved
    case 11:                   // Reserved
    case 12:                   // Reserved
    case 13:                   // Reserved
    case 14:                   // Reserved
      GST_DEBUG ("Ignoring OBU TYPE=%d", obu_type);
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    default:
      break;
  }

  rtpav1pay = GST_RTP_AV1_PAY (basepayload);
  bundle = rtpav1pay->bundle;
  max_bundle_size = rtpav1pay->max_bundle_size;
  mtu = basepayload->mtu;
  if (!bundle) {
    rtpav1pay->bundle = bundle = gst_buffer_list_new ();
    rtpav1pay->max_bundle_size = max_bundle_size = 0;
  }

  if (buffer) {
    leb128_size = 0;
    buffer_size = gst_buffer_get_size (buffer);
    gst_rtp_av1_write_leb128 (buffer_size, &leb128_size);
    gst_buffer_list_add (bundle, gst_buffer_ref (buffer));
    rtpav1pay->max_bundle_size = max_bundle_size =
        max_bundle_size + buffer_size + leb128_size;
  }

  if (!tu_end && gst_rtp_buffer_calc_packet_len (max_bundle_size, 0, 0) <= mtu
      && rtpav1pay->aggregate_mode != GST_RTP_AV1_AGGREGATE_NONE)
    return GST_FLOW_OK;

  GstBuffer *payload = gst_buffer_new_and_alloc (0);
  GstBuffer *fragment_excess = NULL;
  gsize excess_size = 0;
  guint bundle_len = gst_buffer_list_length (bundle);
  guint8 w_bits = 0;
  gsize packaged_bytes = 0;
  gboolean is_fragmented = FALSE;
  GstFlowReturn ret;
  if (payload == NULL)
    goto error;

  if (bundle_len <= 3)
    w_bits = bundle_len;

  for (guint i = 0; i < bundle_len; i++) {
    GstBuffer *buf = gst_buffer_list_get (bundle, i);
    gsize buf_size = gst_buffer_get_size (buf);
    gsize packet_size = gst_rtp_buffer_calc_packet_len (packaged_bytes, 0, 0);
    guint8 leb128_size = 0;

    // Prepend obu size if not w_bit obu (last obu)
    if (w_bits == 0 || i != w_bits - 1) {
      guint64 payload_len_leb128 =
          gst_rtp_av1_write_leb128 (buf_size, &leb128_size);
      GstBuffer *size_header = gst_buffer_new_and_alloc (leb128_size);
      if (size_header == NULL)
        goto error2;
      gsize fill_size =
          gst_buffer_fill (size_header, 0, (guint8 *) & payload_len_leb128,
          leb128_size);
      if (fill_size != leb128_size) {
        gst_buffer_unref (size_header);
        goto error2;
      }

      buf = gst_buffer_append (size_header, buf);
      buf_size += leb128_size;
    }

    if (packet_size + buf_size <= mtu) {
      payload = gst_buffer_append (payload, buf);
      packaged_bytes += buf_size;
    } else {
      gsize remaining_space = mtu - packet_size;

      if (remaining_space > leb128_size) {
        GstBuffer *fragment =
            gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, 0,
            remaining_space);
        excess_size = buf_size - remaining_space;
        fragment_excess =
            gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, remaining_space,
            excess_size);
        gst_buffer_unref (buf);
        payload = gst_buffer_append (payload, fragment);
      } else {
        fragment_excess = buf;
        excess_size = buf_size;
      }

      packaged_bytes += remaining_space;
      is_fragmented = TRUE;
      break;
    }
  }

  gst_rtp_av1_pay_reset_bundle (rtpav1pay);
  ret = gst_rtp_av1_pay_payload_push (basepayload, payload, pts, dts, tu_end
      && !is_fragmented, rtpav1pay->fragment_cont, is_fragmented, w_bits);

  rtpav1pay->fragment_cont = is_fragmented;

  if (is_fragmented) {
    if (gst_rtp_buffer_calc_packet_len (excess_size, 0, 0) > mtu || tu_end) {
      gst_rtp_av1_pay_payload_obu_element (basepayload, fragment_excess,
          obu_type, pts, dts, tu_end);
    } else {
      rtpav1pay->bundle = bundle = gst_buffer_list_new ();
      rtpav1pay->max_bundle_size = max_bundle_size = excess_size;
      gst_buffer_list_add (bundle, gst_buffer_ref (fragment_excess));
    }
  }
  return ret;

error2:
  gst_buffer_unref (payload);
error:
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_rtp_av1_pay_payload_tu (GstRTPBasePayload * basepayload, GstBuffer * buffer,
    GstClockTime pts, GstClockTime dts)
{
  GstMapInfo map = { NULL };
  gboolean map_ret;
  GstBuffer *pay_buffer;
  GstFlowReturn ret;
  gsize buffer_size;
  gsize parsed_bytes;
  guchar *offset;

  buffer_size = gst_buffer_get_size (buffer);
  parsed_bytes = 0;
  map_ret = gst_buffer_map (buffer, &map, GST_MAP_READ);
  offset = map.data;
  if (!map_ret)
    goto error;

  while (parsed_bytes < buffer_size) {
    guint8 obu_header = *offset;
    guint8 obu_forbidden = (obu_header >> 7) & 1;
    guint8 obu_type = (obu_header >> 3) & 0xF;
    guint8 obu_extension_flag = (obu_header >> 2) & 1;
    guint8 obu_size_flag = (obu_header >> 1) & 1;
    guint8 obu_reserved = obu_header & 1;
    gsize obu_payload_size = 0;
    gsize obu_total_bytes = 0;
    gboolean tu_end = FALSE;

    GST_DEBUG ("forbidden: %d, type: %d, extension: %d, size: %d, reserved: %d",
        obu_forbidden, obu_type, obu_extension_flag,
        obu_size_flag, obu_reserved);

    if (obu_forbidden) {
      GST_WARNING ("Parsing failed (forbidden bit set)");
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }

    offset++;
    obu_total_bytes++;

    if (obu_extension_flag) {
      offset++;
      obu_total_bytes++;
    }

    if (obu_size_flag) {
      guint8 read = 0;
      obu_payload_size =
          gst_rtp_av1_read_leb128 (offset, &read,
          buffer_size - obu_total_bytes - parsed_bytes);

      offset += read;
      obu_total_bytes += read;
      obu_total_bytes += obu_payload_size;
      GST_DEBUG ("payload size: %ld, %ld", obu_total_bytes, read);
    } else {
      GST_WARNING ("OBU didn't have a size field");
      obu_payload_size = buffer_size - parsed_bytes - obu_total_bytes;
      obu_total_bytes += obu_payload_size;
    }

    pay_buffer =
        gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, parsed_bytes,
        obu_total_bytes);

    if (parsed_bytes + obu_total_bytes >= buffer_size)
      tu_end = TRUE;

    ret =
        gst_rtp_av1_pay_payload_obu_element (basepayload, pay_buffer, obu_type,
        pts, dts, tu_end);

    if (ret != GST_FLOW_OK)
      break;

    offset += obu_payload_size;
    parsed_bytes += obu_total_bytes;
  }

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);
  return ret;

error:
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

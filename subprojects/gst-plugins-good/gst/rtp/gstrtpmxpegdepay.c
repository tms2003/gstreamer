/* GStreamer
 * Copyright (C) <2023> Chris Wiggins <chris@safercities.com>
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

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

#include <string.h>
#include "gstrtpelements.h"
#include "gstrtpmxpegdepay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpmxpegdepay_debug);
#define GST_CAT_DEFAULT (rtpmxpegdepay_debug)

static GstStaticPadTemplate gst_rtp_mxpeg_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mxpeg")
    );

static GstStaticPadTemplate gst_rtp_mxpeg_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"X-MXPEG\"")
    );

#define gst_rtp_mxpeg_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpMxpegDepay, gst_rtp_mxpeg_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpmxpegdepay, "rtpmxpegdepay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_MXPEG_DEPAY, rtp_element_init (plugin));

static void gst_rtp_mxpeg_depay_finalize (GObject * object);

static gboolean gst_rtp_mxpeg_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mxpeg_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp);

static GstStateChangeReturn gst_rtp_mxpeg_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rtp_mxpeg_depay_class_init (GstRtpMxpegDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mxpeg_depay_finalize;

  gstelement_class->change_state = gst_rtp_mxpeg_depay_change_state;

  gstrtpbasedepayload_class->process_rtp_packet = gst_rtp_mxpeg_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_mxpeg_depay_setcaps;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_mxpeg_depay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_mxpeg_depay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP MXPEG video depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts MXPEG video from RTP packets",
      "Chris Wiggins <chris@safercities.com>");

  GST_DEBUG_CATEGORY_INIT (rtpmxpegdepay_debug, "rtpmxpegdepay", 0,
      "MXPEG video RTP Depayloader");
}

static void
gst_rtp_mxpeg_depay_init (GstRtpMxpegDepay * rtpmxpegdepay)
{
  rtpmxpegdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mxpeg_depay_finalize (GObject * object)
{
  GstRtpMxpegDepay *rtpmxpegdepay;

  rtpmxpegdepay = GST_RTP_MXPEG_DEPAY (object);

  g_object_unref (rtpmxpegdepay->adapter);
  rtpmxpegdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_mxpeg_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *srccaps;
  gint clock_rate;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_empty_simple ("video/x-mxpeg");
  res = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return res;
}

static GstBuffer *
gst_rtp_mxpeg_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp)
{
  GstRtpMxpegDepay *rtpmxpegdepay;
  GstBuffer *pbuf, *outbuf = NULL;
  gboolean marker;

  rtpmxpegdepay = GST_RTP_MXPEG_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (rtp->buffer))
    gst_adapter_clear (rtpmxpegdepay->adapter);

  pbuf = gst_rtp_buffer_get_payload_buffer (rtp);
  marker = gst_rtp_buffer_get_marker (rtp);

  gst_adapter_push (rtpmxpegdepay->adapter, pbuf);

  /* if this was the last packet, create and push a buffer */
  if (marker) {
    guint avail;

    avail = gst_adapter_available (rtpmxpegdepay->adapter);
    outbuf = gst_adapter_take_buffer (rtpmxpegdepay->adapter, avail);

    GST_DEBUG_OBJECT (rtpmxpegdepay, "gst_rtp_mxpeg_depay_chain: pushing buffer of size %"
        G_GSIZE_FORMAT, gst_buffer_get_size (outbuf));
    gst_rtp_drop_non_video_meta (rtpmxpegdepay, outbuf);
  }

  return outbuf;
}

static GstStateChangeReturn
gst_rtp_mxpeg_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMxpegDepay *rtpmxpegdepay;
  GstStateChangeReturn ret;

  rtpmxpegdepay = GST_RTP_MXPEG_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpmxpegdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }
  return ret;
}

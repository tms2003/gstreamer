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
#ifndef __GST_RTP_AV1_DEPAY_H__
#define __GST_RTP_AV1_DEPAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_AV1_DEPAY \
  (gst_rtp_av1_depay_get_type())
#define GST_RTP_AV1_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_AV1_DEPAY,GstRtpAv1Depay))
#define GST_RTP_AV1_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_AV1_DEPAY,GstRtpAv1DepayClass))
#define GST_IS_RTP_AV1_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_AV1_DEPAY))
#define GST_IS_RTP_AV1_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_AV1_DEPAY))

typedef struct _GstRtpAv1Depay GstRtpAv1Depay;
typedef struct _GstRtpAv1DepayClass GstRtpAv1DepayClass;

struct _GstRtpAv1Depay
{
    GstRTPBaseDepayload depayload;
    GstBuffer *prev_fragment;
};

struct _GstRtpAv1DepayClass
{
  GstRTPBaseDepayloadClass parent_class;
};

GType gst_rtp_av1_depay_get_type (void);

G_END_DECLS

#endif /* __GST_RTP_AV1_DEPAY_H__ */
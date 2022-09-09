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
#ifndef __GST_RTP_AV1_PAY_H__
#define __GST_RTP_AV1_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/rtp/gstrtppayloads.h>

G_BEGIN_DECLS
#define GST_TYPE_RTP_AV1_PAY \
  (gst_rtp_av1_pay_get_type())
#define GST_RTP_AV1_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_AV1_PAY,GstRtpAv1Pay))
#define GST_RTP_AV1_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_AV1_PAY,GstRtpAv1PayClass))
#define GST_IS_RTP_AV1_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_AV1_PAY))
#define GST_IS_RTP_AV1_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_AV1_PAY))
    typedef enum
{
  GST_RTP_AV1_AGGREGATE_NONE,
  GST_RTP_AV1_AGGREGATE_TU,
} GstRTPAV1AggregateMode;

typedef struct _GstRtpAv1Pay GstRtpAv1Pay;
typedef struct _GstRtpAv1PayClass GstRtpAv1PayClass;

struct _GstRtpAv1Pay
{
  GstRTPBasePayload payload;
  GstBufferList *bundle;
  gsize max_bundle_size;
  GstRTPAV1AggregateMode aggregate_mode;
  gboolean first_packet;
  gboolean fragment_cont;
};

struct _GstRtpAv1PayClass
{
  GstRTPBasePayloadClass parent_class;
};

GType gst_rtp_av1_pay_get_type (void);

G_END_DECLS
#endif /* __GST_RTP_AV1_PAY_H__ */

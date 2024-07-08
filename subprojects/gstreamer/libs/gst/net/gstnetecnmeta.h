/* GStreamer
 * Copyright (C) <2022> British Broadcasting Corporation
 *   Author: Sam Hurst <sam.hurst@bbc.co.uk>
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

#ifndef __GST_NET_ECN_META_H__
#define __GST_NET_ECN_META_H__

#include <gst/gst.h>
#include <gst/net/net-prelude.h>

G_BEGIN_DECLS

typedef struct _GstNetEcnMeta GstNetEcnMeta;

/**
 * GstNetEcnCp:
 * @GST_NET_ECN_META_NO_ECN: Non ECN-capable transport
 * @GST_NET_ECN_META_ECT_1: ECN Capable Transport, ECT(1)
 * @GST_NET_ECN_META_ECT_0: ECN Capable Transport, ECT(0)
 * @GST_NET_ECN_META_ECT_CE: Congestion Encountered, CE
 *
 * ECN codepoints.
 *
 * Since: 1.24
 */
typedef enum _GstNetEcnCp {
  GST_NET_ECN_META_NO_ECN = 0x0,
  GST_NET_ECN_META_ECT_1  = 0x1,
  GST_NET_ECN_META_ECT_0  = 0x2,
  GST_NET_ECN_META_ECT_CE = 0x3
} GstNetEcnCp;

GST_NET_API
GType gst_net_ecn_cp_get_type (void);
#define GST_NET_ECN_CP_TYPE (gst_net_ecn_cp_get_type())

/**
 * GstNetEcnMeta:
 * @meta: the parent type
 * @cp: The ECN CP for the received buffer
 *
 * Buffer metadata for Explicit Congestion Notification on received buffers
 *
 * Since: 1.24
 */
struct _GstNetEcnMeta {
  GstMeta meta;

  GstNetEcnCp cp;
};

GST_NET_API
GType gst_net_ecn_meta_api_get_type (void);
#define GST_NET_ECN_META_API_TYPE (gst_net_ecn_meta_api_get_type())

/* implementation */

GST_NET_API
const GstMetaInfo *gst_net_ecn_meta_get_info (void);
#define GST_NET_ECN_META_INFO (gst_net_ecn_meta_get_info())

GST_NET_API
GstNetEcnMeta * gst_buffer_add_net_ecn_meta (GstBuffer   *buffer,
                                             GstNetEcnCp cp);

GST_NET_API
GstNetEcnMeta * gst_buffer_get_net_ecn_meta (GstBuffer   *buffer);

G_END_DECLS

#endif /* __GST_NET_ECN_META_H__ */

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
#ifndef __GST_RTP_AV1_COMMON_H__
#define __GST_RTP_AV1_COMMON_H__

#include <gst/gst.h>

guint32
gst_rtp_av1_read_leb128(const guint8 *leb128, guint8 *read_bytes,
                        guint max_len);

gsize gst_rtp_av1_write_leb128(guint64 value, guint8 *leb128);

#endif /* __GST_RTP_AV1_COMMON_H__ */
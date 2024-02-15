/* gstrtpvp8parse.h
 * Copyright (C) 2024 Benjamin Gaignard <benjamin.gaignard@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GST_RTP_VP8_PARSE_H__
#define __GST_RTP_VP8_PARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstRtpVP8Parse GstRtpVP8Parse;

struct _GstRtpVP8Parse
{
  gboolean is_keyframe;
  gboolean refresh_golden_frame;
  gboolean refresh_alternate_frame;
  gint n_partitions;
  /* Treat frame header & tag & partition size block as the first partition,
   * folowed by max. 8 data partitions. last offset is the end of the buffer */
  guint partition_offset[10];
  guint partition_size[9];
};

gboolean gst_rtp_vp8_parse_header (GstBuffer * buffer,
	gsize buffer_size, GstRtpVP8Parse * parsed_header);

G_END_DECLS

#endif /* #ifndef __GST_RTP_VP8_PARSE_H__ */

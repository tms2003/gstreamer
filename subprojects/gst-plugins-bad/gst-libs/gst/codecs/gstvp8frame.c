/* GStreamer
 * Copyright (C) 2022 Benjamin Gaignard <benjamin.gaignard@collabora.com>
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
#include <config.h>
#endif

#include "gstvp8frame.h"

GST_DEBUG_CATEGORY_EXTERN (gst_vp8_encoder_debug);
#define GST_CAT_DEFAULT gst_vp8_encoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstVp8Frame, gst_vp8_frame);

static void
_gst_vp8_frame_free (GstVp8Frame * frame)
{
  GST_TRACE ("Free frame %p", frame);

  gst_video_codec_frame_unref (frame->frame);

  g_free (frame);
}

/**
 * gst_vp8_frame_new:
 *
 * Create new #GstVp8Frame
 *
 * Returns: a new #GstVp8Frame
 */
GstVp8Frame *
gst_vp8_frame_new (GstVideoCodecFrame * f)
{
  GstVp8Frame *frame;

  if (!f)
    return NULL;

  frame = g_new0 (GstVp8Frame, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (frame), 0,
      GST_TYPE_VP8_FRAME, NULL, NULL,
      (GstMiniObjectFreeFunction) _gst_vp8_frame_free);

  frame->frame = gst_video_codec_frame_ref (f);

  GST_TRACE ("New frame %p", frame);

  return frame;
}

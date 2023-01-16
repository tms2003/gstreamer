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

#ifndef __GTS_VP8_FRAME_H__
#define __GTS_VP8_FRAME_H__

#include <gst/codecs/codecs-prelude.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_VP8_FRAME     (gst_vp8_frame_get_type())
#define GST_IS_VP8_FRAME(obj)  (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_VP8_FRAME))
#define GST_VP8_FRAME(obj)     ((GstVp8Frame *)obj)
#define GST_VP8_FRAME_CAST(obj) (GST_VP8_FRAME(obj))

typedef struct _GstVp8Frame GstVp8Frame;

enum
{
  GstVp8Keyframe,
  GstVp8Inter,
};

struct _GstVp8Frame
{
  GstMiniObject parent;
  gint type;
  gint quality;

  GstVideoCodecFrame *frame;
};

GST_CODECS_API
GType gst_vp8_frame_get_type (void);

GST_CODECS_API
GstVp8Frame * gst_vp8_frame_new (GstVideoCodecFrame *f);

static inline GstVp8Frame *
gst_vp8_frame_ref (GstVp8Frame * frame)
{
  return (GstVp8Frame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

static inline void
gst_vp8_frame_unref (GstVp8Frame * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

G_END_DECLS

#endif /* __GTS_VP8_FRAME_H__ */

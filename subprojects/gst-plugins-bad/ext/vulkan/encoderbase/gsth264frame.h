/* GStreamer
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
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

#ifndef __GTS_H264_FRAME_H__
#define __GTS_H264_FRAME_H__

#include <gst/codecs/codecs-prelude.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_H264_FRAME     (gst_h264_encode_frame_get_type())
#define GST_IS_H264_FRAME(obj)  (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_H264_FRAME))
#define GST_H264_FRAME(obj)     ((GstH264EncodeFrame *)obj)
#define GST_H264_FRAME_CAST(obj) (GST_H264_FRAME(obj))

typedef struct _GstH264EncodeFrame GstH264EncodeFrame;


struct _GstH264EncodeFrame
{
  GstMiniObject parent;

  GstVideoCodecFrame *frame;
  GstH264SliceType type;
  gboolean is_ref;
  guint pyramid_level;
  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;

  gint poc;
  gint frame_num;
  /* The pic_num will be marked as unused_for_reference, which is
   * replaced by this frame. -1 if we do not need to care about it
   * explicitly. */
  gint unused_for_reference_pic_num;

  /* The total frame count we handled. */
  guint64 total_frame_count;

  gpointer       user_data;
  GDestroyNotify user_data_destroy_notify;

  gboolean last_frame;
};

GST_CODECS_API
GType gst_h264_encode_frame_get_type (void);

GST_CODECS_API
GstH264EncodeFrame *    gst_h264_encode_frame_new                (GstVideoCodecFrame *f);
GST_CODECS_API
void                    gst_h264_encode_frame_free               (gpointer pframe);

GST_CODECS_API
void                    gst_h264_encode_frame_set_user_data      (GstH264EncodeFrame * frame, gpointer user_data, GDestroyNotify notify);
GST_CODECS_API
gpointer                gst_h264_encode_frame_get_user_data      (GstH264EncodeFrame * frame);

static inline GstH264EncodeFrame *
gst_h264_encode_frame_ref (GstH264EncodeFrame * frame)
{
  return (GstH264EncodeFrame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

static inline void
gst_h264_encode_frame_unref (void * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

G_END_DECLS

#endif /* __GTS_H264_FRAME_H__ */
/* GStreamer
 * Copyright (C) 2023 Stéphane Cerveau <scerveau@igalia.com>
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
#include "gsth265frame.h"

GST_DEBUG_CATEGORY_EXTERN (gst_h265_encoder_debug);
#define GST_CAT_DEFAULT gst_h265_encoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstH265EncodeFrame, gst_h265_encode_frame);

void
gst_h265_encode_frame_free (gpointer pframe)
{
  GstH265EncodeFrame *frame = pframe;
  GST_TRACE ("Free frame %p", frame);
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  g_free (frame);
}

/**
 * gst_h265_encode_frame_new:
 *
 * Create new #GstH265EncodeFrame
 *
 * Returns: a new #GstH265EncodeFrame
 */
GstH265EncodeFrame *
gst_h265_encode_frame_new (GstVideoCodecFrame * f)
{
  GstH265EncodeFrame *frame;

  if (!f)
    return NULL;

  frame = g_new0 (GstH265EncodeFrame, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (frame), 0,
      GST_TYPE_H265_FRAME, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_h265_encode_frame_free);

  frame->frame = f;

  GST_TRACE ("New frame  %p", frame);

  return frame;
}

/**
 * gst_h265_encode_frame_set_user_data:
 * @frame: a #GstH265EncodeFrame
 * @user_data: private data
 * @notify: (closure user_data): a #GDestroyNotify
 *
 * Sets @user_data on the frame and the #GDestroyNotify that will be called when
 * the frame is freed. Allows to attach private data by the subclass to frames.
 *
 * If a @user_data was previously set, then the previous set @notify will be called
 * before the @user_data is replaced.
 */
void
gst_h265_encode_frame_set_user_data (GstH265EncodeFrame * frame,
    gpointer user_data, GDestroyNotify notify)
{
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  frame->user_data = user_data;
  frame->user_data_destroy_notify = notify;
}

/**
 * gst_h265_encode_frame_get_user_data:
 * @frame: a #GstH265EncodeFrame
 *
 * Gets private data set on the frame by the subclass via
 * gst_video_codec_frame_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_h265_encode_frame_get_user_data (GstH265EncodeFrame * frame)
{
  return frame->user_data;
}

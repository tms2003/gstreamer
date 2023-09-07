/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstjpegpicture.h"

GST_DEFINE_MINI_OBJECT_TYPE (GstJpegPicture, gst_jpeg_picture);

static void
gst_jpeg_picture_free (GstJpegPicture * picture)
{
  GstCodecPicture *codec_picture = GST_CODEC_PICTURE (picture);

  if (codec_picture->notify)
    codec_picture->notify (codec_picture->user_data);

  if (codec_picture->discont_state)
    gst_video_codec_state_unref (codec_picture->discont_state);

  g_free (picture);
}

GstJpegPicture *
gst_jpeg_picture_new (void)
{
  GstJpegPicture *picture;

  picture = g_new0 (GstJpegPicture, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (picture), 0,
      GST_TYPE_JPEG_PICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_jpeg_picture_free);

  return picture;
}

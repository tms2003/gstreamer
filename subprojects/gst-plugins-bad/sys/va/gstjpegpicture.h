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

#pragma once

#include <gst/codecs/gstcodecpicture.h>

G_BEGIN_DECLS

#define GST_TYPE_JPEG_PICTURE     (gst_jpeg_picture_get_type())
#define GST_JPEG_PICTURE(obj)     ((GstJpegPicture *)obj)

typedef struct _GstJpegPicture GstJpegPicture;

struct _GstJpegPicture
{
  /*< private >*/
  GstCodecPicture parent;
};

GType gst_jpeg_picture_get_type (void);

GstJpegPicture * gst_jpeg_picture_new (void);

static inline GstJpegPicture *
gst_jpeg_picture_ref (GstJpegPicture * picture)
{
  return (GstJpegPicture *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (picture));
}

static inline void
gst_jpeg_picture_unref (GstJpegPicture * picture)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (picture));
}

static inline gboolean
gst_jpeg_picture_replace (GstJpegPicture ** old_picture,
    GstJpegPicture * new_picture)
{
  return gst_mini_object_replace ((GstMiniObject **) old_picture,
      (GstMiniObject *) new_picture);
}

static inline void
gst_clear_jpeg_picture (GstJpegPicture ** picture)
{
  gst_clear_mini_object ((GstMiniObject **) picture);
}

static inline void
gst_jpeg_picture_set_user_data (GstJpegPicture * picture, gpointer user_data,
    GDestroyNotify notify)
{
  gst_codec_picture_set_user_data (GST_CODEC_PICTURE (picture),
      user_data, notify);
}

static inline gpointer
gst_jpeg_picture_get_user_data (GstJpegPicture * picture)
{
  return gst_codec_picture_get_user_data (GST_CODEC_PICTURE (picture));
}

G_END_DECLS

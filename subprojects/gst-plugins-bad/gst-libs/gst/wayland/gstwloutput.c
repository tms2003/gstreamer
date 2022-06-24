/* GStreamer Wayland Library
 *
 * Copyright (C) 2022 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwloutput.h"

typedef struct _GstWlOutputPrivate
{
  uint32_t id;
  enum wl_output_transform transform;
} GstWlOutputPrivate;

G_DEFINE_TYPE_WITH_CODE (GstWlOutput, gst_wl_output, G_TYPE_OBJECT,
    G_ADD_PRIVATE (GstWlOutput)
    );

enum
{
  SIGNAL_0,
  SIGNAL_DESTROY,
  SIGNAL_GEOMETRY_CHANGED,
  LAST_SIGNAL
};

static guint gst_wl_output_signals[LAST_SIGNAL] = { 0 };

static void gst_wl_output_finalize (GObject * gobject);

static void
gst_wl_output_class_init (GstWlOutputClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_wl_output_finalize;

  gst_wl_output_signals[SIGNAL_DESTROY] =
      g_signal_new ("destroy", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  gst_wl_output_signals[SIGNAL_GEOMETRY_CHANGED] =
      g_signal_new ("geometry-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
gst_wl_output_init (GstWlOutput * self)
{
}

GstWlOutput *
gst_wl_output_new (uint32_t id)
{
  GstWlOutput *self;
  GstWlOutputPrivate *priv;

  self = g_object_new (GST_TYPE_WL_OUTPUT, NULL);
  priv = gst_wl_output_get_instance_private (self);

  priv->id = id;

  return self;
}

static void
gst_wl_output_finalize (GObject * gobject)
{
  GstWlOutput *self = GST_WL_OUTPUT (gobject);

  g_signal_emit (self, gst_wl_output_signals[SIGNAL_DESTROY], 0);

  G_OBJECT_CLASS (gst_wl_output_parent_class)->finalize (gobject);
}

uint32_t
gst_wl_output_get_id (GstWlOutput * self)
{
  GstWlOutputPrivate *priv = gst_wl_output_get_instance_private (self);

  return priv->id;
}

enum wl_output_transform
gst_wl_output_get_transform (GstWlOutput * self)
{
  GstWlOutputPrivate *priv = gst_wl_output_get_instance_private (self);

  return priv->transform;
}

void
gst_wl_output_set_transform (GstWlOutput * self,
    enum wl_output_transform transform)
{
  GstWlOutputPrivate *priv = gst_wl_output_get_instance_private (self);

  priv->transform = transform;
  g_signal_emit (self, gst_wl_output_signals[SIGNAL_GEOMETRY_CHANGED], 0);
}

enum wl_output_transform
gst_wl_output_transform_from_orientation (GstVideoOrientationMethod method)
{
  switch (method) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
      return WL_OUTPUT_TRANSFORM_NORMAL;
    case GST_VIDEO_ORIENTATION_90R:
      return WL_OUTPUT_TRANSFORM_90;
    case GST_VIDEO_ORIENTATION_180:
      return WL_OUTPUT_TRANSFORM_180;
    case GST_VIDEO_ORIENTATION_90L:
      return WL_OUTPUT_TRANSFORM_270;
    case GST_VIDEO_ORIENTATION_HORIZ:
      return WL_OUTPUT_TRANSFORM_FLIPPED;
    case GST_VIDEO_ORIENTATION_VERT:
      return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    case GST_VIDEO_ORIENTATION_UL_LR:
      return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    case GST_VIDEO_ORIENTATION_UR_LL:
      return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    default:
      g_assert_not_reached ();
  }
}

GstVideoOrientationMethod
gst_wl_output_orientation_from_transform (enum wl_output_transform transform)
{
  switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      return GST_VIDEO_ORIENTATION_IDENTITY;
    case WL_OUTPUT_TRANSFORM_90:
      return GST_VIDEO_ORIENTATION_90R;
    case WL_OUTPUT_TRANSFORM_180:
      return GST_VIDEO_ORIENTATION_180;
    case WL_OUTPUT_TRANSFORM_270:
      return GST_VIDEO_ORIENTATION_90L;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
      return GST_VIDEO_ORIENTATION_HORIZ;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      return GST_VIDEO_ORIENTATION_VERT;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      return GST_VIDEO_ORIENTATION_UL_LR;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      return GST_VIDEO_ORIENTATION_UR_LL;
    default:
      g_assert_not_reached ();
  }
}

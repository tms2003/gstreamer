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

#include <gst/textlayoutoverlay/gstbasetextlayoutoverlay.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_TIME_OVERLAY             (gst_base_time_overlay_get_type())
#define GST_BASE_TIME_OVERLAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_TIME_OVERLAY,GstBaseTimeOverlay))
#define GST_BASE_TIME_OVERLAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BASE_TIME_OVERLAY,GstBaseTimeOverlayClass))
#define GST_BASE_TIME_OVERLAY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_BASE_TIME_OVERLAY,GstBaseTimeOverlayClass))
#define GST_IS_BASE_TIME_OVERLAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_TIME_OVERLAY))
#define GST_IS_BASE_TIME_OVERLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BASE_TIME_OVERLAY))

typedef struct _GstBaseTimeOverlay GstBaseTimeOverlay;
typedef struct _GstBaseTimeOverlayClass GstBaseTimeOverlayClass;
typedef struct _GstBaseTimeOverlayPrivate GstBaseTimeOverlayPrivate;

/**
 * GstBaseTimeOverlay:
 *
 * Since: 1.24
 */
struct _GstBaseTimeOverlay
{
  GstBaseTextLayoutOverlay parent;

  /*< private >*/
  GstBaseTimeOverlayPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBaseTimeOverlayClass:
 *
 * Since: 1.24
 */
struct _GstBaseTimeOverlayClass
{
  GstBaseTextLayoutOverlayClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_TEXT_LAYOUT_OVERLAY_API
GType gst_base_time_overlay_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstBaseTimeOverlay, gst_object_unref)

G_END_DECLS

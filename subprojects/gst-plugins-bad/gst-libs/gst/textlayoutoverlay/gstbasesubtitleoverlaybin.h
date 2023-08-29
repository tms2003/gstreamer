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

#include <gst/gst.h>
#include <gst/textlayoutoverlay/textlayoutoverlay-prelude.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN             (gst_base_subtitle_overlay_bin_get_type())
#define GST_BASE_SUBTITLE_OVERLAY_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN,GstBaseSubtitleOverlayBin))
#define GST_BASE_SUBTITLE_OVERLAY_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN,GstBaseSubtitleOverlayBinClass))
#define GST_BASE_SUBTITLE_OVERLAY_BIN_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN,GstBaseSubtitleOverlayBinClass))
#define GST_IS_BASE_SUBTITLE_OVERLAY_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN))
#define GST_IS_BASE_SUBTITLE_OVERLAY_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN))

typedef struct _GstBaseSubtitleOverlayBin GstBaseSubtitleOverlayBin;
typedef struct _GstBaseSubtitleOverlayBinClass GstBaseSubtitleOverlayBinClass;
typedef struct _GstBaseSubtitleOverlayBinPrivate GstBaseSubtitleOverlayBinPrivate;

/**
 * GstBaseSubtitleOverlayBin:
 *
 * Since: 1.24
 */
struct _GstBaseSubtitleOverlayBin
{
  GstBin parent;

  /*< private >*/
  GstBaseSubtitleOverlayBinPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBaseSubtitleOverlayBinClass:
 *
 * Since: 1.24
 */
struct _GstBaseSubtitleOverlayBinClass
{
  GstBinClass parent_class;

  /**
   * GstBaseSubtitleOverlayBinClass::get_overlay_factory:
   * @overlay: a #GstBaseSubtitleOverlayBin
   *
   * Gets subtitle overlay element factory. The element must be subclass of
   * #GstBaseSubtitleOverlay
   */
  const gchar * (*get_overlay_factory) (GstBaseSubtitleOverlayBin * overlay);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_TEXT_LAYOUT_OVERLAY_API
GType gst_base_subtitle_overlay_bin_get_type (void);

GST_TEXT_LAYOUT_OVERLAY_API
GstElement * gst_base_subtitle_overlay_bin_get_overlay (GstBaseSubtitleOverlayBin * overlay);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstBaseSubtitleOverlayBin, gst_object_unref)

G_END_DECLS

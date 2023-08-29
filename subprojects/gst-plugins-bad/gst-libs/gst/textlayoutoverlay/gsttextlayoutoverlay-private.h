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

G_BEGIN_DECLS

#define GST_TYPE_BASE_SUBTITLE_OVERLAY_SOURCE (gst_base_subtitle_overlay_source_get_type())

typedef enum
{
  GST_BASE_SUBTITLE_OVERLAY_SOURCE_SUBTITLE = (1 << 0),
  GST_BASE_SUBTITLE_OVERLAY_SOURCE_CC = (1 << 1),
  GST_BASE_SUBTITLE_OVERLAY_SOURCE_PREFER_CC = (1 << 2),
} GstBaseSubtitleOverlaySource;

GType gst_base_subtitle_overlay_source_get_type (void);

void  gst_base_text_layout_overlay_install_properties (GObjectClass * object_class,
                                                       guint last_prop_index,
                                                       guint * prop_index);

void  gst_base_subtitle_overlay_install_properties (GObjectClass * object_class,
                                                    guint last_prop_index,
                                                    guint * n_props);

G_END_DECLS

/* GStreamer Wayland Library
 *
 * Copyright (C) 2023 Collabora Ltd.
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

#pragma once

#include <gst/wayland/wayland.h>

G_BEGIN_DECLS

#define GST_TYPE_WL_DMABUF_FEEDBACK (gst_wl_dmabuf_feedback_get_type ())
G_DECLARE_FINAL_TYPE (GstWlDmaBufFeedback, gst_wl_dmabuf_feedback, GST, WL_DMABUF_FEEDBACK, GObject)

GstWlDmaBufFeedback* gst_wl_dmabuf_feedback_new_for_display (GstWlDisplay *display);

GstWlDmaBufFeedback* gst_wl_dmabuf_feedback_new_for_display_legacy (GstWlDisplay *display);

gboolean gst_wl_dmabuf_feedback_query_format_support (GstWlDmaBufFeedback *self, guint32 fourcc, guint64 modifier,
  gboolean *is_modifier, gboolean *is_implicit, gboolean *is_linear);

gboolean gst_wl_dmabuf_feedback_fill_drm_format_list (GstWlDmaBufFeedback *self, GValue *format_list);

gchar *gst_wl_dmabuf_feedback_get_main_device (GstWlDmaBufFeedback *self);

G_END_DECLS

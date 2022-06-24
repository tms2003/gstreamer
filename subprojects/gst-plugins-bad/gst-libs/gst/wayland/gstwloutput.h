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

#pragma once

#include <gst/wayland/wayland.h>

G_BEGIN_DECLS

#define GST_TYPE_WL_OUTPUT (gst_wl_output_get_type ())
G_DECLARE_FINAL_TYPE (GstWlOutput, gst_wl_output, GST, WL_OUTPUT, GObject);

struct _GstWlOutput {
  GObject parent_instance;
};

GstWlOutput * gst_wl_output_new (uint32_t id);

uint32_t gst_wl_output_get_id (GstWlOutput * self);

enum wl_output_transform gst_wl_output_get_transform (GstWlOutput * self);

void gst_wl_output_set_transform (GstWlOutput * self,
    enum wl_output_transform transform);

enum wl_output_transform gst_wl_output_transform_from_orientation (GstVideoOrientationMethod method);

GstVideoOrientationMethod gst_wl_output_orientation_from_transform (enum wl_output_transform transform);

G_END_DECLS

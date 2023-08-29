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

#include <gst/video/video.h>
#include <gst/base/base.h>

G_BEGIN_DECLS

#define GST_TYPE_PANGO_OVERLAY_OBJECT (gst_pango_overlay_object_get_type())
G_DECLARE_FINAL_TYPE (GstPangoOverlayObject,
    gst_pango_overlay_object, GST, PANGO_OVERLAY_OBJECT, GstObject);

GstPangoOverlayObject * gst_pango_overlay_object_new (void);

gboolean  gst_pango_overlay_object_start (GstPangoOverlayObject * object);

gboolean  gst_pango_overlay_object_stop (GstPangoOverlayObject * object);

gboolean  gst_pango_overlay_object_set_caps (GstPangoOverlayObject * object,
                                             GstElement * elem,
                                             GstCaps * out_caps,
                                             gboolean * supported);

gboolean gst_pango_overlay_object_decide_allocation (GstPangoOverlayObject * object,
                                                     GstElement * elem,
                                                     GstQuery * query);

gboolean gst_pango_overlay_object_accept_attribute (GstPangoOverlayObject * object,
                                                    GstTextAttr * attr);

GstFlowReturn gst_pango_overlay_object_draw (GstPangoOverlayObject * object,
                                              GstTextLayout * layout,
                                              GstBuffer * buffer);

G_END_DECLS

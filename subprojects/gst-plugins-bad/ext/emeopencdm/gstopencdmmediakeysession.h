/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2022, 2023 Collabora Ltd.
 *   @author Jordan Yelloz <jordan.yelloz@collabora.com>
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
#include <gst/eme/eme.h>

G_BEGIN_DECLS

#define GST_FLOW_EME_SESSION_TIMEOUT GST_FLOW_CUSTOM_ERROR

#define GST_TYPE_OPEN_CDM_MEDIA_KEY_SESSION \
    (gst_open_cdm_media_key_session_get_type())

G_DECLARE_FINAL_TYPE (GstOpenCDMMediaKeySession,
    gst_open_cdm_media_key_session, GST, OPEN_CDM_MEDIA_KEY_SESSION,
    GstObject);

GstOpenCDMMediaKeySession *gst_open_cdm_media_key_session_new (
    GstMediaKeySessionType type, GstObject * parent);

void gst_open_cdm_media_key_session_unparent (GstOpenCDMMediaKeySession * self);

gboolean gst_open_cdm_media_key_session_populate (
    GstOpenCDMMediaKeySession * self, GstBuffer * key_id);

GstFlowReturn gst_open_cdm_media_key_session_decrypt (
    GstOpenCDMMediaKeySession * self, GstBuffer * buffer, GstBuffer * iv,
    GstBuffer * key_id, GstBuffer * subsamples, guint32 subsample_count,
    GstClockTime timeout);

G_END_DECLS

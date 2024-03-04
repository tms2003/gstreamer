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

G_BEGIN_DECLS

#define GST_TYPE_OPEN_CDM_PROTECTION_SYSTEM \
    (gst_open_cdm_protection_system_get_type())

G_DECLARE_FINAL_TYPE (GstOpenCDMProtectionSystem,
    gst_open_cdm_protection_system, GST, OPEN_CDM_PROTECTION_SYSTEM,
    GstElement);

void gst_open_cdm_protection_system_request_media_key_system_access (
    GstOpenCDMProtectionSystem * self, const gchar * key_system_id,
    GstCaps * supported_configurations, GstPromise * promise);

G_END_DECLS

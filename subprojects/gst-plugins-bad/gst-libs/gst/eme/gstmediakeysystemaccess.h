/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2022, 2023 Collabora Ltd.
 *   @author George Kiagiadakis <george.kiagiadakis@collabora.com>
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

#include "eme-prelude.h"

G_BEGIN_DECLS

#define GST_TYPE_MEDIA_KEY_SYSTEM_ACCESS ( \
    gst_media_key_system_access_get_type ())

GST_EME_API
G_DECLARE_INTERFACE (GstMediaKeySystemAccess, gst_media_key_system_access,
    GST, MEDIA_KEY_SYSTEM_ACCESS, GObject);

/**
 * GstMediaKeySystemAccess:
 *
 * Since: 1.24
 */

/**
 * GstMediaKeySystemAccessInterface:
 * @g_iface: base interface
 *
 * Since: 1.24
 */
struct _GstMediaKeySystemAccessInterface
{
  /*< private >*/
  GTypeInterface g_iface;

  /*< public >*/

  /**
   * GstMediaKeySystemAccessInterface.get_key_system:
   * @self: #GstMediaKeySystemAccess instance
   *
   * Implementation of #gst_media_key_system_access_get_key_system()
   *
   * Returns: (transfer none): The key system ID in text format
   * Since: 1.24
   */
  const gchar * (*get_key_system)    (GstMediaKeySystemAccess * self);

  /**
   * GstMediaKeySystemAccessInterface.get_configuration:
   * @self: #GstMediaKeySystemAccess instance
   *
   * Implementation of #gst_media_key_system_access_get_configuration()
   *
   * Returns: (transfer full): a new #GstCaps containing the configuration of
   * @self
   *
   * Since: 1.24
   */
  GstCaps *     (*get_configuration) (GstMediaKeySystemAccess * self);

  /**
   * GstMediaKeySystemAccessInterface.create_media_keys:
   * @self: #GstMediaKeySystemAccess instance
   * @promise: (transfer none): Implementations must answer this promise with a
   * #GstMediaKeys implementation or an error
   *
   * Implementation of #gst_media_key_system_access_create_media_keys()
   *
   * Since: 1.24
   */
  void          (*create_media_keys) (GstMediaKeySystemAccess * self,
                                      GstPromise              * promise);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_EME_API
const gchar * gst_media_key_system_access_get_key_system (
    GstMediaKeySystemAccess * self);

GST_EME_API
GstCaps * gst_media_key_system_access_get_configuration (
    GstMediaKeySystemAccess * self);

GST_EME_API
void gst_media_key_system_access_create_media_keys (
    GstMediaKeySystemAccess * self, GstPromise * promise);

G_END_DECLS

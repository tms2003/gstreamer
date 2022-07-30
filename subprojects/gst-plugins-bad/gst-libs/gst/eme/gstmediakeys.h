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
#include "gstmediakeysession.h"

G_BEGIN_DECLS

#define GST_TYPE_MEDIA_KEYS (gst_media_keys_get_type ())

GST_EME_API
G_DECLARE_INTERFACE (GstMediaKeys, gst_media_keys, GST, MEDIA_KEYS, GObject);

/**
 * GstMediaKeySessionType:
 * @GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY: Data associated with this type of
 * session will not be stored in persistent storage
 * @GST_MEDIA_KEY_SESSION_TYPE_PERSISTENT_LICENSE: Data associated with this
 * type of session may be stored in persistent storage and loaded from that
 * storage.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysessiontype)
 *
 * Since: 1.24
 */
typedef enum
{
  GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY,
  GST_MEDIA_KEY_SESSION_TYPE_PERSISTENT_LICENSE,
} GstMediaKeySessionType;

/**
 * GstMediaKeys:
 *
 * Since: 1.24
 */

/**
 * GstMediaKeysInterface:
 * @g_iface: base interface
 *
 * Since: 1.24
 */
struct _GstMediaKeysInterface
{
  GTypeInterface g_iface;

  /**
   * GstMediaKeysInterface.create_session:
   * @self: #GstMediaKeys instance
   * @type: #GstMediaKeys instance
   * @error: (out) (optional) (nullable) (transfer full): the resulting error or
   * `NULL`
   *
   * Implementation of #gst_media_keys_create_session()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeys-createsession)
   *
   * Returns: (transfer full): A new #GstMediaKeySession or `NULL`
   * Since: 1.24
   */
  GstMediaKeySession * (*create_session) (GstMediaKeys * self,
      GstMediaKeySessionType type, GError ** error);
  /**
   * GstMediaKeysInterface.set_server_certificate:
   * @self: #GstMediaKeys instance
   * @certificate: (transfer none): #GstBuffer containing the server certificate
   * data
   * @promise: (transfer none): #GstPromise which will be answered either
   * successfully or unsuccessfully based on the underlying CDM's behavior
   *
   * Implementation of #gst_media_keys_set_server_certificate()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeys-setservercertificate)
   *
   * Since: 1.24
   */
  void (*set_server_certificate) (GstMediaKeys * self,
    GstBuffer * certificate, GstPromise * promise);

  gpointer _gst_reserved[GST_PADDING];
};

GST_EME_API
GstMediaKeySession * gst_media_keys_create_session (GstMediaKeys * self,
    GstMediaKeySessionType type, GError ** error);

GST_EME_API
void gst_media_keys_set_server_certificate (GstMediaKeys * self,
    GstBuffer * certificate, GstPromise * promise);

G_END_DECLS

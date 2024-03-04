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

/**
 * SECTION:gstmediakeys
 * @title: GstMediaKeys
 * @short_description: Media Keys
 *
 * #GstMediaKeys is an interface which maps to an instance of an underlying
 * Content Decryption Module (CDM). It maintains a set of #GstMediaKeySession
 * children which can be used to decrypt specific groups of content.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#mediakeys-interface)
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmediakeys.h"
#include "gstemelogging-private.h"

G_DEFINE_INTERFACE_WITH_CODE (GstMediaKeys, gst_media_keys, G_TYPE_OBJECT,
    gst_eme_init_logging ());


static void
gst_media_keys_default_init (GstMediaKeysInterface * iface)
{
}

/**
 * gst_media_keys_create_session:
 * @self: #GstMediaKeys instance
 * @type: #GstMediaKeySessionType the type of session to create
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or
 * `NULL`
 *
 * Attempts to create a new session for the given session type.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeys-createsession)
 *
 * Returns: (transfer full) (nullable): a new, empty #GstMediaKeySession not yet
 * associated with any Initialization Data on success, otherwise `NULL`
 * Since: 1.24
 */
GstMediaKeySession *
gst_media_keys_create_session (GstMediaKeys * self, GstMediaKeySessionType type,
    GError ** error)
{
  GstMediaKeysInterface *iface = GST_MEDIA_KEYS_GET_IFACE (self);

  return iface->create_session (self, type, error);
}

/**
 * gst_media_keys_set_server_certificate:
 * @self: #GstMediaKeys instance
 * @certificate: (transfer none): #GstBuffer containing the server certificate
 * data
 * @promise: (transfer none): #GstPromise which will be answered either
 * successfully or unsuccessfully based on the underlying CDM's behavior
 *
 * Attempts to supply a server certificate to the underlying CDM. The CDM can
 * use this certificate to encrypt messages intended to be sent to a server
 * acting as the license authority.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeys-setservercertificate)
 *
 * Since: 1.24
 */
void
gst_media_keys_set_server_certificate (GstMediaKeys * self,
    GstBuffer * certificate, GstPromise * promise)
{
  GstMediaKeysInterface *iface = GST_MEDIA_KEYS_GET_IFACE (self);

  iface->set_server_certificate (self, certificate, promise);
}

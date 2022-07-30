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
 * SECTION:gstmediakeysystemaccess
 * @title: GstMediaKeySystemAccess
 * @short_description: Media Key System Access
 *
 * #GstMediaKeySystemAccess is an interface which provides access to a
 * #GstMediaKeys instance.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#mediakeysystemaccess-interface)
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmediakeysystemaccess.h"
#include "gstemelogging-private.h"

G_DEFINE_INTERFACE_WITH_CODE (GstMediaKeySystemAccess,
    gst_media_key_system_access, G_TYPE_OBJECT, gst_eme_init_logging ());

static void
gst_media_key_system_access_default_init (GstMediaKeySystemAccessInterface *
    iface)
{
}

/**
 * gst_media_key_system_access_get_key_system:
 * @self: #GstMediaKeySystemAccess instance
 *
 * Get the Key System ID for for the requested instance.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysystemaccess-keysystem)
 *
 * Returns: (transfer none): A string containing the identifier of the key
 * system supported by this instance.
 *
 * Since: 1.24
 */
const gchar *
gst_media_key_system_access_get_key_system (GstMediaKeySystemAccess * self)
{
  GstMediaKeySystemAccessInterface *iface =
      GST_MEDIA_KEY_SYSTEM_ACCESS_GET_IFACE (self);

  return iface->get_key_system (self);
}

/**
 * gst_media_key_system_access_get_configuration:
 * @self: #GstMediaKeySystemAccess instance
 *
 * Get the configuration for the requested instance.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysystemaccess-getconfiguration)
 *
 * Returns: (transfer full): A new #GstCaps object representing the
 * configuration options supplied by the application when it created this
 * instance.
 *
 * Since: 1.24
 */
GstCaps *
gst_media_key_system_access_get_configuration (GstMediaKeySystemAccess * self)
{
  GstMediaKeySystemAccessInterface *iface =
      GST_MEDIA_KEY_SYSTEM_ACCESS_GET_IFACE (self);

  return iface->get_configuration (self);
}

/**
 * gst_media_key_system_access_create_media_keys:
 * @self: #GstMediaKeySystemAccess instance
 * @promise: (transfer none): #GstPromise which will be answered when the
 * #GstMediaKeys is successfully created or fails to be created
 *
 * Attempt to create a #GstMediaKeys instance. This is an asynchronous operation
 * and the response will eventually be placed in in the supplied #GstPromise
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysystemaccess-createmediakeys)
 *
 * Since: 1.24
 */
void
gst_media_key_system_access_create_media_keys (GstMediaKeySystemAccess * self,
    GstPromise * promise)
{
  GstMediaKeySystemAccessInterface *iface =
      GST_MEDIA_KEY_SYSTEM_ACCESS_GET_IFACE (self);

  iface->create_media_keys (self, promise);
}

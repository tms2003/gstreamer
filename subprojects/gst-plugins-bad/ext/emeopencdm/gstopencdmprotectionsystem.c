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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/eme/gstemeutils.h>

#include "gstopencdmprotectionsystem.h"
#include "gstemeopencdmlogging.h"
#include "gstopencdmmediakeysystemaccess.h"

#include "open_cdm.h"

struct _GstOpenCDMProtectionSystem
{
  GstElement parent_instance;
};

G_DEFINE_TYPE (GstOpenCDMProtectionSystem, gst_open_cdm_protection_system,
    GST_TYPE_ELEMENT);
#define parent_class gst_open_cdm_protection_system_parent_class

enum
{
  REQUEST_MEDIA_KEY_SYSTEM_ACCESS,

  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static void
gst_open_cdm_protection_system_class_init (GstOpenCDMProtectionSystemClass *
    klass)
{
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);

  gst_emeopencdm_init_logging ();

  /**
   * GstOpenCDMProtectionSystem::request-media-key-system-access:
   * @object: the #GstOpenCDMProtectionSystem
   * @key_system_id: a string containing the requested key system ID
   * @supported_configurations: a #GstCaps describing the supported
   * configurations
   * @promise: a #GstPromise which will receive the #GstMediaKeySystemAccess
   *
   * Since: 1.24
   */
  signals[REQUEST_MEDIA_KEY_SYSTEM_ACCESS] =
      g_signal_new_class_handler ("request-media-key-system-access",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK
      (gst_open_cdm_protection_system_request_media_key_system_access),
      NULL, NULL, NULL,
      G_TYPE_NONE, 3, G_TYPE_STRING, GST_TYPE_CAPS, GST_TYPE_PROMISE);

  gst_element_class_set_static_metadata (eclass,
      "W3C EME OpenCDM protection system",
      "Protection",
      "Allows OpenCDM implementations to integrate with the Encrypted Media "
      "Extensions requested for a Pipeline. Use the action signal "
      "'request-media-key-system-access' to request a GstMediaKeySystemAccess.",
      "Jordan Yelloz <jordan.yelloz@collabora.com>");
}

static gboolean
validate_configuration (const gchar * key_system_id,
    const GstStructure * config)
{
  const gchar *content_type = config == NULL ? NULL :
      gst_structure_get_string (config, "original-media-type");
  return opencdm_is_type_supported (key_system_id, content_type) == ERROR_NONE;
}

static GstStructure *
open_cdm_system_access_response (const gchar * key_system_id, const GstCaps *
    configuration)
{
  GstMediaKeySystemAccess *system_access =
      GST_MEDIA_KEY_SYSTEM_ACCESS (gst_open_cdm_media_key_system_access_new
      (key_system_id, configuration));
  GstStructure *response = gst_eme_response_system_access (system_access);
  gst_object_unref (system_access);
  return response;
}

typedef struct
{
  GstOpenCDMProtectionSystem *self;
  const gchar *key_system_id;
  GstPromise *promise;
} ValidateCapsArgs;

static gboolean
validate_caps (G_GNUC_UNUSED GstCapsFeatures * caps, GstStructure * structure,
    gpointer user_data)
{
  ValidateCapsArgs *args = user_data;
  if (validate_configuration (args->key_system_id, NULL)) {
    gst_promise_reply (args->promise,
        open_cdm_system_access_response (args->key_system_id, GST_CAPS_ANY));
    return FALSE;
  }
  return TRUE;
}

void gst_open_cdm_protection_system_request_media_key_system_access
    (GstOpenCDMProtectionSystem * self, const gchar * key_system_id,
    GstCaps * supported_configurations, GstPromise * promise)
{
  GST_DEBUG_OBJECT (self, "requesting for %s, caps=%" GST_PTR_FORMAT,
      key_system_id, supported_configurations);
  if (gst_caps_is_empty (supported_configurations) ||
      gst_caps_is_any (supported_configurations)) {
    if (validate_configuration (key_system_id, NULL)) {
      gst_promise_reply (promise,
          open_cdm_system_access_response (key_system_id, GST_CAPS_ANY));
      return;
    }
    goto rejected;
  }

  ValidateCapsArgs args = {
    .self = self,
    .key_system_id = key_system_id,
    .promise = promise,
  };
  if (gst_caps_foreach (supported_configurations, validate_caps, &args)) {
    goto rejected;
  }

  return;

rejected:
  gst_promise_reply (promise, NULL);
}

static void
gst_open_cdm_protection_system_init (GstOpenCDMProtectionSystem * self)
{
}

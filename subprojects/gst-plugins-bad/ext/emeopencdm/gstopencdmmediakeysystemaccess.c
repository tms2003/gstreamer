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

#include "gstemeopencdmlogging.h"
#include "gstopencdmmediakeysystemaccess.h"
#include "gstopencdmmediakeys.h"

#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>
#include <gst/eme/gstmediakeysystemaccess.h>

struct _GstOpenCDMMediaKeySystemAccess
{
  GstObject parent_instance;

  gchar *key_system;
  GstCaps *configuration;
};

enum
{
  PROP_0,

  PROP_KEY_SYSTEM_ID,
  PROP_CONFIGURATION,

  N_PROPS,
};

static GParamSpec *properties[N_PROPS];

/* *INDENT-OFF* */
static void open_cdm_media_key_system_access_init (gpointer g_iface,
    gpointer iface_data);
static const gchar *media_key_system_access_get_key_system (
    GstMediaKeySystemAccess * access);
static GstCaps *media_key_system_access_get_configuration (
    GstMediaKeySystemAccess * access);
static void media_key_system_access_create_media_keys (
    GstMediaKeySystemAccess * access, GstPromise * promise);
/* *INDENT-ON* */

G_DEFINE_TYPE_WITH_CODE (GstOpenCDMMediaKeySystemAccess,
    gst_open_cdm_media_key_system_access, GST_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_MEDIA_KEY_SYSTEM_ACCESS,
        open_cdm_media_key_system_access_init));

#define parent_class gst_open_cdm_media_key_system_access_parent_class

GstOpenCDMMediaKeySystemAccess *
gst_open_cdm_media_key_system_access_new (const gchar * key_system_id,
    const GstCaps * configuration)
{
  g_return_val_if_fail (key_system_id != NULL, NULL);
  g_return_val_if_fail (GST_IS_CAPS (configuration), NULL);
  GstOpenCDMMediaKeySystemAccess *self =
      g_object_new (GST_TYPE_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS, "key-system-id",
      key_system_id, "configuration", configuration, NULL);
  return gst_object_ref_sink (self);
}

static void
set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstOpenCDMMediaKeySystemAccess *self =
      GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (object);

  switch (prop_id) {
    case PROP_KEY_SYSTEM_ID:
      g_clear_pointer (&self->key_system, g_free);
      self->key_system = g_value_dup_string (value);
      break;
    case PROP_CONFIGURATION:
      gst_clear_caps (&self->configuration);
      self->configuration = gst_caps_copy (gst_value_get_caps (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOpenCDMMediaKeySystemAccess *self =
      GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (object);

  switch (prop_id) {
    case PROP_KEY_SYSTEM_ID:
      g_value_set_string (value, self->key_system);
      break;
    case PROP_CONFIGURATION:
      gst_value_set_caps (value, self->configuration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
finalize (GObject * object)
{
  GstOpenCDMMediaKeySystemAccess *self =
      GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (object);
  g_clear_pointer (&self->key_system, g_free);
  gst_clear_caps (&self->configuration);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
    gst_open_cdm_media_key_system_access_class_init
    (GstOpenCDMMediaKeySystemAccessClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  gst_emeopencdm_init_logging ();

  oclass->get_property = get_property;
  oclass->set_property = set_property;
  oclass->finalize = finalize;

  properties[PROP_KEY_SYSTEM_ID] = g_param_spec_string ("key-system-id",
      "Key System ID",
      "The current key system ID",
      NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_CONFIGURATION] = g_param_spec_boxed ("configuration",
      "Configuration",
      "The current media (en/de)cryption configuration",
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (oclass, N_PROPS, properties);
}

static void
gst_open_cdm_media_key_system_access_init (GstOpenCDMMediaKeySystemAccess *
    self)
{
  self->key_system = NULL;
  self->configuration = NULL;
}

static void
open_cdm_media_key_system_access_init (gpointer g_iface, gpointer iface_data)
{
  GstMediaKeySystemAccessInterface *iface =
      (GstMediaKeySystemAccessInterface *) g_iface;

  iface->create_media_keys = media_key_system_access_create_media_keys;
  iface->get_key_system = media_key_system_access_get_key_system;
  iface->get_configuration = media_key_system_access_get_configuration;
}

static const gchar *
media_key_system_access_get_key_system (GstMediaKeySystemAccess * access)
{
  return GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (access)->key_system;
}

static GstCaps *
media_key_system_access_get_configuration (GstMediaKeySystemAccess * access)
{
  GstOpenCDMMediaKeySystemAccess *self =
      GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (access);
  return gst_caps_copy (self->configuration);
}

static void
media_key_system_access_create_media_keys (GstMediaKeySystemAccess * access,
    GstPromise * promise)
{
  g_return_if_fail (promise != NULL);
  GstOpenCDMMediaKeySystemAccess *self =
      GST_OPEN_CDM_MEDIA_KEY_SYSTEM_ACCESS (access);
  struct OpenCDMSystem *cdm = opencdm_create_system (self->key_system);
  if (cdm == NULL) {
    GST_ERROR_OBJECT (self, "failed to create CDM for %s", self->key_system);
    gst_promise_reply (promise, NULL);
    return;
  }
  GstOpenCDMMediaKeys *keys = gst_open_cdm_media_keys_new (cdm);
  gst_promise_reply (promise,
      gst_eme_response_media_keys (GST_MEDIA_KEYS (keys)));
  gst_clear_object (&keys);
}

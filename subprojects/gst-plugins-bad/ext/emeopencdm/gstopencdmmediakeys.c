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
#include "gstopencdmmediakeys.h"
#include "gstopencdmmediakeysession.h"

#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>

#include "open_cdm.h"
#include "open_cdm_adapter.h"

struct _GstOpenCDMMediaKeys
{
  GstObject parent_instance;

  GHashTable *sessions;

  struct OpenCDMSystem *system;
};

static void media_keys_iface_init (GstMediaKeysInterface * iface);
static GstMediaKeySession *media_keys_create_session (GstMediaKeys * keys,
    GstMediaKeySessionType type, GError ** error);
static void media_keys_set_server_certificate (GstMediaKeys * keys,
    GstBuffer * certificate, GstPromise * promise);

G_DEFINE_TYPE_WITH_CODE (GstOpenCDMMediaKeys, gst_open_cdm_media_keys,
    GST_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_MEDIA_KEYS, media_keys_iface_init));

#define parent_class gst_open_cdm_media_keys_parent_class

GstOpenCDMMediaKeys *
gst_open_cdm_media_keys_new (struct OpenCDMSystem *cdm)
{
  g_return_val_if_fail (cdm != NULL, NULL);
  GstOpenCDMMediaKeys *self = g_object_new (GST_TYPE_OPEN_CDM_MEDIA_KEYS, NULL);
  self->system = cdm;
  return gst_object_ref_sink (self);
}

GstMediaKeySession *
gst_open_cdm_media_keys_get_session_for_key (GstOpenCDMMediaKeys * self,
    GstBuffer * key_id)
{
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->sessions);
  for (GstMediaKeySession * session = NULL;
      g_hash_table_iter_next (&iter, (gpointer *) & session, NULL);) {
    if (gst_media_key_session_get_media_key_status (session, key_id) ==
        GST_MEDIA_KEY_STATUS_USABLE) {
      return session;
    }
  }
  return NULL;
}

static void
gst_open_cdm_media_keys_dispose (GObject * object)
{
  GstOpenCDMMediaKeys *self = (GstOpenCDMMediaKeys *) object;

  g_hash_table_remove_all (self->sessions);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_open_cdm_media_keys_finalize (GObject * object)
{
  GstOpenCDMMediaKeys *self = (GstOpenCDMMediaKeys *) object;

  g_hash_table_unref (self->sessions);
  g_clear_pointer (&self->system, opencdm_destruct_system);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_open_cdm_media_keys_class_init (GstOpenCDMMediaKeysClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_open_cdm_media_keys_dispose;
  object_class->finalize = gst_open_cdm_media_keys_finalize;

  gst_emeopencdm_init_logging ();
}

static void
remove_session (gpointer ptr)
{
  GstOpenCDMMediaKeySession *session = GST_OPEN_CDM_MEDIA_KEY_SESSION (ptr);
  gst_open_cdm_media_key_session_unparent (session);
}

static void
gst_open_cdm_media_keys_init (GstOpenCDMMediaKeys * self)
{
  self->sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      remove_session, NULL);
}

static void
media_keys_iface_init (GstMediaKeysInterface * iface)
{
  iface->create_session = media_keys_create_session;
  iface->set_server_certificate = media_keys_set_server_certificate;
}

static GstMediaKeySession *
media_keys_create_session (GstMediaKeys * keys, GstMediaKeySessionType type,
    GError ** error)
{
  GstOpenCDMMediaKeys *self = GST_OPEN_CDM_MEDIA_KEYS (keys);
  GstOpenCDMMediaKeySession *session =
      gst_open_cdm_media_key_session_new (type, GST_OBJECT_CAST (self));
  g_hash_table_add (self->sessions, session);
  return GST_MEDIA_KEY_SESSION (session);
}

static GstStructure *
reply_error (OpenCDMError error)
{
  // TODO: convert OpenCDMError to DOM Error
  return gst_structure_new ("error", "error", G_TYPE_INT, error, NULL);
}

static void
media_keys_set_server_certificate (GstMediaKeys * keys, GstBuffer * certificate,
    GstPromise * promise)
{
  g_return_if_fail (GST_IS_OPEN_CDM_MEDIA_KEYS (keys));
  g_return_if_fail (GST_IS_BUFFER (certificate));
  g_return_if_fail (promise != NULL);

  GstOpenCDMMediaKeys *self = GST_OPEN_CDM_MEDIA_KEYS (keys);
  if (!opencdm_system_supports_server_certificate (self->system)) {
    gst_promise_reply (promise, NULL);
    return;
  }
  // TODO: Sanitize certificate data.

  GstMapInfo info = GST_MAP_INFO_INIT;
  gst_buffer_map (certificate, &info, GST_MAP_READ);
  OpenCDMError result = opencdm_system_set_server_certificate (self->system,
      info.data, info.size);
  gst_buffer_unmap (certificate, &info);

  if (result == ERROR_NONE) {
    gst_promise_reply (promise, gst_eme_response_ok ());
  } else {
    gst_promise_reply (promise, reply_error (result));
  }
}

gpointer
gst_open_cdm_media_keys_get_cdm_instance (GstOpenCDMMediaKeys * self)
{
  return self->system;
}

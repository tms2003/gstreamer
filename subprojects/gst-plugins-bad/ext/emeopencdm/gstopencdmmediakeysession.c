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
#include <gst/base/gstdataqueue.h>

#include "open_cdm.h"
#include "open_cdm_adapter.h"

typedef struct
{
  GWeakRef parent;

  gboolean ready;
  struct OpenCDMSession *current;
  struct OpenCDMSession *pending;

  GMutex lock;
  GCond cond;
} SessionRotator;

typedef struct
{
  GstOpenCDMMediaKeySession *session;
  GstTask *task;
  GRecMutex mutex;
  GstBus *bus;
} BackgroundTask;

struct _GstOpenCDMMediaKeySession
{
  GstObject parent_instance;

  GstMediaKeySessionType type;

  gboolean closed;
  gboolean callable;

  GstBus *bus;

  gchar *init_data_type;
  GstBuffer *init_data;
  SessionRotator rotator;
  BackgroundTask *task;
  OpenCDMSessionCallbacks cdm_callbacks;

  GstPromise *pending_update;
};

#define SHUTDOWN "shutdown"

static const gchar *const supported_init_data_types[] = {
  "keyids",
  "cenc",
  "webm",
  NULL,
};

static void media_key_session_iface_init (GstMediaKeySessionInterface * iface);
static const gchar *media_key_session_get_session_id (GstMediaKeySession *
    session);
static GstClockTime media_key_session_get_expiration (GstMediaKeySession *
    session);
static void media_key_session_load (GstMediaKeySession * session,
    const gchar * session_id, GstPromise * promise);
static void media_key_session_update (GstMediaKeySession * session,
    GstBuffer * response, GstPromise * promise);
static void media_key_session_remove (GstMediaKeySession * session,
    GstPromise * promise);
static void media_key_session_close (GstMediaKeySession * session,
    GstPromise * promise);
static void media_key_session_generate_request (GstMediaKeySession * session,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise);
static GstMediaKeyStatus
media_key_session_get_media_key_status (GstMediaKeySession * session,
    GstBuffer * key_id);
static gboolean media_key_session_has_media_key_status (GstMediaKeySession *
    session, GstBuffer * key_id);
static void enqueue_message (GstOpenCDMMediaKeySession * self,
    GstMessage * message);
static inline void send_shutdown (GstOpenCDMMediaKeySession * self);
static inline LicenseType session_type_to_license_type (GstMediaKeySessionType
    type);
static inline GstMediaKeyStatus
media_key_status_from_opencdm_key_status (KeyStatus status);
static void process_challenge_cb (struct OpenCDMSession *session,
    gpointer user_data, const gchar * url, const guint8 * payload,
    guint16 payload_length);
static void key_update_cb (struct OpenCDMSession *session, gpointer user_data,
    const guint8 * key_id, guint8 length);
static void keys_updated_cb (const struct OpenCDMSession *session,
    gpointer user_data);
static void error_message_cb (struct OpenCDMSession *session,
    gpointer user_data, const gchar * message);

static void background_task (gpointer user_data);
static void background_task_cleanup (gpointer ptr);
static struct OpenCDMSystem
    *get_opencdm_system_unlocked (GstOpenCDMMediaKeySession * self);

G_DEFINE_TYPE_WITH_CODE (GstOpenCDMMediaKeySession,
    gst_open_cdm_media_key_session, GST_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_MEDIA_KEY_SESSION,
        media_key_session_iface_init));

#define parent_class gst_open_cdm_media_key_session_parent_class

enum
{
  PROP_0,
  PROP_SESSION_TYPE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

#define OPENCDM_CHALLENGE    "opencdm-challenge"
#define OPENCDM_KEY_UPDATE   "opencdm-key-update"
#define OPENCDM_KEYS_UPDATED "opencdm-keys-updated"

#define FIELD_CHALLENGE      "challenge"
#define FIELD_KEY_ID         "key-id"
#define FIELD_STATUS         "status"

static gboolean
gst_message_parse_opencdm_challenge (GstMessage * message,
    GstBuffer ** challenge)
{
  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_CHALLENGE, GST_TYPE_BUFFER,
      challenge, NULL);
}

static GstMessage *
gst_message_new_opencdm_challenge (GstBuffer * payload)
{
  GstStructure *structure = gst_structure_new (OPENCDM_CHALLENGE,
      FIELD_CHALLENGE, GST_TYPE_BUFFER, payload, NULL);
  return gst_message_new_application (NULL, structure);
}

static gboolean
gst_message_parse_opencdm_key_update (GstMessage * message, KeyStatus * status,
    GstBuffer ** key_id)
{
  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_STATUS, G_TYPE_INT, status,
      FIELD_KEY_ID, GST_TYPE_BUFFER, key_id, NULL);
}

static GstMessage *
gst_message_new_opencdm_key_update (KeyStatus status, GstBuffer * key_id)
{
  GstStructure *structure = gst_structure_new (OPENCDM_KEY_UPDATE, FIELD_STATUS,
      G_TYPE_INT, status, FIELD_KEY_ID, GST_TYPE_BUFFER, key_id, NULL);
  return gst_message_new_application (NULL, structure);
}

static GstMessage *
gst_message_new_opencdm_keys_updated (void)
{
  GstStructure *structure = gst_structure_new_empty (OPENCDM_KEYS_UPDATED);
  return gst_message_new_application (NULL, structure);
}

static void
session_rotator_init (SessionRotator * self, GstOpenCDMMediaKeySession * parent)
{
  g_weak_ref_init (&self->parent, parent);
  self->ready = FALSE;
  self->current = NULL;
  self->pending = NULL;
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
}

static void
session_rotator_clear (SessionRotator * self)
{
  g_weak_ref_clear (&self->parent);
  g_clear_pointer (&self->current, opencdm_destruct_session);
  g_clear_pointer (&self->pending, opencdm_destruct_session);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);
}

static void
session_rotator_notify_if_ready (SessionRotator * self)
{
  GstObject *parent = g_weak_ref_get (&self->parent);
  if (parent == NULL) {
    return;
  }
  g_mutex_lock (&self->lock);
  if (self->pending == NULL) {
    self->ready = TRUE;
    GST_TRACE_OBJECT (parent, "ready");
    g_cond_signal (&self->cond);
  }
  g_mutex_unlock (&self->lock);
  GST_TRACE_OBJECT (parent, "done");
  gst_clear_object (&parent);
}

static struct OpenCDMSession *
session_rotator_peek_current (SessionRotator * self)
{
  return self->current;
}

static struct OpenCDMSession *
session_rotator_peek_pending_or_current (SessionRotator * self)
{
  g_mutex_lock (&self->lock);
  struct OpenCDMSession *session =
      self->pending ? self->pending : self->current;
  g_mutex_unlock (&self->lock);
  return session;
}

static struct OpenCDMSession *
session_rotator_get_current (SessionRotator * self, guint64 deadline)
{
  GstObject *parent = g_weak_ref_get (&self->parent);
  if (parent == NULL) {
    return NULL;
  }
  g_mutex_lock (&self->lock);
  while (!self->ready) {
    GST_DEBUG_OBJECT (parent, "waiting for session to be ready");
    if (!g_cond_wait_until (&self->cond, &self->lock, deadline)) {
      GST_DEBUG_OBJECT (parent, "timeout");
      g_mutex_unlock (&self->lock);
      gst_clear_object (&parent);
      return NULL;
    }
  }
  struct OpenCDMSession *session = self->current;
  g_mutex_unlock (&self->lock);
  GST_DEBUG_OBJECT (parent, "have session %s", opencdm_session_id (session));
  gst_clear_object (&parent);
  return session;
}

static gboolean
session_rotator_new_pending_unlocked (SessionRotator * self)
{
  g_return_val_if_fail (self->pending == NULL, FALSE);
  GstOpenCDMMediaKeySession *parent = g_weak_ref_get (&self->parent);
  if (parent == NULL) {
    GST_WARNING ("no parent, cannot create session");
    return FALSE;
  }
  struct OpenCDMSystem *system = get_opencdm_system_unlocked (parent);
  if (system == NULL) {
    GST_ERROR_OBJECT (parent, "no system in parent, cannot create session");
    return FALSE;
  }
  OpenCDMError result;
  if (parent->init_data == NULL) {
    result = opencdm_construct_session (system,
        session_type_to_license_type (parent->type), parent->init_data_type,
        NULL, 0, NULL, 0, &parent->cdm_callbacks, parent, &self->pending);
  } else {
    GstMapInfo info = GST_MAP_INFO_INIT;
    if (!gst_buffer_map (parent->init_data, &info, GST_MAP_READ)) {
      GST_ERROR_OBJECT (parent, "failed to map init data");
      return FALSE;
    }
    result = opencdm_construct_session (system,
        session_type_to_license_type (parent->type), parent->init_data_type,
        info.data, info.size, NULL, 0, &parent->cdm_callbacks, parent,
        &self->pending);
    gst_buffer_unmap (parent->init_data, &info);
  }
  self->ready = FALSE;
  GST_DEBUG_OBJECT (parent, "created new pending session %s",
      opencdm_session_id (self->pending));
  gst_clear_object (&parent);
  return result == ERROR_NONE;
}

static void
session_rotator_apply_pending_unlocked (SessionRotator * self)
{
  if (self->pending) {
    self->current = g_steal_pointer (&self->pending);
  }
}

static gboolean
session_rotator_new_pending (SessionRotator * self)
{

  g_mutex_lock (&self->lock);
  gboolean result = session_rotator_new_pending_unlocked (self);
  g_mutex_unlock (&self->lock);
  return result;
}

static void
session_rotator_apply_pending (SessionRotator * self)
{
  g_mutex_lock (&self->lock);
  session_rotator_apply_pending_unlocked (self);
  g_mutex_unlock (&self->lock);
}

static void
session_rotator_rotate (SessionRotator * self, GstBuffer * key_id)
{
  g_mutex_lock (&self->lock);
  if (self->pending) {
    g_clear_pointer (&self->current, opencdm_destruct_session);
    self->current = g_steal_pointer (&self->pending);
    GstMapInfo info = GST_MAP_INFO_INIT;
    gst_buffer_map (key_id, &info, GST_MAP_READ);
    self->ready =
        opencdm_session_status (self->current, info.data, info.size) == Usable;
    gst_buffer_unmap (key_id, &info);
  } else {
    GstOpenCDMMediaKeySession *parent = g_weak_ref_get (&self->parent);
    if (parent) {
      GST_OBJECT_LOCK (parent);
      session_rotator_new_pending_unlocked (self);
      GST_OBJECT_UNLOCK (parent);
    }
    gst_clear_object (&parent);
  }
  g_mutex_unlock (&self->lock);
}

static inline struct OpenCDMSystem *
get_opencdm_system_unlocked (GstOpenCDMMediaKeySession * self)
{
  GstObject *parent = GST_OBJECT_PARENT (self);
  if (parent == NULL) {
    GST_ERROR_OBJECT (self,
        "cannot get opencdm system from unparented session");
    return NULL;
  }
  GstOpenCDMMediaKeys *keys = GST_OPEN_CDM_MEDIA_KEYS (parent);
  return gst_open_cdm_media_keys_get_cdm_instance (keys);
}

static struct OpenCDMSession *
get_current_opencdm_session (GstOpenCDMMediaKeySession * self)
{
  return session_rotator_peek_current (&self->rotator);
}

static struct OpenCDMSession *
get_opencdm_session (GstOpenCDMMediaKeySession * self)
{
  return session_rotator_peek_pending_or_current (&self->rotator);
}

static inline gboolean
is_closed (GstOpenCDMMediaKeySession * self)
{
  return self->closed;
}

static inline gboolean
is_callable (GstOpenCDMMediaKeySession * self)
{
  return self->callable;
}

GstOpenCDMMediaKeySession *
gst_open_cdm_media_key_session_new (GstMediaKeySessionType type,
    GstObject * parent)
{
  GstOpenCDMMediaKeySession *session =
      g_object_new (GST_TYPE_OPEN_CDM_MEDIA_KEY_SESSION, "parent", parent,
      "session-type", type, NULL);

  gchar *task_name =
      g_strdup_printf ("%s:background", GST_OBJECT_NAME (session));
  gst_object_set_name (GST_OBJECT_CAST (session->task->task), task_name);
  g_free (task_name);

  return gst_object_ref_sink (session);
}

void
gst_open_cdm_media_key_session_unparent (GstOpenCDMMediaKeySession * self)
{
  g_return_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (self));
  gst_object_unparent (GST_OBJECT_CAST (self));
}

static void
gst_open_cdm_media_key_session_dispose (GObject * object)
{
  GstOpenCDMMediaKeySession *self = (GstOpenCDMMediaKeySession *) object;

  GST_OBJECT_LOCK (self);
  send_shutdown (self);
  gst_task_stop (self->task->task);
  GST_OBJECT_UNLOCK (self);

  gst_task_join (self->task->task);
  session_rotator_clear (&self->rotator);
  g_clear_pointer (&self->task, background_task_cleanup);
  gst_clear_object (&self->bus);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_open_cdm_media_key_session_finalize (GObject * object)
{
  GstOpenCDMMediaKeySession *self = (GstOpenCDMMediaKeySession *) object;

  gst_clear_buffer (&self->init_data);
  g_clear_pointer (&self->init_data_type, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_open_cdm_media_key_session_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (object);

  switch (prop_id) {
    case PROP_SESSION_TYPE:
      g_value_set_enum (value, self->type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_open_cdm_media_key_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (object);

  switch (prop_id) {
    case PROP_SESSION_TYPE:
      self->type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_open_cdm_media_key_session_class_init (GstOpenCDMMediaKeySessionClass *
    klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->dispose = gst_open_cdm_media_key_session_dispose;
  oclass->finalize = gst_open_cdm_media_key_session_finalize;
  oclass->get_property = gst_open_cdm_media_key_session_get_property;
  oclass->set_property = gst_open_cdm_media_key_session_set_property;

  properties[PROP_SESSION_TYPE] = g_param_spec_enum ("session-type",
      "Session Type",
      "Either Temporary or Persistent",
      GST_TYPE_MEDIA_KEY_SESSION_TYPE, GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (oclass, N_PROPS, properties);

  gst_emeopencdm_init_logging ();
}

static void
background_task_cleanup (gpointer ptr)
{
  BackgroundTask *task = (BackgroundTask *) ptr;
  gst_task_join (task->task);
  task->session = NULL;
  gst_clear_object (&task->bus);
  gst_clear_object (&task->task);
  g_rec_mutex_clear (&task->mutex);
  g_free (task);
}

static BackgroundTask *
background_task_new (GstOpenCDMMediaKeySession * session)
{
  BackgroundTask *task = g_new0 (BackgroundTask, 1);
  g_rec_mutex_init (&task->mutex);
  task->session = session;
  task->bus = gst_object_ref (session->bus);
  task->task = gst_task_new (background_task, task, NULL);
  gst_task_set_lock (task->task, &task->mutex);
  return task;
}

static void
gst_open_cdm_media_key_session_init (GstOpenCDMMediaKeySession * self)
{
  self->callable = FALSE;
  self->closed = FALSE;
  self->type = GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY;

  session_rotator_init (&self->rotator, self);
  self->init_data = NULL;
  self->init_data_type = NULL;
  self->bus = gst_bus_new ();
  BackgroundTask *task = background_task_new (self);
  self->task = task;
  self->cdm_callbacks.process_challenge_callback = process_challenge_cb;
  self->cdm_callbacks.key_update_callback = key_update_cb;
  self->cdm_callbacks.keys_updated_callback = keys_updated_cb;
  self->cdm_callbacks.error_message_callback = error_message_cb;
  self->pending_update = NULL;
  gst_task_start (task->task);
}

static void
media_key_session_iface_init (GstMediaKeySessionInterface * iface)
{
  iface->get_session_id = media_key_session_get_session_id;
  iface->get_expiration = media_key_session_get_expiration;
  iface->load = media_key_session_load;
  iface->update = media_key_session_update;
  iface->generate_request = media_key_session_generate_request;
  iface->remove = media_key_session_remove;
  iface->close = media_key_session_close;
  iface->get_media_key_status = media_key_session_get_media_key_status;
  iface->has_media_key_status = media_key_session_has_media_key_status;
}

static const gchar *
media_key_session_get_session_id (GstMediaKeySession * session)
{
  g_return_val_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (session), NULL);
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);
  struct OpenCDMSession *cdm_session = get_current_opencdm_session (self);
  return cdm_session == NULL ? NULL : opencdm_session_id (cdm_session);
}

static GstClockTime
media_key_session_get_expiration (GstMediaKeySession * session)
{
  g_return_val_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (session),
      GST_CLOCK_TIME_NONE);
  // XXX: OpenCDM does not provide a mechanism to query the expiration time.
  return GST_CLOCK_TIME_NONE;
}

static void
enqueue_message (GstOpenCDMMediaKeySession * self, GstMessage * message)
{
  gst_bus_post (self->bus, message);
}

static void
media_key_session_generate_request (GstMediaKeySession * session,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise)
{
  g_return_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (session));
  g_return_if_fail (init_data_type != NULL);
  g_return_if_fail (GST_IS_BUFFER (init_data));
  g_return_if_fail (GST_IS_PROMISE (promise));
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  GST_DEBUG_OBJECT (self, "generate request for %s", init_data_type);

  if (is_closed (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  struct OpenCDMSession *cdm_session = get_current_opencdm_session (self);
  if (cdm_session) {
    GST_ERROR_OBJECT (self, "already have session %s", opencdm_session_id
        (cdm_session));
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  enqueue_message (self,
      gst_message_new_eme_generate_request (NULL, init_data_type, init_data,
          promise));
}

static void
media_key_session_load (GstMediaKeySession * session, const gchar * session_id,
    GstPromise * promise)
{
  g_return_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (session));
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  GST_DEBUG_OBJECT (session, "load %s", session_id);

  if (is_closed (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (g_strcmp0 ("", session_id) == 0) {
    gst_promise_reply (promise, gst_eme_response_type_error ());
    return;
  }

  enqueue_message (self, gst_message_new_eme_load (NULL, session_id, promise));
}

static void
media_key_session_update (GstMediaKeySession * session, GstBuffer * response,
    GstPromise * promise)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  if (is_closed (self)) {
    GST_DEBUG_OBJECT (self, "closed");
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (!is_callable (self)) {
    GST_DEBUG_OBJECT (self, "not callable");
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (gst_buffer_get_size (response) == 0) {
    GST_DEBUG_OBJECT (self, "response is empty");
    gst_promise_reply (promise, gst_eme_response_type_error ());
    return;
  }

  enqueue_message (self, gst_message_new_eme_update (NULL, response, promise));
}

static void
media_key_session_remove (GstMediaKeySession * session, GstPromise * promise)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  if (is_closed (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (!is_callable (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (get_current_opencdm_session (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  enqueue_message (self, gst_message_new_eme_remove (NULL, promise));
}

static void
media_key_session_close (GstMediaKeySession * session, GstPromise * promise)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  if (is_closed (self)) {
    gst_promise_reply (promise, gst_eme_response_ok ());
    return;
  }

  if (is_callable (self)) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  enqueue_message (self, gst_message_new_eme_close (NULL, promise));
}

static inline LicenseType
session_type_to_license_type (GstMediaKeySessionType type)
{
  switch (type) {
    case GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY:
      return Temporary;
    case GST_MEDIA_KEY_SESSION_TYPE_PERSISTENT_LICENSE:
      return PersistentLicense;
    default:
      g_assert_not_reached ();
  }
}

static inline GstMediaKeyStatus
media_key_status_from_opencdm_key_status (KeyStatus status)
{
  switch (status) {
    case Usable:
      return GST_MEDIA_KEY_STATUS_USABLE;
    case Expired:
      return GST_MEDIA_KEY_STATUS_EXPIRED;
    case Released:
      return GST_MEDIA_KEY_STATUS_RELEASED;
    case OutputRestricted:
    case OutputRestrictedHDCP22:
      return GST_MEDIA_KEY_STATUS_OUTPUT_RESTRICTED;
    case OutputDownscaled:
      return GST_MEDIA_KEY_STATUS_OUTPUT_DOWNSCALED;
    case StatusPending:
      return GST_MEDIA_KEY_STATUS_STATUS_PENDING;
    case InternalError:
    case HWError:
      return GST_MEDIA_KEY_STATUS_INTERNAL_ERROR;
    default:
      g_assert_not_reached ();
      return GST_MEDIA_KEY_STATUS_INTERNAL_ERROR;
  }
}

static GstMediaKeyStatus
media_key_session_get_media_key_status (GstMediaKeySession * session,
    GstBuffer * key_id)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  struct OpenCDMSession *cdm_session = get_current_opencdm_session (self);
  if (cdm_session == NULL) {
    return GST_MEDIA_KEY_STATUS_EXPIRED;
  }

  GstMapInfo info = GST_MAP_INFO_INIT;
  gst_buffer_map (key_id, &info, GST_MAP_READ);
  KeyStatus status = opencdm_session_status (cdm_session, info.data, info.size);
  gst_buffer_unmap (key_id, &info);

  return media_key_status_from_opencdm_key_status (status);
}

static gboolean
media_key_session_has_media_key_status (GstMediaKeySession * session,
    GstBuffer * key_id)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (session);

  struct OpenCDMSession *cdm_session = get_current_opencdm_session (self);
  if (cdm_session == NULL) {
    return FALSE;
  }

  GstMapInfo info = GST_MAP_INFO_INIT;
  gst_buffer_map (key_id, &info, GST_MAP_READ);
  gboolean exists =
      opencdm_session_has_key_id (cdm_session, info.size, info.data);
  gst_buffer_unmap (key_id, &info);

  return exists;
}

static void
handle_challenge (GstOpenCDMMediaKeySession * self, GstBuffer * challenge)
{
  GstMessage *message = gst_message_new_eme_license_request (NULL, challenge);
  gst_media_key_session_publish_message (GST_MEDIA_KEY_SESSION (self), message);
}

static void
handle_key_update (GstOpenCDMMediaKeySession * self, GstBuffer * key_id,
    KeyStatus status)
{
  switch (status) {
    case Expired:
      session_rotator_new_pending (&self->rotator);
      return;
    case Usable:
      session_rotator_apply_pending (&self->rotator);
      session_rotator_notify_if_ready (&self->rotator);
      return;
    default:
      return;
  }
}

static void
handle_keys_updated (GstOpenCDMMediaKeySession * self)
{
  GstMediaKeySession *session = GST_MEDIA_KEY_SESSION (self);
  session_rotator_notify_if_ready (&self->rotator);
  gst_media_key_session_publish_key_statuses_change (session);
  GstPromise *pending_update = g_steal_pointer (&self->pending_update);
  if (pending_update) {
    gst_promise_reply (pending_update, gst_eme_response_ok ());
  }
}

static void
handle_generate_request (GstOpenCDMMediaKeySession * self,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise)
{
  struct OpenCDMSystem *system = get_opencdm_system_unlocked (self);

  if (system == NULL) {
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  if (g_strv_contains (supported_init_data_types, init_data_type)) {
    gsize init_data_length = gst_buffer_get_size (init_data);
    if (init_data_length == 0) {
      gst_promise_reply (promise, NULL);
      return;
    }

    struct OpenCDMSession *cdm_session = get_current_opencdm_session (self);
    if (cdm_session) {
      GST_ERROR_OBJECT (self, "session already exists");
    }

    self->callable = TRUE;

    g_clear_pointer (&self->init_data_type, g_free);
    self->init_data_type = g_strdup (init_data_type);
    gst_buffer_replace (&self->init_data, init_data);
    gboolean result = session_rotator_new_pending (&self->rotator);

    if (!result) {
      GST_ERROR_OBJECT (self, "failed to create session: 0x%x", result);
      gst_promise_reply (promise, NULL);
      return;
    }

    gst_promise_reply (promise, gst_eme_response_init_data (init_data));
    return;
  }

  GST_ERROR_OBJECT (self, "unsupported init data type %s", init_data_type);

  gst_promise_reply (promise, gst_eme_response_type_error ());
}

static void
expire_pending_update (GstOpenCDMMediaKeySession * self)
{
  GstPromise *pending_update = g_steal_pointer (&self->pending_update);
  if (pending_update == NULL) {
    return;
  }
  gst_promise_expire (pending_update);
  g_clear_pointer (&self->pending_update, gst_promise_unref);
}

static void
handle_load (GstOpenCDMMediaKeySession * self, const gchar * session_id,
    GstPromise * promise)
{
  struct OpenCDMSession *session = get_current_opencdm_session (self);
  if (session) {
    GST_ERROR_OBJECT (self, "already have session %s",
        opencdm_session_id (session));
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  struct OpenCDMSystem *system = get_opencdm_system_unlocked (self);
  if (system == NULL) {
    GST_ERROR_OBJECT (self, "failed to get underlying CDM");
    gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
    return;
  }

  GST_DEBUG_OBJECT (self, "creating empty session");

  expire_pending_update (self);

  gst_clear_buffer (&self->init_data);
  session_rotator_new_pending (&self->rotator);
  session = get_opencdm_session (self);

  GST_DEBUG_OBJECT (self, "have session %s", opencdm_session_id (session));

  OpenCDMError result = opencdm_session_load (session);
  switch (result) {
    case ERROR_NONE:
      GST_DEBUG_OBJECT (self, "loaded successfully");
      self->pending_update = g_steal_pointer (&promise);
      return;
    case ERROR_INVALID_ARG:
      GST_ERROR_OBJECT (self, "load error " G_STRINGIFY (ERROR_INVALID_ARG));
      gst_promise_reply (promise, gst_eme_response_type_error ());
      return;
    default:
      GST_ERROR_OBJECT (self, "load error 0x%x", result);
      gst_promise_reply (promise, gst_eme_response_invalid_state_error ());
      return;
  }
}

static void
handle_update (GstOpenCDMMediaKeySession * self, GstBuffer * response,
    GstPromise * promise)
{
  struct OpenCDMSession *session = get_opencdm_session (self);
  if (session == NULL) {
    GST_ERROR_OBJECT (self, "tried to update empty session");
    gst_promise_reply (promise, NULL);
    return;
  }

  expire_pending_update (self);

  GstMapInfo info = GST_MAP_INFO_INIT;
  gst_buffer_map (response, &info, GST_MAP_READ);
  OpenCDMError result = opencdm_session_update (session, info.data, info.size);
  gst_buffer_unmap (response, &info);

  if (result != ERROR_NONE) {
    GST_ERROR_OBJECT (self, "update failed: 0x%x", result);
    gst_promise_reply (promise, NULL);
    return;
  }
  self->pending_update = g_steal_pointer (&promise);
}

static void
handle_remove (GstOpenCDMMediaKeySession * self, GstPromise * promise)
{
  struct OpenCDMSession *session = get_current_opencdm_session (self);
  if (session) {
    opencdm_session_remove (session);
  }
  gst_promise_reply (promise, gst_eme_response_ok ());
}

static void
handle_close (GstOpenCDMMediaKeySession * self, GstPromise * promise)
{
  self->closed = TRUE;
  struct OpenCDMSession *session = get_current_opencdm_session (self);
  if (session != NULL) {
    opencdm_session_close (session);
  }
  gst_promise_reply (promise, gst_eme_response_ok ());
}

static inline void
send_shutdown (GstOpenCDMMediaKeySession * self)
{
  GstMessage *message = gst_message_new_application (NULL,
      gst_structure_new_empty (SHUTDOWN));
  enqueue_message (self, message);
}

static void
background_task (gpointer user_data)
{
  BackgroundTask *task = (BackgroundTask *) user_data;
  GstMessage *message = gst_bus_timed_pop (task->bus, GST_CLOCK_TIME_NONE);
  if (gst_message_has_name (message, SHUTDOWN)) {
    GST_DEBUG_OBJECT (task->task, "shutdown task");
    gst_bus_set_flushing (task->bus, TRUE);
    gst_task_stop (task->task);
    goto done_unlocked;
  }

  GstOpenCDMMediaKeySession *self = task->session;
  GST_OBJECT_LOCK (self);

  if (gst_message_has_name (message, OPENCDM_CHALLENGE)) {
    GST_DEBUG_OBJECT (task->task, "challenge from opencdm session");
    GstBuffer *challenge = NULL;
    if (!gst_message_parse_opencdm_challenge (message, &challenge)) {
      g_error ("failed to unpack challenge");
      goto done;
    }
    handle_challenge (self, challenge);
    gst_clear_buffer (&challenge);
    goto done;
  }

  if (gst_message_has_name (message, OPENCDM_KEY_UPDATE)) {
    GST_DEBUG_OBJECT (task->task, "key update from opencdm session");
    KeyStatus status = -1;
    GstBuffer *key_id = NULL;
    if (!gst_message_parse_opencdm_key_update (message, &status, &key_id)) {
      g_error ("failed to unpack key update");
      goto done;
    }
    handle_key_update (self, key_id, status);
    gst_clear_buffer (&key_id);
    goto done;
  }

  if (gst_message_has_name (message, OPENCDM_KEYS_UPDATED)) {
    GST_DEBUG_OBJECT (task->task, "keys updated from opencdm session");
    handle_keys_updated (self);
    goto done;
  }

  switch (gst_eme_message_get_type (message)) {
    case GST_EME_MESSAGE_TYPE_GENERATE_REQUEST:{
      GST_DEBUG_OBJECT (self, "generate request %" GST_PTR_FORMAT, message);
      gchar *init_data_type = NULL;
      GstBuffer *init_data = NULL;
      GstPromise *promise = NULL;
      if (!gst_message_parse_eme_generate_request (message, &init_data_type,
              &init_data, &promise)) {
        g_error ("failed to parse generate-request message");
      } else {
        handle_generate_request (self, init_data_type, init_data, promise);
      }
      gst_clear_buffer (&init_data);
      g_clear_pointer (&init_data_type, g_free);
      g_clear_pointer (&promise, gst_promise_unref);
      break;
    }
    case GST_EME_MESSAGE_TYPE_LOAD:{
      GST_DEBUG_OBJECT (self, "load %" GST_PTR_FORMAT, message);
      gchar *session_id = NULL;
      GstPromise *promise = NULL;
      if (!gst_message_parse_eme_load (message, &session_id, &promise)) {
        g_error ("failed to parse load message");
      } else {
        handle_load (self, session_id, promise);
      }
      g_clear_pointer (&session_id, g_free);
      g_clear_pointer (&promise, gst_promise_unref);
      break;
    }
    case GST_EME_MESSAGE_TYPE_UPDATE:{
      GST_DEBUG_OBJECT (self, "update %" GST_PTR_FORMAT, message);
      GstBuffer *response = NULL;
      GstPromise *promise = NULL;
      if (!gst_message_parse_eme_update (message, &response, &promise)) {
        g_error ("failed to parse update message");
      } else {
        handle_update (self, response, promise);
      }
      gst_clear_buffer (&response);
      g_clear_pointer (&promise, gst_promise_unref);
      break;
    }
    case GST_EME_MESSAGE_TYPE_REMOVE:{
      GST_DEBUG_OBJECT (self, "remove %" GST_PTR_FORMAT, message);
      GstPromise *promise = NULL;
      if (!gst_message_parse_eme_remove (message, &promise)) {
        g_error ("failed to parse remove message");
      } else {
        handle_remove (self, promise);
      }
      g_clear_pointer (&promise, gst_promise_unref);
      break;
    }
    case GST_EME_MESSAGE_TYPE_CLOSE:{
      GST_DEBUG_OBJECT (self, "close %" GST_PTR_FORMAT, message);
      GstPromise *promise = NULL;
      if (!gst_message_parse_eme_close (message, &promise)) {
        g_error ("failed to parse close message");
      } else {
        handle_close (self, promise);
      }
      g_clear_pointer (&promise, gst_promise_unref);
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "unexpected message %" GST_PTR_FORMAT, message);
      g_assert_not_reached ();
  }
done:
  GST_OBJECT_UNLOCK (self);
done_unlocked:
  gst_clear_message (&message);
}

static void
process_challenge_cb (struct OpenCDMSession *session, gpointer user_data,
    const gchar * url, const guint8 * payload, guint16 payload_length)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (user_data);
  GstBuffer *buffer = payload && payload_length ?
      gst_buffer_new_memdup (payload, payload_length) : NULL;
  enqueue_message (self, gst_message_new_opencdm_challenge (buffer));
  gst_clear_buffer (&buffer);
}

static void
key_update_cb (struct OpenCDMSession *session, gpointer user_data,
    const guint8 * key_id, guint8 length)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (user_data);
  KeyStatus status = opencdm_session_status (session, key_id, length);
  GstBuffer *buffer = gst_buffer_new_memdup (key_id, length);
  enqueue_message (self, gst_message_new_opencdm_key_update (status, buffer));
  gst_clear_buffer (&buffer);
}

static void
keys_updated_cb (const struct OpenCDMSession *session, gpointer user_data)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (user_data);
  GstMessage *msg = gst_message_new_opencdm_keys_updated ();
  enqueue_message (self, gst_message_new_opencdm_keys_updated ());
  gst_clear_message (&msg);
}

static void
error_message_cb (struct OpenCDMSession *session, gpointer user_data,
    const gchar * message)
{
  GstOpenCDMMediaKeySession *self = GST_OPEN_CDM_MEDIA_KEY_SESSION (user_data);
  GST_ERROR_OBJECT (self, "error=%s", message);
}

GstFlowReturn
gst_open_cdm_media_key_session_decrypt (GstOpenCDMMediaKeySession * self,
    GstBuffer * buffer, GstBuffer * iv, GstBuffer * key_id,
    GstBuffer * subsamples, guint32 subsample_count, GstClockTime timeout)
{
  g_return_val_if_fail (GST_IS_OPEN_CDM_MEDIA_KEY_SESSION (self),
      GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (iv), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (key_id), GST_FLOW_ERROR);
  g_return_val_if_fail (subsample_count == 0 || GST_IS_BUFFER (subsamples),
      GST_FLOW_ERROR);

  while (TRUE) {
    GST_TRACE_OBJECT (self, "waiting for session");
    struct OpenCDMSession *cdm_session =
        session_rotator_get_current (&self->rotator,
        g_get_monotonic_time () + GST_TIME_AS_USECONDS (timeout));
    if (cdm_session == NULL) {
      GST_DEBUG_OBJECT (self, "no session after timeout");
      return GST_FLOW_EME_SESSION_TIMEOUT;
    }

    OpenCDMError result = opencdm_gstreamer_session_decrypt (cdm_session,
        buffer, subsamples, subsample_count, iv, key_id, 0);

    if (result == ERROR_INVALID_SESSION) {
      GST_DEBUG_OBJECT (self, "%s: session is invalid, rotating",
          opencdm_session_id (cdm_session));
      session_rotator_rotate (&self->rotator, key_id);
      continue;
    }

    if (result == ERROR_NONE) {
      GST_TRACE_OBJECT (self, "%s: decrypted successfully",
          opencdm_session_id (cdm_session));
      return GST_FLOW_OK;
    }

    GST_ERROR_OBJECT (self, "%s: failed to decrypt data: 0x%x",
        opencdm_session_id (cdm_session), result);
    return GST_FLOW_ERROR;
  }
}

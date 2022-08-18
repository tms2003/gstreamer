#include <open_cdm.h>
#include <open_cdm_adapter.h>
#include <gst/gst.h>

typedef struct OpenCDMSystem System;
typedef struct OpenCDMSession Session;

struct OpenCDMSystem
{
  Session *session;
};

typedef struct
{
  GstBus *bus;
  GstTask *task;
  GRecMutex mutex;
  Session *session;
} BackgroundTask;

struct OpenCDMSession
{
  gchar *id;
  GstBuffer *init_data;
  OpenCDMSessionCallbacks *callbacks;
  gpointer user_data;
  GstBus *bus;
  BackgroundTask *task;
  GCond update_cond;
  GMutex update_mutex;
  gboolean updated;
};

#define UPDATE "update"
#define CHALLENGE "challenge"
#define SHUTDOWN "shutdown"

static void
signal_updated (Session * session)
{
  g_mutex_lock (&session->update_mutex);
  session->updated = TRUE;
  g_cond_signal (&session->update_cond);
  g_mutex_unlock (&session->update_mutex);
}

static void
await_updated (Session * session)
{
  g_mutex_lock (&session->update_mutex);
  while (!session->updated) {
    g_cond_wait (&session->update_cond, &session->update_mutex);
  }
  session->updated = FALSE;
  g_mutex_unlock (&session->update_mutex);
}

static void
post_empty_message (Session * session, const gchar * name)
{
  gst_bus_post (session->bus, gst_message_new_application (NULL,
          gst_structure_new_empty (name)));
}

OpenCDMError
opencdm_is_type_supported (const gchar * key_system, const gchar * mime_type)
{
  GST_LOG ("%s,%s", key_system, mime_type);
  return ERROR_NONE;
}

System *
opencdm_create_system (const gchar * key_system)
{
  GST_LOG ("%s", key_system);
  System *system = g_new0 (System, 1);
  opencdm_construct_session (system, Temporary, "kids", NULL, 0, NULL, 0, NULL,
      NULL, &system->session);
  return system;
}

OpenCDMError
opencdm_destruct_system (System * system)
{
  GST_LOG ("%p", system);
  g_clear_pointer (&system->session, opencdm_destruct_session);
  g_free (system);
  return ERROR_NONE;
}

OpenCDMBool
opencdm_system_supports_server_certificate (System * system)
{
  GST_LOG ("%p", system);
  return OPENCDM_BOOL_FALSE;
}

Session *
opencdm_get_system_session (System * system, const guint8 * key_id,
    const guint8 length, const guint32 wait_time)
{
  GST_LOG ("%p", system);
  return system->session;
}

OpenCDMError
opencdm_system_set_server_certificate (System * system, const guint8 * data,
    const guint16 length)
{
  GST_LOG ("%p", system);
  return ERROR_NONE;
}

static void
background_task (gpointer user_data)
{
  BackgroundTask *task = (BackgroundTask *) user_data;
  GstMessage *message = gst_bus_timed_pop (task->bus, GST_CLOCK_TIME_NONE);
  if (gst_message_has_name (message, SHUTDOWN)) {
    gst_bus_set_flushing (task->bus, TRUE);
    gst_task_stop (task->task);
    goto done;
  }

  Session *session = task->session;

  if (gst_message_has_name (message, CHALLENGE)) {
    if (!session->callbacks || !session->callbacks->process_challenge_callback)
      goto done;

    if (GST_IS_BUFFER (session->init_data)) {
      GstMapInfo info;
      gst_buffer_map (session->init_data, &info, GST_MAP_READ);
      session->callbacks->process_challenge_callback (session,
          session->user_data, NULL, info.data, info.size);
      gst_buffer_unmap (session->init_data, &info);
    } else {
      session->callbacks->process_challenge_callback (session,
          session->user_data, NULL, NULL, 0);
    }
  } else if (gst_message_has_name (message, UPDATE)) {
    if (session->callbacks && session->callbacks->key_update_callback) {
      session->callbacks->key_update_callback (session, session->user_data,
          (guint8 *) "key", 3);
    }
    if (session->callbacks && session->callbacks->keys_updated_callback) {
      session->callbacks->keys_updated_callback (session, session->user_data);
    }
    signal_updated (session);
    goto done;
  }

done:
  gst_clear_message (&message);
}


static void
background_task_free (gpointer ptr)
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
background_task_new (Session * session)
{
  BackgroundTask *task = g_new0 (BackgroundTask, 1);
  g_rec_mutex_init (&task->mutex);
  task->session = session;
  task->bus = gst_object_ref (session->bus);
  task->task = gst_task_new (background_task, task, NULL);
  gchar *task_name = g_strdup_printf ("testcdm-%s", session->id);
  gst_object_set_name (GST_OBJECT_CAST (task->task), task_name);
  g_free (task_name);
  gst_task_set_lock (task->task, &task->mutex);
  return task;
}

OpenCDMError
opencdm_construct_session (System * system, const LicenseType license_type,
    const gchar * init_data_type, const guint8 * init_data,
    const guint16 init_data_length, const guint8 * cdm_data,
    const guint16 cdm_data_length, OpenCDMSessionCallbacks * callbacks,
    gpointer user_data, Session ** session)
{
  GST_LOG ("%p", system);
  static guint id = 0;
  GstBuffer *init_data_buffer = init_data ?
      gst_buffer_new_memdup (init_data, init_data_length) : NULL;

  Session *s = g_new0 (Session, 1);
  s->callbacks = callbacks;
  s->user_data = user_data;
  s->init_data = init_data_buffer;
  s->id = g_strdup_printf ("%d", id++);
  s->bus = gst_bus_new ();
  g_cond_init (&s->update_cond);
  g_mutex_init (&s->update_mutex);
  s->updated = FALSE;
  s->task = background_task_new (s);
  gst_task_start (s->task->task);

  post_empty_message (s, CHALLENGE);

  *session = s;

  return ERROR_NONE;
}

OpenCDMError
opencdm_destruct_session (Session * session)
{
  GST_LOG ("%p", session);
  post_empty_message (session, SHUTDOWN);
  background_task_free (session->task);
  gst_clear_object (&session->bus);
  g_cond_clear (&session->update_cond);
  g_mutex_clear (&session->update_mutex);
  gst_clear_buffer (&session->init_data);
  g_clear_pointer (&session->id, g_free);
  g_free (session);
  return ERROR_NONE;
}

const gchar *
opencdm_session_id (const Session * session)
{
  GST_LOG ("%p", session);
  return session->id;
}

KeyStatus
opencdm_session_status (const Session * session, const guint8 * key_id,
    const guint8 length)
{
  GST_LOG ("%p", session);
  return Usable;
}

guint32
opencdm_session_has_key_id (Session * session, const guint8 length,
    const guint8 * key_id)
{
  GST_LOG ("%p", session);
  return TRUE;
}

OpenCDMError
opencdm_session_load (Session * session)
{
  GST_LOG ("%p", session);
  post_empty_message (session, UPDATE);
  await_updated (session);
  return ERROR_NONE;
}

OpenCDMError
opencdm_session_update (Session * session, const guint8 * key_message,
    const guint16 key_length)
{
  GST_LOG ("%p", session);
  post_empty_message (session, UPDATE);
  await_updated (session);
  return ERROR_NONE;
}

OpenCDMError
opencdm_session_remove (Session * session)
{
  GST_LOG ("%p", session);
  return ERROR_UNKNOWN;
}

OpenCDMError
opencdm_session_close (Session * session)
{
  GST_LOG ("%p", session);
  return ERROR_NONE;
}

OpenCDMError
opencdm_gstreamer_session_decrypt (Session * session, GstBuffer * buffer,
    GstBuffer * subsamples, const guint32 subsample_count,
    GstBuffer * iv, GstBuffer * key_id, guint32 init_with_last_15)
{
  GST_LOG ("%p", session);
  GstProtectionMeta *meta = gst_buffer_get_protection_meta (buffer);
  if (meta) {
    gst_buffer_remove_meta (buffer, GST_META_CAST (meta));
  }
  return ERROR_NONE;
}

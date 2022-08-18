#include <glib.h>
#include <gst/gst.h>
#include <gst/play/gstplay.h>
#include <gst/play/gstplay-signal-adapter.h>
#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>
#include <libsoup/soup.h>

#define DRM_PREFERRED_CONTEXT           "drm-preferred-decryption-system-id"
#define REQUEST_MEDIA_KEY_SYSTEM_ACCESS "request-media-key-system-access"

#define WIDEVINE_ID          "com.widevine.alpha"
#define WIDEVINE_UUID        "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"
#define WIDEVINE_LICENSE_URL "https://proxy.staging.widevine.com/proxy"
#define WIDEVINE_DASH_URL    "https://storage.googleapis.com/wvmedia/cenc/hevc/tears/tears_hd.mpd"

GST_DEBUG_CATEGORY (emedemo);
#define GST_CAT_DEFAULT emedemo

typedef struct
{
  GstPlay *play;
  GMainLoop *loop;

  GAsyncQueue *license_requests;

  GstPromise *waiting_for_key;
  GstMediaKeys *keys;
} Application;

static inline GstMediaKeySystemAccess *
setup_system_access (GstElement * protection_system, const gchar * system_id)
{
  GstPromise *promise = gst_promise_new ();
  g_signal_emit_by_name (protection_system, REQUEST_MEDIA_KEY_SYSTEM_ACCESS,
      system_id, GST_CAPS_ANY, promise);
  gst_promise_wait (promise);
  GstMediaKeySystemAccess *access = gst_eme_resolve_system_access (promise);
  gst_clear_promise (&promise);
  return access;
}

static inline GstMediaKeys *
setup_media_keys (GstMediaKeySystemAccess * system_access)
{
  GstPromise *promise = gst_promise_new ();
  gst_media_key_system_access_create_media_keys (system_access, promise);
  gst_promise_wait (promise);
  GstMediaKeys *keys = gst_eme_resolve_media_keys (promise);
  gst_clear_promise (&promise);
  return keys;
}

static GBytes *
request_license (GBytes * request_body, const gchar * url)
{
  SoupSession *session = soup_session_new ();
  SoupMessage *message = soup_message_new (SOUP_METHOD_POST, url);

  soup_message_set_request_body_from_bytes (message, NULL, request_body);
  GBytes *response = soup_session_send_and_read (session, message, NULL, NULL);
  g_clear_object (&message);
  g_clear_object (&session);
  return response;
}

typedef struct
{
  GBytes *data;
  GstMediaKeySession *session;
} LicenseRequest;

static LicenseRequest *
license_request_new (GstBuffer * buffer, GstMediaKeySession * session)
{
  GstMapInfo info;
  gst_buffer_map (buffer, &info, GST_MAP_READ);
  GBytes *data = g_bytes_new (info.data, info.size);
  gst_buffer_unmap (buffer, &info);
  LicenseRequest request = {
    .data = data,
    .session = gst_object_ref (session),
  };
  return g_memdup2 (&request, sizeof (LicenseRequest));
}

static void
license_request_free (LicenseRequest * request)
{
  g_clear_pointer (&request->data, g_bytes_unref);
  gst_clear_object (&request->session);
  g_free (request);
}

static void
license_request_clear (LicenseRequest ** request)
{
  g_clear_pointer (request, license_request_free);
}

static void
on_session_message (GstMediaKeySession * session, GstMessage * message,
    Application * progress)
{
  GST_LOG_OBJECT (session, "got message %" GST_PTR_FORMAT, message);
  switch (gst_eme_media_key_message_get_type (message)) {
    case GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_REQUEST:{

      GstBuffer *payload = NULL;
      if (!gst_message_parse_eme_license_request (message, &payload)) {
        GST_ERROR_OBJECT (session, "bad message");
        return;
      }

      g_async_queue_push (progress->license_requests,
          license_request_new (payload, session));
      gst_clear_buffer (&payload);
      return;
    }
    default:
      GST_ERROR_OBJECT (session, "unexpected message %" GST_PTR_FORMAT,
          message);
      return;
  }
}

static void
setup_eme (Application * app, GstElement * element,
    const gchar * init_data_type, GstBuffer * init_data)
{
  if (!GST_IS_MEDIA_KEYS (app->keys)) {
    GST_ERROR ("missing media keys");
    return;
  }
  GError *error = NULL;
  GstMediaKeySession *session = gst_media_keys_create_session (app->keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, &error);

  if (error) {
    GST_ERROR_OBJECT (app->keys, "failed to create session: %s",
        error->message);
    g_clear_error (&error);
    return;
  }
  g_clear_error (&error);

  GstPromise *promise = gst_promise_new ();
  g_signal_connect (session, "on-message", G_CALLBACK (on_session_message),
      app);
  gst_media_key_session_generate_request (session, init_data_type, init_data,
      promise);
  gst_promise_wait (promise);
  gst_clear_promise (&promise);

  GstContext *context = gst_eme_context_new_media_keys (app->keys);
  gst_element_set_context (element, context);
  gst_clear_context (&context);
}

static GstMediaKeys *
setup_widevine_media_keys (void)
{
  GstElement *protection_system =
      gst_element_factory_make ("emeopencdmprotectionsystem", NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (protection_system), NULL);

  GstMediaKeySystemAccess *access =
      setup_system_access (protection_system, WIDEVINE_ID);
  gst_clear_object (&protection_system);
  g_return_val_if_fail (GST_IS_MEDIA_KEY_SYSTEM_ACCESS (access), NULL);

  GstMediaKeys *keys = setup_media_keys (access);
  gst_clear_object (&access);
  return keys;
}

static void
need_context_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * message,
    G_GNUC_UNUSED Application * app)
{
  const gchar *context_type = NULL;
  gst_message_parse_context_type (message, &context_type);

  if (g_strcmp0 (context_type, DRM_PREFERRED_CONTEXT)) {
    return;
  }

  GstElement *src = GST_ELEMENT (GST_MESSAGE_SRC (message));
  GST_LOG ("setting preferred DRM on %" GST_PTR_FORMAT, src);
  GstContext *context =
      gst_eme_context_new_protection_system_id (WIDEVINE_UUID);
  gst_element_set_context (src, context);
  gst_clear_context (&context);
}

static void
element_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * message, Application * app)
{
  gchar *init_data_type = NULL;
  GstBuffer *init_data = NULL;
  if (gst_message_parse_eme_encrypted (message, &init_data_type, &init_data)) {
    GST_LOG ("encrypted: %" GST_PTR_FORMAT, message);
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
    setup_eme (app, element, init_data_type, init_data);
    g_clear_pointer (&init_data_type, g_free);
    gst_clear_buffer (&init_data);
    return;
  }

  GstBuffer *key_id = NULL;
  GstPromise *promise = NULL;
  if (gst_message_parse_eme_waiting_for_key (message, &key_id, &promise)) {
    GST_LOG ("waiting for key: %" GST_PTR_FORMAT, message);
    app->waiting_for_key = g_steal_pointer (&promise);
    gst_clear_buffer (&key_id);
    gst_clear_promise (&promise);
    return;
  }
}

static void
end_of_stream (G_GNUC_UNUSED GstPlaySignalAdapter * signals, Application * app)
{
  gst_play_set_uri (app->play, NULL);
  gst_play_stop (app->play);
  g_main_loop_quit (app->loop);
}

static gconstpointer QUEUE_FINAL_MESSAGE = NULL;

static gpointer
license_request_task (Application * app)
{
  while (TRUE) {
    gpointer item = g_async_queue_pop (app->license_requests);
    if (item == &QUEUE_FINAL_MESSAGE) {
      break;
    }
    LicenseRequest *request = item;
    GBytes *response = request_license (request->data, WIDEVINE_LICENSE_URL);
    if (response == NULL) {
      GST_ERROR ("failed to request license");
      goto next;
    }
    GstBuffer *response_buffer = gst_buffer_new_wrapped_bytes (response);
    GstPromise *promise = gst_promise_new ();
    GST_MEMDUMP ("license response", g_bytes_get_data (response, NULL),
        g_bytes_get_size (response));
    gst_media_key_session_update (request->session, response_buffer, promise);

    gst_clear_buffer (&response_buffer);
    g_clear_pointer (&response, g_bytes_unref);

    GST_LOG ("waiting for session update response");
    gst_promise_wait (promise);
    const GstStructure *update_response = gst_promise_get_reply (promise);
    GST_LOG ("got update reply %" GST_PTR_FORMAT, update_response);
    gst_clear_promise (&promise);

    if (app->waiting_for_key) {
      gst_promise_reply (app->waiting_for_key, NULL);
      gst_clear_promise (&app->waiting_for_key);
    }
  next:
    license_request_clear (&request);
  }
  return NULL;
}

static void
application_clear (Application * app)
{
  gst_clear_object (&app->play);
  g_clear_pointer (&app->loop, g_main_loop_unref);
  g_clear_pointer (&app->license_requests, g_async_queue_unref);
  gst_clear_promise (&app->waiting_for_key);
  gst_clear_object (&app->keys);
}

gint
main (gint argc, gchar ** argv)
{
  g_setenv ("GST_PLAY_USE_PLAYBIN3", "1", TRUE);

  gst_init (&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (emedemo, "emedemo", 0, "EME demo");

  Application app = {
    .play = gst_play_new (NULL),
    .loop = g_main_loop_new (NULL, FALSE),
    .license_requests = g_async_queue_new (),
    .waiting_for_key = NULL,
    .keys = setup_widevine_media_keys (),
  };

  GMainContext *context = g_main_loop_get_context (app.loop);
  GstPlaySignalAdapter *signals =
      gst_play_signal_adapter_new_with_main_context (app.play, context);
  GstElement *pipeline = gst_play_get_pipeline (app.play);
  GstBus *bus = gst_element_get_bus (pipeline);

  g_signal_connect (signals, "end-of-stream", G_CALLBACK (end_of_stream), &app);
  g_signal_connect (bus, "message::need-context", G_CALLBACK (need_context_cb),
      &app);
  g_signal_connect (bus, "message::element", G_CALLBACK (element_cb), &app);

  GThread *license_requests_thread = g_thread_new ("license-requests",
      (GThreadFunc) license_request_task, &app);

  gst_play_set_uri (app.play, WIDEVINE_DASH_URL);

  gst_play_play (app.play);
  g_main_loop_run (app.loop);
  g_async_queue_push (app.license_requests, &QUEUE_FINAL_MESSAGE);
  g_thread_join (license_requests_thread);
  g_clear_pointer (&license_requests_thread, g_thread_unref);
  g_clear_object (&signals);
  gst_clear_object (&pipeline);
  gst_clear_object (&bus);

  application_clear (&app);

  return 0;
}

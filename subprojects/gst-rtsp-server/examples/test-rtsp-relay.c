/*******************************************************************
 * Dynamic RTSP Relay
 *
 * This application provides a simple relay and server based on the
 * GStreamer RTSP server.
 *
 * The relay server allows to register resources dynamically. The syntax is
 *
 * In the following example, a camera is registered to "garage"
 * /garage -> rtsp://garage.fritz.box
 *
 * The first step is the URI encode the camera the relay is to connect to:
 *
 * python3 -c 'import urllib.parse; print("rtsp://localhost:8554/garage?uri=%s" % urllib.parse.quote("rtsp://garage.fritz.box/", safe=""))'
 *
 * rtsp://localhost:8554/garage?uri=rtsp%3A%2F%2Fgarage.fritz.box%2F
 *
 * When the relay gets the options, it will inspect if the resource
 * /garage (the path in the URI) is registered. If it is not, it will
 * register it. From that point onwards, all other clients connecting to
 * '/garage' will receive the same relayed stream.
 *
 *
 * The source the relay needs to connect to, is URI encoded in the 'uri'
 * key in the query part of the connection string.
 *
 * rtsp://server/<path-to-register>?uri=<uri-encoded-network-source>
 *
 * Once the last client disconnects, the relay session is removed.
 *
 * Many sessions can be registered in parallel
 *
 * Connect to the server with:
 * gst-play-1.0 rtsp://localhost:8554/garage?uri=rtsp%3A%2F%2Fgarage.fritz.box%2F
 *
 ******************************************************************/
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>


#define DEFAULT_RTSP_PORT                "8554"
#define DEFAULT_SESSION_CLEANUP_INTERVAL (2)

#define TEST_RTSP_SERVER_LOCK(f)           (g_mutex_lock(&(lock)))
#define TEST_RTSP_SERVER_UNLOCK(f)         (g_mutex_unlock(&(lock)))

GMutex lock;

/**
 * test_rtsp_server_cleanup_timeout:
 *
 * @server: #GstRTSPServer instance
 *
 * Callback from timer to remove unused sessions. This is called
 * periodically to clean up the expired sessions from the pool.
 *
 * Returns True: schedule again
 *
 */
static gboolean
test_rtsp_server_cleanup_timeout (GstRTSPServer * server)
{
  g_autoptr (GstRTSPSessionPool) pool =
      gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);

  return TRUE;
}

/**
 * test_rtsp_server_register_uri_mount:
 *
 * @server: the #GstRTSPServer object to interact with
 * @uri: the uri to register
 * @mount: the mount location on the server to register the uri
 * @protocols: #GstRTSPLowerTrans protocols to use (override)
 * @shared: should the resource be re-used or not (media)
 *
 * Register the a uri on the RTSP Server on the mount location
 */
static void
test_rtsp_server_register_uri_mount (GstRTSPServer * server, const gchar * uri,
    const gchar * mount, GstRTSPLowerTrans protocols, gboolean shared)
{
  g_autoptr (GstRTSPMountPoints) mounts =
      gst_rtsp_server_get_mount_points (server);
  g_autoptr (GstRTSPMediaFactoryURI) factory =
      gst_rtsp_media_factory_uri_new ();

  g_return_if_fail (uri != NULL);
  g_return_if_fail (mount != NULL);

  gst_rtsp_media_factory_uri_set_uri (GST_RTSP_MEDIA_FACTORY_URI (factory),
      uri);

  gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (factory), shared);
  gst_rtsp_mount_points_add_factory (mounts, mount,
      GST_RTSP_MEDIA_FACTORY (gst_object_ref (factory)));

  g_print ("stream (uri) registered at rtsp://127.0.0.1%s\n", mount);
}

/**
 * test_rtsp_server_register_mount:
 *
 * @server: the #GstRTSPServer object to interact with
 * @uri: the uri to register
 * @mount: the mount location on the server to register the uri
 * @protocols: #GstRTSPLowerTrans protocols to use (override)
 * @shared: should the resource be re-used or not (media)
 *
 * Wrapper for test_rtsp_register_uri_mount and
 * test_rtsp_register_replay_mount, see description there.
 */
static void
test_rtsp_server_register_mount (GstRTSPServer * server, const gchar * uri,
    const gchar * mount, GstRTSPLowerTrans protocols, gboolean shared)
{
  g_autoptr (GstRTSPMountPoints) mounts = NULL;
  g_autoptr (GstRTSPMediaFactory) factory = NULL;

  TEST_RTSP_SERVER_LOCK (server);
  mounts = gst_rtsp_server_get_mount_points (server);
  factory = gst_rtsp_mount_points_match (mounts, mount, NULL);

  if (factory == NULL) {
    g_print ("%s: not yet registered\n", mount);
    test_rtsp_server_register_uri_mount (server, uri, mount, protocols, shared);
  } else {
    g_print ("%s: already registered\n", mount);
  }
  TEST_RTSP_SERVER_UNLOCK (server);
}

/**
 * test_rtsp_server_client_options_request:
 *
 * @client: the #GstRTSPClient that sends the options
 * @ctx: the GstRTSPContext of the session
 * @user_data: user data, #GstRTSPServer
 *
 * Callback on a Client session when a client sends an OPTIONS request. At
 * this point, check if a new RTSP uri/mount combo needs to be added.
 */
static void
test_rtsp_server_client_options_request (GstRTSPClient * client,
    GstRTSPContext * ctx, gpointer user_data)
{
  GstRTSPServer *server = GST_RTSP_SERVER (user_data);
  g_autofree gchar *uri = gst_rtsp_url_get_request_uri (ctx->uri);
  g_autoptr (GstUri) g_uri = gst_uri_from_string (uri);
  const gchar *k_uri = gst_uri_get_query_value (g_uri, "uri");
  /* See if the client requests a certain protocol, if so; fish it out
   * and use it to configure the lower transport protocols for RTSP */
  const gchar *k_protocols = gst_uri_get_query_value (g_uri, "protocols");
  g_autofree gchar *k_mount = gst_uri_get_path (g_uri);
  guint protocols =
      (k_protocols) ? g_ascii_strtoull (k_protocols, NULL, 16) : 0x0;
  gboolean shared = TRUE;

  g_print ("  uri: %s\n", uri);

  if ((k_uri != NULL)
      && (k_mount != NULL)
      && gst_uri_is_valid (k_uri)) {
    g_print ("Received a dynamic URI \"%s\"\n", uri);

    test_rtsp_server_register_mount (server, k_uri, k_mount, protocols, shared);
  } else {
    g_printerr ("Unknown URI: %s", uri);
  }
}

/**
 * test_rtsp_server_client_connected:
 *
 * @server: the #GstRTSPServer object that is active
 * @client: the new #GstRTSPClient object that connected
 * @user_data:
 *
 * Callback on client-connected on the RTSP server.
 */
static void
test_rtsp_server_client_connected (GstRTSPServer * server,
    GstRTSPClient * client, gpointer user_data)
{
  const gchar *ip =
      gst_rtsp_connection_get_ip (gst_rtsp_client_get_connection (client));
  g_print ("New client connected from \"%s\"\n", ip);

  g_signal_connect (client, "options-request",
      (GCallback) test_rtsp_server_client_options_request, server);
}

int
main (int argc, char *argv[])
{
  g_autoptr (GstRTSPServer) server = gst_rtsp_server_new ();
  g_autoptr (GError) error = NULL;
  g_autoptr (GMainLoop) loop;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* Lock to enforce serialising requests */
  g_mutex_init (&(lock));

  g_print ("Starting Dynamic RTSP relay\n");

  g_object_set (server, "service", DEFAULT_RTSP_PORT, NULL);

  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_signal_connect (server, "client-connected",
      (GCallback) test_rtsp_server_client_connected, NULL);

  /* do session cleanup every x (default 2) seconds */
  g_timeout_add_seconds (DEFAULT_SESSION_CLEANUP_INTERVAL,
      (GSourceFunc) test_rtsp_server_cleanup_timeout, server);

  g_print ("Waiting for connections\n");

  g_main_loop_run (loop);

  g_mutex_clear (&(lock));

  return 0;

failed:
  {
    g_printerr ("failed to attach the server\n");
    return -1;
  }
}

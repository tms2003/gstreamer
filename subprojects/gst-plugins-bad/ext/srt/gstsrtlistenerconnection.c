/* GStreamer
 *   Author: Jonas Danielsson <jonas.danielsson@spiideo,com>
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
 */

#include "gstsrtobject.h"
#include "gstsrtcaller.h"
#include "gstsrtlistenerconnection.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_srtobject);
#define GST_CAT_DEFAULT gst_debug_srtobject

/*
 * This code is responsible for SRT listening connections. That means
 * listening for- and accepting connections from SRT callers.
 *
 * We want to be able to multiplex several SRT connections over the same UDP
 * socket so we keep a global table of connections, indexed by an unique id.
 *
 * When a GstSRTConnection is created we start a thread for accepting callers
 * and subsequent users of the same connection will rely on that thread for
 * accepting callers.
 */

/* This is a global table of all listening connections for an application */
static GHashTable *connections_table;

/* This lock guards access to the connections_table */
static GMutex connections_lock;

static void
gst_srt_listener_connection_destroy (GstSRTListenerConnection * connection)
{
  GST_DEBUG ("Destroying listener connection");

  g_hash_table_remove (connections_table, (gconstpointer) connection->key);

  if (connection->sock != SRT_INVALID_SOCK) {
    srt_close (connection->sock);
  }

  if (connection->rsock != SRT_INVALID_SOCK) {
    srt_close (connection->rsock);
  }

  g_free (connection->key);
  g_list_free (connection->objects);

  g_thread_join (connection->accept_thread);

  g_free (connection);
}

static GHashTable *
gst_srt_connections_get_unlocked (gboolean create)
{
  if (connections_table == NULL && create) {
    connections_table =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gst_srt_listener_connection_destroy);
  }
  return connections_table;
}

static void
gst_srt_listener_connection_remove_unlocked (GstSRTListenerConnection *
    connection)
{
  GHashTable *connections = gst_srt_connections_get_unlocked (FALSE);
  if (connections) {
    g_hash_table_remove (connections, connection->key);
  }
}

static int
gst_srt_listener_connection_compare_func (gconstpointer object,
    gconstpointer stream_id)
{
  const char *object_stream_id =
      gst_structure_get_string (((GstSRTObject *) object)->parameters,
      "streamid");

  return g_strcmp0 (object_stream_id, (const gchar *) stream_id);
}

static GstSRTObject *
gst_srt_listener_connection_get_object (GstSRTListenerConnection
    * connection, char *stream_id)
{
  GList *item;

  g_mutex_lock (&connections_lock);

  // If no connection key is set then this is a single element connection
  if (!connection->key_is_set) {
    item = g_list_first (connection->objects);
  } else {
    item = g_list_find_custom (connection->objects, stream_id,
        gst_srt_listener_connection_compare_func);
  }

  g_mutex_unlock (&connections_lock);

  if (item) {
    return item->data;
  }

  return NULL;
}

static GSocketAddress *
peeraddr_to_g_socket_address (const struct sockaddr *peeraddr)
{
  gsize peeraddr_len;

  switch (peeraddr->sa_family) {
    case AF_INET:
      peeraddr_len = sizeof (struct sockaddr_in);
      break;
    case AF_INET6:
      peeraddr_len = sizeof (struct sockaddr_in6);
      break;
    default:
      g_warning ("Unsupported address family %d", peeraddr->sa_family);
      return NULL;
  }
  return g_socket_address_new_from_native ((gpointer) peeraddr, peeraddr_len);
}

static gint
srt_listen_callback_func (GstSRTListenerConnection * connection, SRTSOCKET sock,
    int hs_version, const struct sockaddr *peeraddr, const char *stream_id)
{
  GSocketAddress *addr = NULL;
  GstSRTObject *object = NULL;

  object =
      gst_srt_listener_connection_get_object (connection, (gchar *) stream_id);
  if (!object) {
    GST_DEBUG ("Caller with streamid: %s not part of connection: %s",
        stream_id, connection->key);
    return -1;
  }

  addr = peeraddr_to_g_socket_address (peeraddr);
  if (!addr) {
    GST_WARNING ("Invalid peer address. Rejecting sink %d streamid: %s",
        sock, stream_id);
    return -1;
  }

  if (object->authentication) {
    gboolean authenticated = FALSE;

    /* notifying caller-connecting */
    g_signal_emit_by_name (object->element, "caller-connecting", addr,
        stream_id, &authenticated);

    if (!authenticated)
      goto reject_auth;
  }

  GST_INFO_OBJECT (object->element, "Accepting sink %d streamid: %s", sock,
      stream_id);
  g_object_unref (addr);
  return 0;
reject_auth:
  GST_WARNING_OBJECT (object->element,
      "Rejecting baed on authentication, sink %d streamid: %s", sock,
      stream_id);

  /* notifying caller-rejected */
  g_signal_emit_by_name (object->element, "caller-rejected", addr, stream_id);
  g_object_unref (addr);
  return -1;
}

static gpointer
gst_srt_accept_thread_func (gpointer data)
{
  GstSRTListenerConnection *connection = data;

  while (g_list_length (connection->objects) > 0) {
    gint ret;
    SRTSOCKET rsock = SRT_INVALID_SOCK;
    gint rsocklen = 1;
    SYSSOCKET rsys, wsys;
    gint rsyslen = 1, wsyslen = 1;
    SRTSOCKET caller_sock;
    union
    {
      struct sockaddr_storage ss;
      struct sockaddr sa;
    } caller_sa = { 0, };
    int caller_sa_len = sizeof (caller_sa);
    GstSRTCaller *caller;
    gint flag = SRT_EPOLL_ERR;
    gint fd, fd_flags = SRT_EPOLL_ERR | SRT_EPOLL_IN;
    char caller_streamid[513];

    switch (srt_getsockstate (connection->sock)) {
      case SRTS_BROKEN:
      case SRTS_CLOSING:
      case SRTS_CLOSED:
      case SRTS_NONEXIST:
        SRT_CONNECTION_ELEMENT_ERROR (connection, RESOURCE, FAILED,
            ("Socket is broken or closed"), (NULL));
        break;

      default:
        break;
    }

    ret =
        srt_epoll_wait (connection->poll_id, &rsock, &rsocklen, NULL, 0,
        connection->poll_timeout, &rsys, &rsyslen, &wsys, &wsyslen);
    if (ret < 0) {
      gint srt_errno = srt_getlasterror (NULL);
      if (srt_errno != SRT_ETIMEOUT) {
        GST_WARNING ("Failed to poll socket: %s", srt_getlasterror_str ());
        break;
      }
    }

    if (rsock == SRT_INVALID_SOCK || rsocklen != 1) {
      continue;
    }

    connection->rsock = rsock;

    GST_DEBUG ("Waiting for accept, connection: %s", connection->key);
    caller_sock = srt_accept (connection->rsock, &caller_sa.sa, &caller_sa_len);
    if (caller_sock == SRT_INVALID_SOCK) {
      GST_DEBUG ("Failed to accept connection: %s", srt_getlasterror_str ());
      continue;
    }

    caller = gst_srt_caller_new ();
    caller->sockaddr =
        g_socket_address_new_from_native (&caller_sa.sa, caller_sa_len);
    caller->poll_id = srt_epoll_create ();
    caller->sock = caller_sock;

    int len = 512 + 1;
    srt_getsockopt (caller->sock, 0, SRTO_STREAMID, &caller_streamid, &len);

    GstSRTObject *srtobject =
        gst_srt_listener_connection_get_object (connection,
        (gchar *) & caller_streamid);
    fd = g_cancellable_get_fd (srtobject->cancellable);
    if (fd >= 0)
      srt_epoll_add_ssock (srtobject->poll_id, fd, &fd_flags);

    if (gst_uri_handler_get_uri_type (GST_URI_HANDLER
            (srtobject->element)) == GST_URI_SRC) {
      flag |= SRT_EPOLL_IN;
    } else {
      flag |= SRT_EPOLL_OUT;
    }

    if (srt_epoll_add_usock (caller->poll_id, caller_sock, &flag) < 0) {
      GST_ELEMENT_WARNING (srtobject->element, LIBRARY, SETTINGS,
          ("%s", srt_getlasterror_str ()), (NULL));

      gst_srt_caller_free (caller);

      /* try-again */
      continue;
    }

    GST_INFO_OBJECT (srtobject->element,
        "Accepted to connect, socket: %d, streamid: %s, connection: %s",
        caller->sock, caller_streamid, connection->key);

    g_mutex_lock (&srtobject->sock_lock);
    srtobject->callers = g_list_prepend (srtobject->callers, caller);
    g_cond_signal (&srtobject->sock_cond);
    g_mutex_unlock (&srtobject->sock_lock);

    /* notifying caller-added */
    g_signal_emit_by_name (srtobject->element, "caller-added", 0,
        caller->sockaddr);
  }

  GST_DEBUG ("Accept thread for connection: %s exited", connection->key);

  return NULL;
}

static gboolean
gst_srt_listener_connection_accept_callers (GstSRTListenerConnection *
    connection)
{
  gboolean ret = TRUE;

  if (!connection->accept_thread) {
    connection->accept_thread =
        g_thread_try_new ("GstSRTObjectAccepter", gst_srt_accept_thread_func,
        connection, NULL);
    if (connection->accept_thread == NULL) {
      GST_ERROR ("Failed to start thread");
      ret = FALSE;
    }
  }

  return ret;
}


static gboolean
gst_srt_listener_connection_init (GstSRTListenerConnection * connection,
    GstSRTObject * srtobject, guint local_port, GError ** error)
{
  gint sock_flags = SRT_EPOLL_ERR | SRT_EPOLL_IN;
  GSocketAddress *bind_addr = NULL;
  gboolean poll_added = FALSE;
  gpointer bind_sa;
  gsize bind_sa_len;
  SRTSOCKET sock = SRT_INVALID_SOCK;

  bind_addr =
      gst_srt_object_resolve (srtobject, GST_SRT_DEFAULT_LOCALADDRESS,
      local_port, error);
  if (!bind_addr) {
    goto failed;
  }

  bind_sa_len = g_socket_address_get_native_size (bind_addr);
  bind_sa = g_alloca (bind_sa_len);

  if (!g_socket_address_to_native (bind_addr, bind_sa, bind_sa_len, error)) {
    goto failed;
  }

  g_clear_object (&bind_addr);

  sock = srt_create_socket ();
  if (sock == SRT_INVALID_SOCK) {
    g_set_error (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, "%s",
        srt_getlasterror_str ());
    goto failed;
  }

  if (!gst_srt_object_set_common_params (sock, srtobject, error)) {
    goto failed;
  }

  GST_DEBUG_OBJECT (srtobject->element, "Binding SRT connection to port: %d",
      local_port);
  if (srt_bind (sock, bind_sa, bind_sa_len) == SRT_ERROR) {
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_OPEN_READ_WRITE, "Cannot bind to %s:%d - %s",
        GST_SRT_DEFAULT_LOCALADDRESS, local_port, srt_getlasterror_str ());
    goto failed;
  }

  connection->sock = sock;

  if (srt_epoll_add_usock (connection->poll_id, sock, &sock_flags) < 0) {
    g_set_error (error, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_SETTINGS,
        "%s", srt_getlasterror_str ());
    goto failed;
  }
  poll_added = TRUE;

  if (srt_listen (connection->sock, 5) == SRT_ERROR) {
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_OPEN_READ_WRITE,
        "Cannot listen on bind socket: %s", srt_getlasterror_str ());
    goto failed;
  }

  /* Register the SRT listen callback */
  if (srt_listen_callback (connection->sock,
          (srt_listen_callback_fn *) srt_listen_callback_func, connection)) {
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_OPEN_READ_WRITE, "Cannot bind to %s:%d - %s",
        GST_SRT_DEFAULT_LOCALADDRESS, local_port, srt_getlasterror_str ());
    goto failed;
  }

  gst_srt_listener_connection_accept_callers (connection);
  connection->initialized = TRUE;

  return TRUE;

failed:
  if (poll_added) {
    srt_epoll_remove_usock (connection->poll_id, sock);
  }
  if (sock != SRT_INVALID_SOCK) {
    srt_close (sock);
  }
  g_clear_object (&bind_addr);
  srtobject->sock = SRT_INVALID_SOCK;
  return FALSE;
}


/**
 * gst_srt_listener_connection_add_object:
 * @srtobject: A #GstSRTObject
 * @error: A #GError to be set in case something goes wrong
 *
 * Add a #GstSRTObject to a listener connection. A new connection will be
 * created if one matching the `connection-key` property of the object does
 * not exist. If the `connection-key` property is not set the connection will
 * be identified by a UUID.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_srt_listener_connection_add_object (GstSRTObject * srtobject,
    GError ** error)
{
  GstSRTListenerConnection *connection = NULL;
  const char *stream_id =
      gst_structure_get_string (srtobject->parameters, "streamid");
  char *connection_key = NULL;
  gboolean key_is_set = FALSE;
  gboolean ret = TRUE;

  g_mutex_lock (&connections_lock);
  GST_OBJECT_LOCK (srtobject->element);

  GHashTable *connections = gst_srt_connections_get_unlocked (TRUE);
  if (srtobject->connection_key != NULL) {
    connection_key = g_strdup (srtobject->connection_key);
    key_is_set = TRUE;
  } else {
    connection_key = g_uuid_string_random ();
    srtobject->connection_key = g_strdup (connection_key);
  }
  GST_OBJECT_UNLOCK (srtobject->element);

  GST_DEBUG_OBJECT (srtobject->element, "Looking for connection with key: %s",
      connection_key);

  connection = g_hash_table_lookup (connections, connection_key);
  if (connection == NULL) {
    connection = g_new0 (GstSRTListenerConnection, 1);
    connection->initialized = FALSE;
    connection->poll_id = srt_epoll_create ();
    connection->key = g_strdup (connection_key);
    connection->key_is_set = key_is_set;
    connection->poll_timeout = GST_SRT_DEFAULT_POLL_TIMEOUT;
    GST_DEBUG_OBJECT (srtobject->element, "Creating new connection: %s",
        connection->key);
    g_hash_table_insert (connections, g_strdup (connection_key), connection);
  } else {
    GST_INFO_OBJECT (srtobject->element, "Found existing connection: %s",
        connection->key);

    gboolean added = FALSE;
    if (!connection->key_is_set) {
      added = g_list_length (connection->objects) != 0;
    } else {
      added = g_list_find_custom (connection->objects, stream_id,
          gst_srt_listener_connection_compare_func) != NULL;
    }

    if (added) {
      GST_WARNING ("The streamid '%s' is already part of the connection",
          stream_id);
      goto out;
    }
  }

  connection->objects = g_list_append (connection->objects, srtobject);
  GST_INFO_OBJECT (srtobject->element,
      "Added object with streamid: %s to connection: %s",
      stream_id == NULL ? "<unset>" : stream_id, connection_key);

  if (!connection->initialized) {
    guint local_port = 0;

    gst_structure_get_uint (srtobject->parameters, "localport", &local_port);
    if (!gst_srt_listener_connection_init (connection, srtobject, local_port,
            error)) {
      ret = FALSE;
      goto out;
    }
  }

out:
  g_free (connection_key);
  g_mutex_unlock (&connections_lock);

  return ret;
}

/**
 * gst_srt_listener_connection_remove_object:
 * @srtobject: A #GstSRTObject
 * @error: A #GError to be set in case something goes wrong
 *
 * Remove a #GstSRTObject from a listener connection.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_srt_listener_connection_remove_object (GstSRTObject * srtobject,
    GError ** error)
{
  gboolean ret = TRUE;
  GstSRTListenerConnection *connection = NULL;
  GList *item = NULL;
  const char *stream_id = gst_structure_get_string (srtobject->parameters,
      "streamid");

  if (srtobject->connection_key == NULL)
    return TRUE;

  g_mutex_lock (&connections_lock);

  GHashTable *connections = gst_srt_connections_get_unlocked (TRUE);
  connection = g_hash_table_lookup (connections, srtobject->connection_key);
  if (connection == NULL) {
    ret = FALSE;
    goto out;
  }

  item = g_list_find_custom (connection->objects, stream_id,
      gst_srt_listener_connection_compare_func);
  if (!item) {
    ret = FALSE;
    goto out;
  }

  connection->objects = g_list_remove (connection->objects, item->data);
  guint remaining = g_list_length (connection->objects);
  GST_DEBUG_OBJECT (srtobject->element,
      "Removed from connection %s, remaining objects in connection: %u",
      srtobject->connection_key, remaining);

  // If this was a single element connection then make sure we clean up the
  // generated UUID connection key.
  if (!connection->key_is_set) {
    g_free (srtobject->connection_key);
    srtobject->connection_key = NULL;
  }

  if (remaining == 0)
    gst_srt_listener_connection_remove_unlocked (connection);

out:
  g_mutex_unlock (&connections_lock);
  return ret;
}

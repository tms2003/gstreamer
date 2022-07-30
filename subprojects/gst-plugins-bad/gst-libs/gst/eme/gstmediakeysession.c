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
 * SECTION:gstmediakeysession
 * @title: GstMediaKeySession
 * @short_description: Media Key Session
 *
 * #GstMediaKeySession is an interface which groups a set of keys that are
 * relevant to a specific unit of Initialization Data. Every time an application
 * encounters new Initialization Data, it should request that a new session is
 * created which will be used to manage the keys necessary to work with the
 * associated media.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#mediakeysession-interface)
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmediakeysession.h"
#include "gstemelogging-private.h"

G_DEFINE_INTERFACE_WITH_CODE (GstMediaKeySession, gst_media_key_session,
    G_TYPE_OBJECT, gst_eme_init_logging ());

enum
{
  ON_KEY_STATUSES_CHANGE,
  ON_MESSAGE,

  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static void
gst_media_key_session_default_init (GstMediaKeySessionInterface * iface)
{
  /**
   * GstMediaKeySession::on-key-statuses-change:
   * @self: #GstMediaKeySession instance
   *
   * Emitted when the status of any key contained in @self changes.
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-onkeystatuseschange)
   *
   * Since: 1.24
   */
  signals[ON_KEY_STATUSES_CHANGE] = g_signal_new ("on-key-statuses-change",
      G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstMediaKeySession::on-message:
   * @self: #GstMediaKeySession instance
   * @message: #GstMessage instance
   *
   * Emitted when @self wishes to send some information to the license
   * authority. The application should listen to this event and forward
   * messages to the license authority as appropriate.
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-onmessage)
   *
   * Since: 1.24
   */
  signals[ON_MESSAGE] = g_signal_new ("on-message",
      G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_MESSAGE);
}

/**
 * gst_media_key_session_get_session_id:
 * @self: #GstMediaKeySession instance
 *
 * Get the identifier of the current session.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-sessionid)
 *
 * Returns: (transfer none) (nullable): a text string containing the current
 * session ID or `NULL` if it hasn't been set yet
 * Since: 1.24
 */
const gchar *
gst_media_key_session_get_session_id (GstMediaKeySession * self)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  return iface->get_session_id (self);
}

/**
 * gst_media_key_session_get_expiration:
 * @self: #GstMediaKeySession instance
 *
 * Get the expiration time of all keys contained by this session.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-expiration)
 *
 * Returns: a #GstClockTime containing the expiration time of this session
 * or %GST_CLOCK_TIME_NONE if there's no expiration time
 * Since: 1.24
 */
GstClockTime
gst_media_key_session_get_expiration (GstMediaKeySession * self)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  return iface->get_expiration (self);
}

/**
 * gst_media_key_session_get_closed:
 * @self: #GstMediaKeySession instance
 * @promise: (transfer none): #GstPromise which will be answered with the closed
 * state of this session
 *
 * Query whether this session's state is closed. This is an asynchronous
 * operation and the answer will be returned in the supplied @promise.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-closed)
 *
 * Since: 1.24
 */
void
gst_media_key_session_get_closed (GstMediaKeySession * self,
    GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->get_closed (self, promise);
}

/**
 * gst_media_key_session_has_media_key_status:
 * @self: #GstMediaKeySession instance
 * @key_id: (transfer none): #GstBuffer raw data representing the Key ID which is being
 * queried
 *
 * Answers whether the current session has any record of @key_id.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeystatusmap-has)
 *
 * Returns: `TRUE` if current session has any record of @key_id, otherwise `FALSE`
 * Since: 1.24
 */
gboolean
gst_media_key_session_has_media_key_status (GstMediaKeySession * self,
    GstBuffer * key_id)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  return iface->get_media_key_status (self, key_id);
}

/**
 * gst_media_key_session_get_media_key_status:
 * @self: #GstMediaKeySession instance
 * @key_id: (transfer none): #GstBuffer raw data representing the Key ID which
 * is being queried
 *
 * Gets the #GstMediaKeyStatus of @key_id within the current session
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeystatusmap-get)
 *
 * Returns: The #GstMediaKeyStatus for the supplied Key ID
 * Since: 1.24
 */
GstMediaKeyStatus
gst_media_key_session_get_media_key_status (GstMediaKeySession * self,
    GstBuffer * key_id)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  return iface->get_media_key_status (self, key_id);
}

/**
 * gst_media_key_session_get_media_key_status_count:
 * @self: #GstMediaKeySession instance
 *
 * Gets the number of keys contained by the current session
 *
 * Returns: The number of keys that this session contains
 * Since: 1.24
 */
gsize
gst_media_key_session_get_media_key_status_count (GstMediaKeySession * self)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  return iface->get_media_key_status_count (self);
}

/**
 * gst_media_key_session_generate_request:
 * @self: #GstMediaKeySession instance
 * @init_data_type: string identifying the format of @init_data. Typical values
 * include `cenc`, `keyids`, and `webm`.
 * @init_data: (transfer none): #GstBuffer containing the raw initialization
 * data to be used by the underlying CDM to create a license request
 * @promise: (transfer none): #GstPromise which will be answered with a license
 * request on success or an error on failure
 *
 * Generates a license request based on the supplied Initialization Data. This
 * is an asynchronous operation and the supplied @promise will contain the
 * license request generated by the CDM when it is resolved successfully.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-generaterequest)
 *
 * Since: 1.24
 */
void
gst_media_key_session_generate_request (GstMediaKeySession * self,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->generate_request (self, init_data_type, init_data, promise);
}

/**
 * gst_media_key_session_load:
 * @self: #GstMediaKeySession instance
 * @session_id: string representing the Session ID
 * @promise: (transfer none): #GstPromise which will be answered when the operation either
 * succeeds or fails
 *
 * Attempts to load data into this session from persistent storage.
 *
 * Since: 1.24
 */
void
gst_media_key_session_load (GstMediaKeySession * self,
    const gchar * session_id, GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->load (self, session_id, promise);
}

/**
 * gst_media_key_session_update:
 * @self: #GstMediaKeySession instance
 * @response: (transfer none): #GstBuffer containing raw data of a license authority's response
 * to a license request
 * @promise: (transfer none): #GstPromise which will be answered when the
 * operation either succeeds or fails
 *
 * Attempts to supply a message sent by the license authority to the
 * underlying CDM. Typically this will be used to populate this session with
 * decryption keys that were previously requested. This is an asynchronous
 * operation and the underlying CDM will have processed the update when @promise
 * is resolved successfully.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-update)
 *
 * Since: 1.24
 */
void
gst_media_key_session_update (GstMediaKeySession * self, GstBuffer * response,
    GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->update (self, response, promise);
}

/**
 * gst_media_key_session_close:
 * @self: #GstMediaKeySession instance
 * @promise: (transfer none): #GstPromise which will be answered when the operation either
 * succeeds or fails
 *
 * Attempts to close the session when the application doesn't need it
 * anymore. This is an asynchronous operation and all temporary resources held
 * by the session should be freed when @promise is resolved successfully.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-close)
 *
 * Since: 1.24
 */
void
gst_media_key_session_close (GstMediaKeySession * self, GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->close (self, promise);
}

/**
 * gst_media_key_session_remove:
 * @self: #GstMediaKeySession instance
 * @promise: (transfer none): #GstPromise which will be answered when the
 * operation either succeeds or fails
 *
 * Attempts to remove all credentials held by this session. This is an
 * asynchronous operation and all persistent data stored by the CDM for this
 * session should be removed when @promise is resolved successfully.
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-remove)
 *
 * Since: 1.24
 */
void
gst_media_key_session_remove (GstMediaKeySession * self, GstPromise * promise)
{
  GstMediaKeySessionInterface *iface = GST_MEDIA_KEY_SESSION_GET_IFACE (self);

  iface->remove (self, promise);
}

/**
 * gst_media_key_session_publish_key_statuses_change:
 * @self: #GstMediaKeySession instance
 *
 * For implementors of the #GstMediaKeySession interface, this is a helper
 * function to emit the signal "on-key-statuses-change"
 *
 * Since: 1.24
 */
void
gst_media_key_session_publish_key_statuses_change (GstMediaKeySession * self)
{
  g_return_if_fail (GST_IS_MEDIA_KEY_SESSION (self));
  g_signal_emit (self, signals[ON_KEY_STATUSES_CHANGE], 0);
}

/**
 * gst_media_key_session_publish_message:
 * @self: #GstMediaKeySession instance
 * @message: (transfer full): #GstMessage which will be passed to the
 * application
 *
 * For implementors of the #GstMediaKeySession interface, this is a helper
 * function to emit the signal "on-message" with the supplied @message
 *
 * Since: 1.24
 */
void
gst_media_key_session_publish_message (GstMediaKeySession * self,
    GstMessage * message)
{
  g_return_if_fail (GST_IS_MEDIA_KEY_SESSION (self));
  g_return_if_fail (GST_IS_MESSAGE (message));
  g_signal_emit (self, signals[ON_MESSAGE], 0, message);
  gst_clear_message (&message);
}

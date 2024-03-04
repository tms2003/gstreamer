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

/**
 * SECTION:gstemeutils
 * @title: EME Utilities
 * @short_description: EME Utility Functions
 *
 * Various helper functions that applications working with the GST EME library
 * as well as implementors of the GST EME interfaces can use to simplify the
 * authoring and processing of custom data stored in #GstStructure<!---->s
 * contained in #GstMessage and #GstPromise objects.
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstemeutils.h"
#include "gstemelogging-private.h"

#define SESSION_MESSAGE_LICENSE_REQUEST            "gst-eme-license-request"
#define SESSION_MESSAGE_LICENSE_RENEWAL            "gst-eme-license-renewal"
#define SESSION_MESSAGE_LICENSE_RELEASE            "gst-eme-license-release"
#define SESSION_MESSAGE_INDIVIDUALIZATION_REQUEST  "gst-eme-individualization-request"

#define SESSION_ASYNC_GENERATE_REQUEST             "gst-eme-generate-request"
#define SESSION_ASYNC_LOAD                         "gst-eme-load"
#define SESSION_ASYNC_UPDATE                       "gst-eme-update"
#define SESSION_ASYNC_REMOVE                       "gst-eme-remove"
#define SESSION_ASYNC_CLOSE                        "gst-eme-close"

#define DECRYPTOR_MESSAGE_ENCRYPTED                "gst-eme-encrypted"
#define DECRYPTOR_MESSAGE_WAITING_FOR_KEY          "gst-eme-waiting-for-key"
#define DECRYPTOR_MESSAGE_HAVE_KEY                 "gst-eme-have-key"

#define FIELD_DECRYPTION_SYSTEM_ID                 "decryption-system-id"
#define FIELD_INIT_DATA                            "init-data"
#define FIELD_INIT_DATA_ORIGIN                     "init-data-origin"
#define FIELD_INIT_DATA_TYPE                       "init-data-type"
#define FIELD_KEY_ID                               "key-id"
#define FIELD_MEDIA_KEYS                           "media-keys"
#define FIELD_MEDIA_KEY_SYSTEM_ACCESS              "media-key-system-access"
#define FIELD_MESSAGE                              "message"
#define FIELD_MESSAGE_TYPE                         "message-type"
#define FIELD_PROMISE                              "promise"
#define FIELD_RAW_INIT_DATA                        "raw-init-data"
#define FIELD_RESPONSE                             "response"
#define FIELD_SESSION_ID                           "session-id"

#define CONTEXT_DRM_PREFERRED_DECRYPTION_SYSTEM_ID \
    "drm-preferred-decryption-system-id"
#define CONTEXT_MEDIA_KEYS FIELD_MEDIA_KEYS

typedef enum
{
  GST_MEDIA_KEY_SESSION_ERROR_NONE,
  GST_MEDIA_KEY_SESSION_ERROR_INVALID_STATE,
  GST_MEDIA_KEY_SESSION_ERROR_TYPE,
} GstMediaKeySessionError;

/**
 * gst_eme_context_new_protection_system_id:
 * @uuid: (transfer none): UUID of the application's preferred protection system
 *
 * Creates a new #GstContext containing an appropriate response for
 * %GST_MESSAGE_NEED_CONTEXT messages sent by an element in the pipeline when it
 * encounters protected media. The application should place this context in the
 * message source's element using #gst_element_set_context.
 *
 * Returns: a new #GstContext
 * Since: 1.24
 */
GstContext *
gst_eme_context_new_protection_system_id (const gchar * uuid)
{
  g_return_val_if_fail (uuid != NULL, NULL);
  GstContext *context =
      gst_context_new (CONTEXT_DRM_PREFERRED_DECRYPTION_SYSTEM_ID, FALSE);
  GstStructure *structure = gst_context_writable_structure (context);
  gst_structure_set (structure, FIELD_DECRYPTION_SYSTEM_ID, G_TYPE_STRING, uuid,
      NULL);
  return context;
}

/**
 * gst_eme_context_new_media_keys:
 * @media_keys: (transfer none): #GstMediaKeys instance to insert into context
 *
 * Creates a new #GstContext containing an appropriate response for
 * %GST_EME_MESSAGE_TYPE_EME_ENCRYPTED messages sent by an element in the
 * pipeline when it encounters protected media. The application should place
 * this context in the message source's element using #gst_element_set_context.
 *
 * Returns: a new #GstContext
 * Since: 1.24
 */
GstContext *
gst_eme_context_new_media_keys (GstMediaKeys * media_keys)
{
  g_return_val_if_fail (GST_IS_MEDIA_KEYS (media_keys), NULL);
  GstContext *context = gst_context_new (CONTEXT_MEDIA_KEYS, TRUE);
  GstStructure *structure = gst_context_writable_structure (context);
  gst_structure_set (structure, FIELD_MEDIA_KEYS, GST_TYPE_MEDIA_KEYS,
      media_keys, NULL);
  return context;
}

/**
 * gst_eme_context_get_media_keys:
 * @context: (transfer none): #GstContext to inspect
 * @media_keys: (out) (nullable) (transfer full): #GstMediaKeys to retrieve
 *
 * Attempts to extract a #GstMediaKeys instance into @media_keys from @context.
 *
 * Returns: `TRUE` when successful, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_eme_context_get_media_keys (GstContext * context, GstMediaKeys **
    media_keys)
{
  g_return_val_if_fail (media_keys != NULL, FALSE);
  const GstStructure *structure = gst_context_get_structure (context);
  return gst_structure_get (structure, FIELD_MEDIA_KEYS, GST_TYPE_MEDIA_KEYS,
      media_keys, NULL);
}

/**
 * gst_eme_response_type_error:
 *
 * Creates a #GstStructure suitable for signalling an error to a
 * #GstPromise that is analogous to a TypeError in the DOM specification.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_type_error (void)
{
  return gst_structure_new ("error", "error", G_TYPE_INT,
      GST_MEDIA_KEY_SESSION_ERROR_TYPE, NULL);
}

/**
 * gst_eme_response_invalid_state_error:
 *
 * Creates a #GstStructure suitable for signalling an error to a
 * #GstPromise that is analogous to an InvalidStateError in the DOM
 * specification.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_invalid_state_error (void)
{
  return gst_structure_new ("error", "error", G_TYPE_INT,
      GST_MEDIA_KEY_SESSION_ERROR_INVALID_STATE, NULL);
}

/**
 * gst_eme_response_ok:
 *
 * Creates a #GstStructure suitable for signalling a successful result to a
 * #GstPromise.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_ok (void)
{
  return gst_structure_new_empty ("ok");
}

/**
 * gst_eme_response_system_access:
 * @system_access: (transfer none): The #GstMediaKeySystemAccess instance to
 * include in the response
 *
 * Creates a #GstStructure suitable for a successful response to the action
 * signal ::request-media-key-system-access handled by any implementor of a
 * "Protection System" pseudo-element.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_system_access (GstMediaKeySystemAccess * system_access)
{
  g_return_val_if_fail (GST_IS_MEDIA_KEY_SYSTEM_ACCESS (system_access), NULL);
  GstStructure *structure = gst_structure_new (FIELD_RESPONSE,
      FIELD_MEDIA_KEY_SYSTEM_ACCESS, GST_TYPE_MEDIA_KEY_SYSTEM_ACCESS,
      system_access, NULL);
  return structure;
}

/**
 * gst_eme_resolve_system_access:
 * @promise: (transfer none): the #GstPromise, which must already be in the
 * %GST_PROMISE_RESULT_REPLIED state.
 *
 * Attempts to extract a #GstMediaKeySystemAccess from a #GstPromise that was
 * supplied to the ::request-media-key-system-access action signal of a
 * "Protection System" pseudo-element.
 *
 * Returns: (transfer full): the #GstMediaKeySystemAccess object or `NULL`
 * Since: 1.24
 */
GstMediaKeySystemAccess *
gst_eme_resolve_system_access (GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);
  const GstStructure *reply = gst_promise_get_reply (promise);
  if (!GST_IS_STRUCTURE (reply)) {
    return NULL;
  }
  GstMediaKeySystemAccess *access = NULL;
  if (!gst_structure_get (reply, FIELD_MEDIA_KEY_SYSTEM_ACCESS,
          GST_TYPE_MEDIA_KEY_SYSTEM_ACCESS, &access, NULL)) {
    return NULL;
  }
  return access;
}

/**
 * gst_eme_response_media_keys:
 * @keys: (transfer none): The #GstMediaKeys instance to include in the response
 *
 * Creates a #GstStructure suitable for a successful response to the method
 * #gst_media_key_system_access_create_media_keys() by any implementor of
 * #GstMediaKeySystemAccess.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_media_keys (GstMediaKeys * keys)
{
  g_return_val_if_fail (GST_IS_MEDIA_KEYS (keys), NULL);
  return gst_structure_new (FIELD_RESPONSE, FIELD_MEDIA_KEYS,
      GST_TYPE_MEDIA_KEYS, keys, NULL);
}

/**
 * gst_eme_resolve_media_keys:
 * @promise: the #GstPromise, which must already be in the
 * %GST_PROMISE_RESULT_REPLIED state.
 *
 * Attempts to extract a #GstMediaKeys from a #GstPromise that was
 * supplied to #gst_media_key_system_access_create_media_keys().
 *
 * Returns: (transfer full): the #GstMediaKeys object or `NULL`
 * Since: 1.24
 */
GstMediaKeys *
gst_eme_resolve_media_keys (GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);
  const GstStructure *reply = gst_promise_get_reply (promise);
  if (!GST_IS_STRUCTURE (reply)) {
    return NULL;
  }
  GstMediaKeys *keys = NULL;
  if (!gst_structure_get (reply, FIELD_MEDIA_KEYS, GST_TYPE_MEDIA_KEYS, &keys,
          NULL)) {
    return NULL;
  }
  return keys;
}

/**
 * gst_eme_response_init_data:
 * @init_data: (transfer none): A #GstBuffer containing initialization data
 *
 * Creates a #GstStructure suitable for a successful response to the method
 * #gst_media_key_session_generate_request() by any implementor of
 * #GstMediaKeySession.
 *
 * Returns: (transfer full): a new #GstStructure
 * Since: 1.24
 */
GstStructure *
gst_eme_response_init_data (GstBuffer * init_data)
{
  g_return_val_if_fail (GST_IS_BUFFER (init_data), NULL);
  return gst_structure_new (FIELD_RESPONSE,
      FIELD_MESSAGE_TYPE, G_TYPE_STRING, SESSION_MESSAGE_LICENSE_REQUEST,
      FIELD_MESSAGE, GST_TYPE_BUFFER, init_data, NULL);
}

/**
 * gst_eme_resolve_init_data:
 * @promise: the #GstPromise, which must already be in the
 * %GST_PROMISE_RESULT_REPLIED state.
 *
 * Attempts to extract a #GstBuffer of initialization data from a #GstPromise
 * that was supplied to #gst_media_key_session_generate_request().
 *
 * Returns: (transfer full): the initialization data or `NULL`
 * Since: 1.24
 */
GstBuffer *
gst_eme_resolve_init_data (GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);
  const GstStructure *reply = gst_promise_get_reply (promise);
  if (!GST_IS_STRUCTURE (reply)) {
    return NULL;
  }
  GstBuffer *init_data = NULL;
  if (!gst_structure_get (reply, FIELD_INIT_DATA, GST_TYPE_BUFFER, &init_data,
          NULL)) {
    return NULL;
  }
  return init_data;
}

/**
 * gst_eme_media_key_message_get_type:
 * @message: A #GstMessage sent by #GstMediaKeySession::on-message
 *
 * Determines the #GstEmeMediaKeyMessageType of @message
 *
 * Returns: #GstEmeMediaKeyMessageType or
 * %GST_EME_MEDIA_KEY_MESSAGE_TYPE_UNKNOWN when it cannot be determined
 * Since: 1.24
 */
GstEmeMediaKeyMessageType
gst_eme_media_key_message_get_type (GstMessage * message)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message),
      GST_EME_MEDIA_KEY_MESSAGE_TYPE_UNKNOWN);

  if (gst_message_has_name (message, SESSION_MESSAGE_LICENSE_REQUEST))
    return GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_REQUEST;
  if (gst_message_has_name (message, SESSION_MESSAGE_LICENSE_RENEWAL))
    return GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_RENEWAL;
  if (gst_message_has_name (message, SESSION_MESSAGE_LICENSE_RELEASE))
    return GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_RELEASE;
  if (gst_message_has_name (message, SESSION_MESSAGE_INDIVIDUALIZATION_REQUEST))
    return GST_EME_MEDIA_KEY_MESSAGE_TYPE_INDIVIDUALIZATION_REQUEST;

  GST_DEBUG ("invalid message %" GST_PTR_FORMAT, message);
  return GST_EME_MEDIA_KEY_MESSAGE_TYPE_UNKNOWN;
}

/**
 * gst_eme_message_get_type:
 * @message: A #GstMessage created by the GStreamer EME API
 *
 * Determines the #GstEmeMessageType of @message
 *
 * Returns: #GstEmeMessageType or %GST_EME_MESSAGE_TYPE_UNKNOWN when it cannot
 * be determined
 * Since: 1.24
 */
GstEmeMessageType
gst_eme_message_get_type (GstMessage * message)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), GST_EME_MESSAGE_TYPE_UNKNOWN);

  if (gst_message_has_name (message, SESSION_ASYNC_GENERATE_REQUEST)) {
    return GST_EME_MESSAGE_TYPE_GENERATE_REQUEST;
  }
  if (gst_message_has_name (message, SESSION_ASYNC_LOAD)) {
    return GST_EME_MESSAGE_TYPE_LOAD;
  }
  if (gst_message_has_name (message, SESSION_ASYNC_UPDATE)) {
    return GST_EME_MESSAGE_TYPE_UPDATE;
  }
  if (gst_message_has_name (message, SESSION_ASYNC_REMOVE)) {
    return GST_EME_MESSAGE_TYPE_REMOVE;
  }
  if (gst_message_has_name (message, SESSION_ASYNC_CLOSE)) {
    return GST_EME_MESSAGE_TYPE_CLOSE;
  }

  GST_DEBUG ("invalid message %" GST_PTR_FORMAT, message);
  return GST_EME_MESSAGE_TYPE_UNKNOWN;
}

/**
 * gst_message_new_eme_generate_request:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @init_data_type: (transfer none): A string supplied to
 * #gst_media_key_session_generate_request containing the type of initialization
 * data
 * @init_data: (transfer none): A #GstBuffer supplied to
 * #gst_media_key_session_generate_request containing the initialization data
 * @promise: (transfer none):  A #GstPromise supplied to
 * #gst_media_key_session_generate_request
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * an internal #GstMessage that the session can process asynchronously to answer
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_generate_request (GstObject * src,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise)
{
  g_return_val_if_fail (init_data_type != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (init_data), NULL);
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);

  GstStructure *structure = gst_structure_new (SESSION_ASYNC_GENERATE_REQUEST,
      FIELD_INIT_DATA_TYPE, G_TYPE_STRING, init_data_type,
      FIELD_INIT_DATA, GST_TYPE_BUFFER, init_data,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);

  return gst_message_new_application (src, structure);
}

/**
 * gst_message_parse_eme_generate_request:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to
 * itself
 * @init_data_type: (out) (transfer full): The type of Initialization Data as a
 * string value or `NULL` on failure
 * @init_data: (out) (transfer full): The Initialization Data as raw bytes or
 * `NULL` on failure
 * @promise: (out) (transfer full): The promise supplied by the application that
 * must be answered by the session or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to itself when
 * asynchronously processing a request triggered by
 * #gst_media_key_session_generate_request
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_generate_request (GstMessage * message, gchar
    ** init_data_type, GstBuffer ** init_data, GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);
  g_return_val_if_fail (init_data_type != NULL, FALSE);
  g_return_val_if_fail (init_data != NULL, FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  const GstStructure *structure = gst_message_get_structure (message);
  if (gst_eme_message_get_type (message) !=
      GST_EME_MESSAGE_TYPE_GENERATE_REQUEST) {
    return FALSE;
  }

  return gst_structure_get (structure,
      FIELD_INIT_DATA_TYPE, G_TYPE_STRING, init_data_type,
      FIELD_INIT_DATA, GST_TYPE_BUFFER, init_data,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_load:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @session_id: (transfer none): The session ID supplied to
 * #gst_media_key_session_load
 * @promise: (transfer none):  A #GstPromise supplied to
 * #gst_media_key_session_load
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * an internal #GstMessage that the session can process asynchronously to answer
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_load (GstObject * src, const gchar * session_id,
    GstPromise * promise)
{
  g_return_val_if_fail (session_id != NULL, NULL);
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);

  GstStructure *structure = gst_structure_new (SESSION_ASYNC_LOAD,
      FIELD_SESSION_ID, G_TYPE_STRING, session_id,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);

  return gst_message_new_application (src, structure);
}

/**
 * gst_message_parse_eme_load:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to
 * itself
 * @session_id: (out) (transfer full): The session ID as a string value or `NULL`
 * on failure
 * @promise: (out) (transfer full): The promise supplied by the application that
 * must be answered by the session or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to itself when
 * asynchronously processing a request triggered by
 * #gst_media_key_session_load
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_load (GstMessage * message, gchar ** session_id,
    GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);
  g_return_val_if_fail (session_id != NULL, FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  const GstStructure *structure = gst_message_get_structure (message);
  if (gst_eme_message_get_type (message) != GST_EME_MESSAGE_TYPE_LOAD) {
    return FALSE;
  }

  return gst_structure_get (structure,
      FIELD_SESSION_ID, G_TYPE_STRING, session_id,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_remove:
 * @src: (nullable) (transfer none): The sender of the message, typically the
 * #GstMediaKeySession generating this message or `NULL`
 * @promise: (transfer none):  A #GstPromise supplied by the application to
 * #gst_media_key_session_generate_request
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * an internal #GstMessage that the session can process asynchronously to answer
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_remove (GstObject * src, GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);
  GstStructure *structure = gst_structure_new (SESSION_ASYNC_REMOVE,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
  return gst_message_new_application (src, structure);
}

/**
 * gst_message_parse_eme_remove:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to
 * itself
 * @promise: (out) (transfer full): The promise supplied by the application that
 * must be answered by the session or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to itself when
 * asynchronously processing a request triggered by
 * #gst_media_key_session_remove
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_remove (GstMessage * message, GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  const GstStructure *structure = gst_message_get_structure (message);
  if (gst_eme_message_get_type (message) != GST_EME_MESSAGE_TYPE_REMOVE) {
    return FALSE;
  }

  return gst_structure_get (structure,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_close:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @promise: (transfer none):  A #GstPromise supplied to
 * #gst_media_key_session_generate_request
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * an internal #GstMessage that the session can process asynchronously to answer
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_close (GstObject * src, GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);

  GstStructure *structure = gst_structure_new (SESSION_ASYNC_CLOSE,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);

  return gst_message_new_application (src, structure);
}

/**
 * gst_message_parse_eme_close:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to
 * itself
 * @promise: (out) (transfer full): The promise supplied by the application that
 * must be answered by the session or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to itself when
 * asynchronously processing a request triggered by #gst_media_key_session_close
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_close (GstMessage * message, GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  const GstStructure *structure = gst_message_get_structure (message);
  if (gst_eme_message_get_type (message) != GST_EME_MESSAGE_TYPE_CLOSE) {
    return FALSE;
  }

  return gst_structure_get (structure,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_update:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @response: (transfer none): A #GstBuffer supplied to
 * #gst_media_key_session_update containing the license authority's response
 * @promise: (transfer none):  A #GstPromise supplied to
 * #gst_media_key_session_update
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * an internal #GstMessage that the session can process asynchronously to answer
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_update (GstObject * src, GstBuffer * response,
    GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_BUFFER (response), NULL);
  g_return_val_if_fail (GST_IS_PROMISE (promise), NULL);

  GstStructure *structure = gst_structure_new (SESSION_ASYNC_UPDATE,
      FIELD_RESPONSE, GST_TYPE_BUFFER, response,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise,
      NULL);

  return gst_message_new_application (src, structure);
}

/**
 * gst_message_parse_eme_update:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to
 * itself
 * @response (out) (transfer full): A #GstBuffer containing the response
 * supplied by the application
 * @promise: (out) (transfer full): The promise supplied by the application that
 * must be answered by the session or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to itself when
 * asynchronously processing a request triggered by
 * #gst_media_key_session_update
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_update (GstMessage * message, GstBuffer ** response,
    GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);
  g_return_val_if_fail (response != NULL, FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  const GstStructure *structure = gst_message_get_structure (message);
  if (gst_eme_message_get_type (message) != GST_EME_MESSAGE_TYPE_UPDATE) {
    return FALSE;
  }

  return gst_structure_get (structure,
      FIELD_RESPONSE, GST_TYPE_BUFFER, response,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_license_request:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @payload: (nullable) (transfer none): A #GstBuffer containing data generated
 * by the CDM which must be sent to the license authority to request a license
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * a #GstMessage that the session will send to the application for the purpose
 * of requesting a new license from the license authority.
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_license_request (GstObject * src, GstBuffer * payload)
{
  GstStructure *structure = gst_structure_new (SESSION_MESSAGE_LICENSE_REQUEST,
      FIELD_MESSAGE, GST_TYPE_BUFFER, payload, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_license_request:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to the
 * application
 * @payload: (out) (transfer full): The license request message generated by the
 * CDM or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to the
 * application when the underlying CDM needs a license to decrypt protected
 * media
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_license_request (GstMessage * message,
    GstBuffer ** payload)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (gst_eme_media_key_message_get_type (message) !=
      GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_REQUEST) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_MESSAGE, GST_TYPE_BUFFER, payload,
      NULL);
}

/**
 * gst_message_new_eme_license_renewal:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @payload: (nullable) (transfer none): A #GstBuffer containing data generated
 * by the CDM which must be sent to the license authority to renew a license
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * a #GstMessage that the session will send to the application for the purpose
 * of requesting license renewal from a license authority.
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_license_renewal (GstObject * src, GstBuffer * payload)
{
  GstStructure *structure = gst_structure_new (SESSION_MESSAGE_LICENSE_RENEWAL,
      FIELD_MESSAGE, GST_TYPE_BUFFER, payload, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_license_renewal:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to the
 * application
 * @payload: (out) (transfer full): The license renewal message generated by the
 * CDM or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to the
 * application when the underlying CDM needs to renew a license to decrypt
 * protected media
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_license_renewal (GstMessage * message,
    GstBuffer ** payload)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (gst_eme_media_key_message_get_type (message) !=
      GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_RENEWAL) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_MESSAGE, GST_TYPE_BUFFER, payload,
      NULL);
}

/**
 * gst_message_new_eme_license_release:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @payload: (nullable) (transfer none): A #GstBuffer containing data generated
 * by the CDM indicating that a persistent license has been destroyed
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * a #GstMessage that the session will send to the application so it can persist
 * this record.
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_license_release (GstObject * src, GstBuffer * payload)
{
  GstStructure *structure = gst_structure_new (SESSION_MESSAGE_LICENSE_RELEASE,
      FIELD_MESSAGE, GST_TYPE_BUFFER, payload, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_license_release:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to the
 * application
 * @payload: (out) (transfer full): The record of license destruction generated
 * by the CDM or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to the
 * application when the underlying CDM has destroyed a persistent license
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_license_release (GstMessage * message,
    GstBuffer ** payload)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (gst_eme_media_key_message_get_type (message) !=
      GST_EME_MEDIA_KEY_MESSAGE_TYPE_LICENSE_RELEASE) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_MESSAGE, GST_TYPE_BUFFER, payload,
      NULL);
}

/**
 * gst_message_new_eme_individualization_request:
 * @src: (nullable) (transfer none): The sender of the message,
 * typically the #GstMediaKeySession generating this message or `NULL`
 * @payload: (nullable) (transfer none): A #GstBuffer containing an
 * individualization request generated by the CDM
 *
 * For implementors of the GST EME API, this function simplifies the creation of
 * a #GstMessage that the session will send to the application when the
 * underlying CDM generates an individualization request
 *
 * Returns: (transfer full): A new #GstMessage containing the supplied
 * parameters
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_individualization_request (GstObject * src,
    GstBuffer * payload)
{
  GstStructure *structure =
      gst_structure_new (SESSION_MESSAGE_INDIVIDUALIZATION_REQUEST,
      FIELD_MESSAGE, GST_TYPE_BUFFER, payload, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_individualization_request:
 * @message: (transfer none): A #GstMessage sent by a #GstMediaKeySession to the
 * application
 * @payload: (out) (transfer full): The individualization request generated by
 * the CDM or `NULL` on failure
 *
 * Attempts to parse a #GstMessage sent by a #GstMediaKeySession to the
 * application when the underlying CDM has generated an individualization
 * request
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_individualization_request (GstMessage * message,
    GstBuffer ** payload)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (gst_eme_media_key_message_get_type (message) !=
      GST_EME_MEDIA_KEY_MESSAGE_TYPE_INDIVIDUALIZATION_REQUEST) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_MESSAGE, GST_TYPE_BUFFER, payload,
      NULL);
}

/**
 * gst_message_new_eme_encrypted:
 * @src: (transfer none) (nullable): The element that encountered encrypted
 * media or `NULL`
 * @init_data_type: (transfer none): The kind of initialization data encountered
 * by the decryptor element
 * @init_data: (transfer full): A #GstBuffer containing the initialization data
 *
 * Creates a new #GstMessage containing a structure suitable for the
 * "eme-encrypted" event sent by a decryptor element.
 *
 * Returns: (transfer full): A new #GstMessage describing the intiailization
 * data encountered
 *
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_encrypted (GstElement * src, const gchar * init_data_type,
    GstBuffer * init_data)
{
  return gst_message_new_eme_encrypted_full (src, init_data_type, init_data,
      NULL, NULL);
}

/**
 * gst_message_parse_eme_encrypted:
 * @message: (transfer none): The message to parse
 * @init_data_type: (out) (optional) (transfer full): The type of initialization
 * data
 * @init_data: (out) (optional) (transfer full): The initialization data
 *
 * Attempts to parse @message sent by a decryptor element, extracting
 * @init_data_type and @init_data.
 *
 * The application should create a new #GstMediaKeySession and then call
 * #gst_media_key_session_generate_request() on it with these parameters to
 * begin a typical EME setup workflow.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_encrypted (GstMessage * message, gchar ** init_data_type,
    GstBuffer ** init_data)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);

  if (!gst_message_has_name (message, DECRYPTOR_MESSAGE_ENCRYPTED)) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  if (init_data_type != NULL && !gst_structure_get (structure,
          FIELD_INIT_DATA_TYPE, G_TYPE_STRING, init_data_type, NULL)) {
    return FALSE;
  }
  if (init_data != NULL && !gst_structure_get (structure, FIELD_INIT_DATA,
          GST_TYPE_BUFFER, init_data, NULL)) {
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_message_new_eme_encrypted_full:
 * @src: (transfer none) (nullable): The element that encountered encrypted
 * media or `NULL`
 * @init_data_type: (transfer none): The kind of initialization data encountered
 * by the decryptor element
 * @init_data: (transfer full): A #GstBuffer containing the initialization data
 * which may have been pre-processed by the decryptor element.
 * @init_data_origin: (transfer none) (nullable): Identifies the initialization
 * data was extracted. It is common for either the container such as MP4 or WebM
 * to produce this information as well as a DASH manifest.
 * @raw_init_data: (transfer full) (nullable): The initialization data as seen
 * by the element that discovered it, without any pre-processing applied.
 *
 * Creates a new #GstMessage containing a structure suitable for the
 * "eme-encrypted" event sent by a decryptor element.
 *
 * Returns: (transfer full): A new #GstMessage describing the intiailization
 * data encountered
 *
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_encrypted_full (GstElement * src,
    const gchar * init_data_type, GstBuffer * init_data,
    const gchar * init_data_origin, GstBuffer * raw_init_data)
{
  g_return_val_if_fail (init_data_type != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (init_data), NULL);
  GstStructure *structure = gst_structure_new (DECRYPTOR_MESSAGE_ENCRYPTED,
      FIELD_INIT_DATA_TYPE, G_TYPE_STRING, init_data_type,
      FIELD_INIT_DATA, GST_TYPE_BUFFER, init_data,
      FIELD_RAW_INIT_DATA, GST_TYPE_BUFFER, raw_init_data,
      FIELD_INIT_DATA_ORIGIN, G_TYPE_STRING, init_data_origin, NULL);
  return gst_message_new_element (GST_OBJECT_CAST (src), structure);
}

/**
 * gst_message_new_eme_waiting_for_key:
 * @src: (transfer none) (nullable): The decryptor element that needs a key or
 * `NULL`
 * @key_id: (transfer none): The key that is needed to decrypt upcoming media
 * @promise: (transfer none): A #GstPromise that must be answered by the
 * application when the key should be available to the decryptor
 *
 * Creates a new #GstMessage that a Decryptor element can send to the
 * application, indicating that it needs a specific key to decrypt the stream.
 * The application must answer the promise in the message.
 *
 * Returns: (transfer full): A new #GstMessage describing which key is needed
 *
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_waiting_for_key (GstObject * src, GstBuffer * key_id,
    GstPromise * promise)
{
  g_return_val_if_fail (GST_IS_BUFFER (key_id), NULL);
  g_return_val_if_fail (promise != NULL, NULL);
  GstStructure *structure =
      gst_structure_new (DECRYPTOR_MESSAGE_WAITING_FOR_KEY,
      FIELD_KEY_ID, GST_TYPE_BUFFER, key_id,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_waiting_for_key:
 * @message: (transfer none): The message to parse
 * @key_id: (out) (transfer full): The key that is needed to decrypt upcoming media
 * @promise: (out) (transfer full): A #GstPromise that must be answered by the
 * application when the key is available to the decryptor
 *
 * Attempts to parse @message, extracting @key_id and @promise.
 * When @key_id is available to the application, it should answer @promise with
 * a successful response. If @key_id will never be available, it should expire
 * the promise.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_waiting_for_key (GstMessage * message,
    GstBuffer ** key_id, GstPromise ** promise)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (key_id != NULL, FALSE);
  g_return_val_if_fail (promise != NULL, FALSE);

  if (!gst_message_has_name (message, DECRYPTOR_MESSAGE_WAITING_FOR_KEY)) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_KEY_ID, GST_TYPE_BUFFER, key_id,
      FIELD_PROMISE, GST_TYPE_PROMISE, promise, NULL);
}

/**
 * gst_message_new_eme_have_key:
 * @src: (transfer none) (nullable): The decryptor element that has a key or
 * `NULL`
 * @key_id: (transfer none): The key that is now available to the decryptor
 *
 * Creates a new #GstMessage that a Decryptor element can send to the
 * application, indicating that it now has access to @key_id. The application is
 * not required to handle this message.
 *
 * Returns: (transfer full): A new #GstMessage describing which key is available
 * Since: 1.24
 */
GstMessage *
gst_message_new_eme_have_key (GstObject * src, GstBuffer * key_id)
{
  g_return_val_if_fail (GST_IS_BUFFER (key_id), NULL);
  GstStructure *structure = gst_structure_new (DECRYPTOR_MESSAGE_HAVE_KEY,
      FIELD_KEY_ID, GST_TYPE_BUFFER, key_id, NULL);
  return gst_message_new_element (src, structure);
}

/**
 * gst_message_parse_eme_have_key:
 * @message: (transfer none): The message to parse
 * @key_id: (out) (transfer full): The key that is needed to decrypt upcoming media
 *
 * Attempts to parse @message, extracting @key_id. No action is required from
 * the application when it receives this message.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_message_parse_eme_have_key (GstMessage * message, GstBuffer ** key_id)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  g_return_val_if_fail (key_id != NULL, FALSE);

  if (!gst_message_has_name (message, DECRYPTOR_MESSAGE_HAVE_KEY)) {
    return FALSE;
  }

  const GstStructure *structure = gst_message_get_structure (message);
  return gst_structure_get (structure, FIELD_KEY_ID, GST_TYPE_BUFFER, key_id,
      NULL);
}

typedef struct
{
  gchar *value;
  gchar *scheme_id_uri;
  gchar *default_kid;
  GPtrArray *license_acquisition_urls;
  GPtrArray *authorization_urls;
  GBytes *pssh;
} ContentProtectionBlock;

#define CONTENT_PROTECTION_BLOCK_INIT { 0 }
#define PARSER_INIT { 0 }

static gboolean content_protection_block_parser_parse (const gchar *, gsize,
    ContentProtectionBlock *, GError **);
static void content_protection_block_clear (ContentProtectionBlock *);

/**
 * gst_eme_parse_dash_protection_message:
 * @message: (transfer none): The #GstMessage containing DASH initialization data
 * @license_acquisition_urls: (transfer full) (out) (optional): The license
 * acquisition server URLs (`<dashif:laurl>` elements) contained in the block in
 * order of appearance.
 * @authorization_urls: (transfer full) (out) (optional): The authorization
 * server URLs (`<dashif:authzurl>` elements) contained in the block in order of
 * appearance.
 *
 * Attempts to extract the license acquisition and authorization server
 * URLs from a DASH manifest's `<ContentProtection>` block.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */

gboolean
gst_eme_parse_dash_protection_message (GstMessage * message,
    gchar *** license_acquisition_urls, gchar *** authorization_urls)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);
  const GstStructure *structure = gst_message_get_structure (message);
  GstBuffer *raw_init_data = NULL;
  if (!gst_structure_get (structure, FIELD_RAW_INIT_DATA, GST_TYPE_BUFFER,
          &raw_init_data, NULL)) {
    return FALSE;
  }
  if (!gst_eme_parse_dash_content_protection_block (raw_init_data, NULL,
          license_acquisition_urls, authorization_urls, NULL)) {
    gst_clear_buffer (&raw_init_data);
    return FALSE;
  }
  gst_clear_buffer (&raw_init_data);
  return TRUE;
}

/**
 * gst_eme_parse_dash_content_protection_block:
 * @buffer: (transfer none): A #GstBuffer containing a DASH
 * `<ContentProtection>` block
 * @scheme_uuid: (transfer full) (out) (optional) (nullable): The UUID of the
 * protection scheme found in the block
 * @license_acquisition_urls: (transfer full) (out) (optional): The license
 * acquisition server URLs (`<dashif:laurl>` elements) contained in the block in
 * order of appearance.
 * @authorization_urls: (transfer full) (out) (optional): The authorization
 * server URLs (`<dashif:authzurl>` elements) contained in the block in order of
 * appearance.
 * @init_data: (transfer full) (out) (optional) (nullable): The initialization
 * data found in the block's `<cenc:pssh>` element.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_eme_parse_dash_content_protection_block (GstBuffer * buffer,
    gchar ** scheme_uuid, gchar *** license_acquisition_urls,
    gchar *** authorization_urls, GstBuffer ** init_data)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  GError *error = NULL;
  ContentProtectionBlock block = CONTENT_PROTECTION_BLOCK_INIT;
  GstMapInfo info = GST_MAP_INFO_INIT;
  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    goto error;
  }
  content_protection_block_parser_parse ((gchar *) info.data, info.size, &block,
      &error);
  gst_buffer_unmap (buffer, &info);
  if (error) {
    g_clear_error (&error);
    goto error;
  }

  if (block.pssh && init_data) {
    *init_data = gst_buffer_new_wrapped_bytes (g_bytes_ref (block.pssh));
  }

  if (license_acquisition_urls) {
    *license_acquisition_urls =
        (gchar **) g_ptr_array_free (block.license_acquisition_urls, FALSE);
    block.license_acquisition_urls = NULL;
  }

  if (authorization_urls) {
    *authorization_urls =
        (gchar **) g_ptr_array_free (block.authorization_urls, FALSE);
    block.authorization_urls = NULL;
  }

  content_protection_block_clear (&block);
  return TRUE;
error:
  content_protection_block_clear (&block);
  return FALSE;
}

#define TAG_CONTENT_PROTECTION "ContentProtection"
#define TAG_CENC_PSSH "cenc:pssh"
#define TAG_DASHIF_LAURL "dashif:laurl"
#define TAG_DASHIF_AUTHZURL "dashif:authzurl"

#define ATTR_VALUE "value"
#define ATTR_SCHEME_ID_URI "schemeIdUri"
#define ATTR_CENC_DEFAULT_KID "cenc:default_KID"

typedef enum
{
  NEW = 0,
  INSIDE_CONTENT_PROTECTION,
  INSIDE_PSSH,
  INSIDE_DASHIF_LAURL,
  INSIDE_DASHIF_AUTHZURL,
  DONE,
  FAILED,
} ContentProtectionParserState;

typedef struct
{
  ContentProtectionParserState state;
  ContentProtectionBlock block;
  GMarkupParseContext *ctx;
} ContentProtectionBlockParser;

static void on_start_content_protection (GMarkupParseContext *, const gchar *,
    const gchar **, const gchar **, gpointer, GError **);
static void on_end_content_protection (GMarkupParseContext *, const gchar *,
    gpointer, GError **);
static void on_start_child (GMarkupParseContext *, const gchar *,
    const gchar **, const gchar **, gpointer, GError **);
static void on_text_child (GMarkupParseContext *, const gchar *, gsize,
    gpointer, GError **);

static const GMarkupParser content_protection_parser = {
  .start_element = on_start_content_protection,
  .end_element = on_end_content_protection,
};

static const GMarkupParser content_protection_child_parser = {
  .start_element = on_start_child,
  .text = on_text_child,
};

static gboolean
is_content_protection (const gchar * element_name)
{
  return g_strcmp0 (TAG_CONTENT_PROTECTION, element_name) == 0;
}

static gboolean
is_cenc_pssh (const gchar * element_name)
{
  return g_strcmp0 (TAG_CENC_PSSH, element_name) == 0;
}

static gboolean
is_dashif_laurl (const gchar * element_name)
{
  return g_ascii_strcasecmp (TAG_DASHIF_LAURL, element_name) == 0;
}

static gboolean
is_dashif_authzurl (const gchar * element_name)
{
  return g_ascii_strcasecmp (TAG_DASHIF_AUTHZURL, element_name) == 0;
}

static void
on_start_content_protection (G_GNUC_UNUSED GMarkupParseContext * ctx,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error)
{
  ContentProtectionBlockParser *self = user_data;
  g_return_if_fail (self->state == NEW);
  if (!is_content_protection (element_name)) {
    return;
  }
  const gchar *value = NULL;
  const gchar *scheme_id_uri = NULL;
  const gchar *default_kid = NULL;
  gboolean ok = g_markup_collect_attributes (element_name,
      attribute_names,
      attribute_values,
      error,
      G_MARKUP_COLLECT_STRING | G_MARKUP_COLLECT_OPTIONAL,
      ATTR_VALUE,
      &value,
      G_MARKUP_COLLECT_STRING,
      ATTR_SCHEME_ID_URI,
      &scheme_id_uri,
      G_MARKUP_COLLECT_STRING | G_MARKUP_COLLECT_OPTIONAL,
      ATTR_CENC_DEFAULT_KID,
      &default_kid,
      G_MARKUP_COLLECT_INVALID);

  if (ok) {
    self->state = INSIDE_CONTENT_PROTECTION;
    self->block.scheme_id_uri = g_strdup (scheme_id_uri);
    if (value) {
      self->block.value = g_strdup (value);
    }
    if (default_kid) {
      self->block.default_kid = g_strdup (default_kid);
    }
    g_markup_parse_context_push (ctx, &content_protection_child_parser, self);
  } else {
    self->state = FAILED;
  }
}

static void
on_end_content_protection (GMarkupParseContext * ctx,
    const gchar * element_name, gpointer user_data,
    G_GNUC_UNUSED GError ** error)
{
  ContentProtectionBlockParser *self =
      (ContentProtectionBlockParser *) user_data;

  if (self->state == INSIDE_CONTENT_PROTECTION
      && is_content_protection (element_name)) {
    self->state = DONE;
    g_markup_parse_context_pop (ctx);
  } else {
    GST_DEBUG ("done with element %s", element_name);
  }
}

static void
on_start_child (G_GNUC_UNUSED GMarkupParseContext * ctx, const gchar *
    element_name, G_GNUC_UNUSED const gchar ** attribute_names,
    G_GNUC_UNUSED const gchar ** attribute_values, gpointer user_data,
    G_GNUC_UNUSED GError ** error)
{
  ContentProtectionBlockParser *self =
      (ContentProtectionBlockParser *) user_data;
  if (self->state == INSIDE_CONTENT_PROTECTION) {
    const GSList *parent =
        g_slist_next (g_markup_parse_context_get_element_stack (ctx));
    g_return_if_fail (parent);
    gchar *const parent_element = parent->data;
    if (is_content_protection (parent_element)) {
      if (is_cenc_pssh (element_name)) {
        self->state = INSIDE_PSSH;
      } else if (is_dashif_laurl (element_name)) {
        self->state = INSIDE_DASHIF_LAURL;
      } else if (is_dashif_authzurl (element_name)) {
        self->state = INSIDE_DASHIF_AUTHZURL;
      }
    }
  }
}

static void
on_text_child (G_GNUC_UNUSED GMarkupParseContext * ctx, const gchar * text,
    gsize text_len, gpointer user_data, G_GNUC_UNUSED GError ** error)
{
  ContentProtectionBlockParser *self = user_data;
  switch (self->state) {
    case INSIDE_PSSH:{
      gint state = 0;
      guint save = 0;
      guchar *pssh_scratch = g_malloc0 (text_len * 3 / 4);
      gsize pssh_len =
          g_base64_decode_step (text, text_len, pssh_scratch, &state, &save);
      self->block.pssh = g_bytes_new (pssh_scratch, pssh_len);
      self->state = INSIDE_CONTENT_PROTECTION;
      g_free (pssh_scratch);
      return;
    }
    case INSIDE_DASHIF_LAURL:{
      gchar *url = g_strndup (text, text_len);
      self->state = INSIDE_CONTENT_PROTECTION;
      GST_DEBUG ("adding laurl `%s'", url);
      g_ptr_array_add (self->block.license_acquisition_urls, url);
      return;
    }
    case INSIDE_DASHIF_AUTHZURL:{
      gchar *url = g_strndup (text, text_len);
      self->state = INSIDE_CONTENT_PROTECTION;
      GST_DEBUG ("adding authzurl `%s'", url);
      g_ptr_array_add (self->block.authorization_urls, url);
      return;
    }
    default:
      return;
  }
}

static void
content_protection_block_init (ContentProtectionBlock * self)
{
  self->pssh = NULL;
  self->scheme_id_uri = NULL;
  self->default_kid = NULL;
  self->license_acquisition_urls = g_ptr_array_new ();
  self->authorization_urls = g_ptr_array_new ();
}

static void
content_protection_block_parser_init (ContentProtectionBlockParser * self)
{
  self->ctx =
      g_markup_parse_context_new (&content_protection_parser, 0, self, NULL);
  self->state = NEW;
  content_protection_block_init (&self->block);
}

static void
content_protection_block_clear (ContentProtectionBlock * self)
{
  g_clear_pointer (&self->default_kid, g_free);
  g_clear_pointer (&self->license_acquisition_urls, g_ptr_array_unref);
  g_clear_pointer (&self->authorization_urls, g_ptr_array_unref);
  g_clear_pointer (&self->pssh, g_bytes_unref);
  g_clear_pointer (&self->pssh, g_free);
  g_clear_pointer (&self->scheme_id_uri, g_free);
  g_clear_pointer (&self->value, g_free);
}

static void
content_protection_block_parser_clear (ContentProtectionBlockParser * self)
{
  g_clear_pointer (&self->ctx, g_markup_parse_context_free);
  content_protection_block_clear (&self->block);
}

static void
steal_block (ContentProtectionBlock * source, ContentProtectionBlock * dest)
{
  g_return_if_fail (source);
  g_return_if_fail (dest);
  dest->pssh = g_steal_pointer (&source->pssh);
  dest->scheme_id_uri = g_steal_pointer (&source->scheme_id_uri);
  dest->default_kid = g_steal_pointer (&source->default_kid);
  dest->license_acquisition_urls =
      g_steal_pointer (&source->license_acquisition_urls);
  dest->authorization_urls = g_steal_pointer (&source->authorization_urls);
}

static gboolean
content_protection_block_parser_parse (const gchar * data, gsize data_length,
    ContentProtectionBlock * block, GError ** error)
{
  ContentProtectionBlockParser self = PARSER_INIT;
  content_protection_block_parser_init (&self);
  if (!g_markup_parse_context_parse (self.ctx, data, data_length, error)) {
    goto error;
  }

  if (!g_markup_parse_context_end_parse (self.ctx, error)) {
    goto error;
  }

  if (self.state == DONE) {
    if (block) {
      steal_block (&self.block, block);
    }
    goto ok;
  } else {
    g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
        "Invalid ContentProtection block");
    goto error;
  }

ok:
  content_protection_block_parser_clear (&self);
  return TRUE;
error:
  content_protection_block_parser_clear (&self);
  return FALSE;
}

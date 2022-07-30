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

#pragma once

#include "eme-prelude.h"

G_BEGIN_DECLS

#define GST_TYPE_MEDIA_KEY_SESSION (gst_media_key_session_get_type ())

GST_EME_API
G_DECLARE_INTERFACE (GstMediaKeySession, gst_media_key_session,
    GST, MEDIA_KEY_SESSION, GObject);

/**
 * GstMediaKeyStatus:
 * @GST_MEDIA_KEY_STATUS_USABLE: A key with this status can be used to
 * decrypt media
 * @GST_MEDIA_KEY_STATUS_EXPIRED: A key with this status has passed its
 * expiration time and is no longer usable
 * @GST_MEDIA_KEY_STATUS_RELEASED: A key with this status is known to the CDM
 * but it's not usable to decrypt media
 * @GST_MEDIA_KEY_STATUS_OUTPUT_RESTRICTED: This key can't be used to output
 * media with the current system configuration
 * @GST_MEDIA_KEY_STATUS_OUTPUT_DOWNSCALED: This key can only be used to
 * decrypt a reduced quality version of the media
 * @GST_MEDIA_KEY_STATUS_STATUS_PENDING: The status of this key is not known
 * @GST_MEDIA_KEY_STATUS_INTERNAL_ERROR: The CDM encountered an error with
 * this key
 *
 * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeystatus)
 *
 * Since: 1.24
 */
typedef enum
{
  GST_MEDIA_KEY_STATUS_USABLE,
  GST_MEDIA_KEY_STATUS_EXPIRED,
  GST_MEDIA_KEY_STATUS_RELEASED,
  GST_MEDIA_KEY_STATUS_OUTPUT_RESTRICTED,
  GST_MEDIA_KEY_STATUS_OUTPUT_DOWNSCALED,
  GST_MEDIA_KEY_STATUS_STATUS_PENDING,
  GST_MEDIA_KEY_STATUS_INTERNAL_ERROR,
} GstMediaKeyStatus;

/**
 * GstMediaKeySession:
 *
 * Since: 1.24
 */

/**
 * GstMediaKeySessionInterface:
 *
 * Since: 1.24
 */
struct _GstMediaKeySessionInterface
{
  GTypeInterface g_iface;

  /**
   * GstMediaKeySessionInterface.get_session_id:
   * @self: #GstMediaKeySession instance
   *
   * Implementation of #gst_media_key_session_get_session_id()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-sessionid)
   *
   * Returns: (transfer none) (nullable): a text string containing the current
   * session ID or `NULL` if it hasn't been set yet
   * Since: 1.24
   */
  const gchar * (*get_session_id)           (GstMediaKeySession * self);

  /**
   * GstMediaKeySessionInterface.get_expiration:
   * @self: #GstMediaKeySession instance
   *
   * Implementation of #gst_media_key_session_get_expiration()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-expiration)
   *
   * Returns: a #GstClockTime containing the expiration time of this session
   * or %GST_CLOCK_TIME_NONE if there's no expiration time
   * Since: 1.24
   */
  GstClockTime (*get_expiration)            (GstMediaKeySession * self);

  /**
   * GstMediaKeySessionInterface.get_closed:
   * @self: #GstMediaKeySession instance
   * @promise: (transfer none): #GstPromise which will be answered with the closed
   * state of this session
   *
   * Implementation of #gst_media_key_session_get_closed()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-closed)
   *
   * Since: 1.24
   */
  void (*get_closed) (GstMediaKeySession * self, GstPromise * promise);

  /**
   * GstMediaKeySessionInterface.has_media_key_status:
   * @self: #GstMediaKeySession instance
   * @key_id: (transfer none): #GstBuffer raw data representing the Key ID which
   * is being queried
   *
   * Implementation of #gst_media_key_session_has_media_key_status()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeystatusmap-has)
   *
   * Returns: `TRUE` if the current session has any record of @key_id, otherwise
   * `FALSE`
   * Since: 1.24
   */
  gboolean (*has_media_key_status)          (GstMediaKeySession * self,
                                             GstBuffer          * key_id);

  /**
   * GstMediaKeySessionInterface.get_media_key_status:
   * @self: #GstMediaKeySession instance
   * @key_id: (transfer none): #GstBuffer raw data representing the Key ID which
   * is being queried
   *
   * Implementation of #gst_media_key_session_get_media_key_status()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeystatusmap-get)
   *
   * Returns: The status of @key_id within this session or
   * %GST_MEDIA_KEY_STATUS_EXPIRED if there is no record of @key_id
   * Since: 1.24
   */
  GstMediaKeyStatus (*get_media_key_status) (GstMediaKeySession * self,
                                             GstBuffer          * key_id);

  /**
   * GstMediaKeySessionInterface.get_media_key_status_count:
   * @self: #GstMediaKeySession instance
   *
   * Implementation of #gst_media_key_session_get_media_key_status_count()
   *
   * Returns: The number of keys that this session contains
   * Since: 1.24
   */
  gsize (*get_media_key_status_count)       (GstMediaKeySession * self);

  /**
   * GstMediaKeySessionInterface.generate_request:
   * @self: #GstMediaKeySession instance
   * @init_data_type: (transfer none): a string identifying the format of
   * @init_data. Typical values include `cenc`, `keyids`, and `webm`.
   * @init_data: (transfer none): #GstBuffer containing the raw initialization
   * data to be used by the underlying CDM to create a license request
   * @promise: (transfer none): #GstPromise which will be answered with a
   * license request on success or an error on failure
   *
   * Implementation of #gst_media_key_session_generate_request()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-generaterequest)
   *
   * Since: 1.24
   */
  void (*generate_request)                  (GstMediaKeySession * self,
                                             const gchar        * init_data_type,
                                             GstBuffer          * init_data,
                                             GstPromise         * promise);

  /**
   * GstMediaKeySessionInterface.load:
   * @self: #GstMediaKeySession instance
   * @session_id: string representing the Session ID
   * @promise: (transfer none): #GstPromise which will be answered when the
   * operation either succeeds or fails
   *
   * Implementation of #gst_media_key_session_load()
   *
   *
   * Since: 1.24
   */
  void (*load)                              (GstMediaKeySession * self,
                                             const gchar        * session_id,
                                             GstPromise         * promise);

  /**
   * GstMediaKeySessionInterface.update:
   * @self: #GstMediaKeySession instance
   * @response: (transfer none): #GstBuffer containing raw data of a license
   * authority's response to a license request
   * @promise: (transfer none): #GstPromise which will be answered when the
   * operation either succeeds or fails
   *
   * Implementation of #gst_media_key_session_update()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-update)
   *
   * Since: 1.24
   */
  void (*update)                            (GstMediaKeySession * self,
                                             GstBuffer          * response,
                                             GstPromise         * promise);

  /**
   * GstMediaKeySessionInterface.close:
   * @self: #GstMediaKeySession instance
   * @promise: (transfer none): #GstPromise which will be answered when the
   * operation either succeeds or fails
   *
   * Implementation of #gst_media_key_session_close()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-close)
   *
   * Since: 1.24
   */
  void (*close)                             (GstMediaKeySession * self,
                                             GstPromise         * promise);

  /**
   * GstMediaKeySessionInterface.remove:
   * @self: #GstMediaKeySession instance
   * @promise: (transfer none): #GstPromise which will be answered when the
   * operation either succeeds or fails
   *
   * Implementation of #gst_media_key_session_remove()
   *
   * [Specification](https://www.w3.org/TR/encrypted-media/#dom-mediakeysession-remove)
   *
   * Since: 1.24
   */
  void (*remove)                            (GstMediaKeySession * self,
                                             GstPromise         * promise);

  gpointer _gst_reserved[GST_PADDING];
};

GST_EME_API
const gchar * gst_media_key_session_get_session_id (GstMediaKeySession * self);

GST_EME_API
GstClockTime gst_media_key_session_get_expiration (GstMediaKeySession * self);

GST_EME_API
void gst_media_key_session_get_closed (GstMediaKeySession * self,
    GstPromise * promise);

GST_EME_API
gboolean gst_media_key_session_has_media_key_status (GstMediaKeySession * self,
    GstBuffer * key_id);

GST_EME_API
GstMediaKeyStatus gst_media_key_session_get_media_key_status (
    GstMediaKeySession * self, GstBuffer * key_id);

GST_EME_API
gsize gst_media_key_session_get_media_key_status_count (
    GstMediaKeySession * self);

GST_EME_API
void gst_media_key_session_generate_request (GstMediaKeySession * self,
    const gchar * init_data_type, GstBuffer * init_data, GstPromise * promise);

GST_EME_API
void gst_media_key_session_load (GstMediaKeySession * self,
    const gchar * session_id, GstPromise *promise);

GST_EME_API
void gst_media_key_session_update (GstMediaKeySession * self,
    GstBuffer * response, GstPromise * promise);

GST_EME_API
void gst_media_key_session_close (GstMediaKeySession * self,
    GstPromise * promise);

GST_EME_API
void gst_media_key_session_remove (GstMediaKeySession * self,
    GstPromise * promise);

GST_EME_API
void gst_media_key_session_publish_key_statuses_change (
    GstMediaKeySession * self);

GST_EME_API
void gst_media_key_session_publish_message (GstMediaKeySession * self,
    GstMessage * message);

G_END_DECLS

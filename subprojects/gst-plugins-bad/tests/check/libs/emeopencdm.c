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

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/check/gstcheck.h>

#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>

#define DEFAULT_TCASE_TIMEOUT 15

#ifdef HAVE_VALGRIND
#define TCASE_TIMEOUT (RUNNING_ON_VALGRIND ? (5 * 60) : DEFAULT_TCASE_TIMEOUT)
#else
#define TCASE_TIMEOUT DEFAULT_TCASE_TIMEOUT
#endif

static GstStaticCaps basic_config =
GST_STATIC_CAPS ("MediaKeySystemConfiguration, label = (string) \"\", "
    "initDataTypes = (string) { cenc, }, "
    "audioCapabilities = (GstCaps) [ANY], "
    "videoCapabilities = (GstCaps) [ANY], "
    "distinctiveIdentifier = (string) optional ,"
    "persistentState = (string) optional");

#define SAMPLE_KEY_ID "nrQFDeRLSAKTLifXUIPiZg"
#define SAMPLE_KEY    "FmY0xnWCPCNaSpRG-tUuTQ"
#define VALID_INIT_DATA "{\"kids\": [\"" SAMPLE_KEY_ID "\"]}"
#define VALID_INIT_DATA_LEN (G_N_ELEMENTS (VALID_INIT_DATA) - 1)
#define VALID_REPLY "{\"keys\":[{\"kty\":\"oct\",\"k\":\"" SAMPLE_KEY \
  "\",\"kid\":\"" SAMPLE_KEY_ID "\"}],\"type\":\"temporary\"}"
#define VALID_REPLY_LEN (G_N_ELEMENTS (VALID_REPLY) - 1)

static GstBuffer *
new_static_buffer (const gchar * data, gsize length)
{
  return gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      length, 0, length, NULL, NULL);
}

static GstElement *
create_protection_system (void)
{
  return gst_element_factory_make ("emeopencdmprotectionsystem", NULL);
}

static GstMediaKeySystemAccess *
get_media_key_system_access (void)
{
  GstElement *system = create_protection_system ();
  GstCaps *config = gst_static_caps_get (&basic_config);
  GstPromise *promise = gst_promise_new ();
  g_signal_emit_by_name (system, "request-media-key-system-access",
      "org.w3.clearkey", config, promise);
  gst_promise_wait (promise);
  GstMediaKeySystemAccess *access = gst_eme_resolve_system_access (promise);
  gst_promise_unref (promise);
  gst_clear_object (&system);
  gst_clear_caps (&config);
  return access;
}

static GstMediaKeys *
get_media_keys (void)
{
  GstMediaKeySystemAccess *access = get_media_key_system_access ();
  GstPromise *promise = gst_promise_new ();
  gst_media_key_system_access_create_media_keys (access, promise);
  gst_promise_wait (promise);
  GstMediaKeys *keys = gst_eme_resolve_media_keys (promise);
  gst_promise_unref (promise);
  gst_clear_object (&access);
  return keys;
}

START_TEST (protection_system_create_and_free)
{
  GstElement *system = create_protection_system ();
  gst_check_object_destroyed_on_unref (system);
}

END_TEST;

START_TEST (protection_system_request_access)
{
  GstElement *system = create_protection_system ();
  GstPromise *promise = gst_promise_new ();
  GstCaps *config = gst_static_caps_get (&basic_config);
  g_signal_emit_by_name (system, "request-media-key-system-access",
      "org.w3.clearkey", config, promise);
  gst_promise_wait (promise);
  GstMediaKeySystemAccess *access = gst_eme_resolve_system_access (promise);
  fail_unless (GST_IS_MEDIA_KEY_SYSTEM_ACCESS (access));
  gst_promise_unref (promise);
  gst_clear_object (&system);
  gst_check_object_destroyed_on_unref (access);
}

END_TEST;

START_TEST (key_system_access_get_key_system)
{
  GstMediaKeySystemAccess *access = get_media_key_system_access ();
  assert_equals_string (gst_media_key_system_access_get_key_system (access),
      "org.w3.clearkey");
  gst_check_object_destroyed_on_unref (access);
}

END_TEST;

START_TEST (key_system_access_get_configuration)
{
  GstMediaKeySystemAccess *access = get_media_key_system_access ();
  GstCaps *config = gst_media_key_system_access_get_configuration (access);
  fail_unless (GST_IS_CAPS (config));
  gst_clear_caps (&config);
  gst_clear_object (&access);
}

END_TEST;

START_TEST (key_system_access_create_media_keys)
{
  GstMediaKeys *keys = get_media_keys ();
  fail_unless (GST_IS_MEDIA_KEYS (keys));
  gst_check_object_destroyed_on_unref (keys);
}

END_TEST;

START_TEST (media_keys_create_session)
{
  GstMediaKeys *keys = get_media_keys ();
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, NULL);

  fail_unless (GST_IS_MEDIA_KEY_SESSION (session));
  gst_object_unref (session);
  gst_object_unref (keys);
}

END_TEST;

START_TEST (media_keys_set_server_certificate)
{
  GstMediaKeys *keys = get_media_keys ();
  GstPromise *rejected = gst_promise_new ();
  GstBuffer *buffer = gst_buffer_new ();

  gst_media_keys_set_server_certificate (keys, buffer, rejected);
  gst_promise_wait (rejected);
  fail_unless (gst_promise_get_reply (rejected) == NULL);

  gst_clear_object (&keys);
  g_clear_pointer (&rejected, gst_promise_unref);
  gst_clear_buffer (&buffer);
}

END_TEST;

static void
await_generate_request (GstMediaKeySession * session)
{
  GstPromise *promise = gst_promise_new ();
  GstBuffer *init_data =
      new_static_buffer (VALID_INIT_DATA, VALID_INIT_DATA_LEN);
  gst_media_key_session_generate_request (session, "keyids", init_data,
      promise);
  gst_promise_wait (promise);
  gst_clear_buffer (&init_data);
  g_clear_pointer (&promise, gst_promise_unref);
}

START_TEST (session_generate_request)
{
  GstMediaKeys *keys = get_media_keys ();
  GError *error = NULL;
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, &error);
  g_assert_no_error (error);
  GstPromise *promise = gst_promise_new ();

  GstBuffer *init_data = new_static_buffer (VALID_INIT_DATA,
      VALID_INIT_DATA_LEN);

  gst_media_key_session_generate_request (session, "keyids", init_data,
      promise);
  gst_promise_wait (promise);
  const GstStructure *request = gst_promise_get_reply (promise);
  const gchar *message_type =
      gst_structure_get_string (request, "message-type");

  fail_unless_equals_string ("gst-eme-license-request", message_type);
  fail_unless (gst_structure_has_field_typed (request, "message",
          GST_TYPE_BUFFER));

  g_clear_pointer (&promise, gst_promise_unref);
  gst_clear_buffer (&init_data);
  gst_clear_object (&session);
  gst_clear_object (&keys);
}

END_TEST;

START_TEST (session_load)
{
  GstMediaKeys *keys = get_media_keys ();
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_PERSISTENT_LICENSE, NULL);
  GstPromise *promise = gst_promise_new ();

  gst_media_key_session_load (session, "1", promise);
  gst_promise_wait (promise);

  const GstStructure *reply = gst_promise_get_reply (promise);
  fail_unless_equals_string ("ok", gst_structure_get_name (reply));

  g_clear_pointer (&promise, gst_promise_unref);
  gst_clear_object (&session);
  gst_clear_object (&keys);
}

END_TEST;

START_TEST (session_update)
{
  GstMediaKeys *keys = get_media_keys ();
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, NULL);

  await_generate_request (session);

  GstPromise *promise = gst_promise_new ();
  GstBuffer *response = new_static_buffer (VALID_REPLY, VALID_REPLY_LEN);
  gst_media_key_session_update (session, response, promise);
  gst_promise_wait (promise);
  const GstStructure *reply = gst_promise_get_reply (promise);

  fail_unless_equals_string ("ok", gst_structure_get_name (reply));

  gst_clear_buffer (&response);
  g_clear_pointer (&promise, gst_promise_unref);
  gst_clear_object (&session);
  gst_clear_object (&keys);
}

END_TEST;

START_TEST (session_close)
{
  GstMediaKeys *keys = get_media_keys ();
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, NULL);

  GstPromise *promise = gst_promise_new ();
  gst_media_key_session_close (session, promise);
  gst_promise_wait (promise);
  const GstStructure *reply = gst_promise_get_reply (promise);

  fail_unless_equals_string ("ok", gst_structure_get_name (reply));

  g_clear_pointer (&promise, gst_promise_unref);
  gst_clear_object (&session);
  gst_clear_object (&keys);
}

END_TEST;

START_TEST (session_remove)
{
  GstMediaKeys *keys = get_media_keys ();
  GstMediaKeySession *session = gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, NULL);

  await_generate_request (session);

  GstPromise *promise = gst_promise_new ();
  gst_media_key_session_remove (session, promise);
  gst_promise_wait (promise);
  const GstStructure *reply = gst_promise_get_reply (promise);

  fail_unless_equals_string ("ok", gst_structure_get_name (reply));

  g_clear_pointer (&promise, gst_promise_unref);
  gst_clear_object (&session);
  gst_clear_object (&keys);
}

END_TEST;

static inline TCase *
new_tcase (const gchar * name)
{
  TCase *tcase = tcase_create (name);
  tcase_set_timeout (tcase, TCASE_TIMEOUT);
  return tcase;
}

static Suite *
emeopencdm_suite (void)
{
  g_setenv ("WEBKIT_SPARKLE_CDM_MODULE_PATH", TEST_CDM_PATH, TRUE);

  Suite *s = suite_create ("GstEmeOpenCDM");

  TCase *tc_protection_system = new_tcase ("GstOpenCDMProtectionSystem");
  TCase *tc_system_access = new_tcase ("GstOpenCDMMediaKeySystemAccess");
  TCase *tc_media_keys = new_tcase ("GstOpenCDMMediaKeys");
  TCase *tc_session = new_tcase ("GstOpenCDMMediaKeySession");

  tcase_add_test (tc_protection_system, protection_system_create_and_free);
  tcase_add_test (tc_protection_system, protection_system_request_access);

  tcase_add_test (tc_system_access, key_system_access_get_key_system);
  tcase_add_test (tc_system_access, key_system_access_get_configuration);
  tcase_add_test (tc_system_access, key_system_access_create_media_keys);

  tcase_add_test (tc_media_keys, media_keys_create_session);
  tcase_add_test (tc_media_keys, media_keys_set_server_certificate);

  tcase_add_test (tc_session, session_generate_request);
  tcase_add_test (tc_session, session_load);
  tcase_add_test (tc_session, session_update);
  tcase_add_test (tc_session, session_remove);
  tcase_add_test (tc_session, session_close);

  suite_add_tcase (s, tc_protection_system);
  suite_add_tcase (s, tc_system_access);
  suite_add_tcase (s, tc_media_keys);
  suite_add_tcase (s, tc_session);

  return s;
}

GST_CHECK_MAIN (emeopencdm)

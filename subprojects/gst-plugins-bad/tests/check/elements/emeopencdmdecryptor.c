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

#include <gst/check/check.h>
#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>

#define ELEMENT_NAME "emeopencdmdecryptor"
#define PROTECTION_SYSTEM_ELEMENT_NAME "opencdmprotectionsystem"

static GstElement *
new_element (void)
{
  return gst_check_setup_element (ELEMENT_NAME);
}

GST_START_TEST (test_setup_teardown)
{
  GstElement *element = new_element ();
  gst_check_teardown_element (element);
}

GST_END_TEST;

GST_START_TEST (test_push_unencrypted_buffer_passthrough)
{
  GstHarness *h = gst_harness_new_parse (ELEMENT_NAME);
  gst_harness_set_src_caps_str (h, "application/x-cenc, "
      "original-media-type = video/x-h264, "
      "protection-system = 1077efec-c0b2-4d02-ace3-3c1e52e2fb4b ");
  GstBuffer *input = gst_harness_create_buffer (h, 1);
  gst_harness_push (h, input);
  GstBuffer *output = gst_harness_pull (h);
  fail_unless_equals_pointer (input, output);
  gst_clear_buffer (&output);
  gst_harness_teardown (h);
}

GST_END_TEST;

static GstObject *
create_protection_system (void)
{
  return
      GST_OBJECT_CAST (gst_element_factory_make ("emeopencdmprotectionsystem",
          NULL));
}

#define CLEARKEY_H264_CAPS_STR "application/x-cenc, " \
      "original-media-type = video/x-h264, " \
      "protection-system = 1077efec-c0b2-4d02-ace3-3c1e52e2fb4b"

static GstStaticCaps basic_config =
GST_STATIC_CAPS ("MediaKeySystemConfiguration, label = (string) \"\", "
    "initDataTypes = (string) { cenc, }, "
    "audioCapabilities = (GstCaps) [ANY], "
    "videoCapabilities = (GstCaps) [ANY], "
    "distinctiveIdentifier = (string) optional ,"
    "persistentState = (string) optional");

static GstMediaKeySystemAccess *
get_media_key_system_access (void)
{
  GstObject *system = create_protection_system ();
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

static GstMediaKeySession *
new_session (GstMediaKeys * keys)
{
  return gst_media_keys_create_session (keys,
      GST_MEDIA_KEY_SESSION_TYPE_TEMPORARY, NULL);
}

static GstBuffer *
new_cenc_encrypted_buffer (GstHarness * harness, gsize size)
{
  GstBuffer *buffer = gst_harness_create_buffer (harness, size);
  GstBuffer *kid = gst_harness_create_buffer (harness, 16);
  GstBuffer *iv = gst_harness_create_buffer (harness, 16);
  GstStructure *protection = gst_structure_new ("application/x-cenc", "iv_size",
      G_TYPE_UINT, 16, "encrypted", G_TYPE_BOOLEAN, TRUE, "kid",
      GST_TYPE_BUFFER, kid, "iv", GST_TYPE_BUFFER, iv, NULL);
  gst_buffer_add_protection_meta (buffer, protection);
  gst_clear_buffer (&kid);
  gst_clear_buffer (&iv);
  return buffer;
}

static void
setup_decryptor (GstHarness * h, GstMediaKeys * keys)
{
  GstElement *decryptor = gst_harness_find_element (h, ELEMENT_NAME);
  GstContext *context = gst_eme_context_new_media_keys (keys);
  gst_element_set_context (decryptor, context);
  g_clear_pointer (&context, gst_context_unref);
  gst_clear_object (&decryptor);
}

static void
setup_session (GstHarness * h, GstMediaKeySession * session)
{
  {
    GstPromise *promise = gst_promise_new ();
    GstBuffer *init_data = gst_harness_create_buffer (h, 1);
    gst_media_key_session_generate_request (session, "cenc", init_data,
        promise);
    gst_promise_wait (promise);
    g_clear_pointer (&promise, gst_promise_unref);
    gst_clear_buffer (&init_data);
  }

  {
    GstPromise *promise = gst_promise_new ();
    GstBuffer *response = gst_harness_create_buffer (h, 1);
    gst_media_key_session_update (session, response, promise);
    gst_clear_buffer (&response);
    gst_promise_wait (promise);
    g_clear_pointer (&promise, gst_promise_unref);
  }
}

GST_START_TEST (test_push_encrypted_buffer_decrypts)
{
  GstHarness *h = gst_harness_new_parse (ELEMENT_NAME);

  GstMediaKeys *keys = get_media_keys ();
  setup_decryptor (h, keys);
  GstMediaKeySession *session = new_session (keys);
  setup_session (h, session);

  gst_harness_set_src_caps_str (h, CLEARKEY_H264_CAPS_STR);
  GstBuffer *input = new_cenc_encrypted_buffer (h, 1);
  gst_harness_push (h, input);
  GstBuffer *output = gst_harness_pull (h);
  fail_unless_equals_pointer (input, output);
  g_assert_null (gst_buffer_get_protection_meta (output));

  gst_clear_buffer (&output);
  gst_clear_object (&session);
  gst_clear_object (&keys);
  gst_harness_teardown (h);
}

GST_END_TEST;

#define DEFAULT_TCASE_TIMEOUT 10

#ifdef HAVE_VALGRIND
#define TCASE_TIMEOUT (RUNNING_ON_VALGRIND ? (5 * 60) : DEFAULT_TCASE_TIMEOUT)
#else
#define TCASE_TIMEOUT DEFAULT_TCASE_TIMEOUT
#endif

static inline TCase *
new_tcase (const gchar * name)
{
  TCase *tcase = tcase_create (name);
  tcase_set_timeout (tcase, TCASE_TIMEOUT);
  return tcase;
}

static Suite *
opencdmdecryptor_suite (void)
{
  g_setenv ("WEBKIT_SPARKLE_CDM_MODULE_PATH", TEST_CDM_PATH, TRUE);

  Suite *s = suite_create ("GstOpenCdmDecryptor");

  TCase *tc_element = new_tcase ("Element");
  tcase_add_test (tc_element, test_setup_teardown);
  tcase_add_test (tc_element, test_push_unencrypted_buffer_passthrough);
  tcase_add_test (tc_element, test_push_encrypted_buffer_decrypts);

  suite_add_tcase (s, tc_element);

  return s;
}

GST_CHECK_MAIN (opencdmdecryptor)

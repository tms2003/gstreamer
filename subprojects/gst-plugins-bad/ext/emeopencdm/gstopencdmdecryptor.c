/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Based on:
 * subprojects/gst-devtools/validate/gst/validate/gst-validate-mockdecryptor.c
 * Copyright (C) 2019 Igalia S.L
 * Copyright (C) 2019 Metrological
 *   @author Charlie Turner <cturner@igalia.com>
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

#include <glib/gi18n-lib.h>
#include <gst/eme/eme.h>
#include <gst/eme/gstemeutils.h>

#include "gstemeopencdmlogging.h"
#include "gstopencdmdecryptor.h"
#include "gstopencdmmediakeys.h"
#include "gstopencdmmediakeysession.h"

typedef struct
{
  GstOpenCDMDecryptor *self;
  GstPromise *promise;
} TimeoutContext;

struct _GstOpenCDMDecryptor
{
  GstBaseTransform parent_instance;

  gchar *current_system_id;

  GstClockTime session_attach_timeout;

  GstMediaKeySession *session;
};

G_DEFINE_TYPE (GstOpenCDMDecryptor, gst_open_cdm_decryptor,
    GST_TYPE_BASE_TRANSFORM);
#define parent_class gst_open_cdm_decryptor_parent_class

enum
{
  PROP_0,

  PROP_SESSION_ATTACH_TIMEOUT,

  N_PROPS,
};

static GParamSpec *properties[N_PROPS];

static const char *cenc_content_types[] = {
  "video/mp4",
  "audio/mp4",
  "video/x-h264",
  "video/x-h265",
  "audio/mpeg",
  "audio/x-eac3",
  "audio/x-ac3",
  "audio/x-flac",
  "video/x-vp9",
  NULL,
};

#define cbcs_content_types cenc_content_types

static const char *webm_content_types[] = {
  "video/webm",
  "audio/webm",
  "video/x-vp9",
  "audio/x-opus",
  "audio/x-vorbis",
  "video/x-vp8",
  NULL,
};

#define DEFAULT_SESSION_ATTACH_TIMEOUT (GST_SECOND * 10)
#define ORIGINAL_MEDIA_TYPE "original-media-type"
#define PROTECTION_SYSTEM "protection-system"

#define CENC_TYPE "application/x-cenc"
#define CBCS_TYPE "application/x-cbcs"
#define WEBM_TYPE "application/x-webm-enc"

#define MEDIA_KEYS "media-keys"

typedef struct
{
  const gchar *uuid;
  const gchar *id;
} ProtectionSystemRecord;

static const ProtectionSystemRecord WELL_KNOWN_PROTECTION_SYSTEMS[] = {
  {.id = "org.w3.clearkey",.uuid = "1077efec-c0b2-4d02-ace3-3c1e52e2fb4b"},
  {.id = "com.widevine.alpha",.uuid = "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"},
  {.id = "com.microsoft.playready",
      .uuid = "9a04f079-9840-4286-ab92-e65be0885f95"},
  {.id = "com.apple.fps",.uuid = "94ce86fb-07ff-4f43-adb8-93d2fa968ca2"},
  {.id = NULL,.uuid = NULL},
};

static GstCaps *
create_sink_pad_template_caps (void)
{
  GstCaps *caps = gst_caps_new_empty ();

  for (guint i = 0;; i++) {
    const gchar *content_type = cenc_content_types[i];
    if (content_type == NULL) {
      break;
    }
    gst_caps_append_structure (caps, gst_structure_new (CENC_TYPE,
            ORIGINAL_MEDIA_TYPE, G_TYPE_STRING, content_type, NULL));
    for (const ProtectionSystemRecord * system = WELL_KNOWN_PROTECTION_SYSTEMS;
        system->id != NULL; system++) {
      gst_caps_append_structure (caps, gst_structure_new (CENC_TYPE,
              ORIGINAL_MEDIA_TYPE, G_TYPE_STRING, content_type,
              PROTECTION_SYSTEM, G_TYPE_STRING, system->uuid, NULL));
      gst_caps_append_structure (caps, gst_structure_new (CENC_TYPE,
              ORIGINAL_MEDIA_TYPE, G_TYPE_STRING, content_type,
              PROTECTION_SYSTEM, G_TYPE_STRING, system->id, NULL));
    }
  }

  for (guint i = 0;; i++) {
    const gchar *content_type = cbcs_content_types[i];
    if (content_type == NULL) {
      break;
    }
    gst_caps_append_structure (caps, gst_structure_new (CBCS_TYPE,
            ORIGINAL_MEDIA_TYPE, G_TYPE_STRING, content_type, NULL));
  }

  for (guint i = 0;; i++) {
    const gchar *content_type = webm_content_types[i];
    if (content_type == NULL) {
      break;
    }
    gst_caps_append_structure (caps, gst_structure_new (WEBM_TYPE,
            ORIGINAL_MEDIA_TYPE, G_TYPE_STRING, content_type, NULL));
  }

  return caps;
}

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_open_cdm_decryptor_finalize (GObject * object)
{
  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (object);
  g_clear_pointer (&self->current_system_id, g_free);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn base_transform_transform_ip (GstBaseTransform *,
    GstBuffer *);
static GstCaps *base_transform_transform_caps (GstBaseTransform *,
    GstPadDirection, GstCaps *, GstCaps *);
static gboolean base_transform_sink_event (GstBaseTransform *, GstEvent *);
static gboolean base_transform_stop (GstBaseTransform *);

static void
gst_open_cdm_decryptor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (object);

  switch (prop_id) {
    case PROP_SESSION_ATTACH_TIMEOUT:
      g_value_set_uint64 (value, self->session_attach_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_open_cdm_decryptor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (object);

  switch (prop_id) {
    case PROP_SESSION_ATTACH_TIMEOUT:
      self->session_attach_timeout = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_open_cdm_decryptor_class_init (GstOpenCDMDecryptorClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *btclass = GST_BASE_TRANSFORM_CLASS (klass);

  oclass->get_property =
      GST_DEBUG_FUNCPTR (gst_open_cdm_decryptor_get_property);
  oclass->set_property =
      GST_DEBUG_FUNCPTR (gst_open_cdm_decryptor_set_property);
  oclass->finalize = GST_DEBUG_FUNCPTR (gst_open_cdm_decryptor_finalize);

  gst_emeopencdm_init_logging ();

  /**
   * GstOpenCDMDecryptor:session-attach-timeout:
   *
   * Maximum duration of time in nanoseconds for the decryptor to wait for a
   * #GstMediaKeySession containing decryption keys for the current
   * initialization data before failing
   *
   * Since: 1.24
   */
  properties[PROP_SESSION_ATTACH_TIMEOUT] =
      g_param_spec_uint64 ("session-attach-timeout", "Session Attach Timeout",
      "Maximum duration of time in nanoseconds for the decryptor to wait for a "
      "GstMediaKeySession containing decryption keys for the current "
      "initialization data before failing", 0, G_MAXUINT64,
      DEFAULT_SESSION_ATTACH_TIMEOUT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (oclass, N_PROPS, properties);

  gst_element_class_set_static_metadata (eclass, "W3C EME OpenCDM decryptor",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts data streams using the W3C EME API and OpenCDM-compatible "
      "decryption modules", "Jordan Yelloz <jordan.yelloz@collabora.com>");

  gst_element_class_add_static_pad_template (eclass, &src_template);

  GstPadTemplate *sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_SOMETIMES, create_sink_pad_template_caps ());

  gst_element_class_add_pad_template (eclass, sink_template);

  btclass->transform_ip = GST_DEBUG_FUNCPTR (base_transform_transform_ip);
  btclass->transform_ip_on_passthrough = FALSE;
  btclass->transform_caps = GST_DEBUG_FUNCPTR (base_transform_transform_caps);
  btclass->sink_event = GST_DEBUG_FUNCPTR (base_transform_sink_event);
  btclass->stop = GST_DEBUG_FUNCPTR (base_transform_stop);
}

static void
gst_open_cdm_decryptor_init (GstOpenCDMDecryptor * self)
{
  self->session = NULL;
  self->current_system_id = NULL;
  self->session_attach_timeout = DEFAULT_SESSION_ATTACH_TIMEOUT;
}

static GstBuffer *
get_buffer_from_structure (const GstStructure * structure, const gchar * field)
{
  const GValue *value = gst_structure_get_value (structure, field);
  return value == NULL ? NULL : gst_value_get_buffer (value);
}

static GstOpenCDMMediaKeys *
get_media_keys (GstOpenCDMDecryptor * self)
{
  GstContext *context = gst_element_get_context (GST_ELEMENT (self),
      MEDIA_KEYS);

  if (context == NULL) {
    GST_TRACE_OBJECT (self, "no context for " MEDIA_KEYS);
    goto no_keys;
  }

  GstMediaKeys *keys = NULL;
  if (!gst_eme_context_get_media_keys (context, &keys)) {
    GST_TRACE_OBJECT (self, "media keys missing from %" GST_PTR_FORMAT,
        context);
    goto no_keys;
  }

  if (!GST_IS_OPEN_CDM_MEDIA_KEYS (keys)) {
    GST_WARNING_OBJECT (self, "media keys in %" GST_PTR_FORMAT
        " is not a GstOpenCDMMediaKeys", context);
    goto no_keys;
  }

  GST_DEBUG_OBJECT (self, "context contains %" GST_PTR_FORMAT, keys);

  g_clear_pointer (&context, gst_context_unref);
  return GST_OPEN_CDM_MEDIA_KEYS (keys);

no_keys:
  g_clear_pointer (&context, gst_context_unref);
  return NULL;
}

static gboolean
attach_open_cdm_session (GstOpenCDMDecryptor * self, GstBuffer * key_id)
{
  if (GST_IS_MEDIA_KEY_SESSION (self->session)) {
    GST_TRACE_OBJECT (self, "already attached %" GST_PTR_FORMAT, self->session);
    return TRUE;
  }

  GstOpenCDMMediaKeys *keys = get_media_keys (self);
  if (keys == NULL) {
    GST_DEBUG_OBJECT (self, "failed to obtain media keys");
    return FALSE;
  }

  GstMediaKeySession *session =
      gst_open_cdm_media_keys_get_session_for_key (keys, key_id);

  gst_clear_object (&keys);

  if (GST_IS_MEDIA_KEY_SESSION (session)) {
    g_set_object (&self->session, session);
    return TRUE;
  }

  return FALSE;
}

static gboolean
need_key (GstOpenCDMDecryptor * self, GstBuffer * key_id, GstPromise * promise)
{
  GstMessage *message =
      gst_message_new_eme_waiting_for_key (GST_OBJECT_CAST (self), key_id,
      promise);
  return gst_element_post_message (GST_ELEMENT_CAST (self), message);
}

static gboolean
have_key (GstOpenCDMDecryptor * self, GstBuffer * key_id)
{
  GstMessage *message =
      gst_message_new_eme_have_key (GST_OBJECT_CAST (self), key_id);
  return gst_element_post_message (GST_ELEMENT_CAST (self), message);
}

static gboolean
timeout_promise (GstClock * clock, GstClockTime time, GstClockID id, gpointer
    user_data)
{
  TimeoutContext *context = user_data;
  GstOpenCDMDecryptor *self = context->self;
  GST_WARNING_OBJECT (self, "no answer from application, cancelling promise");
  gst_promise_interrupt (context->promise);
  return FALSE;
}

static GstFlowReturn
base_transform_transform_ip (GstBaseTransform * trans, GstBuffer * buffer)
{
  GstProtectionMeta *protection = gst_buffer_get_protection_meta (buffer);
  if (protection == NULL) {
    return GST_FLOW_OK;
  }

  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (trans);

  GST_DEBUG_OBJECT (self, "protection meta %" GST_PTR_FORMAT, protection->info);

  guint subsample_count = 0;
  gboolean has_subsamples = gst_structure_get_uint (protection->info,
      "subsample_count", &subsample_count);

  GstBuffer *iv = get_buffer_from_structure (protection->info, "iv");
  GstBuffer *kid = get_buffer_from_structure (protection->info, "kid");
  GstBuffer *subsamples = has_subsamples ?
      get_buffer_from_structure (protection->info, "subsamples") : NULL;
  protection = NULL;

  GstClock *clock = gst_system_clock_obtain ();
  if (clock == NULL) {
    GST_ERROR_OBJECT (self, "no system clock");
    return GST_FLOW_ERROR;
  }

  gboolean requested_key = FALSE;
  if (!attach_open_cdm_session (self, kid)) {
    requested_key = TRUE;

    GST_DEBUG_OBJECT (self,
        "failed to attach session, requesting from application");

    GstPromise *promise = gst_promise_new ();
    need_key (self, kid, promise);

    TimeoutContext timeout = {
      .self = self,
      .promise = promise,
    };

    GstClockID id = gst_clock_new_single_shot_id (clock, gst_clock_get_time
        (clock) + self->session_attach_timeout);
    gst_clock_id_wait_async (id, timeout_promise, &timeout, NULL);

    gst_promise_wait (promise);
    gst_clock_id_unschedule (id);
    gst_clock_id_unref (id);
    gst_promise_unref (promise);
  }
  gst_clear_object (&clock);

  if (!attach_open_cdm_session (self, kid)) {
    GST_ERROR_OBJECT (self, "failed to attach session, even after waiting");
    GST_ELEMENT_ERROR (self, STREAM, DECRYPT_NOKEY,
        (_("Missing decryption key")), ("no session found for required key"));
    return GST_FLOW_ERROR;
  }

  if (requested_key) {
    have_key (self, kid);
  }

  GST_DEBUG_OBJECT (self, "decrypting with %" GST_PTR_FORMAT, self->session);
  GstFlowReturn result =
      gst_open_cdm_media_key_session_decrypt (GST_OPEN_CDM_MEDIA_KEY_SESSION
      (self->session), buffer, iv, kid,
      subsamples, subsample_count, self->session_attach_timeout);
  if (result != GST_FLOW_OK) {
    GstStructure *s = gst_structure_new ("data", "keyid", GST_TYPE_BUFFER,
        kid, "iv", GST_TYPE_BUFFER, iv, NULL);
    GST_ERROR_OBJECT (self, "failed to decrypt: %s, %" GST_PTR_FORMAT,
        gst_flow_get_name (result), s);
    gst_clear_structure (&s);
    GST_ELEMENT_ERROR (self, STREAM, DECRYPT_NOKEY,
        (_("Missing decryption key")),
        ("session timed out before finding key"));
    return GST_FLOW_ERROR;
  }

  protection = gst_buffer_get_protection_meta (buffer);
  if (protection != NULL) {
    gst_buffer_remove_meta (buffer, GST_META_CAST (protection));
  }

  return GST_FLOW_OK;
}

static inline gboolean
gst_structure_is_protected_media (const GstStructure * cap)
{
  return gst_structure_has_field (cap, ORIGINAL_MEDIA_TYPE);
}

static GstCaps *
base_transform_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  if (direction != GST_PAD_SINK) {
    return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_TRANSFORM_CLASS,
        transform_caps, (trans, direction, caps, filter), NULL);
  }

  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (trans);

  GST_DEBUG_OBJECT (self,
      "direction: %s, caps: %" GST_PTR_FORMAT " filter: %" GST_PTR_FORMAT,
      (direction == GST_PAD_SRC) ? "src" : "sink", caps, filter);
  GstCaps *transformed_caps = gst_caps_new_empty ();

  guint n_incoming_caps = gst_caps_get_size (caps);
  for (guint i = 0; i < n_incoming_caps; i++) {
    GstStructure *incoming_structure = gst_caps_get_structure (caps, i);
    GstStructure *outgoing_structure;

    if (direction == GST_PAD_SINK) {
      if (!gst_structure_is_protected_media (incoming_structure)) {
        continue;
      }

      outgoing_structure = gst_structure_copy (incoming_structure);
      gst_structure_set_name (outgoing_structure,
          gst_structure_get_string (outgoing_structure, ORIGINAL_MEDIA_TYPE));

      gst_structure_remove_fields (outgoing_structure, PROTECTION_SYSTEM,
          ORIGINAL_MEDIA_TYPE, "encryption-algorithm", "encoding-scope",
          "cipher-mode", NULL);
    } else {
      outgoing_structure = gst_structure_copy (incoming_structure);

      gst_structure_remove_fields (outgoing_structure, "base-profile",
          "codec_data", "height", "framerate", "level", "pixel-aspect-ratio",
          "profile", "rate", "width", NULL);

      gst_structure_set (outgoing_structure,
          PROTECTION_SYSTEM, G_TYPE_STRING, self->current_system_id,
          ORIGINAL_MEDIA_TYPE, G_TYPE_STRING,
          gst_structure_get_name (incoming_structure), NULL);

      gst_structure_set_name (outgoing_structure, CENC_TYPE);
    }

    transformed_caps =
        gst_caps_merge_structure (transformed_caps, outgoing_structure);
  }

  if (filter) {
    GST_DEBUG_OBJECT (self, "Using filter caps %" GST_PTR_FORMAT, filter);
    GstCaps *intersection = gst_caps_intersect_full (transformed_caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_take (&transformed_caps, intersection);
  }

  GST_DEBUG_OBJECT (self, "returning %" GST_PTR_FORMAT, transformed_caps);
  return transformed_caps;
}

static gboolean
post_eme_encrypted_message (GstOpenCDMDecryptor * self, const gchar * system_id,
    const gchar * init_data_type, GstBuffer * init_data, const gchar * origin,
    GstBuffer * raw_init_data)
{
  GstElement *element = GST_ELEMENT_CAST (self);
  GstMessage *message = gst_message_new_eme_encrypted_full (element,
      init_data_type, init_data, origin, raw_init_data);
  return gst_element_post_message (element, message);
}

static gboolean
process_dash_protection_data (GstOpenCDMDecryptor * self,
    const gchar * system_id, GstBuffer * data, const gchar * origin)
{
  GstBuffer *init_data = NULL;
  gchar *scheme_uuid = NULL;
  gboolean success = gst_eme_parse_dash_content_protection_block (data,
      &scheme_uuid, NULL, NULL, &init_data);
  if (!success) {
    GST_ERROR_OBJECT (self,
        "failed to parse DASH XML content protection block");
    return FALSE;
  }
  if (init_data == NULL) {
    GST_DEBUG ("skipping content protection block due to no init data");
    return TRUE;
  }
  return post_eme_encrypted_message (self, system_id, "cenc", init_data, origin,
      data);
}

static gboolean
process_cenc_protection_data (GstOpenCDMDecryptor * self, const gchar
    * system_id, GstBuffer * data, const gchar * origin)
{
  return post_eme_encrypted_message (self, system_id, "cenc", data, origin,
      data);
}

static gboolean
base_transform_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (trans);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_PROTECTION:{
      const gchar *system_id = NULL;
      GstBuffer *protection_data = NULL;
      const gchar *origin = NULL;

      gst_event_parse_protection (event, &system_id, &protection_data, &origin);
      GST_DEBUG_OBJECT (self, "system id=%s, origin=%s", system_id, origin);
      GstMapInfo info = GST_MAP_INFO_INIT;
      gst_buffer_map (protection_data, &info, GST_MAP_READ);
      GST_MEMDUMP_OBJECT (self, "protection data", info.data, info.size);
      gst_buffer_unmap (protection_data, &info);

      if (g_strcmp0 (origin, "dash/mpd") == 0) {
        process_dash_protection_data (self, system_id, protection_data, origin);
      } else {
        process_cenc_protection_data (self, system_id, protection_data, origin);
      }

      G_GNUC_FALLTHROUGH;
    }
    default:
      return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
  }
}

static gboolean
base_transform_stop (GstBaseTransform * trans)
{
  GstOpenCDMDecryptor *self = GST_OPEN_CDM_DECRYPTOR (trans);
  gst_clear_object (&self->session);
  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_TRANSFORM_CLASS, stop, (trans),
      TRUE);
}

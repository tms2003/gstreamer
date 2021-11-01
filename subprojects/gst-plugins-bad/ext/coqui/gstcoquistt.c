/*
 * GStreamer Coqui plugin
 * Copyright (C) 2017 Mike Sheldon <elleo@gnu.org>
 * Copyright (C) 2021, 2022 Philippe Normand <philn@igalia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-coquistt
 * @title: coquistt
 *
 * Speech recognition element suitable for continuous dictation based on
 * [Coqui-AI's](https://coqui.ai/) STT model. This audio filter should be
 * combined with a Voice Activity Detection element such as webrtcdsp. Upon VAD
 * detection, the filter will queue incoming audio samples until end of
 * utterance, at which point the whole utterance will be fed to the Coqui-STT
 * engine. Finally the resulting text is posted on the #GstBus using an element
 * message.
 *
 * Model files can be downloaded from the [Model Zoo](https://coqui.ai/models).
 *
 * ## Example launch line
 *
 * ```bash
 * $ gst-launch-1.0 --no-position -m pulsesrc ! queue ! \
 *   webrtcdsp voice-detection=1 echo-cancel=0 noise-suppression=0 ! \
 *   audioconvert ! audiorate ! audioresample ! \
 *   coquistt model-path=/path/to/model.tflite ! queue ! fakeaudiosink
 * ```
 *
 * ## Message layout
 *
 * * `timestamp`: #uint64 PTS,
 * * `stream-time`: #uint64, stream time,
 * * `running-time`: #uint64, running time,
 * * `text`: #str, STT result
 *
 */

#include <coqui-stt.h>

#include "gstcoquistt.h"

GST_DEBUG_CATEGORY_EXTERN (gst_coqui_debug);
#define GST_CAT_DEFAULT gst_coqui_debug

#define DEFAULT_SPEECH_MODEL "/usr/share/coqui/models/english-model.tflite"
#define DEFAULT_SCORER "/usr/share/coqui/models/english.scorer"

#define AUDIO_FRAME_SIZE 2048

enum
{
  PROP_0,
  PROP_SPEECH_MODEL,
  PROP_SCORER,
  PROP_LAST,
};

#define ALLOWED_CAPS "audio/x-raw,format=S16LE,rate=16000,channels=1"

#define GST_COQUI_STT_LOCK(obj) g_mutex_lock (&GST_COQUI_STT (obj)->lock)
#define GST_COQUI_STT_UNLOCK(obj) g_mutex_unlock (&GST_COQUI_STT (obj)->lock)

struct _GstCoquiSTT
{
  GstAudioFilter parent;

  GMutex lock;
  GstAudioInfo info;
  ModelState *model_state;
  StreamingState *streaming_state;
  GstAdapter *adapter;
  gchar *speech_model_path;
  gchar *scorer_path;
  gchar *previous_intermediate_result;
  gboolean has_voice;
};

struct _GstCoquiSTTClass
{
  GstAudioFilterClass parent_class;
};

#define gst_coqui_stt_parent_class parent_class
G_DEFINE_TYPE (GstCoquiSTT, gst_coqui_stt, GST_TYPE_AUDIO_FILTER);

static GstBuffer *
gst_coqui_stt_take_buffer (GstCoquiSTT * self, gsize nbytes)
{
  GstBuffer *buffer;
  GstClockTime timestamp;
  guint64 distance;
  gboolean at_discont;

  timestamp = gst_adapter_prev_pts (self->adapter, &distance);
  distance /= self->info.bpf;

  timestamp +=
      gst_util_uint64_scale_int (distance, GST_SECOND, self->info.rate);

  buffer = gst_adapter_take_buffer (self->adapter, nbytes);
  at_discont = (gst_adapter_pts_at_discont (self->adapter) == timestamp);

  GST_BUFFER_PTS (buffer) = timestamp;

  if (at_discont && distance == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  return buffer;
}

static GstMessage *
gst_coqui_stt_message_new (GstCoquiSTT * self, GstClockTime pts,
    const char *text)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (self);
  GstStructure *s;
  GstClockTime running_time, stream_time;

  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      pts);
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      pts);

  GST_DEBUG_OBJECT (self, "PTS: %" GST_TIME_FORMAT, GST_TIME_ARGS (pts));

  s = gst_structure_new ("coqui",
      "timestamp", G_TYPE_UINT64, pts,
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "text", G_TYPE_STRING, text, NULL);

  return gst_message_new_element (GST_OBJECT_CAST (self), s);
}

static gboolean
gst_coqui_stt_create_stream (GstCoquiSTT * self)
{
  int status = STT_CreateStream (self->model_state, &self->streaming_state);
  if (status != STT_ERR_OK) {
    char *error = STT_ErrorCodeToErrorMessage (status);
    GST_ELEMENT_ERROR (GST_ELEMENT_CAST (self), LIBRARY, INIT,
        ("Could not create stream"), ("%s", error));
    g_free (error);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Stream created");
  return TRUE;
}

static void
gst_coqui_stt_process_utterance (GstCoquiSTT * self)
{
  GstBuffer *buf;
  gsize available;
  GstMapInfo info;
  char *result;
  gsize i = 0;

  GST_COQUI_STT_LOCK (self);
  available = gst_adapter_available (self->adapter);
  if (!available)
    goto beach;

  buf = gst_coqui_stt_take_buffer (self, available);

  if (!self->streaming_state)
    gst_coqui_stt_create_stream (self);

  gst_buffer_map (buf, &info, GST_MAP_READ);
  GST_DEBUG_OBJECT (self, "Total size: %" G_GSIZE_FORMAT, info.size);
  while (i < info.size) {
    gsize s = AUDIO_FRAME_SIZE;
    if (i + s > info.size)
      s = info.size - i;
    GST_LOG_OBJECT (self, "Processing chunk of size: %" G_GSIZE_FORMAT, s);
    STT_FeedAudioContent (self->streaming_state, (const short *) info.data + i,
        (unsigned int) s);
    i += s;
  }
  gst_buffer_unmap (buf, &info);
  result = STT_FinishStream (self->streaming_state);
  self->streaming_state = NULL;

  if (strlen (result) > 0) {
    gst_element_post_message (GST_ELEMENT (self),
        gst_coqui_stt_message_new (self, GST_CLOCK_TIME_NONE, result));
  }

  STT_FreeString (result);
  gst_buffer_unref (buf);

beach:
  GST_COQUI_STT_UNLOCK (self);
}

static void
gst_coqui_stt_free_model (GstCoquiSTT * self)
{
  GST_COQUI_STT_LOCK (self);
  if (self->streaming_state)
    STT_FreeStream (self->streaming_state);
  self->streaming_state = NULL;

  if (self->model_state)
    STT_FreeModel (self->model_state);
  self->model_state = NULL;
  GST_COQUI_STT_UNLOCK (self);
}

static gboolean
gst_coqui_stt_load_model (GstCoquiSTT * self)
{
  gboolean ret = FALSE;
  int status;
  gst_coqui_stt_free_model (self);

  GST_COQUI_STT_LOCK (self);
  GST_INFO_OBJECT (self, "Loading model from %s", self->speech_model_path);
  status = STT_CreateModel (self->speech_model_path, &self->model_state);
  if (status != STT_ERR_OK) {
    char *error = STT_ErrorCodeToErrorMessage (status);
    GST_ELEMENT_ERROR (GST_ELEMENT_CAST (self), LIBRARY, INIT,
        ("Could not load model from %s", self->speech_model_path), ("%s",
            error));
    g_free (error);
    goto beach;
  }

  if (self->scorer_path) {
    GST_INFO_OBJECT (self, "Loading external scorer from %s",
        self->scorer_path);
    status = STT_EnableExternalScorer (self->model_state, self->scorer_path);
    if (status != STT_ERR_OK) {
      char *error = STT_ErrorCodeToErrorMessage (status);
      GST_ELEMENT_ERROR (GST_ELEMENT_CAST (self), LIBRARY, INIT,
          ("Could not load external scorer from %s", self->scorer_path),
          ("%s", error));
      g_free (error);
      goto beach;
    }
  }
  ret = gst_coqui_stt_create_stream (self);
beach:
  GST_COQUI_STT_UNLOCK (self);
  return ret;
}

static void
gst_coqui_stt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCoquiSTT *self = GST_COQUI_STT (object);

  switch (prop_id) {
    case PROP_SPEECH_MODEL:
      GST_COQUI_STT_LOCK (self);
      if (self->speech_model_path)
        g_free (self->speech_model_path);
      self->speech_model_path = g_value_dup_string (value);
      GST_COQUI_STT_UNLOCK (self);
      break;
    case PROP_SCORER:
      GST_COQUI_STT_LOCK (self);
      if (self->scorer_path)
        g_free (self->scorer_path);
      self->scorer_path = g_value_dup_string (value);
      GST_COQUI_STT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_coqui_stt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCoquiSTT *self = GST_COQUI_STT (object);

  switch (prop_id) {
    case PROP_SPEECH_MODEL:
      GST_COQUI_STT_LOCK (self);
      g_value_set_string (value, self->speech_model_path);
      GST_COQUI_STT_UNLOCK (self);
      break;
    case PROP_SCORER:
      GST_COQUI_STT_LOCK (self);
      g_value_set_string (value, self->scorer_path);
      GST_COQUI_STT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_coqui_stt_setup (GstAudioFilter * filter, const GstAudioInfo * info)
{
  GstCoquiSTT *self = GST_COQUI_STT (filter);

  GST_LOG_OBJECT (self, "setting format to %s with %i Hz and %i channels",
      info->finfo->description, info->rate, info->channels);

  GST_OBJECT_LOCK (self);
  gst_adapter_clear (self->adapter);
  self->info = *info;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static GstStateChangeReturn
gst_coqui_stt_change_state (GstElement * element, GstStateChange transition)
{
  GstCoquiSTT *self = GST_COQUI_STT (element);
  GstStateChangeReturn ret;

  GST_DEBUG_OBJECT (self, "%s", gst_state_change_get_name (transition));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_coqui_stt_load_model (self)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_coqui_stt_free_model (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_coqui_stt_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstCoquiSTT *self = GST_COQUI_STT (trans);

  GST_LOG_OBJECT (self, "Received: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_coqui_stt_process_utterance (self);
      break;
    default:
      break;
  }
  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_TRANSFORM_CLASS, sink_event,
      (trans, event), FALSE);
}

static GstFlowReturn
gst_coqui_stt_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstCoquiSTT *self = GST_COQUI_STT (trans);
  GstAudioLevelMeta *audio_meta = gst_buffer_get_audio_level_meta (buf);

  GST_COQUI_STT_LOCK (self);
  if (audio_meta) {
    if (!audio_meta->voice_activity && self->has_voice) {
      GST_COQUI_STT_UNLOCK (self);
      gst_coqui_stt_process_utterance (self);
      GST_COQUI_STT_LOCK (self);
      self->has_voice = FALSE;
    } else if (audio_meta->voice_activity)
      self->has_voice = TRUE;
  }

  if (self->has_voice) {
    buf = gst_buffer_make_writable (buf);
    GST_BUFFER_PTS (buf) =
        gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buf));
    gst_adapter_push (self->adapter, gst_buffer_ref (buf));
  }

  GST_COQUI_STT_UNLOCK (self);
  return GST_FLOW_OK;
}

static void
gst_coqui_stt_init (GstCoquiSTT * self)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (self);

  gst_base_transform_set_in_place (trans, TRUE);

  g_mutex_init (&self->lock);
  self->speech_model_path = g_strdup (DEFAULT_SPEECH_MODEL);
  self->scorer_path = NULL;
  self->model_state = NULL;
  self->streaming_state = NULL;
  self->adapter = gst_adapter_new ();
  self->previous_intermediate_result = NULL;
  self->has_voice = FALSE;
}

static void
gst_coqui_stt_finalize (GObject * object)
{
  GstCoquiSTT *self = GST_COQUI_STT (object);

  gst_coqui_stt_free_model (self);
  g_clear_object (&self->adapter);
  g_free (self->speech_model_path);
  g_free (self->scorer_path);

  if (self->previous_intermediate_result != NULL)
    STT_FreeString (self->previous_intermediate_result);

  g_mutex_clear (&self->lock);
  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_coqui_stt_class_init (GstCoquiSTTClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *audiofilter_class = GST_AUDIO_FILTER_CLASS (klass);
  GstCaps *caps = gst_caps_from_string (ALLOWED_CAPS);

  gst_audio_filter_class_add_pad_templates (audiofilter_class, caps);
  gst_caps_unref (caps);

  gobject_class->finalize = gst_coqui_stt_finalize;
  gobject_class->set_property = gst_coqui_stt_set_property;
  gobject_class->get_property = gst_coqui_stt_get_property;

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_coqui_stt_transform_ip);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_coqui_stt_sink_event);

  audiofilter_class->setup = GST_DEBUG_FUNCPTR (gst_coqui_stt_setup);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_coqui_stt_change_state);

  g_object_class_install_property (gobject_class, PROP_SPEECH_MODEL,
      g_param_spec_string ("speech-model", "Speech Model",
          "Location of the speech graph file.", DEFAULT_SPEECH_MODEL,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SCORER,
      g_param_spec_string ("scorer", "Scorer", "Location of the scorer file.",
          DEFAULT_SCORER, G_PARAM_READWRITE));

  gst_element_class_set_details_simple (gstelement_class,
      "coquistt",
      "Filter/Audio",
      "Performs speech recognition using the Coqui-AI STT model",
      "Mike Sheldon <elleo@gnu.org>, Philippe Normand <philn@igalia.com>");
}

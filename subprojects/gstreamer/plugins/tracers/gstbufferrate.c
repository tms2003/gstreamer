/* GStreamer
 * Copyright (C) 2022 Jimena Salas <jimena.salas@ridgerun.com>
 *
 * gstbufferrate.c: tracing module that logs buffer and bits per second stats
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
 * SECTION: tracer-bufferrate
 * @short_description: shows the buffer and bit rate on every src pad in the
 * pipeline.
 *
 * A tracing module that displays the amount of buffers and bits per second on
 * every src pad of every element of the running pipeline.
 *
 * ```
 * GST_DEBUG=GST_TRACER:7 GST_TRACERS="bufferrate" gst-launch-1.0 \
 *   videotestsrc is-live=true ! queue ! fakesink
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbufferrate.h"

struct _GstBufferRateTracer
{
  GstTracer parent;

  /* GstBufferRateCounter struct for every pad. Protected by object lock */
  GHashTable *buffer_counters;
  /* Periodic callback ID */
  guint callback_id;
  /* Number of running pipelines. Log should be done only if one or more
   * pipelines are running */
  guint pipes_running;
};

struct _GstBufferRateCounter
{
  guint64 buffer_count;
  guint64 bit_count;
};

typedef struct _GstBufferRateCounter GstBufferRateCounter;

static gboolean log_buffer_rate (gpointer * data);
static void add_count_to_pad (GstBufferRateTracer * self, GstPad * pad,
    guint64 bit_count);
static void pad_push_buffer_pre (GstBufferRateTracer * self, guint64 timestamp,
    GstPad * pad, GstBuffer * buffer);
static void pad_push_list_pre (GstBufferRateTracer * self,
    GstClockTime timestamp, GstPad * pad, GstBufferList * list);
static void pad_pull_range_post (GstBufferRateTracer * self,
    GstClockTime timestamp, GstPad * pad, GstBuffer * buffer,
    GstFlowReturn ret);
static void element_change_state_post (GstBufferRateTracer * self,
    guint64 timestamp, GstElement * element, GstStateChange transition,
    GstStateChangeReturn result);
static GstElement *get_real_pad_parent (GstPad * pad);
static void set_periodic_callback (GstBufferRateTracer * self);

static void remove_periodic_callback (GstBufferRateTracer * self);
static void reset_pad_counters (GstBufferRateTracer * self);
static void gst_buffer_rate_tracer_finalize (GObject * obj);

static GstTracerRecord *tr_buffer_rate = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_buffer_rate_debug);
#define GST_CAT_DEFAULT gst_buffer_rate_debug

#define gst_buffer_rate_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstBufferRateTracer, gst_buffer_rate_tracer,
    GST_TYPE_TRACER, GST_DEBUG_CATEGORY_INIT (gst_buffer_rate_debug,
        "bufferrate", 0, "buffer rate tracer"));

#define BUFFER_RATE_LOG_PERIOD_SECONDS 1

/* TODO(jsalas98): This function is already used by other tracers so it should
   be refactored to a common header or exposed as part of the pad API */
static GstElement *
get_real_pad_parent (GstPad * pad)
{
  GstObject *parent;

  if (!pad)
    return NULL;

  parent = gst_object_get_parent (GST_OBJECT_CAST (pad));

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    GstObject *tmp;
    pad = GST_PAD_CAST (parent);
    tmp = gst_object_get_parent (GST_OBJECT_CAST (pad));
    gst_object_unref (parent);
    parent = tmp;
  }
  return GST_ELEMENT_CAST (parent);
}

static gboolean
log_buffer_rate (gpointer * data)
{
  GstBufferRateTracer *self = NULL;
  gpointer key = NULL;
  gpointer value = NULL;
  GHashTableIter iter = { 0 };

  g_return_val_if_fail (data, FALSE);

  self = GST_BUFFER_RATE_TRACER (data);

  /* Lock the tracer to make sure no new pad is added while we are logging */
  GST_OBJECT_LOCK (self);

  /* Using the iterator functions to go through the hash table and print the
     buffer rate of every element stored */
  g_hash_table_iter_init (&iter, self->buffer_counters);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstPad *pad = GST_PAD_CAST (key);
    GstElement *element = get_real_pad_parent (pad);
    GstBufferRateCounter *pad_counter = (GstBufferRateCounter *) value;
    gdouble buffers_per_second =
        ((gdouble) pad_counter->buffer_count) / BUFFER_RATE_LOG_PERIOD_SECONDS;
    gdouble bits_per_second =
        ((gdouble) pad_counter->bit_count) / BUFFER_RATE_LOG_PERIOD_SECONDS;

    gst_tracer_record_log (tr_buffer_rate, GST_OBJECT_NAME (element),
        GST_OBJECT_NAME (pad), buffers_per_second, bits_per_second);

    /* Reset counters */
    pad_counter->buffer_count = 0;
    pad_counter->bit_count = 0;
  }

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static void
add_count_to_pad (GstBufferRateTracer * self, GstPad * pad, guint64 bit_count)
{
  GstBufferRateCounter *pad_counter = NULL;

  g_return_if_fail (self);
  g_return_if_fail (pad);

  GST_OBJECT_LOCK (self);

  pad_counter =
      (GstBufferRateCounter *) g_hash_table_lookup (self->buffer_counters, pad);

  if (NULL != pad_counter) {
    pad_counter->buffer_count += 1;
    pad_counter->bit_count += bit_count;
  } else {
    pad_counter = g_malloc (sizeof (GstBufferRateCounter));
    pad_counter->buffer_count = 1;
    pad_counter->bit_count = bit_count;

    g_hash_table_insert (self->buffer_counters, gst_object_ref (pad),
        pad_counter);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
pad_push_buffer_pre (GstBufferRateTracer * self, guint64 timestamp,
    GstPad * pad, GstBuffer * buffer)
{
  static const guint bits_per_byte = 8;

  guint64 bit_count = gst_buffer_get_size (buffer) * bits_per_byte;

  add_count_to_pad (self, pad, bit_count);
}

static void
pad_push_list_pre (GstBufferRateTracer * self, GstClockTime timestamp,
    GstPad * pad, GstBufferList * list)
{
  guint idx = 0;
  GstBuffer *buffer = NULL;

  for (idx = 0; idx < gst_buffer_list_length (list); ++idx) {
    buffer = gst_buffer_list_get (list, idx);
    pad_push_buffer_pre (self, timestamp, pad, buffer);
  }
}

static void
pad_pull_range_post (GstBufferRateTracer * self, GstClockTime timestamp,
    GstPad * pad, GstBuffer * buffer, GstFlowReturn ret)
{
  pad_push_buffer_pre (self, timestamp, pad, buffer);
}

static void
element_change_state_post (GstBufferRateTracer * self, guint64 timestamp,
    GstElement * element, GstStateChange transition,
    GstStateChangeReturn result)
{
  /* We are only interested in capturing when a pipeline goes to
     playing, but this hook reports for every element in the
     pipeline
   */
  if (FALSE == GST_IS_PIPELINE (element)) {
    return;
  }

  if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING
      && result == GST_STATE_CHANGE_SUCCESS) {
    GST_DEBUG_OBJECT (self, "Pipeline %s changed to playing",
        GST_OBJECT_NAME (element));
    set_periodic_callback (self);
  } else if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    GST_DEBUG_OBJECT (self, "Pipeline %s changed to paused",
        GST_OBJECT_NAME (element));
    remove_periodic_callback (self);
  }
}

static void
set_periodic_callback (GstBufferRateTracer * self)
{
  g_return_if_fail (self);

  GST_OBJECT_LOCK (self);

  if (0 == self->pipes_running) {
    GST_INFO_OBJECT (self,
        "First pipeline started running, starting profiling");

    reset_pad_counters (self);

    self->callback_id =
        g_timeout_add_seconds (BUFFER_RATE_LOG_PERIOD_SECONDS,
        (GSourceFunc) log_buffer_rate, (gpointer) self);
  }

  self->pipes_running++;
  GST_DEBUG_OBJECT (self, "Pipes running: %d", self->pipes_running);

  GST_OBJECT_UNLOCK (self);
}

static void
remove_periodic_callback (GstBufferRateTracer * self)
{
  g_return_if_fail (self);

  GST_OBJECT_LOCK (self);

  if (1 == self->pipes_running) {
    GST_INFO_OBJECT (self, "Last pipeline stopped running, stopped profiling");
    g_source_remove (self->callback_id);
    self->callback_id = 0;
  }

  self->pipes_running--;
  GST_DEBUG_OBJECT (self, "Pipes running: %d", self->pipes_running);

  GST_OBJECT_UNLOCK (self);
}

static void
reset_pad_counters (GstBufferRateTracer * self)
{
  gpointer key = NULL;
  gpointer value = NULL;
  GHashTableIter iter = { 0 };

  g_return_if_fail (self);

  g_hash_table_iter_init (&iter, self->buffer_counters);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstBufferRateCounter *pad_counter = (GstBufferRateCounter *) value;
    pad_counter->buffer_count = 0;
    pad_counter->bit_count = 0;
  }
}

static void
gst_buffer_rate_tracer_finalize (GObject * obj)
{
  GstBufferRateTracer *self = GST_BUFFER_RATE_TRACER (obj);

  g_hash_table_destroy (self->buffer_counters);

  G_OBJECT_CLASS (gst_buffer_rate_tracer_parent_class)->finalize (obj);
}

static void
gst_buffer_rate_tracer_class_init (GstBufferRateTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_buffer_rate_tracer_finalize;

  /* *INDENT-OFF* */
  tr_buffer_rate = gst_tracer_record_new ("bufferrate.class",
      "element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "src-pad", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
          NULL),
      "buffers-per-second", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_DOUBLE,
          "description", G_TYPE_STRING, "Buffers per second",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS,
          GST_TRACER_VALUE_FLAGS_AGGREGATED,
          "min", G_TYPE_DOUBLE, 0.0,
          "max", G_TYPE_DOUBLE, G_MAXDOUBLE, NULL),
      "bits-per-second", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_DOUBLE,
          "description", G_TYPE_STRING, "Bits per second",
          "min", G_TYPE_DOUBLE, 0.0,
          "max", G_TYPE_DOUBLE, G_MAXDOUBLE,
          NULL),
      NULL);
  /* *INDENT-ON* */

  GST_OBJECT_FLAG_SET (tr_buffer_rate, GST_OBJECT_FLAG_MAY_BE_LEAKED);
}

static void
gst_buffer_rate_tracer_init (GstBufferRateTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  self->pipes_running = 0;
  self->callback_id = 0;
  self->buffer_counters =
      g_hash_table_new_full (NULL, NULL, gst_object_unref, g_free);

  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (pad_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (pad_push_list_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (pad_pull_range_post));
  gst_tracing_register_hook (tracer, "element-change-state-post",
      G_CALLBACK (element_change_state_post));
}

/* GStreamer
 * Copyright (C) 2022 Jimena Salas <jimena.salas@ridgerun.com>
 *
 * gstframerate.c: tracing module that logs processing framerate stats
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
 * SECTION: tracer-framerate
 * @short_description: shows the framerate on every src pad in the pipeline.
 *
 * A tracing module that displays the amount of frames per second on every
 * src pad of every element of the running pipeline.
 *
 * ```
 * GST_TRACERS="framerate" GST_DEBUG=GST_TRACER:7 ./...
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstframerate.h"

struct _GstFramerateTracer
{
  GstTracer parent;

  GHashTable *frame_counters;
  guint callback_id;
  guint pipes_running;
};

static gboolean log_framerate (gpointer * data);
static void add_frame_count_to_pad (GstFramerateTracer * self, GstPad * pad,
    guint count);
static void pad_push_buffer_pre (GstFramerateTracer * self, guint64 timestamp,
    GstPad * pad, GstBuffer * buffer);
static void pad_push_list_pre (GstFramerateTracer * self,
    GstClockTime timestamp, GstPad * pad, GstBufferList * list);
static void pad_pull_range_pre (GstFramerateTracer * self,
    GstClockTime timestamp, GstPad * pad, guint64 offset, guint size);
static void element_change_state_post (GstFramerateTracer * self,
    guint64 timestamp, GstElement * element, GstStateChange transition,
    GstStateChangeReturn result);
static GstElement *get_real_pad_parent (GstPad * pad);
static void set_periodic_callback (GstFramerateTracer * self);

static void remove_periodic_callback (GstFramerateTracer * self);
static void reset_pad_counters (GstFramerateTracer * self);
static void gst_framerate_tracer_finalize (GObject * obj);

static GstTracerRecord *tr_framerate = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_framerate_debug);
#define GST_CAT_DEFAULT gst_framerate_debug

#define gst_framerate_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFramerateTracer, gst_framerate_tracer,
    GST_TYPE_TRACER, GST_DEBUG_CATEGORY_INIT (gst_framerate_debug, "framerate",
        0, "framerate tracer"));

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
log_framerate (gpointer * data)
{
  GstFramerateTracer *self = NULL;
  gpointer key = NULL;
  gpointer value = NULL;
  GHashTableIter iter = { 0 };

  g_return_val_if_fail (data, FALSE);

  self = GST_FRAMERATE_TRACER (data);

  /* Lock the tracer to make sure no new pad is added while we are logging */
  GST_OBJECT_LOCK (self);

  /* Using the iterator functions to go through the Hash table and print the
     framerate of every element stored */
  g_hash_table_iter_init (&iter, self->frame_counters);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstPad *pad = GST_PAD_CAST (key);
    GstElement *element = get_real_pad_parent (pad);
    guint count = GPOINTER_TO_UINT (value);

    gst_tracer_record_log (tr_framerate, GST_OBJECT_NAME (element),
        GST_OBJECT_NAME (pad), count);
    /* Reset counter */
    g_hash_table_insert (self->frame_counters, pad, NULL);
  }

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static void
add_frame_count_to_pad (GstFramerateTracer * self, GstPad * pad, guint count)
{
  gpointer ptr = NULL;
  guint frames_so_far = 0;

  g_return_if_fail (self);
  g_return_if_fail (pad);

  GST_OBJECT_LOCK (self);
  /* If the key doesn't exist (the first time the hook gets called for
     a pad) lookup will return NULL, which works in our favor because
     it is converted to 0
   */
  ptr = g_hash_table_lookup (self->frame_counters, pad);

  frames_so_far = GPOINTER_TO_UINT (ptr);
  frames_so_far += count;

  ptr = GUINT_TO_POINTER (frames_so_far);

  g_hash_table_insert (self->frame_counters, pad, ptr);
  GST_OBJECT_UNLOCK (self);
}

static void
pad_push_buffer_pre (GstFramerateTracer * self, guint64 timestamp, GstPad * pad,
    GstBuffer * buffer)
{
  add_frame_count_to_pad (self, pad, 1);
}

static void
pad_push_list_pre (GstFramerateTracer * self, GstClockTime timestamp,
    GstPad * pad, GstBufferList * list)
{
  add_frame_count_to_pad (self, pad, gst_buffer_list_length (list));
}

static void
pad_pull_range_pre (GstFramerateTracer * self, GstClockTime timestamp,
    GstPad * pad, guint64 offset, guint size)
{
  add_frame_count_to_pad (self, pad, 1);
}

static void
element_change_state_post (GstFramerateTracer * self, guint64 timestamp,
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
    reset_pad_counters (self);
    set_periodic_callback (self);
  } else if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    GST_DEBUG_OBJECT (self, "Pipeline %s changed to paused",
        GST_OBJECT_NAME (element));
    remove_periodic_callback (self);
  }
}

static void
set_periodic_callback (GstFramerateTracer * self)
{
  static const guint framerate_log_period = 1;

  g_return_if_fail (self);

  GST_OBJECT_LOCK (self);

  if (0 == self->pipes_running) {
    GST_INFO_OBJECT (self,
        "First pipeline started running, starting profiling");

    self->callback_id =
        g_timeout_add_seconds (framerate_log_period,
        (GSourceFunc) log_framerate, (gpointer) self);
  }

  self->pipes_running++;
  GST_DEBUG_OBJECT (self, "Pipes running: %d", self->pipes_running);

  GST_OBJECT_UNLOCK (self);
}

static void
remove_periodic_callback (GstFramerateTracer * self)
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
reset_pad_counters (GstFramerateTracer * self)
{
  gpointer key = NULL;
  gpointer value = NULL;
  GHashTableIter iter = { 0 };

  g_return_if_fail (self);

  GST_OBJECT_LOCK (self);
  g_hash_table_iter_init (&iter, self->frame_counters);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstPad *pad = GST_PAD_CAST (key);
    g_hash_table_insert (self->frame_counters, pad, NULL);
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_framerate_tracer_finalize (GObject * obj)
{
  GstFramerateTracer *self = GST_FRAMERATE_TRACER (obj);

  g_hash_table_destroy (self->frame_counters);

  G_OBJECT_CLASS (gst_framerate_tracer_parent_class)->finalize (obj);
}

static void
gst_framerate_tracer_class_init (GstFramerateTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_framerate_tracer_finalize;

  /* *INDENT-OFF* */
  tr_framerate = gst_tracer_record_new ("framerate.class",
      "element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "src-pad", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
          NULL),
      "fps", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "Frames per second",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS,
          GST_TRACER_VALUE_FLAGS_AGGREGATED, "min", G_TYPE_UINT, 0, "max",
          G_TYPE_UINT, G_MAXUINT, NULL),
      NULL);
  /* *INDENT-ON* */

  GST_OBJECT_FLAG_SET (tr_framerate, GST_OBJECT_FLAG_MAY_BE_LEAKED);
}

static void
gst_framerate_tracer_init (GstFramerateTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  self->pipes_running = 0;
  self->callback_id = 0;
  self->frame_counters = g_hash_table_new (NULL, NULL);

  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (pad_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (pad_push_list_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (pad_pull_range_pre));
  gst_tracing_register_hook (tracer, "element-change-state-post",
      G_CALLBACK (element_change_state_post));
}

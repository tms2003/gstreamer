/* GStreamer
 *
 * unit test for GstTracer
 *
 * Copyright (C) 2018 Yeongjin Jeong <gingerbk247@gmail.com>
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

#include <gst/check/gstcheck.h>
#include <gst/gsttracer.h>

#ifndef GST_DISABLE_GST_TRACER_HOOKS
/* dummy tracer based GstTracer */

#define GST_TYPE_DUMMY_TRACER (gst_dummy_tracer_get_type())
#define GST_DUMMY_TRACER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DUMMY_TRACER,GstDummyTracer))
#define GST_DUMMY_TRACER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DUMMY_TRACER,GstDummyTracerClass))
#define GST_IS_DUMMY_TRACER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DUMMY_TRACER))
#define GST_IS_DUMMY_TRACER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DUMMY_TRACER))
#define GST_DUMMY_TRACER_CAST(obj) ((GstDummyTracer *)(obj))

typedef struct _GstDummyTracer GstDummyTracer;
typedef struct _GstDummyTracerClass GstDummyTracerClass;

static GType gst_dummy_tracer_get_type (void);

static gint dummytracer_marker;

struct _GstDummyTracer
{
  GstTracer parent;
};

struct _GstDummyTracerClass
{
  GstTracerClass parent_class;
};

#define gst_dummy_tracer_parent_class parent_class
G_DEFINE_TYPE (GstDummyTracer, gst_dummy_tracer, GST_TYPE_TRACER);

static void
do_push_event_pre (GstTracer * self, GstClockTime ts,
    GstPad * pad, GstEvent * event)
{
  GstElement *parent = GST_ELEMENT_CAST (gst_pad_get_parent (pad));

  if (parent) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
      dummytracer_marker += 1;
      GST_DEBUG ("hooking pad push event on '%s'", GST_OBJECT_NAME (self));
    }
    gst_object_unref (parent);
  }
}

static void
gst_dummy_tracer_class_init (GstDummyTracerClass * klass)
{
}

static void
gst_dummy_tracer_init (GstDummyTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
}

static gboolean
gst_dummy_tracer_plugin_init (GstPlugin * plugin)
{
  if (!gst_tracer_register (plugin, "dummytracer", GST_TYPE_DUMMY_TRACER))
    return FALSE;
  return TRUE;
}

static gboolean
gst_dummy_tracer_plugin_register (void)
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "dummytracer",
      "GStreamer dummy tracer",
      gst_dummy_tracer_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
}

GST_START_TEST (test_register_static_dummy_tracer)
{
  GstPlugin *unloaded_plugin;
  GstPlugin *loaded_plugin;
  GstPluginFeature *feature;
  GstElement *identity;
  GstPad *srcpad;

  dummytracer_marker = 0;

  unloaded_plugin =
      gst_registry_find_plugin (gst_registry_get (), "dummytracer");
  fail_unless (unloaded_plugin == NULL);

  /* hook pad push event */
  identity = gst_element_factory_make ("identity", "identity");
  srcpad = gst_element_get_static_pad (identity, "src");

  gst_element_set_state (identity, GST_STATE_PAUSED);

  /* sticky event should return flow ok */
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("the-stream")));

  fail_unless (dummytracer_marker == 0,
      "Pad-push-event should not be hooked yet, by the dummytracer.");

  gst_dummy_tracer_plugin_register ();

  loaded_plugin = gst_registry_find_plugin (gst_registry_get (), "dummytracer");
  fail_if (loaded_plugin == NULL, "Failed to find dummytracer plugin");
  ASSERT_OBJECT_REFCOUNT (loaded_plugin, "loaded_plugin in registry", 2);

  feature = gst_registry_find_feature (gst_registry_get (),
      "dummytracer", GST_TYPE_TRACER_FACTORY);
  fail_if (feature == NULL, "Failed to find dummy tracer factory");

  /* sticky event should return flow ok */
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("the-stream")));

  fail_unless (dummytracer_marker == 1, "Failed to create dummy tracer");

  gst_element_set_state (identity, GST_STATE_NULL);

  /* clean up */
  gst_object_unref (srcpad);
  gst_object_unref (identity);
  gst_object_unref (feature);
  gst_object_unref (loaded_plugin);
}

GST_END_TEST;
#endif /* GST_DISABLE_GST_TRACER_HOOKS */

static Suite *
gst_tracer_suite (void)
{
  Suite *s = suite_create ("GstTracer");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
#ifndef GST_DISABLE_GST_TRACER_HOOKS
  tcase_add_test (tc_chain, test_register_static_dummy_tracer);
#endif

  return s;
}

#define _init                                     \
{                                                 \
  g_setenv ("GST_TRACERS", "dummytracer", TRUE);  \
}                                                 \

GST_CHECK_MAIN_WITH_CODE (gst_tracer, _init);

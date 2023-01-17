#include <gst/gst.h>

#define MAKE_AND_ADD(var, pipe, name, label, elem_name) \
G_STMT_START { \
  if (G_UNLIKELY (!(var = (gst_element_factory_make (name, elem_name))))) { \
    GST_ERROR ("Could not create element %s", name); \
    goto label; \
  } \
  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipe), var))) { \
    GST_ERROR ("Could not add element %s", name); \
    goto label; \
  } \
} G_STMT_END

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, GstContext * context)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *context_type = NULL;

      gst_message_parse_context_type (message, &context_type);

      if (context_type && !g_strcmp0 (context_type, "gst-index")) {
        GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));

        gst_element_set_context (element, context);
      }

      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static void
pad_added_cb (GstElement * element, GstPad * pad, GstElement * pipe)
{
  GstElement *fakesink;
  GstPad *sinkpad;

  MAKE_AND_ADD (fakesink, pipe, "fakesink", fail, NULL);

  gst_element_sync_state_with_parent (fakesink);

  sinkpad = gst_element_get_static_pad (fakesink, "sink");

  g_assert (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK);

  return;

fail:
  g_assert_not_reached ();
}

typedef enum
{
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP,
} GstAutoplugSelectResult;

static GstAutoplugSelectResult
autoplug_select_cb (GstElement * element, GstPad * pad, GstCaps * caps,
    GstElementFactory * factory)
{
  if (gst_element_factory_list_is_type (factory,
          GST_ELEMENT_FACTORY_TYPE_DECODER)) {
    return GST_AUTOPLUG_SELECT_EXPOSE;
  } else {
    return GST_AUTOPLUG_SELECT_TRY;
  }
}

int
main (int ac, char **av)
{
  GstElement *pipe;
  GstBus *bus;
  int ret = 0;
  GstElement *uridecodebin;
  GstContext *ctx;
  GstMemIndex *index = NULL;
  GstStructure *s;
  GstMessage *msg;
  GVariant *variant;

  gst_init (NULL, NULL);

  pipe = gst_pipeline_new (NULL);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipe));
  ctx = gst_context_new ("gst-index", TRUE);

  if (ac != 3) {
    g_print ("Usage: %s INPUT_URI INDEX_PATH\n", *av);
    goto fail;
  }

  if (g_file_test (av[2], G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
    gchar *data;
    gsize length;
    GVariant *inner;

    if (!g_file_get_contents (av[2], &data, &length, NULL)) {
      g_printerr ("Failed to read from %s", av[2]);
      goto fail;
    }

    if (!(variant = g_variant_new_from_data (G_VARIANT_TYPE ("v"), data, length,
                TRUE, NULL, NULL))) {
      g_printerr ("Failed to parse variant from %s", av[2]);
      goto fail;
    }

    inner = g_variant_get_variant (variant);

    index = gst_mem_index_new_from_variant (inner);

    g_variant_unref (inner);
    g_variant_unref (variant);

    if (!index) {
      g_printerr ("Failed to parse index from %s", av[2]);
      goto fail;
    }

    g_print ("Loaded index from %s\n", av[2]);
  } else {
    index = gst_mem_index_new ();
  }

  MAKE_AND_ADD (uridecodebin, pipe, "uridecodebin", fail, NULL);

  g_signal_connect (uridecodebin, "pad-added", G_CALLBACK (pad_added_cb), pipe);
  g_signal_connect (uridecodebin, "autoplug-select",
      G_CALLBACK (autoplug_select_cb), NULL);

  g_object_set (uridecodebin, "uri", av[1], NULL);

  s = gst_context_writable_structure (ctx);
  gst_structure_set (s, "index", G_TYPE_OBJECT, index, NULL);

  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, ctx,
      NULL);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);

  /* The whole file was consumed, our index should be full */

  g_assert (msg != NULL);

  gst_message_unref (msg);

  variant = gst_mem_index_to_variant (index);

  variant = g_variant_new_variant (variant);

  if (!g_file_set_contents (av[2],
          g_variant_get_data (variant), g_variant_get_size (variant), NULL)) {
    g_variant_unref (variant);
    g_printerr ("Failed to write index to %s", av[2]);
    goto done;
  }

  g_print ("Wrote index to %s!\n", av[2]);

  g_variant_unref (variant);

done:
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_context_unref (ctx);
  gst_object_unref (pipe);

  if (index)
    g_object_unref (index);

  gst_object_unref (bus);

  return ret;

fail:
  ret = 1;
  goto done;
}

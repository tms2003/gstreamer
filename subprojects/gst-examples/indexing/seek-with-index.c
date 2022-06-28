#include <gst/gst.h>

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

int
main (int ac, char **av)
{
  GstElement *pipe;
  GstBus *bus = NULL;
  int ret = 0;
  GstContext *ctx = NULL;
  GstMemIndex *index = NULL;
  GstStructure *s;
  GstMessage *msg;
  gchar *endptr = NULL;
  guint64 seek_pos_seconds;

  gst_init (NULL, NULL);

  pipe = gst_element_factory_make ("playbin", NULL);

  if (!pipe) {
    GST_ERROR ("playbin must be available");
    goto fail;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipe));

  if (ac != 4 && ac != 3) {
    g_print ("Usage: %s MEDIA_URI SEEK_POSITION_SECONDS [INDEX_PATH]\n", *av);
    goto fail;
  }

  seek_pos_seconds = g_ascii_strtoull (av[2], &endptr, 10);

  if (!endptr || *endptr != '\0') {
    g_printerr ("Failed to parse seek position %s\n", av[2]);
    goto fail;
  }

  if (ac == 4) {
    GVariant *variant;
    gchar *data;
    gsize length;
    GVariant *inner;

    if (!g_file_test (av[3], G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
      g_printerr ("Index file at %s does not exist\n", av[3]);
      goto done;
    }

    if (!g_file_get_contents (av[3], &data, &length, NULL)) {
      g_printerr ("Failed to read from %s", av[3]);
      goto fail;
    }

    if (!(variant = g_variant_new_from_data (G_VARIANT_TYPE ("v"), data, length,
                TRUE, NULL, NULL))) {
      g_printerr ("Failed to parse variant from %s", av[3]);
      goto fail;
    }

    inner = g_variant_get_variant (variant);

    index = gst_mem_index_new_from_variant (inner);

    g_variant_unref (inner);
    g_variant_unref (variant);

    if (!index) {
      g_printerr ("Failed to parse index from %s", av[3]);
      goto fail;
    }

    g_print ("Loaded index from %s\n", av[3]);

    ctx = gst_context_new ("gst-index", TRUE);
    s = gst_context_writable_structure (ctx);
    gst_structure_set (s, "index", G_TYPE_OBJECT, index, NULL);

    gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, ctx,
        NULL);
  } else {
  }

  g_object_set (pipe, "uri", av[1], NULL);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ASYNC_DONE);

  /* The whole file was consumed, our index should be full */

  g_assert (msg != NULL);

  gst_message_unref (msg);

  if (!gst_element_seek_simple (pipe, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
          seek_pos_seconds * GST_SECOND)) {
    g_printerr ("Failed to seek!\n");
    goto fail;
  }

  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ASYNC_DONE);

  /* The whole file was consumed, our index should be full */

  g_assert (msg != NULL);

  gst_message_unref (msg);

  g_print ("EOS, good bye!\n");

done:
  gst_element_set_state (pipe, GST_STATE_NULL);

  if (ctx)
    gst_context_unref (ctx);

  if (index)
    g_object_unref (index);

  if (bus)
    gst_object_unref (bus);

  gst_object_unref (pipe);

  return ret;

fail:
  ret = 1;
  goto done;
}

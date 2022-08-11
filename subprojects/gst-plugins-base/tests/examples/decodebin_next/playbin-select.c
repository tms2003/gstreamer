/*
 * GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>

static GMainLoop *loop = NULL;
static gboolean disable_video = FALSE;
static gboolean disable_audio = FALSE;
static gboolean disable_text = FALSE;

static GstBusSyncReply
sync_msg_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstObject *src = GST_MESSAGE_SRC (msg);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name = gst_object_get_path_string (src);
      gst_message_parse_error (msg, &err, NULL);

      gst_printerrln ("ERROR: from element %s: %s", name, err->message);
      g_clear_error (&err);
      g_free (name);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_STREAM_COLLECTION: {
      GstStreamCollection *collection = NULL;
      gst_message_parse_stream_collection (msg, &collection);
      if (collection) {
        GList *streams = NULL;
        gboolean video_added = FALSE;
        gboolean audio_added = FALSE;
        gboolean text_added = FALSE;
        guint i;

        gst_println ("Got a collection from %s", GST_OBJECT_NAME (src));

        /* Pick up the first stream if needed */
        for (i = 0; i < gst_stream_collection_get_size (collection); i++) {
          GstStream *stream =
          gst_stream_collection_get_stream (collection, i);
          if (stream) {
            GstStreamType type = gst_stream_get_stream_type (stream);
            const gchar *sid = gst_stream_get_stream_id (stream);

            if ((type & GST_STREAM_TYPE_AUDIO) != 0 &&
                !audio_added && !disable_audio) {
              streams = g_list_append (streams, (gpointer) sid);
              audio_added = TRUE;
            } else if ((type & GST_STREAM_TYPE_VIDEO) != 0 &&
                !video_added && !disable_audio) {
              streams = g_list_append (streams, (gpointer) sid);
              video_added = TRUE;
            } else if ((type & GST_STREAM_TYPE_TEXT) != 0 &&
                !text_added && !disable_text) {
              streams = g_list_append (streams, (gpointer) sid);
              text_added = TRUE;
            }
          }
        }

        if (streams) {
          GstEvent *event = gst_event_new_select_streams (streams);

          gst_println ("Selected streams, video %d, audio %d, text %d",
              video_added, audio_added, text_added);

          g_list_free (streams);
          gst_element_send_event (GST_ELEMENT (msg->src), event);
        }
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static gboolean
msg_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstBin *pipeline = GST_BIN_CAST (user_data);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ASYNC_DONE:
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (pipeline,
          GST_DEBUG_GRAPH_SHOW_ALL, "playbin-select.async-done");
      break;
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, gchar ** argv)
{
  GstBus *bus;
  gchar *uri = NULL;
  GOptionContext *option_ctx;
  GError *error = NULL;
  gboolean ret;
  GstElement *pipeline;
  GOptionEntry options[] = {
    {"disable-video", 0, 0, G_OPTION_ARG_NONE, &disable_video,
        "Disable video stream", NULL}
    ,
    {"disable-audio", 0, 0, G_OPTION_ARG_NONE, &disable_audio,
        "Disable audio stream", NULL}
    ,
    {"disable-text", 0, 0, G_OPTION_ARG_NONE, &disable_text,
        "Disable text stream", NULL}
    ,
    {"uri", 0, 0, G_OPTION_ARG_STRING, &uri, "URI to test", NULL},
    {NULL}
  };

  option_ctx = g_option_context_new ("Playbin3 stream selection example");
  g_option_context_add_main_entries (option_ctx, options, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  ret = g_option_context_parse (option_ctx, &argc, &argv, &error);
  g_option_context_free (option_ctx);

  if (!ret) {
    gst_printerrln ("Option parsing failed: %s", error->message);
    g_clear_error (&error);
    return 1;
  }

  if (!uri) {
    gst_printerrln ("URI is not specified");
    return 1;
  }

  if (!gst_uri_is_valid (uri)) {
    gchar *file = gst_filename_to_uri (uri, NULL);
    g_free (uri);
    uri = file;
  }

  if (!uri) {
    gst_printerrln ("Invalid URI");
    return 1;
  }

  pipeline = gst_element_factory_make ("playbin3", NULL);
  if (pipeline == NULL) {
    gst_printerrln ("Failed to create playbin element");
    return 1;
  }

  g_object_set (pipeline, "uri", uri, NULL);
  g_free (uri);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, sync_msg_handler, NULL, NULL);
  gst_bus_add_watch (bus, msg_handler, pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);
  gst_object_unref (pipeline);

  g_main_loop_unref (loop);

  return 0;
}

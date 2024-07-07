/*
 * gsthcodec2json.c - Helpers for codec 2 json elments
 *
 * Copyright (C) 2023 Collabora
 *   Author: Benjamin Gaignard <benjamin.gaignard@collabora.com>
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
#include <config.h>
#endif

#include <gst/base/base.h>
#include <json-glib/json-glib.h>

#include "gstcodec2json.h"

static gchar *
gst_codec_2_json_get_string_from_json_object (JsonObject * object)
{
  JsonNode *root;
  JsonGenerator *generator;
  gchar *text;

  /* Make it the root node */
  root = json_node_init_object (json_node_alloc (), object);
  generator = json_generator_new ();
  json_generator_set_indent (generator, 2);
  json_generator_set_indent_char (generator, ' ');
  json_generator_set_pretty (generator, TRUE);
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, NULL);

  /* Release everything */
  g_object_unref (generator);
  json_node_free (root);
  return text;
}

GstFlowReturn
gst_codec_2_json_push_outbuffer (JsonObject * object, GstPad * srcpad)
{
  GstMapInfo out_map;
  GstBuffer *out_buf;
  gchar *json_string;
  guint length;

  json_string = gst_codec_2_json_get_string_from_json_object (object);
  /*
   * We got the string from the object release it to free all data
   * and allocate a empty one
   */
  json_object_unref (object);

  length = strlen (json_string);
  if (length <= 2)
    return GST_FLOW_OK;

  out_buf = gst_buffer_new_allocate (NULL, length, NULL);
  gst_buffer_map (out_buf, &out_map, GST_MAP_WRITE);
  if (length)
    memcpy (&out_map.data[0], json_string, length);
  gst_buffer_unmap (out_buf, &out_map);

  g_free (json_string);

  return gst_pad_push (srcpad, out_buf);
}

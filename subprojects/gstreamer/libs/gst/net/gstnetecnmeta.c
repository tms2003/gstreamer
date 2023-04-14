/* GStreamer
 * Copyright (C) <2022> British Broadcasting Corporation
 *   Author: Sam Hurst <sam.hurst@bbc.co.uk>
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
 * SECTION:gstnetecnmeta
 * @title: GstNetEcnMeta
 * @short_description: Explicit Congestion Notification metadata
 *
 * #GstNetEcnMeta can be used to specify whether congestion was encountered
 * by a network element when trying to deliver this buffer.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnetecnmeta.h"

GType
gst_net_ecn_cp_get_type (void)
{
  static gsize g_type = 0;
  static const GEnumValue net_ecn_cp_types[] = {
    {GST_NET_ECN_META_NO_ECN, "Non ECN-Capable Transport", "Non-ECT"},
    {GST_NET_ECN_META_ECT_0, "ECN Capable Transport (0)", "ECT-0"},
    {GST_NET_ECN_META_ECT_1, "ECN Capable Transport (1)", "ECT-1"},
    {GST_NET_ECN_META_ECT_CE, "Congestion Encountered", "CE"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&g_type)) {
    const GType type = g_enum_register_static ("GstNetEcnCp", net_ecn_cp_types);
    g_once_init_leave (&g_type, type);
  }

  return g_type;
}

static gboolean
net_ecn_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstNetEcnMeta *ecnmeta = (GstNetEcnMeta *) meta;

  ecnmeta->cp = GST_NET_ECN_META_NO_ECN;

  return TRUE;
}

static gboolean
net_ecn_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstNetEcnMeta *smeta, *dmeta;
  smeta = (GstNetEcnMeta *) meta;

  /* Always copy */
  dmeta = gst_buffer_add_net_ecn_meta (transbuf, smeta->cp);
  if (!dmeta)
    return FALSE;

  return TRUE;
}

static void
net_ecn_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  /* Nothing to free */
}

GType
gst_net_ecn_meta_api_get_type (void)
{
  static GType type;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstNetEcnMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_net_ecn_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_NET_ECN_META_API_TYPE,
        "GstNetEcnMeta",
        sizeof (GstNetEcnMeta),
        net_ecn_meta_init,
        net_ecn_meta_free,
        net_ecn_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) mi);
  }
  return meta_info;
}

/**
 * gst_buffer_add_net_ecn_meta:
 * @buffer a #GstBuffer
 * @cp: a @GstNetEcnCp ECN codepoint to connect to @buffer
 *
 * Attaches @cp as metadata in a #GstNetEcnMeta to @buffer.
 *
 * Returns: (transfer none): a #GstNetEcnMeta connected to @buffer
 *
 * Since: 1.24
 */
GstNetEcnMeta *
gst_buffer_add_net_ecn_meta (GstBuffer * buffer, GstNetEcnCp cp)
{
  GstNetEcnMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (cp >= GST_NET_ECN_META_NO_ECN, NULL);
  g_return_val_if_fail (cp <= GST_NET_ECN_META_ECT_CE, NULL);

  meta = (GstNetEcnMeta *) gst_buffer_add_meta (buffer,
      GST_NET_ECN_META_INFO, NULL);

  meta->cp = cp;

  return meta;
}

/**
 * gst_buffer_get_net_ecn_meta:
 * @buffer: a #GstBuffer
 *
 * Find the #GstNetEcnMeta on @buffer.
 *
 * Returns: (transfer none): the #GstNetEcnMeta or %NULL when there
 * is no such metadata on @buffer.
 *
 * Since: 1.24
 */
GstNetEcnMeta *
gst_buffer_get_net_ecn_meta (GstBuffer * buffer)
{
  return (GstNetEcnMeta *) gst_buffer_get_meta (buffer,
      GST_NET_ECN_META_API_TYPE);
}

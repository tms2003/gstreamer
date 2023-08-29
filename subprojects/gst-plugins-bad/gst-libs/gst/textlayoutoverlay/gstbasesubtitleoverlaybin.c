/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstbasesubtitleoverlaybin.h"
#include "gsttextlayoutoverlay-private.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (base_subtitle_overlay_bin_debug);
#define GST_CAT_DEFAULT base_subtitle_overlay_bin_debug

static GstStaticPadTemplate video_templ = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate text_templ = GST_STATIC_PAD_TEMPLATE ("text",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw, format = { pango-markup, utf8 }"));

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

struct _GstBaseSubtitleOverlayBinPrivate
{
  GMutex lock;

  GstElement *mux;
  GstElement *overlay;
  GstPad *text_pad;
  GstPad *mux_pad;
};

static GstBinClass *parent_class = NULL;
static gint private_offset = 0;

static void
gst_base_subtitle_overlay_bin_class_init (GstBaseSubtitleOverlayBinClass *
    klass);
static void
gst_base_subtitle_overlay_bin_init (GstBaseSubtitleOverlayBin * self,
    GstBaseSubtitleOverlayBinClass * klass);
static void gst_base_subtitle_overlay_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_subtitle_overlay_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstPadLinkReturn gst_base_subtitle_overlay_bin_text_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_base_subtitle_overlay_bin_text_unlink (GstPad * pad,
    GstObject * parent);
static gboolean gst_base_subtitle_overlay_bin_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

GType
gst_base_subtitle_overlay_bin_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstBaseSubtitleOverlayBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_subtitle_overlay_bin_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSubtitleOverlayBin),
      0,
      (GInstanceInitFunc) gst_base_subtitle_overlay_bin_init,
    };

    _type = g_type_register_static (GST_TYPE_BIN,
        "GstBaseSubtitleOverlayBin", &info, G_TYPE_FLAG_ABSTRACT);

    private_offset = g_type_add_instance_private (_type,
        sizeof (GstBaseSubtitleOverlayBinPrivate));

    g_once_init_leave (&type, _type);
  }
  return type;
}

static inline GstBaseSubtitleOverlayBinPrivate *
gst_base_subtitle_overlay_bin_get_instance_private (GstBaseSubtitleOverlayBin *
    self)
{
  return (G_STRUCT_MEMBER_P (self, private_offset));
}

static void
gst_base_subtitle_overlay_bin_class_init (GstBaseSubtitleOverlayBinClass *
    klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  guint n_props;

  parent_class = g_type_class_peek_parent (klass);
  if (private_offset != 0)
    g_type_class_adjust_private_offset (klass, &private_offset);

  object_class->set_property = gst_base_subtitle_overlay_bin_set_property;
  object_class->get_property = gst_base_subtitle_overlay_bin_get_property;

  gst_base_text_layout_overlay_install_properties (object_class, 0, &n_props);
  gst_base_subtitle_overlay_install_properties (object_class, n_props, NULL);

  gst_element_class_add_static_pad_template (element_class, &video_templ);
  gst_element_class_add_static_pad_template (element_class, &text_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);

  GST_DEBUG_CATEGORY_INIT (base_subtitle_overlay_bin_debug,
      "basesubtitleoverlaybin", 0, "basesubtitleoverlaybin");
}

static void
gst_base_subtitle_overlay_bin_init (GstBaseSubtitleOverlayBin * self,
    GstBaseSubtitleOverlayBinClass * klass)
{
  GstElement *elem = GST_ELEMENT_CAST (self);
  GstPad *gpad;
  GstPad *pad;
  GstPadTemplate *templ;
  GstBaseSubtitleOverlayBinPrivate *priv;
  const gchar *overlay_factory;

  self->priv = priv = gst_base_subtitle_overlay_bin_get_instance_private (self);

  g_assert (klass->get_overlay_factory);
  overlay_factory = klass->get_overlay_factory (self);

  priv->mux = gst_element_factory_make ("subtitlemux", "subtitle-mux");
  priv->overlay = gst_element_factory_make (overlay_factory,
      "subtitle-overlay");

  gst_bin_add_many (GST_BIN_CAST (self), priv->mux, priv->overlay, NULL);
  gst_element_link (priv->mux, priv->overlay);

  pad = gst_element_get_static_pad (priv->mux, "video");
  gpad = gst_ghost_pad_new ("video", pad);
  gst_object_unref (pad);
  gst_element_add_pad (elem, gpad);

  pad = gst_element_get_static_pad (priv->overlay, "src");
  gpad = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);
  gst_element_add_pad (elem, gpad);

  pad = GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (gpad)));
  gst_pad_set_event_function (pad, gst_base_subtitle_overlay_bin_src_event);
  gst_object_unref (pad);

  templ = gst_static_pad_template_get (&text_templ);
  priv->text_pad = gst_ghost_pad_new_no_target_from_template ("text", templ);
  gst_object_unref (templ);
  gst_element_add_pad (elem, priv->text_pad);

  GST_PAD_SET_ACCEPT_INTERSECT (priv->text_pad);
  GST_PAD_SET_ACCEPT_TEMPLATE (priv->text_pad);

  gst_pad_set_link_function (priv->text_pad,
      gst_base_subtitle_overlay_bin_text_link);
  gst_pad_set_unlink_function (priv->text_pad,
      gst_base_subtitle_overlay_bin_text_unlink);
}

static void
gst_base_subtitle_overlay_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlayBin *self = GST_BASE_SUBTITLE_OVERLAY_BIN (object);
  GstBaseSubtitleOverlayBinPrivate *priv = self->priv;

  g_object_set_property (G_OBJECT (priv->overlay), pspec->name, value);
}

static void
gst_base_subtitle_overlay_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlayBin *self = GST_BASE_SUBTITLE_OVERLAY_BIN (object);
  GstBaseSubtitleOverlayBinPrivate *priv = self->priv;

  g_object_get_property (G_OBJECT (priv->overlay), pspec->name, value);
}

static GstPadLinkReturn
gst_base_subtitle_overlay_bin_text_link (GstPad * pad, GstObject * parent,
    GstPad * peer)
{
  GstBaseSubtitleOverlayBin *self = GST_BASE_SUBTITLE_OVERLAY_BIN (parent);
  GstBaseSubtitleOverlayBinPrivate *priv = self->priv;
  GstPad *mux_pad;

  g_mutex_lock (&priv->lock);
  mux_pad = gst_element_request_pad_simple (priv->mux, "text_%u");
  if (!mux_pad) {
    GST_ERROR_OBJECT (self, "Couldn't get mux pad");
    g_mutex_unlock (&priv->lock);
    return GST_PAD_LINK_REFUSED;
  }

  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (priv->text_pad), mux_pad);
  gst_clear_object (&priv->mux_pad);
  priv->mux_pad = mux_pad;
  g_mutex_unlock (&priv->lock);

  GST_DEBUG_OBJECT (self, "Text pad linked");

  return GST_PAD_LINK_OK;
}

static void
gst_base_subtitle_overlay_bin_text_unlink (GstPad * pad, GstObject * parent)
{
  GstBaseSubtitleOverlayBin *self = GST_BASE_SUBTITLE_OVERLAY_BIN (parent);
  GstBaseSubtitleOverlayBinPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);

  /* We cannot clear target on unlink function, since unlink function is
   * called with GST_OBJECT_LOCK and get/set target will take the lock
   * as well. Let ghostpad hold old target but it's fine */
  if (!priv->mux_pad) {
    GST_WARNING_OBJECT (self, "No linked mux pad");
  } else {
    GST_DEBUG_OBJECT (self, "Unlinking text pad");
    gst_element_release_request_pad (priv->mux, priv->mux_pad);
    gst_clear_object (&priv->mux_pad);
  }

  g_mutex_unlock (&priv->lock);
}

static gboolean
gst_base_subtitle_overlay_bin_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  guint32 seqnum;

  /* subtitleoverlay elements will drop flush event if it was passed to text pad
   * based on the pango element's behavior, it should be dropped since
   * aggregator will forward the same flush event to text pad as well.
   * Replace flush event with ours */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      seqnum = gst_event_get_seqnum (event);
      gst_event_unref (event);
      event = gst_event_new_flush_start ();
      gst_event_set_seqnum (event, seqnum);
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      gboolean reset;
      gst_event_parse_flush_stop (event, &reset);
      seqnum = gst_event_get_seqnum (event);
      gst_event_unref (event);
      event = gst_event_new_flush_stop (reset);
      gst_event_set_seqnum (event, seqnum);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

/**
 * gst_base_subtitle_overlay_bin_get_overlay:
 * @overlay: a #GstBaseSubtitleOverlayBin
 *
 * Gets child text overlay elements
 *
 * Returns: (transfer full): the child overlay element
 */
GstElement *
gst_base_subtitle_overlay_bin_get_overlay (GstBaseSubtitleOverlayBin * overlay)
{
  GstElement *elem = NULL;

  g_return_val_if_fail (GST_IS_BASE_SUBTITLE_OVERLAY_BIN (overlay), NULL);

  if (overlay->priv->overlay)
    elem = gst_object_ref (overlay->priv->overlay);

  return elem;
}

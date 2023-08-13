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

#include "gstdwriteclockoverlay.h"
#include "gstdwriteoverlayobject.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (dwrite_clock_overlay_debug);
#define GST_CAT_DEFAULT dwrite_clock_overlay_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

enum
{
  PROP_0,
  PROP_COLOR_FONT,
};

#define DEFAULT_COLOR_FONT TRUE

struct GstDWriteClockOverlayPrivate
{
  GstDWriteClockOverlayPrivate ()
  {
    overlay = gst_dwrite_overlay_object_new ();
  }

   ~GstDWriteClockOverlayPrivate ()
  {
    gst_object_unref (overlay);
  }

  GstDWriteOverlayObject *overlay;
  GstDWriteBlendMode blend_mode = GstDWriteBlendMode::NOT_SUPPORTED;

  std::mutex prop_lock;
  gboolean color_font = DEFAULT_COLOR_FONT;
};

struct _GstDWriteClockOverlay
{
  GstBaseClockOverlay parent;

  GstDWriteClockOverlayPrivate *priv;
};

static void gst_dwrite_clock_overlay_finalize (GObject * object);
static void gst_dwrite_clock_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dwrite_clock_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_dwrite_clock_overlay_set_context (GstElement * elem,
    GstContext * context);
static gboolean gst_dwrite_clock_overlay_start (GstBaseTransform * trans);
static gboolean gst_dwrite_clock_overlay_stop (GstBaseTransform * trans);
static gboolean gst_dwrite_clock_overlay_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean
gst_dwrite_clock_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean
gst_dwrite_clock_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static void gst_dwrite_clock_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean
gst_dwrite_clock_overlay_set_info (GstBaseTextLayoutOverlay * overlay,
    GstCaps * in_caps, const GstVideoInfo * in_info,
    GstCaps * out_caps, const GstVideoInfo * out_info);
static GstFlowReturn
gst_dwrite_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout);
static GstFlowReturn
gst_dwrite_clock_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf);

#define gst_dwrite_clock_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDWriteClockOverlay, gst_dwrite_clock_overlay,
    GST_TYPE_BASE_CLOCK_OVERLAY);

static void
gst_dwrite_clock_overlay_class_init (GstDWriteClockOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstBaseTextLayoutOverlayClass *overlay_class =
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (klass);

  object_class->finalize = gst_dwrite_clock_overlay_finalize;
  object_class->set_property = gst_dwrite_clock_overlay_set_property;
  object_class->get_property = gst_dwrite_clock_overlay_get_property;

#ifdef HAVE_DWRITE_COLOR_FONT
  if (gst_dwrite_is_windows_10_or_greater ()) {
    g_object_class_install_property (object_class, PROP_COLOR_FONT,
        g_param_spec_boolean ("color-font", "Color Font",
            "Enable color font, requires Windows 10 or newer",
            DEFAULT_COLOR_FONT,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }
#endif

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "DirectWrite Clock Overlay", "Filter/Editor/Video",
      "Overlays the current clock time on a video stream",
      "Seungha Yang <seungha@centricular.com>");

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_set_context);

  trans_class->start = GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_query);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_decide_allocation);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_propose_allocation);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_before_transform);

  overlay_class->set_info =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_set_info);
  overlay_class->generate_layout =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_generate_layout);
  overlay_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_dwrite_clock_overlay_generate_output);

  GST_DEBUG_CATEGORY_INIT (dwrite_clock_overlay_debug,
      "dwriteclockoverlay", 0, "dwriteclockoverlay");
}

static void
gst_dwrite_clock_overlay_init (GstDWriteClockOverlay * self)
{
  self->priv = new GstDWriteClockOverlayPrivate ();
  g_object_set (self, "font-family", "MS Reference Sans Serif", nullptr);
}

static void
gst_dwrite_clock_overlay_finalize (GObject * object)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dwrite_clock_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (object);
  GstDWriteClockOverlayPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  switch (prop_id) {
    case PROP_COLOR_FONT:
      priv->color_font = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dwrite_clock_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (object);
  GstDWriteClockOverlayPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  switch (prop_id) {
    case PROP_COLOR_FONT:
      g_value_set_boolean (value, priv->color_font);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dwrite_clock_overlay_set_context (GstElement * elem, GstContext * context)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (elem);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  gst_dwrite_overlay_object_set_context (priv->overlay, elem, context);

  GST_ELEMENT_CLASS (parent_class)->set_context (elem, context);
}

static gboolean
gst_dwrite_clock_overlay_start (GstBaseTransform * trans)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (!gst_dwrite_overlay_object_start (priv->overlay))
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_dwrite_clock_overlay_stop (GstBaseTransform * trans)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  gst_dwrite_overlay_object_stop (priv->overlay);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_dwrite_clock_overlay_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (gst_dwrite_overlay_object_handle_query (priv->overlay,
          GST_ELEMENT (self), query)) {
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans,
      direction, query);
}

static gboolean
gst_dwrite_clock_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (!gst_dwrite_overlay_object_decide_allocation (priv->overlay,
          GST_ELEMENT (self), query)) {
    return FALSE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_dwrite_clock_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Propose allocation");

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  if (!decide_query) {
    GST_DEBUG_OBJECT (self, "Passthrough");
    return TRUE;
  }

  ret = gst_pad_peer_query (trans->srcpad, query);
  if (!ret)
    return FALSE;

  return gst_dwrite_overlay_object_propose_allocation (priv->overlay,
      GST_ELEMENT (self), query);
}

static void
gst_dwrite_clock_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buf)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (trans);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (gst_dwrite_overlay_object_update_device (priv->overlay, buf))
    gst_base_transform_reconfigure (trans);
}

static gboolean
gst_dwrite_clock_overlay_set_info (GstBaseTextLayoutOverlay * overlay,
    GstCaps * in_caps, const GstVideoInfo * in_info,
    GstCaps * out_caps, const GstVideoInfo * out_info)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (overlay);
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (overlay);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (!gst_dwrite_overlay_object_set_caps (priv->overlay,
          GST_ELEMENT (self), out_caps, &priv->blend_mode)) {
    GST_ERROR_OBJECT (self, "Set caps failed");
    return FALSE;
  }

  if (priv->blend_mode == GstDWriteBlendMode::NOT_SUPPORTED)
    gst_base_transform_set_passthrough (trans, TRUE);
  else
    gst_base_transform_set_passthrough (trans, FALSE);

  return TRUE;
}

static GstFlowReturn
gst_dwrite_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (overlay);
  GstDWriteClockOverlayPrivate *priv = self->priv;

  if (priv->blend_mode == GstDWriteBlendMode::NOT_SUPPORTED) {
    *layout = nullptr;
    return GST_FLOW_OK;
  }

  return
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (parent_class)->generate_layout
      (overlay, text, buffer, layout);
}

static GstFlowReturn
gst_dwrite_clock_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf)
{
  GstDWriteClockOverlay *self = GST_DWRITE_CLOCK_OVERLAY (overlay);
  GstDWriteClockOverlayPrivate *priv = self->priv;
  GstFlowReturn ret;

  g_assert (priv->blend_mode != GstDWriteBlendMode::NOT_SUPPORTED);

  ret = gst_dwrite_overlay_object_prepare_output (priv->overlay,
      GST_BASE_TRANSFORM (self), parent_class, in_buf, out_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_dwrite_overlay_object_draw (priv->overlay,
      layout, priv->color_font, *out_buf);

  if (ret != GST_FLOW_OK)
    return ret;

  return
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (parent_class)->generate_output
      (overlay, layout, in_buf, out_buf);
}

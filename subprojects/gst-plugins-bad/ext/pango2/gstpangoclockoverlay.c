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

#include "gstpangoclockoverlay.h"
#include "gstpangooverlayobject.h"

GST_DEBUG_CATEGORY_STATIC (pango_clock_overlay_debug);
#define GST_CAT_DEFAULT pango_clock_overlay_debug

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

struct _GstPangoClockOverlay
{
  GstBaseClockOverlay parent;

  GstPangoOverlayObject *overlay;
  gboolean supported;
};

static void gst_pango_clock_overlay_finalize (GObject * object);
static gboolean gst_pango_clock_overlay_start (GstBaseTransform * trans);
static gboolean gst_pango_clock_overlay_stop (GstBaseTransform * trans);
static gboolean
gst_pango_clock_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean
gst_pango_clock_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_pango_clock_overlay_set_info (GstBaseTextLayoutOverlay * overlay,
    GstCaps * in_caps, const GstVideoInfo * in_info,
    GstCaps * out_caps, const GstVideoInfo * out_info);
static GstFlowReturn
gst_pango_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout);
static gboolean
gst_pango_clock_overlay_accept_attribute (GstBaseTextLayoutOverlay * overlay,
    GstTextAttr * attr);
static GstFlowReturn
gst_pango_clock_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf);

#define gst_pango_clock_overlay_parent_class parent_class
G_DEFINE_TYPE (GstPangoClockOverlay, gst_pango_clock_overlay,
    GST_TYPE_BASE_CLOCK_OVERLAY);

static void
gst_pango_clock_overlay_class_init (GstPangoClockOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstBaseTextLayoutOverlayClass *overlay_class =
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (klass);

  object_class->finalize = gst_pango_clock_overlay_finalize;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Pango Clock Overlay", "Filter/Editor/Video",
      "Overlays the current clock time on a video stream",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->start = GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_stop);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_decide_allocation);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_propose_allocation);

  overlay_class->set_info =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_set_info);
  overlay_class->generate_layout =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_generate_layout);
  overlay_class->accept_attribute =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_accept_attribute);
  overlay_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_pango_clock_overlay_generate_output);

  GST_DEBUG_CATEGORY_INIT (pango_clock_overlay_debug,
      "pangoclockoverlay", 0, "pangoclockoverlay");
}

static void
gst_pango_clock_overlay_init (GstPangoClockOverlay * self)
{
  self->overlay = gst_pango_overlay_object_new ();
  g_object_set (self, "font-family", "Monospace", NULL);
}

static void
gst_pango_clock_overlay_finalize (GObject * object)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (object);

  gst_object_unref (self->overlay);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_pango_clock_overlay_start (GstBaseTransform * trans)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (trans);

  if (!gst_pango_overlay_object_start (self->overlay))
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_pango_clock_overlay_stop (GstBaseTransform * trans)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (trans);

  gst_pango_overlay_object_stop (self->overlay);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_pango_clock_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (trans);

  if (!gst_pango_overlay_object_decide_allocation (self->overlay,
          GST_ELEMENT (self), query)) {
    return FALSE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_pango_clock_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  if (!decide_query)
    return TRUE;

  return gst_pad_peer_query (trans->srcpad, query);
}

static gboolean
gst_pango_clock_overlay_set_info (GstBaseTextLayoutOverlay * overlay,
    GstCaps * in_caps, const GstVideoInfo * in_info,
    GstCaps * out_caps, const GstVideoInfo * out_info)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (overlay);
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (overlay);

  if (!gst_pango_overlay_object_set_caps (self->overlay,
          GST_ELEMENT (self), out_caps, &self->supported)) {
    GST_ERROR_OBJECT (self, "Set caps failed");
    return FALSE;
  }

  if (!self->supported)
    gst_base_transform_set_passthrough (trans, TRUE);
  else
    gst_base_transform_set_passthrough (trans, FALSE);

  return TRUE;
}

static GstFlowReturn
gst_pango_clock_overlay_generate_layout (GstBaseTextLayoutOverlay * overlay,
    const gchar * text, GstBuffer * buffer, GstTextLayout ** layout)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (overlay);

  if (!self->supported) {
    *layout = NULL;
    return GST_FLOW_OK;
  }

  return
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (parent_class)->generate_layout
      (overlay, text, buffer, layout);
}

static gboolean
gst_pango_clock_overlay_accept_attribute (GstBaseTextLayoutOverlay * overlay,
    GstTextAttr * attr)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (overlay);

  return gst_pango_overlay_object_accept_attribute (self->overlay, attr);
}

static GstFlowReturn
gst_pango_clock_overlay_generate_output (GstBaseTextLayoutOverlay * overlay,
    GstTextLayout * layout, GstBuffer * in_buf, GstBuffer ** out_buf)
{
  GstPangoClockOverlay *self = GST_PANGO_CLOCK_OVERLAY (overlay);
  GstFlowReturn ret;

  g_assert (self->supported);

  if (gst_buffer_is_writable (in_buf))
    *out_buf = in_buf;
  else
    *out_buf = gst_buffer_copy (in_buf);

  ret = gst_pango_overlay_object_draw (self->overlay, layout, *out_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  return
      GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS (parent_class)->generate_output
      (overlay, layout, in_buf, out_buf);
}

/* GStreamer
 * Copyright (C) 2021 Vivienne Watermeier <vwatermeier@igalia.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstocio
 *
 * Does color management using OpenColorIO.
 *
 * ## Example
 * |[
 * gst-launch-1.0 gst-launch-1.0 videotestsrc ! video/x-raw,format=RGB ! ociofilter env=/path/to/ocio/config src_color=colorspace_from dest_color=colorspace_to ! videoconvert ! autovideosink
 * ]|
 * Converts the video from one colorspace to another.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "gstocio.h"
#include <OpenColorIO/OpenColorIO.h>

namespace OCIO = OCIO_NAMESPACE;

GST_DEBUG_CATEGORY_STATIC (gst_ocio_debug_category);
#define GST_CAT_DEFAULT gst_ocio_debug_category

/* prototypes */


static void gst_ocio_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_ocio_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_ocio_dispose (GObject * object);
static void gst_ocio_finalize (GObject * object);

static gboolean gst_ocio_start (GstBaseTransform * trans);
static gboolean gst_ocio_stop (GstBaseTransform * trans);
static gboolean gst_ocio_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info);
static GstStateChangeReturn gst_ocio_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_ocio_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe);
static GstFlowReturn gst_ocio_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);

enum
{
  PROP_0,
  PROP_ENV,
  PROP_SRC_COLOR,
  PROP_DEST_COLOR,
};

/* pad templates */

static GstStaticPadTemplate gst_ocio_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-raw, "
      "formats = "
        "RGB"
  )
);

static GstStaticPadTemplate gst_ocio_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-raw, "
      "formats = "
        "RGB"
  )
);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstOcio, gst_ocio, GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_ocio_debug_category, "ociofilter", 0,
        "debug category for ociofilter element"));

static void
gst_ocio_class_init (GstOcioClass * klass)
{
  GstElementClass * element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_ocio_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_ocio_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "OpenColorIO",
      "Filter/Converter/Video",
      "Color management using OpenColorIO.",
      "Vivienne Watermeier <vwatermeier@igalia.com>");

  element_class->change_state = gst_ocio_change_state;
  gobject_class->set_property = gst_ocio_set_property;
  gobject_class->get_property = gst_ocio_get_property;
  gobject_class->dispose = gst_ocio_dispose;
  gobject_class->finalize = gst_ocio_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_ocio_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_ocio_stop);
  video_filter_class->set_info = GST_DEBUG_FUNCPTR (gst_ocio_set_info);
  video_filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_ocio_transform_frame);
  video_filter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_ocio_transform_frame_ip);

  /**
   * GstOcio:env:
   *
   * Path to the OCIO config file, is read from $OCIO by default.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_ENV,
      g_param_spec_string("env", "Environment",
          "Path to the OCIO config file, is read from $OCIO by default.",
          NULL, G_PARAM_READWRITE));

  /**
   * GstOcio:in_color:
   *
   * The colorspace used by the incoming video data.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_SRC_COLOR,
      g_param_spec_string("src_color", "Source Colorspace",
          "The colorspace used by the incoming video data.",
          NULL, G_PARAM_READWRITE));

  /**
   * GstOcio:out_color:
   *
   * The colorspace to convert the video data into.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_DEST_COLOR,
      g_param_spec_string("dest_color", "Destination Colorspace",
          "The colorspace to convert the video data into.",
          NULL, G_PARAM_READWRITE));
}

static void
gst_ocio_init (GstOcio * ocio)
{
  GST_DEBUG_OBJECT (ocio, "init");

  ocio->sinkpad = GST_BASE_TRANSFORM (ocio)->sinkpad;
  ocio->srcpad = GST_BASE_TRANSFORM (ocio)->srcpad;
  
  GST_PAD_SET_PROXY_CAPS (ocio->sinkpad);
  GST_PAD_SET_PROXY_CAPS (ocio->srcpad);
}

void
gst_ocio_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOcio *ocio = GST_OCIO (object);

  GST_DEBUG_OBJECT (ocio, "set_property");

  switch (property_id) {
    case PROP_ENV:
      g_free ((gpointer) ocio->env);
      ocio->env = g_value_dup_string (value);
      break;
    
    case PROP_SRC_COLOR:
      g_free ((gpointer) ocio->src_color);
      ocio->src_color = g_value_dup_string (value);
      break;

    case PROP_DEST_COLOR:
      g_free ((gpointer) ocio->dest_color);
      ocio->dest_color = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_ocio_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstOcio *ocio = GST_OCIO (object);

  GST_DEBUG_OBJECT (ocio, "get_property");

  switch (property_id) {
    case PROP_ENV:
      g_value_set_string (value, ocio->env);
      break;

    case PROP_SRC_COLOR:
      g_value_set_string (value, ocio->src_color);
      break;

    case PROP_DEST_COLOR:
      g_value_set_string (value, ocio->dest_color);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_ocio_dispose (GObject * object)
{
  GstOcio *ocio = GST_OCIO (object);

  GST_DEBUG_OBJECT (ocio, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_ocio_parent_class)->dispose (object);
}

void
gst_ocio_finalize (GObject * object)
{
  GstOcio *ocio = GST_OCIO (object);

  GST_DEBUG_OBJECT (ocio, "finalize");

  /* clean up object here */
  ocio->cpu.reset();
  ocio->processor.reset();
  ocio->config.reset();
  g_free ((gpointer) ocio->env);
  g_free ((gpointer) ocio->src_color);
  g_free ((gpointer) ocio->dest_color);
  
  G_OBJECT_CLASS (gst_ocio_parent_class)->finalize (object);
}

static gboolean
gst_ocio_start (GstBaseTransform * trans)
{
  GstOcio *ocio = GST_OCIO (trans);

  GST_DEBUG_OBJECT (ocio, "start");

  return TRUE;
}

static gboolean
gst_ocio_stop (GstBaseTransform * trans)
{
  GstOcio *ocio = GST_OCIO (trans);

  GST_DEBUG_OBJECT (ocio, "stop");

  return TRUE;
}

static gboolean
gst_ocio_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstOcio *ocio = GST_OCIO (filter);

  GST_DEBUG_OBJECT (ocio, "set_info");

  return TRUE;
}

static GstStateChangeReturn
gst_ocio_change_state (GstElement *element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOcio *ocio = GST_OCIO (element);
  const gchar *msg;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (ocio->env == NULL)
        ocio->env = OCIO::GetEnvVariable ("OCIO");

      if (g_strcmp0 (ocio->env, "") <= 0) {
        msg = "error: 'env' missing, cannot find OCIO configuration file.";
        GST_ERROR_OBJECT (ocio, "%s", msg);
        GST_ELEMENT_ERROR (element, LIBRARY, INIT,
            (NULL), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }

      if (g_strcmp0 (ocio->src_color, "") <= 0
          || g_strcmp0 (ocio->dest_color, "") <= 0)
      {
        msg = "error: missing colorspace";
        GST_ERROR_OBJECT (ocio, "%s", msg);
        GST_ELEMENT_ERROR (element, LIBRARY, INIT,
            (NULL), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }

      ocio->config = OCIO::Config::CreateFromFile (ocio->env);
      ocio->processor = ocio->config->getProcessor (ocio->src_color, ocio->dest_color);
      ocio->cpu = ocio->processor->getOptimizedCPUProcessor (
          OCIO::BIT_DEPTH_UINT8, OCIO::BIT_DEPTH_UINT8, OCIO::OPTIMIZATION_DEFAULT);
      break;
 
    default:
      break;
  }
  
  ret = GST_ELEMENT_CLASS (gst_ocio_parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;
  
  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      ocio->cpu = NULL;
      ocio->processor = NULL;
      ocio->config = NULL;
      break;

    default:
      break;
  }
  
  return ret;
}

/* transform */
static GstFlowReturn
gst_ocio_transform_frame (GstVideoFilter * filter, GstVideoFrame * inframe,
    GstVideoFrame * outframe)
{
  if (!gst_video_frame_copy (outframe, inframe))
    return GST_FLOW_ERROR;

  return gst_ocio_transform_frame_ip (filter, outframe);
}

static OCIO::BitDepth
ocio_bit_depth (guint32 depth)
{
  switch (depth)
  {
    case 8:
      return OCIO::BIT_DEPTH_UINT8;
    case 10:
      return OCIO::BIT_DEPTH_UINT10;
    case 12:
      return OCIO::BIT_DEPTH_UINT12;
    case 14:
      return OCIO::BIT_DEPTH_UINT14;
    case 16:
      return OCIO::BIT_DEPTH_UINT16;
    case 32:
      return OCIO::BIT_DEPTH_UINT32;
    default:
      return OCIO::BIT_DEPTH_UNKNOWN;
  }
}

static GstFlowReturn
gst_ocio_transform_frame_ip (GstVideoFilter * filter, GstVideoFrame * frame)
{
  GstOcio *ocio = GST_OCIO (filter);
  guint32 bitdepth = GST_VIDEO_FRAME_COMP_DEPTH (frame, 0);
  guint32 width = GST_VIDEO_FRAME_COMP_WIDTH (frame, 0);
  guint32 height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
  
  guint32 channel_stride = bitdepth / 8;
  guint32 x_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (frame, 0);
  guint32 y_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);

  OCIO::PackedImageDesc img (
      GST_VIDEO_FRAME_COMP_DATA (frame, 0),
      width,
      height,
      OCIO::CHANNEL_ORDERING_RGB,
      ocio_bit_depth (bitdepth),
      channel_stride, x_stride, y_stride
  );
  ocio->cpu->apply(img);

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "ociofilter", GST_RANK_NONE, GST_TYPE_OCIO);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ocio,
    "Color management using OpenColorIO.",
    plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

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

#include "gstdwritesubtitleoverlay.h"
#include "gstdwrite-utils.h"

GST_DEBUG_CATEGORY_STATIC (dwrite_subtitle_overlay_debug);
#define GST_CAT_DEFAULT dwrite_subtitle_overlay_debug

enum
{
  PROP_0,
  /* dwritetextoverlay */
  PROP_COLOR_FONT,
};

#define DEFAULT_COLOR_FONT TRUE

struct _GstDWriteSubtitleOverlay
{
  GstBaseSubtitleOverlayBin parent;
};

#ifdef HAVE_DWRITE_COLOR_FONT
static void
gst_dwrite_subtitle_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlayBin *bin = GST_BASE_SUBTITLE_OVERLAY_BIN (object);

  switch (prop_id) {
    case PROP_COLOR_FONT:
    {
      GstElement *overlay = gst_base_subtitle_overlay_bin_get_overlay (bin);
      g_object_set_property (G_OBJECT (overlay), pspec->name, value);
      gst_object_unref (overlay);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dwrite_subtitle_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseSubtitleOverlayBin *bin = GST_BASE_SUBTITLE_OVERLAY_BIN (object);

  switch (prop_id) {
    case PROP_COLOR_FONT:
    {
      GstElement *overlay = gst_base_subtitle_overlay_bin_get_overlay (bin);
      g_object_get_property (G_OBJECT (overlay), pspec->name, value);
      gst_object_unref (overlay);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
#endif

static const gchar
    * gst_dwrite_subtitle_overlay_get_overlay_factory (GstBaseSubtitleOverlayBin
    * overlay);

#define gst_dwrite_subtitle_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDWriteSubtitleOverlay, gst_dwrite_subtitle_overlay,
    GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN);

static void
gst_dwrite_subtitle_overlay_class_init (GstDWriteSubtitleOverlayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSubtitleOverlayBinClass *overlay_class =
      GST_BASE_SUBTITLE_OVERLAY_BIN_CLASS (klass);

#ifdef HAVE_DWRITE_COLOR_FONT
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_dwrite_subtitle_overlay_set_property;
  object_class->get_property = gst_dwrite_subtitle_overlay_get_property;

  if (gst_dwrite_is_windows_10_or_greater ()) {
    g_object_class_install_property (object_class, PROP_COLOR_FONT,
        g_param_spec_boolean ("color-font", "Color Font",
            "Enable color font, requires Windows 10 or newer",
            DEFAULT_COLOR_FONT,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }
#endif

  gst_element_class_set_static_metadata (element_class,
      "DirectWrite Subtitle Overlay",
      "Filter/Editor/Video/Overlay/Subtitle",
      "Adds subtitle strings on top of a video buffer",
      "Seungha Yang <seungha@centricular.com>");

  overlay_class->get_overlay_factory =
      GST_DEBUG_FUNCPTR (gst_dwrite_subtitle_overlay_get_overlay_factory);

  GST_DEBUG_CATEGORY_INIT (dwrite_subtitle_overlay_debug,
      "dwritesubtitleoverlay", 0, "dwritesubtitleoverlay");
}

static void
gst_dwrite_subtitle_overlay_init (GstDWriteSubtitleOverlay * self)
{
}

static const gchar *
gst_dwrite_subtitle_overlay_get_overlay_factory (GstBaseSubtitleOverlayBin *
    overlay)
{
  return "dwritetextoverlay";
}

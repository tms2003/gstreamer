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

#include "gstpangosubtitleoverlay.h"

GST_DEBUG_CATEGORY_STATIC (pango_subtitle_overlay_debug);
#define GST_CAT_DEFAULT pango_subtitle_overlay_debug

struct _GstPangoSubtitleOverlay
{
  GstBaseSubtitleOverlayBin parent;
};

/* *INDENT-OFF* */
static const gchar *
gst_pango_subtitle_overlay_get_overlay_factory (GstBaseSubtitleOverlayBin *
    overlay);
/* *INDENT-ON* */

#define gst_pango_subtitle_overlay_parent_class parent_class
G_DEFINE_TYPE (GstPangoSubtitleOverlay, gst_pango_subtitle_overlay,
    GST_TYPE_BASE_SUBTITLE_OVERLAY_BIN);

static void
gst_pango_subtitle_overlay_class_init (GstPangoSubtitleOverlayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSubtitleOverlayBinClass *overlay_class =
      GST_BASE_SUBTITLE_OVERLAY_BIN_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Pango Subtitle Overlay",
      "Filter/Editor/Video/Overlay/Subtitle",
      "Adds subtitle strings on top of a video buffer",
      "Seungha Yang <seungha@centricular.com>");

  overlay_class->get_overlay_factory =
      GST_DEBUG_FUNCPTR (gst_pango_subtitle_overlay_get_overlay_factory);

  GST_DEBUG_CATEGORY_INIT (pango_subtitle_overlay_debug,
      "pangosubtitleoverlay", 0, "pangosubtitleoverlay");
}

static void
gst_pango_subtitle_overlay_init (GstPangoSubtitleOverlay * self)
{
}

static const gchar *
gst_pango_subtitle_overlay_get_overlay_factory (GstBaseSubtitleOverlayBin *
    overlay)
{
  return "pangotextoverlay";
}

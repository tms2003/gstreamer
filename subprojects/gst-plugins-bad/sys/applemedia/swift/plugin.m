/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

#include "../corevideomemory.h"
#include "sckitaudiosrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  gst_apple_core_video_memory_init ();

#ifdef HAVE_SCREENCAPTUREKIT
  /* SCKit audio capture is 13.0+ */
  if (@available(macOS 13.0, *)) {
    /* TODO: Make sure Swift concurrency is loaded! */
    ret &= gst_element_register (plugin, "sckitaudiosrc", GST_RANK_PRIMARY,
        GST_TYPE_SCKIT_AUDIO_SRC);
  }
#endif

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    applemediaswift,
    "TODO: description",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
/* GStreamer
 * Copyright (C) 2024 Benjamin Desef <projekter-git@yahoo.de>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstkmscompositor.h"
#include "gstkmssink.h"

GST_DEBUG_CATEGORY_STATIC (gst_kms_compositor_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_kms_compositor_debug, "kmscompositor", 0,
      "kmscompositor");

  if (!GST_ELEMENT_REGISTER (kmscompositor, plugin))
    return FALSE;

  if (!GST_ELEMENT_REGISTER (kmssink, plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    kms,
    "KMS plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

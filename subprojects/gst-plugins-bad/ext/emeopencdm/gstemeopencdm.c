/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2022, 2023 Collabora Ltd.
 *   @author Jordan Yelloz <jordan.yelloz@collabora.com>
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
 * plugin-emeopencdm:
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstplugin.h>

#include "gstemeopencdmlogging.h"
#include "gstopencdmprotectionsystem.h"
#include "gstopencdmdecryptor.h"

/**
 * SECTION:element-emeopencdmprotectionsystem
 * @title: emeopencdmprotectionsystem
 *
 * Since: 1.24
 */
GST_ELEMENT_REGISTER_DECLARE (opencdmprotectionsystem);
GST_ELEMENT_REGISTER_DEFINE (opencdmprotectionsystem,
    "emeopencdmprotectionsystem", GST_RANK_NONE,
    GST_TYPE_OPEN_CDM_PROTECTION_SYSTEM);

/**
 * SECTION:element-emeopencdmdecryptor
 * @title: emeopencdmdecryptor
 *
 * Since: 1.24
 */
GST_ELEMENT_REGISTER_DECLARE (opencdmdecryptor);
GST_ELEMENT_REGISTER_DEFINE (opencdmdecryptor, "emeopencdmdecryptor",
    GST_RANK_MARGINAL, GST_TYPE_OPEN_CDM_DECRYPTOR);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean result = FALSE;

  gst_emeopencdm_init_logging ();

  result |= GST_ELEMENT_REGISTER (opencdmprotectionsystem, plugin);
  result |= GST_ELEMENT_REGISTER (opencdmdecryptor, plugin);

  return result;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    emeopencdm,
    "OpenCDM Encrypted Media Extensions Support",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

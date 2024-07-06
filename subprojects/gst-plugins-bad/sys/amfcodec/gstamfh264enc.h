/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include "gstamfencoder.h"
#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#else
#include "gst/vulkan/vulkan_fwd.h"
#endif

#ifdef G_OS_WIN32
typedef GstD3D11Device GST_AMF_PLATFORM_DEVICE;
#else
typedef GstVulkanDevice GST_AMF_PLATFORM_DEVICE;
#endif // G_OS_WIN32


G_BEGIN_DECLS

void gst_amf_h264_enc_register (GstPlugin * plugin,
                                      GST_AMF_PLATFORM_DEVICE * device,
                                      gpointer context,
                                      guint rank);

G_END_DECLS

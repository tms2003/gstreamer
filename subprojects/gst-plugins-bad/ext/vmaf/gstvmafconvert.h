/* VMAF plugin
 * Copyright (C) 2021 Hudl
 *   @author: Casey Bateman <Casey.Bateman@hudl.com>
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
#ifndef __GST_VMAFCONVERT_H__
#define __GST_VMAFCONVERT_H__

#include <libvmaf.h>
#include <gst/gst.h>
#include "gstvmafenums.h"

G_BEGIN_DECLS 
enum VmafOutputFormat vmaf_map_log_fmt (GstVmafLogFormats log_fmt);
gint vmaf_map_pix_fmt (const char *fmt);
gint vmaf_map_bit_depth (const char *fmt);
enum VmafPoolingMethod vmaf_map_pooling_method(GstVmafPoolMethodEnum pool_method);
void fill_vmaf_picture_buffer (float *src, VmafPicture * dst, unsigned width, unsigned height, int src_stride);
void fill_vmaf_picture_buffer_hbd (float *src, VmafPicture * dst, unsigned width, unsigned height, int src_stride, unsigned bpc);

G_END_DECLS
#endif /* __GST_VMAFCONVERT_H__ */

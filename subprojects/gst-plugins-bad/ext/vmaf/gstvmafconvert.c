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
#include "gstvmafconvert.h"
#include "gstvmafenums.h"

enum VmafOutputFormat
vmaf_map_log_fmt (GstVmafLogFormats log_fmt)
{
  if (log_fmt == OUTPUT_FORMAT_CSV)
    return VMAF_OUTPUT_FORMAT_CSV;
  if (log_fmt == OUTPUT_FORMAT_XML)
    return VMAF_OUTPUT_FORMAT_XML;
  if (log_fmt == OUTPUT_FORMAT_JSON)
    return VMAF_OUTPUT_FORMAT_JSON;
  return VMAF_OUTPUT_FORMAT_NONE;
}

gint
vmaf_map_pix_fmt (const char *fmt)
{
  if (fmt) {
    if (!strcmp (fmt, "I420"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "NV12"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "YV12"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "Y42B"))
      return VMAF_PIX_FMT_YUV422P;
    if (!strcmp (fmt, "Y444"))
      return VMAF_PIX_FMT_YUV444P;
    if (!strcmp (fmt, "I420_10LE"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "I422_10LE"))
      return VMAF_PIX_FMT_YUV422P;
    if (!strcmp (fmt, "Y444_10LE"))
      return VMAF_PIX_FMT_YUV444P;
  }
  return VMAF_PIX_FMT_UNKNOWN;
}

gint
vmaf_map_bit_depth (const char *fmt)
{
  if (fmt) {
    if (!strcmp (fmt, "I420_10LE"))
      return 10;
    if (!strcmp (fmt, "I422_10LE"))
      return 10;
    if (!strcmp (fmt, "Y444_10LE"))
      return 10;
  }
  return 8;
}

enum VmafPoolingMethod
vmaf_map_pooling_method (GstVmafPoolMethodEnum pool_method)
{
  if (pool_method == POOL_METHOD_MAX)
    return VMAF_POOL_METHOD_MAX;
  if (pool_method == POOL_METHOD_MIN)
    return VMAF_POOL_METHOD_MIN;
  if (pool_method == POOL_METHOD_MEAN)
    return VMAF_POOL_METHOD_MEAN;
  if (pool_method == POOL_METHOD_HARMONIC_MEAN)
    return VMAF_POOL_METHOD_HARMONIC_MEAN;
  return VMAF_POOL_METHOD_UNKNOWN;
}

void
fill_vmaf_picture_buffer (float *src, VmafPicture * dst, unsigned width,
    unsigned height, int src_stride)
{
  float *a = src;
  uint8_t *b = dst->data[0];
  for (unsigned i = 0; i < height; i++) {
    for (unsigned j = 0; j < width; j++) {
      b[j] = a[j];
    }
    a += src_stride / sizeof (float);
    b += dst->stride[0];
  }
}

void
fill_vmaf_picture_buffer_hbd (float *src, VmafPicture * dst, unsigned width,
    unsigned height, int src_stride, unsigned bpc)
{
  float *a = src;
  uint16_t *b = dst->data[0];
  for (unsigned i = 0; i < height; i++) {
    for (unsigned j = 0; j < width; j++) {
      b[j] = a[j] * (1 << (bpc - 8));
    }
    a += src_stride / sizeof (float);
    b += dst->stride[0] / sizeof (uint16_t);
  }
}

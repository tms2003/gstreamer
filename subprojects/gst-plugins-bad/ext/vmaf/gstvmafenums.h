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
#ifndef __GST_VMAFENUMS_H__
#define __GST_VMAFENUMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS 

typedef enum _GstVmafPoolMethodEnum
{
  POOL_METHOD_UNKNOWN = 0,
  POOL_METHOD_MIN = 1,
  POOL_METHOD_MAX = 2,
  POOL_METHOD_MEAN = 3,
  POOL_METHOD_HARMONIC_MEAN = 4,
  POOL_METHOD_NB = 5,
} GstVmafPoolMethodEnum;

typedef enum _GstReadFrameReturnCodes
{
  READING_SUCCESSFUL = 0,
  READING_ERROR = 1,
  READING_EOS = 2,
} GstReadFrameReturnCodes;

typedef enum _GstVmafPropertyTypes
{
  PROP_0,
  PROP_MODEL_FILENAME,
  PROP_DISABLE_CLIP,
  PROP_DISABLE_AVX,
  PROP_ENABLE_TRANSFORM,
  PROP_PHONE_MODEL,
  PROP_PSNR,
  PROP_SSIM,
  PROP_MS_SSIM,
  PROP_NUM_THREADS,
  PROP_SUBSAMPLE,
  PROP_CONF_INT,
  PROP_LAST,
  PROP_POOL_METHOD,
  PROP_FRAME_MESSAGING,
  PROP_VMAF_LOG_FORMAT,
  PROP_VMAF_LOG_FILENAME,
} GstVmafPropertyTypes;

typedef enum _GstVmafMessageBusScoreTypes
{
  MESSAGE_TYPE_FRAME = 0,
  MESSAGE_TYPE_POOLED = 1,
} GstVmafMessageBusScoreTypes;

typedef enum _GstVmafLogFormats
{
  OUTPUT_FORMAT_NONE = 0,
  OUTPUT_FORMAT_CSV = 1,
  OUTPUT_FORMAT_XML = 2,
  OUTPUT_FORMAT_JSON = 3,
} GstVmafLogFormats;


G_END_DECLS
#endif /* __GST_VMAFENUMS_H__ */

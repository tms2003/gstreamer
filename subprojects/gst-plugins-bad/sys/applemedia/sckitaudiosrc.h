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

#ifndef __GST_SCKIT_AUDIO_SRC_H__
#define __GST_SCKIT_AUDIO_SRC_H__

#include <gst/base/gstbasesrc.h>

#include "sckitsrc-swift/sckitsrc-Swift.h"

G_BEGIN_DECLS
#define GST_TYPE_SCKIT_SRC (gst_sckit_audio_src_get_type())
#define GST_SCKIT_AUDIO_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SCKIT_SRC, GstSCKitAudioSrc))
#define GST_SCKIT_AUDIO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SCKIT_SRC, GstSCKitAudioSrcClass))
#define GST_IS_SCKIT_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SCKIT_SRC))
#define GST_IS_SCKIT_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SCKIT_SRC))

typedef struct _GstSCKitAudioSrc GstSCKitAudioSrc;
typedef struct _GstSCKitAudioSrcClass GstSCKitAudioSrcClass;

struct _GstSCKitAudioSrc
{
  GstBaseSrc src;
  SCKitAudioSrc *impl;
};

struct _GstSCKitAudioSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_sckit_audio_src_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (sckitaudiosrc);

G_END_DECLS

#endif /* __GST_SCKIT_AUDIO_SRC_H__ */

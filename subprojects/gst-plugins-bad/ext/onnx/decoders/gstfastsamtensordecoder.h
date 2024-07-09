/*
 * GStreamer gstreamer-fastsamtensordecoder
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstfastsamtensordecoder.h
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


#ifndef __GST_FASTSAM_TENSOR_DECODER_H__
#define __GST_FASTSAM_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_FASTSAM_TENSOR_DECODER (gst_fastsam_tensor_decoder_get_type ())
G_DECLARE_FINAL_TYPE (GstFastSAMTensorDecoder, gst_fastsam_tensor_decoder,
    GST, FASTSAM_TENSOR_DECODER, GstBaseTransform)

/**
 * GstFastSAMTensorDecoder:
 *
 * @box_confi_thresh: Box confidence threshold
 * @cls_confi_thresh: Class confidence threshold
 * @iou_threshold: Min IOU threshold
 * @max_detection: Maximum detections/masks
 *
 * Since: 1.23
 */
struct _GstFastSAMTensorDecoder
{
  GstBaseTransform basetransform;
  /* Box confidence threshold */
  gfloat box_confi_thresh;
  /* Class confidence threshold */
  gfloat cls_confi_thresh;
  /* Intersection-of-Union threshold */
  gfloat iou_thresh;
  /* Maximum detection/mask */
  gsize max_detection;
  /* Video Info */
  GstVideoInfo video_info;

  /*< private >*/
  /* selected_candidates */
  GPtrArray *sel_candidates;
  GPtrArray *selected;
};

struct _GstFastSAMTensorDecoderClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (fastsam_tensor_decoder)

G_END_DECLS
#endif /* __GST_FASTSAM_TENSOR_DECODER_H__ */

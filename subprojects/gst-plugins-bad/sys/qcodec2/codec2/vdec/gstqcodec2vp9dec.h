// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifndef __GST_QCODEC2_VP9_DEC_H__
#define __GST_QCODEC2_VP9_DEC_H__

#include <gst/gst.h>
#include "gstqcodec2vdec.h"
#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gstvp9parser.h>

G_BEGIN_DECLS
#define GST_TYPE_QCODEC2_VP9_DEC \
  (gst_qcodec2_vp9_dec_get_type())
#define GST_QCODEC2_VP9_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9Dec))
#define GST_QCODEC2_VP9_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9DecClass))
#define GST_QCODEC2_VP9_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9DecClass))
#define GST_IS_QCODEC2_VP9_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_VP9_DEC))
#define GST_IS_QCODEC2_VP9_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_VP9_DEC))
typedef struct _GstQcodec2VP9Dec GstQcodec2VP9Dec;
typedef struct _GstQcodec2VP9DecClass GstQcodec2VP9DecClass;

struct _GstQcodec2VP9Dec
{
  GstQcodec2Vdec parent;
  gboolean check_vp9_10bit;
};

struct _GstQcodec2VP9DecClass
{
  GstQcodec2VdecClass parent_class;
};

GType gst_qcodec2_vp9_dec_get_type (void);

G_END_DECLS
#endif /* __GST_QCODEC2_VP9_DEC_H__ */

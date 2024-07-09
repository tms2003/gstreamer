// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifndef __GST_QCODEC2_H265_ENC_H__
#define __GST_QCODEC2_H265_ENC_H__

#include <gst/gst.h>
#include "gstqcodec2venc.h"

G_BEGIN_DECLS
#define GST_TYPE_QCODEC2_H265_ENC \
  (gst_qcodec2_h265_enc_get_type())
#define GST_QCODEC2_H265_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_H265_ENC,GstQcodec2H265Enc))
#define GST_QCODEC2_H265_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_H265_ENC,GstQcodec2H265EncClass))
#define GST_QCODEC2_H265_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_H265_ENC,GstQcodec2H265EncClass))
#define GST_IS_QCODEC2_H265_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_H265_ENC))
#define GST_IS_QCODEC2_H265_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_H265_ENC))
typedef struct _GstQcodec2H265Enc GstQcodec2H265Enc;
typedef struct _GstQcodec2H265EncClass GstQcodec2H265EncClass;

struct _GstQcodec2H265Enc
{
  GstQcodec2Venc parent;
};

struct _GstQcodec2H265EncClass
{
  GstQcodec2VencClass parent_class;
};

GType gst_qcodec2_h265_enc_get_type (void);

G_END_DECLS
#endif /* __GST_QCODEC2_H265_ENC_H__ */

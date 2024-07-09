/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifndef __GST_QCODEC2_VDEC_H__
#define __GST_QCODEC2_VDEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/allocators.h>
#include "codec2wrapper.h"

G_BEGIN_DECLS
#define COMMON_VIDEO_CAPS(min, max) \
    "width = (int) [" #min ", " #max "], "    \
    "height = (int) [" #min ", " #max "]"
#define H264_CAPS \
    "video/x-h264, " \
    "stream-format = (string) { byte-stream }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define H265_CAPS \
    "video/x-h265, " \
    "stream-format = (string) { byte-stream }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define VP9_CAPS \
    "video/x-vp9, " \
    COMMON_VIDEO_CAPS(96, 4096)
#define MPEG2_CAPS \
    "video/mpeg, " \
    "mpegversion = (int)2, " \
    "parsed = (boolean)true, " \
    COMMON_VIDEO_CAPS(96, 1920)
#define QCODEC2_VDEC_SRC_WH_CAPS    \
  "width  = (int) [ 96, 8192 ], "     \
  "height = (int) [ 96, 8192 ]"
#define QCODEC2_VDEC_SRC_FPS_CAPS    \
  "framerate = (fraction) [ 0, 960 ]"
#define QCODEC2_VDEC_RAW_CAPS(formats) \
  "video/x-raw, "                       \
  "format = (string) " formats ", "     \
  QCODEC2_VDEC_SRC_WH_CAPS ", "       \
  QCODEC2_VDEC_SRC_FPS_CAPS
#define QCODEC2_VDEC_RAW_CAPS_WITH_FEATURES(features, formats) \
  "video/x-raw(" features "), "                                 \
  "format = (string) " formats ", "                             \
  QCODEC2_VDEC_SRC_WH_CAPS   ", "                             \
  QCODEC2_VDEC_SRC_FPS_CAPS
#define GST_TYPE_QCODEC2_VDEC          (gst_qcodec2_vdec_get_type())
#define GST_QCODEC2_VDEC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_VDEC,GstQcodec2Vdec))
#define GST_QCODEC2_VDEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_VDEC,GstQcodec2VdecClass))
#define GST_QCODEC2_VDEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_VDEC,GstQcodec2VdecClass))
#define GST_IS_QCODEC2_VDEC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_VDEC))
#define GST_IS_QCODEC2_VDEC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_VDEC))
typedef struct _GstQcodec2Vdec GstQcodec2Vdec;
typedef struct _GstQcodec2VdecClass GstQcodec2VdecClass;

typedef guint64 (*f_get_modifier) (void *bo);
ConfigParams make_pixel_format_param (guint32 fmt, gboolean is_input);
guint32 gst_to_c2_pixelformat (GstQcodec2Vdec * decoder,
    GstVideoFormat format);
gboolean gst_qcodec2_vdec_start_comp_and_config_pool (GstQcodec2Vdec *
    decoder);

/* Maximum number of input frame queued */
#define MAX_QUEUED_FRAME  64
#define DEFAULT_DEINTERLACE TRUE

struct _GstQcodec2Vdec
{
  GstVideoDecoder parent;

  /* Public properties */
  gboolean silent;

  void *comp_store;
  void *comp;
  void *comp_intf;
  gchar *comp_name;

  guint64 queued_frame[MAX_QUEUED_FRAME];
  gboolean downstream_supports_gbm;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  gboolean eos_reached;
  gboolean input_setup;
  gboolean output_setup;

  gint width;
  gint height;
  guint64 frame_index;
  GstVideoInterlaceMode interlace_mode;
  GstVideoFormat output_format;
  guint64 num_input_queued;
  guint64 num_output_done;
  gboolean downstream_supports_dma;
  gboolean output_picture_order_mode;
  gboolean low_latency_mode;

  GMutex pending_lock;
  GCond pending_cond;
  struct timeval start_time;
  struct timeval first_frame_time;
  GstBufferPool *out_port_pool;
  void *gbm_lib;
  f_get_modifier gbm_api_bo_get_modifier;
  gboolean is_ubwc;
  gboolean is_10bit;
  gboolean secure;
  gboolean delay_start;
  comp_cb cb;
  gboolean deinterlace;

  gboolean use_external_buf;
  GHashTable *buffer_table;
  guint max_external_buf_cnt;
  guint acquired_external_buf;
  GMutex external_buf_lock;
  GCond external_buf_cond;
};

/* Param function */
ConfigParams make_deinterlace_param (gboolean deinterlace);

/*
  Class structure should always contain the class structure for the type you're inheriting from.
*/
struct _GstQcodec2VdecClass
{
  GstVideoDecoderClass parent_class;

    gboolean (*open) (GstQcodec2Vdec * decoder);
    gboolean (*set_format) (GstQcodec2Vdec * decoder,
      GstVideoCodecState * state);
    GstFlowReturn (*handle_frame) (GstQcodec2Vdec * decoder,
      GstVideoCodecFrame * frame);
};

GType gst_qcodec2_vdec_get_type (void);

gboolean gst_qcodec2_vdec_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_QCODEC2_VDEC_H__ */

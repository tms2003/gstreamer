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
#ifndef __GST_VMAFELEMENT_H__
#define __GST_VMAFELEMENT_H__

#include <libvmaf.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>
#include "gstvmafenums.h"

G_BEGIN_DECLS
#define GST_TYPE_VMAF (gst_vmaf_get_type())
#define GST_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMAF, GstVmaf))
#define GST_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VMAF, GstVmafClass))
#define GST_IS_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VMAF))
#define GST_IS_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VMAF))
typedef struct _GstVmaf GstVmaf;
typedef struct _GstVmafClass GstVmafClass;

typedef struct
{
  void *ref_ptr;
  void *dist_ptr;
  gint frame_index;
} GstVmafQueueElem;

typedef struct
{
  GstVmaf *gst_vmaf_p;
  GstTask *vmaf_thread;
  GRecMutex vmaf_thread_mutex;
  GAsyncQueue *frame_queue;
  GMutex check_thread_failure;
  gboolean thread_eos;
  gboolean thread_failure;
  gint stream_index;
  gint vmaf_pix_fmt;
  gint frame_height;
  gint frame_width;
  gint frame_index;
  gint last_frame_processed;
  gint bpc;
  gchar *padname;
  VmafContext *vmaf_ctx;
  VmafModel *vmaf_model;
  VmafModelCollection *vmaf_model_collection;
} GstVmafThreadHelper;

struct _GstVmaf
{
  GstVideoAggregator videoaggregator;
  // VMAF settings from cmd
  GstVmafPoolMethodEnum vmaf_config_pool_method;
  GstVmafLogFormats vmaf_config_log_format;
  gchar *vmaf_config_model_filename;
  gboolean vmaf_config_disable_clip;
  gboolean vmaf_config_disable_avx;
  gboolean vmaf_config_enable_transform;
  gboolean vmaf_config_phone_model;
  gboolean vmaf_config_psnr;
  gboolean vmaf_config_ssim;
  gboolean vmaf_config_ms_ssim;
  guint vmaf_config_num_threads;
  guint vmaf_config_subsample;
  gboolean vmaf_config_conf_int;
  gboolean vmaf_config_frame_messaging;
  gchar *vmaf_config_log_filename;
  // Thread helpers
  GstVmafThreadHelper *helper_struct_pointer;
  gint number_of_input_streams;
  gboolean finish_threads;
  GMutex finish_mutex;
};

struct _GstVmafClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_vmaf_get_type (void);

G_END_DECLS
#endif /* __GST_VMAFELEMENT_H__ */

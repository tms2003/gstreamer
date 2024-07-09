// Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear OR LGPL-2.1

#ifndef __GST_QCODEC2BUFFERPOOL_H__
#define __GST_QCODEC2BUFFERPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/allocators.h>
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS
/* buffer pool functions */
#define GST_TYPE_QCODEC2_BUFFER_POOL      (gst_qcodec2_buffer_pool_get_type())
#define GST_IS_QCODEC2_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QCODEC2_BUFFER_POOL))
#define GST_QCODEC2_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QCODEC2_BUFFER_POOL, GstQcodec2BufferPool))
#define GST_QCODEC2_BUFFER_POOL_CAST(obj) ((GstQcodec2BufferPool*)(obj))
typedef struct _GstQcodec2BufferPool GstQcodec2BufferPool;
typedef struct _GstQcodec2BufferPoolClass GstQcodec2BufferPoolClass;
typedef struct _GstBufferPoolInitParam GstBufferPoolInitParam;

#define GST_BUFFER_POOL_OPTION_VIDEO_C2BUF_META "GstVideoC2BufMeta"

typedef enum
{
  DMABUF_MODE = 0,
  DMABUF_WRAP_MODE,
  FDBUF_MODE,
  FDBUF_WRAP_MODE
} PoolMode;

struct _GstBufferPoolInitParam
{
  GstVideoInfo info;
  void *c2_comp;
  GHashTable *buffer_table;
  gboolean is_ubwc;
  PoolMode mode;
};

struct _GstQcodec2BufferPool
{
  GstBufferPool bufferpool;
  GstAllocator *allocator;
  GstBufferPoolInitParam param;
  gboolean add_c2bufmeta;
};

struct _GstQcodec2BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

typedef struct GstBufferPoolAcquireParamsExt
{
  GstBufferPoolAcquireParams params;
  gint32 fd;
  gint32 meta_fd;
  guint64 index;
  guint32 size;
  void *c2_buf;
} GstBufferPoolAcquireParamsExt;

GType gst_qcodec2_buffer_pool_get_type (void);
GstBufferPool *gst_qcodec2_buffer_pool_new (GstBufferPoolInitParam * param);

#define GST_VIDEO_C2BUF_META_API_TYPE  (gst_video_c2buf_meta_api_get_type())
#define GST_VIDEO_C2BUF_META_INFO  (gst_video_c2buf_meta_get_info())
typedef struct _GstVideoC2BufMeta GstVideoC2BufMeta;

/**
 * GstVideoC2BufMeta:
 * @meta: parent #GstMeta
 * @c2_buf: associated pointer of C2 Buffer
 *
 * Extra buffer metadata describing associated pointer of C2 Buffer.
 */
struct _GstVideoC2BufMeta
{
  GstMeta meta;

  void *c2_buf;
};

GType gst_video_c2buf_meta_api_get_type (void);
const GstMetaInfo *gst_video_c2buf_meta_get_info (void);

#define gst_buffer_get_video_c2buf_meta(b) ((GstVideoC2BufMeta*)gst_buffer_get_meta((b),GST_VIDEO_C2BUF_META_API_TYPE))
#define gst_buffer_add_video_c2buf_meta(b) ((GstVideoC2BufMeta*)gst_buffer_add_meta((b),GST_VIDEO_C2BUF_META_INFO, NULL))

G_END_DECLS
#endif /* __GST_QCODEC2BUFFERPOOL_H__ */

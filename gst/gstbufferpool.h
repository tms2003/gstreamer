/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstbufferpool.h: Header for GstBufferPool object
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


#ifndef __GST_BUFFER_POOL_H__
#define __GST_BUFFER_POOL_H__

#include <gst/gstminiobject.h>
#include <gst/gstminiobjectpool.h>
#include <gst/gstpad.h>
#include <gst/gstbuffer.h>

G_BEGIN_DECLS

typedef struct _GstBufferPoolPrivate GstBufferPoolPrivate;
typedef struct _GstBufferPoolClass GstBufferPoolClass;

#define GST_TYPE_BUFFER_POOL                 (gst_buffer_pool_get_type())
#define GST_IS_BUFFER_POOL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BUFFER_POOL))
#define GST_IS_BUFFER_POOL_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BUFFER_POOL))
#define GST_BUFFER_POOL_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BUFFER_POOL, GstBufferPoolClass))
#define GST_BUFFER_POOL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BUFFER_POOL, GstBufferPool))
#define GST_BUFFER_POOL_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BUFFER_POOL, GstBufferPoolClass))
#define GST_BUFFER_POOL_CAST(obj)            ((GstBufferPool *)(obj))

/**
 * GstBufferPoolAcquireFlags:
 * @GST_BUFFER_POOL_ACQUIRE_FLAG_KEY_UNIT: buffer is keyframe
 * @GST_BUFFER_POOL_ACQUIRE_FLAG_DISCONT: buffer is discont
 * @GST_BUFFER_POOL_ACQUIRE_FLAG_LAST: last flag, subclasses can use private flags
 *    starting from this value.
 *
 * Additional flags to control the allocation of a buffer
 */
typedef enum {
  GST_BUFFER_POOL_ACQUIRE_FLAG_KEY_UNIT = (GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_LAST << 0),
  GST_BUFFER_POOL_ACQUIRE_FLAG_DISCONT  = (GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_LAST << 2),
  GST_BUFFER_POOL_ACQUIRE_FLAG_LAST     = (GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_LAST << 15),
} GstBufferPoolAcquireFlags;

/**
 * GST_BUFFER_POOL_ACQUIRE_FLAG_NONE:
 *
 * Alias of #GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_NONE. Use the later on
 * newly written code.
 */
#define GST_BUFFER_POOL_ACQUIRE_FLAG_NONE GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_NONE

/**
 * GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT:
 *
 * Alias of #GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_DONTWAIT. Use the later
 * on newly written code.
 */
#define GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_DONTWAIT

/**
 * GstBufferPoolAcquireParams:
 *
 * Alias of #GstMiniObjectPoolAcquireParams. Use the later on newly written code.
 */
typedef GstMiniObjectPoolAcquireParams GstBufferPoolAcquireParams;

/**
 * GstBufferPool:
 *
 * The structure of a #GstBufferPool. Use the associated macros to access the public
 * variables.
 */
struct _GstBufferPool {
  GstMiniObjectPool    pool;

  /*< private >*/
  GstBufferPoolPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBufferPoolClass:
 * @object_class:  Object parent class
 *
 * The GstBufferPool class.
 */
struct _GstBufferPoolClass {
  GstMiniObjectPoolClass    pool_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING + 9];
};

GST_API
GType       gst_buffer_pool_get_type (void);

/* allocation */

GST_API
GstBufferPool *  gst_buffer_pool_new  (void);

/* state management */

GST_API
gboolean         gst_buffer_pool_set_active      (GstBufferPool *pool, gboolean active);

GST_API
gboolean         gst_buffer_pool_is_active       (GstBufferPool *pool);

GST_API
gboolean         gst_buffer_pool_set_config      (GstBufferPool *pool, GstStructure *config);

GST_API
GstStructure *   gst_buffer_pool_get_config      (GstBufferPool *pool);

GST_API
const gchar **   gst_buffer_pool_get_options     (GstBufferPool *pool);

GST_API
gboolean         gst_buffer_pool_has_option      (GstBufferPool *pool, const gchar *option);

GST_API
void             gst_buffer_pool_set_flushing    (GstBufferPool *pool, gboolean flushing);

/* helpers for configuring the config structure */

GST_API
void             gst_buffer_pool_config_set_params    (GstStructure *config, GstCaps *caps,
                                                       guint size, guint min_buffers, guint max_buffers);

GST_API
gboolean         gst_buffer_pool_config_get_params    (GstStructure *config, GstCaps **caps,
                                                       guint *size, guint *min_buffers, guint *max_buffers);

GST_API
void             gst_buffer_pool_config_set_allocator (GstStructure *config, GstAllocator *allocator,
                                                       const GstAllocationParams *params);

GST_API
gboolean         gst_buffer_pool_config_get_allocator (GstStructure *config, GstAllocator **allocator,
                                                       GstAllocationParams *params);

/* options */

GST_API
guint            gst_buffer_pool_config_n_options   (GstStructure *config);

GST_API
void             gst_buffer_pool_config_add_option  (GstStructure *config, const gchar *option);

GST_API
const gchar *    gst_buffer_pool_config_get_option  (GstStructure *config, guint index);

GST_API
gboolean         gst_buffer_pool_config_has_option  (GstStructure *config, const gchar *option);

GST_API
gboolean         gst_buffer_pool_config_validate_params (GstStructure *config, GstCaps *caps,
                                                         guint size, guint min_buffers, guint max_buffers);

/* buffer management */

GST_API
GstFlowReturn    gst_buffer_pool_acquire_buffer  (GstBufferPool *pool, GstBuffer **buffer,
                                                  GstMiniObjectPoolAcquireParams *params);

GST_API
void             gst_buffer_pool_release_buffer  (GstBufferPool *pool, GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_BUFFER_POOL_H__ */

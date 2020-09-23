/* GStreamer
 * Copyright (C) 2010-2020 Wim Taymans <wim.taymans@gmail.com>
 *                         Michael Gruner <michael.gruner@ridgerun.com>
 *
 * gstminiobjectpool.h: Header for GstMiniObjectPool object
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


#ifndef __GST_MINI_OBJECT_POOL_H__
#define __GST_MINI_OBJECT_POOL_H__

#include <gst/gstminiobject.h>

G_BEGIN_DECLS

typedef struct _GstMiniObjectPoolPrivate GstMiniObjectPoolPrivate;
typedef struct _GstMiniObjectPoolClass GstMiniObjectPoolClass;

#define GST_TYPE_MINI_OBJECT_POOL                 (gst_mini_object_pool_get_type())
#define GST_IS_MINI_OBJECT_POOL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MINI_OBJECT_POOL))
#define GST_IS_MINI_OBJECT_POOL_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MINI_OBJECT_POOL))
#define GST_MINI_OBJECT_POOL_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MINI_OBJECT_POOL, GstMiniObjectPoolClass))
#define GST_MINI_OBJECT_POOL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MINI_OBJECT_POOL, GstMiniObjectPool))
#define GST_MINI_OBJECT_POOL_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MINI_OBJECT_POOL, GstMiniObjectPoolClass))
#define GST_MINI_OBJECT_POOL_CAST(obj)            ((GstMiniObjectPool *)(obj))

/**
 * GstMiniObjectPoolAcquireFlags:
 * @GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_NONE: no flags
 * @GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_DONTWAIT: when the pool is empty, acquire_buffer
 * will by default block until a mini-object is released into the pool again. Setting
 * this flag makes acquire_buffer return #GST_FLOW_EOS instead of blocking.
 * @GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_LAST: last flag, subclasses can
 * use private flags starting from this value.
 *
 * Additional flags to control the allocation of a mini-object
 */
typedef enum {
  GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_NONE = 0,
  GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_DONTWAIT = (1 << 2),
  GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_LAST = (1 << 16),
} GstMiniObjectPoolAcquireFlags;

typedef struct _GstMiniObjectPoolAcquireParams GstMiniObjectPoolAcquireParams;

/**
 * GstMiniObjectPoolAcquireParams:
 * @format: the format of @start and @stop
 * @start: the start position
 * @stop: the stop position
 * @flags: additional flags
 *
 * Parameters passed to the gst_mini_object_pool_acquire_object() function to
 * control the allocation of the mini-object.
 *
 * The default implementation ignores the @start and @stop members but other
 * implementations can use this extra information to decide what mini-object to
 * return.
 */
struct _GstMiniObjectPoolAcquireParams {
  GstFormat                     format;
  gint64                        start;
  gint64                        stop;
  GstMiniObjectPoolAcquireFlags flags;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GST_MINI_OBJECT_POOL_IS_FLUSHING:
 * @pool: a GstMiniObjectPool
 *
 * Check if the pool is flushing. Subclasses might want to check the
 * state of the pool in the acquire function.
 */
#define GST_MINI_OBJECT_POOL_IS_FLUSHING(pool)  (g_atomic_int_get (&pool->flushing))

/**
 * GstMiniObjectPool:
 *
 * The structure of a #GstMiniObjectPool. Use the associated macros to access the public
 * variables.
 */
struct _GstMiniObjectPool {
  GstObject                object;

  /*< protected >*/
  gint                     flushing;

  /*< private >*/
  GstMiniObjectPoolPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstMiniObjectPoolClass:
 * @object_class:  Object parent class
 * @get_options: Optional. Get a list of options supported by this pool
 * @set_config: Optional. Apply the pool configuration. The default configuration
 *        will parse the default config parameters. Subclasses should chain to the
          parent's implementation.
 * @start: Optional. Start the pool. The default implementation will preallocate
 *        min-objects objects and put them in the queue
 * @stop: Optional. Stop the pool. the default implementation will free the
 *        preallocated objects. This function is called when all the mini-objects
 *        are returned to the pool.
 * @acquire_object: Optional. Get a new mini-object from the pool. The default
 *        implementation will take a mini-object from the queue and optionally wait
 *        for an object to be released when there are no more available.
 * @alloc_object: allocate a mini-object. Subclasses must provide an implementation.
 * @reset_object: reset the mini-object to its state when it was freshly allocated.
 *        Subclasses must provide an implementation.
 * @release_object: Optional. Release a mini-object back in the pool. The default
 *        implementation will put the object back in the queue and notify any
 *        blocking acquire_object calls.
 * @free_object: free an object. The default implementation unrefs the mini-object.
 * @flush_start: enter the flushing state.
 * @flush_stop: leave the flushign state.
 *
 * The GstMiniObjectPool class. (Since: 1.19)
 */
struct _GstMiniObjectPoolClass {
  GstObjectClass    object_class;

  /*< public >*/
  const gchar ** (*get_options)    (GstMiniObjectPool *pool);
  gboolean       (*set_config)     (GstMiniObjectPool *pool, GstStructure *config);

  gboolean       (*start)          (GstMiniObjectPool *pool);
  gboolean       (*stop)           (GstMiniObjectPool *pool);

  GstFlowReturn  (*acquire_object) (GstMiniObjectPool *pool, GstMiniObject **object,
                                    GstMiniObjectPoolAcquireParams *params);
  GstFlowReturn  (*alloc_object)   (GstMiniObjectPool *pool, GstMiniObject **object,
                                    GstMiniObjectPoolAcquireParams *params);
  void           (*reset_object)   (GstMiniObjectPool *pool, GstMiniObject *object);
  void           (*release_object) (GstMiniObjectPool *pool, GstMiniObject *object);
  void           (*free_object)    (GstMiniObjectPool *pool, GstMiniObject *object);
  void           (*flush_start)    (GstMiniObjectPool *pool);
  void           (*flush_stop)     (GstMiniObjectPool *pool);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_API
GType       gst_mini_object_pool_get_type (void);

/* state management */

GST_API
gboolean         gst_mini_object_pool_set_active      (GstMiniObjectPool *pool, gboolean active);

GST_API
gboolean         gst_mini_object_pool_is_active       (GstMiniObjectPool *pool);

GST_API
gboolean         gst_mini_object_pool_set_config      (GstMiniObjectPool *pool, GstStructure *config);

GST_API
GstStructure *   gst_mini_object_pool_get_config      (GstMiniObjectPool *pool);

GST_API
const gchar **   gst_mini_object_pool_get_options     (GstMiniObjectPool *pool);

GST_API
gboolean         gst_mini_object_pool_has_option      (GstMiniObjectPool *pool, const gchar *option);

GST_API
void             gst_mini_object_pool_set_flushing    (GstMiniObjectPool *pool, gboolean flushing);

/* helpers for configuring the config structure */

GST_API
void             gst_mini_object_pool_config_set_params    (GstStructure *config, guint min_objects,
							    guint max_objects);

GST_API
gboolean         gst_mini_object_pool_config_get_params    (GstStructure *config, guint *min_objects,
							    guint *max_objects);

/* options */

GST_API
guint            gst_mini_object_pool_config_n_options   (GstStructure *config);

GST_API
void             gst_mini_object_pool_config_add_option  (GstStructure *config, const gchar *option);

GST_API
const gchar *    gst_mini_object_pool_config_get_option  (GstStructure *config, guint index);

GST_API
gboolean         gst_mini_object_pool_config_has_option  (GstStructure *config, const gchar *option);

GST_API
gboolean         gst_mini_object_pool_config_validate_params (GstStructure *config, guint min_objects, guint max_objects);

/* mini object management */

GST_API
GstFlowReturn    gst_mini_object_pool_acquire_object  (GstMiniObjectPool *pool, GstMiniObject **object,
                                                       GstMiniObjectPoolAcquireParams *params);

GST_API
void             gst_mini_object_pool_release_object  (GstMiniObjectPool *pool, GstMiniObject *object);

GST_API
void             gst_mini_object_pool_discard_object  (GstMiniObjectPool *pool, GstMiniObject *object);

G_END_DECLS

#endif /* __GST_MINI_OBJECT_POOL_H__ */

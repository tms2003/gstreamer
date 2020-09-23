/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstbufferpool.c: GstBufferPool baseclass
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

/**
 * SECTION:gstbufferpool
 * @title: GstBufferPool
 * @short_description: Pool for buffers
 * @see_also: #GstBuffer
 *
 * A #GstBufferPool is an object that can be used to pre-allocate and recycle
 * buffers of the same size and with the same properties.
 *
 * A #GstBufferPool is created with gst_buffer_pool_new().
 *
 * Once a pool is created, it needs to be configured. A call to
 * gst_buffer_pool_get_config() returns the current configuration structure from
 * the pool. With gst_buffer_pool_config_set_params() and
 * gst_buffer_pool_config_set_allocator() the bufferpool parameters and
 * allocator can be configured. Other properties can be configured in the pool
 * depending on the pool implementation.
 *
 * A bufferpool can have extra options that can be enabled with
 * gst_buffer_pool_config_add_option(). The available options can be retrieved
 * with gst_buffer_pool_get_options(). Some options allow for additional
 * configuration properties to be set.
 *
 * After the configuration structure has been configured,
 * gst_buffer_pool_set_config() updates the configuration in the pool. This can
 * fail when the configuration structure is not accepted.
 *
 * After the a pool has been configured, it can be activated with
 * gst_buffer_pool_set_active(). This will preallocate the configured resources
 * in the pool.
 *
 * When the pool is active, gst_buffer_pool_acquire_buffer() can be used to
 * retrieve a buffer from the pool.
 *
 * Buffers allocated from a bufferpool will automatically be returned to the
 * pool with gst_buffer_pool_release_buffer() when their refcount drops to 0.
 *
 * The bufferpool can be deactivated again with gst_buffer_pool_set_active().
 * All further gst_buffer_pool_acquire_buffer() calls will return an error. When
 * all buffers are returned to the pool they will be freed.
 *
 * Use gst_object_unref() to release the reference to a bufferpool. If the
 * refcount of the pool reaches 0, the pool will be freed.
 */

#include "gst_private.h"
#include "glib-compat-private.h"

#include "gstquark.h"

#include "gstbufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_buffer_pool_debug

struct _GstBufferPoolPrivate
{
  guint size;
  GstAllocator *allocator;
  GstAllocationParams params;
};

static void gst_buffer_pool_finalize (GObject * object);

G_DEFINE_TYPE_WITH_PRIVATE (GstBufferPool, gst_buffer_pool,
    GST_TYPE_MINI_OBJECT_POOL);

static gboolean default_set_config (GstMiniObjectPool * pool,
    GstStructure * config);
static GstFlowReturn default_alloc_buffer (GstMiniObjectPool * pool,
    GstMiniObject ** buffer, GstMiniObjectPoolAcquireParams * params);
static void default_reset_buffer (GstMiniObjectPool * pool,
    GstMiniObject * buffer);
static void default_release_buffer (GstMiniObjectPool * pool,
    GstMiniObject * buffer);

static void
gst_buffer_pool_class_init (GstBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstMiniObjectPoolClass *pool_class = (GstMiniObjectPoolClass *) klass;

  gobject_class->finalize = gst_buffer_pool_finalize;

  pool_class->set_config = default_set_config;
  pool_class->reset_object = default_reset_buffer;
  pool_class->alloc_object = default_alloc_buffer;
  pool_class->release_object = default_release_buffer;

  GST_DEBUG_CATEGORY_INIT (gst_buffer_pool_debug, "bufferpool", 0,
      "bufferpool debug");
}

static void
gst_buffer_pool_init (GstBufferPool * pool)
{
  GstBufferPoolPrivate *priv;
  GstMiniObjectPool *base;
  GstStructure *config;

  priv = pool->priv = gst_buffer_pool_get_instance_private (pool);
  base = GST_MINI_OBJECT_POOL (pool);

  config = gst_mini_object_pool_get_config (base);

  gst_buffer_pool_config_set_params (config, NULL, 0, 0, 0);
  priv->allocator = NULL;
  gst_allocation_params_init (&priv->params);
  gst_buffer_pool_config_set_allocator (config, priv->allocator, &priv->params);

  GST_DEBUG_OBJECT (pool, "created");
}

static void
gst_buffer_pool_finalize (GObject * object)
{
  GstBufferPool *pool;
  GstBufferPoolPrivate *priv;

  pool = GST_BUFFER_POOL_CAST (object);
  priv = pool->priv;

  GST_DEBUG_OBJECT (pool, "%p finalize", pool);

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  G_OBJECT_CLASS (gst_buffer_pool_parent_class)->finalize (object);
}

/**
 * gst_buffer_pool_new:
 *
 * Creates a new #GstBufferPool instance.
 *
 * Returns: (transfer full): a new #GstBufferPool instance
 */
GstBufferPool *
gst_buffer_pool_new (void)
{
  GstBufferPool *result;

  result = g_object_new (GST_TYPE_BUFFER_POOL, NULL);
  GST_DEBUG_OBJECT (result, "created new buffer pool");

  /* Clear floating flag */
  gst_object_ref_sink (result);

  return result;
}

static gboolean
mark_meta_pooled (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GST_DEBUG_OBJECT (GST_BUFFER_POOL (user_data),
      "marking meta %p as POOLED in buffer %p", *meta, buffer);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_POOLED);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_LOCKED);

  return TRUE;
}

static GstFlowReturn
default_alloc_buffer (GstMiniObjectPool * base, GstMiniObject ** object,
    GstMiniObjectPoolAcquireParams * params)
{
  GstBufferPool *pool = GST_BUFFER_POOL (base);
  GstBuffer **buffer = (GstBuffer **) object;
  GstBufferPoolPrivate *priv = pool->priv;

  *buffer =
      gst_buffer_new_allocate (priv->allocator, priv->size, &priv->params);

  if (!*buffer)
    return GST_FLOW_ERROR;

  /* lock all metadata and mark as pooled, we want this to remain on
   * the buffer and we want to remove any other metadata that gets added
   * later */
  gst_buffer_foreach_meta (*buffer, mark_meta_pooled, pool);

  /* un-tag memory, this is how we expect the buffer when it is
   * released again */
  GST_BUFFER_FLAG_UNSET (*buffer, GST_BUFFER_FLAG_TAG_MEMORY);

  return GST_FLOW_OK;
}

/**
 * gst_buffer_pool_set_active:
 * @pool: a #GstBufferPool
 * @active: the new active state
 *
 * Control the active state of @pool. When the pool is inactive, new calls to
 * gst_buffer_pool_acquire_buffer() will return with %GST_FLOW_FLUSHING.
 *
 * Activating the bufferpool will preallocate all resources in the pool based on
 * the configuration of the pool.
 *
 * Deactivating will free the resources again when there are no outstanding
 * buffers. When there are outstanding buffers, they will be freed as soon as
 * they are all returned to the pool.
 *
 * Returns: %FALSE when the pool was not configured or when preallocation of the
 * buffers failed.
 */
gboolean
gst_buffer_pool_set_active (GstBufferPool * pool, gboolean active)
{
  return gst_mini_object_pool_set_active (GST_MINI_OBJECT_POOL (pool), active);
}

/**
 * gst_buffer_pool_is_active:
 * @pool: a #GstBufferPool
 *
 * Check if @pool is active. A pool can be activated with the
 * gst_buffer_pool_set_active() call.
 *
 * Returns: %TRUE when the pool is active.
 */
gboolean
gst_buffer_pool_is_active (GstBufferPool * pool)
{
  return gst_mini_object_pool_is_active (GST_MINI_OBJECT_POOL (pool));
}

static gboolean
default_set_config (GstMiniObjectPool * base, GstStructure * config)
{
  GstBufferPool *pool = GST_BUFFER_POOL (base);
  GstBufferPoolPrivate *priv = pool->priv;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  GstAllocator *allocator;
  GstAllocationParams params;

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  priv->size = size;

  if (priv->allocator)
    gst_object_unref (priv->allocator);
  if ((priv->allocator = allocator))
    gst_object_ref (allocator);
  priv->params = params;

  return
      GST_MINI_OBJECT_POOL_CLASS (gst_buffer_pool_parent_class)->set_config
      (GST_MINI_OBJECT_POOL (pool), config);

wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
}

/**
 * gst_buffer_pool_set_config:
 * @pool: a #GstBufferPool
 * @config: (transfer full): a #GstStructure
 *
 * Set the configuration of the pool. If the pool is already configured, and
 * the configuration haven't change, this function will return %TRUE. If the
 * pool is active, this method will return %FALSE and active configuration
 * will remain. Buffers allocated form this pool must be returned or else this
 * function will do nothing and return %FALSE.
 *
 * @config is a #GstStructure that contains the configuration parameters for
 * the pool. A default and mandatory set of parameters can be configured with
 * gst_buffer_pool_config_set_params(), gst_buffer_pool_config_set_allocator()
 * and gst_buffer_pool_config_add_option().
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_buffer_pool_get_config().
 *
 * This function takes ownership of @config.
 *
 * Returns: %TRUE when the configuration could be set.
 */
gboolean
gst_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  return gst_mini_object_pool_set_config (GST_MINI_OBJECT_POOL (pool), config);
}

/**
 * gst_buffer_pool_get_config:
 * @pool: a #GstBufferPool
 *
 * Get a copy of the current configuration of the pool. This configuration
 * can either be modified and used for the gst_buffer_pool_set_config() call
 * or it must be freed after usage.
 *
 * Returns: (transfer full): a copy of the current configuration of @pool. use
 * gst_structure_free() after usage or gst_buffer_pool_set_config().
 */
GstStructure *
gst_buffer_pool_get_config (GstBufferPool * pool)
{
  return gst_mini_object_pool_get_config (GST_MINI_OBJECT_POOL (pool));
}

/**
 * gst_buffer_pool_get_options:
 * @pool: a #GstBufferPool
 *
 * Get a %NULL terminated array of string with supported bufferpool options for
 * @pool. An option would typically be enabled with
 * gst_buffer_pool_config_add_option().
 *
 * Returns: (array zero-terminated=1) (transfer none): a %NULL terminated array
 *          of strings.
 */
const gchar **
gst_buffer_pool_get_options (GstBufferPool * pool)
{
  return gst_mini_object_pool_get_options (GST_MINI_OBJECT_POOL (pool));
}

/**
 * gst_buffer_pool_has_option:
 * @pool: a #GstBufferPool
 * @option: an option
 *
 * Check if the bufferpool supports @option.
 *
 * Returns: %TRUE if the buffer pool contains @option.
 */
gboolean
gst_buffer_pool_has_option (GstBufferPool * pool, const gchar * option)
{
  return gst_mini_object_pool_has_option (GST_MINI_OBJECT_POOL (pool), option);
}

/**
 * gst_buffer_pool_config_set_params:
 * @config: a #GstBufferPool configuration
 * @caps: caps for the buffers
 * @size: the size of each buffer, not including prefix and padding
 * @min_buffers: the minimum amount of buffers to allocate.
 * @max_buffers: the maximum amount of buffers to allocate or 0 for unlimited.
 *
 * Configure @config with the given parameters.
 */
void
gst_buffer_pool_config_set_params (GstStructure * config, GstCaps * caps,
    guint size, guint min_buffers, guint max_buffers)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (max_buffers == 0 || min_buffers <= max_buffers);
  g_return_if_fail (caps == NULL || gst_caps_is_fixed (caps));

  gst_mini_object_pool_config_set_params (config, min_buffers, max_buffers);

  gst_structure_id_set (config,
      GST_QUARK (CAPS), GST_TYPE_CAPS, caps,
      GST_QUARK (SIZE), G_TYPE_UINT, size, NULL);
}

/**
 * gst_buffer_pool_config_set_allocator:
 * @config: a #GstBufferPool configuration
 * @allocator: (allow-none): a #GstAllocator
 * @params: (allow-none): #GstAllocationParams
 *
 * Set the @allocator and @params on @config.
 *
 * One of @allocator and @params can be %NULL, but not both. When @allocator
 * is %NULL, the default allocator of the pool will use the values in @param
 * to perform its allocation. When @param is %NULL, the pool will use the
 * provided @allocator with its default #GstAllocationParams.
 *
 * A call to gst_buffer_pool_set_config() can update the allocator and params
 * with the values that it is able to do. Some pools are, for example, not able
 * to operate with different allocators or cannot allocate with the values
 * specified in @params. Use gst_buffer_pool_get_config() to get the currently
 * used values.
 */
void
gst_buffer_pool_config_set_allocator (GstStructure * config,
    GstAllocator * allocator, const GstAllocationParams * params)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (allocator != NULL || params != NULL);

  gst_structure_id_set (config,
      GST_QUARK (ALLOCATOR), GST_TYPE_ALLOCATOR, allocator,
      GST_QUARK (PARAMS), GST_TYPE_ALLOCATION_PARAMS, params, NULL);
}

/**
 * gst_buffer_pool_config_add_option:
 * @config: a #GstBufferPool configuration
 * @option: an option to add
 *
 * Enabled the option in @config. This will instruct the @bufferpool to enable
 * the specified option on the buffers that it allocates.
 *
 * The supported options by @pool can be retrieved with gst_buffer_pool_get_options().
 */
void
gst_buffer_pool_config_add_option (GstStructure * config, const gchar * option)
{
  gst_mini_object_pool_config_add_option (config, option);
}

/**
 * gst_buffer_pool_config_n_options:
 * @config: a #GstBufferPool configuration
 *
 * Retrieve the number of values currently stored in the options array of the
 * @config structure.
 *
 * Returns: the options array size as a #guint.
 */
guint
gst_buffer_pool_config_n_options (GstStructure * config)
{
  return gst_mini_object_pool_config_n_options (config);
}

/**
 * gst_buffer_pool_config_get_option:
 * @config: a #GstBufferPool configuration
 * @index: position in the option array to read
 *
 * Parse an available @config and get the option at @index of the options API
 * array.
 *
 * Returns: a #gchar of the option at @index.
 */
const gchar *
gst_buffer_pool_config_get_option (GstStructure * config, guint index)
{
  return gst_mini_object_pool_config_get_option (config, index);
}

/**
 * gst_buffer_pool_config_has_option:
 * @config: a #GstBufferPool configuration
 * @option: an option
 *
 * Check if @config contains @option.
 *
 * Returns: %TRUE if the options array contains @option.
 */
gboolean
gst_buffer_pool_config_has_option (GstStructure * config, const gchar * option)
{
  return gst_mini_object_pool_config_has_option (config, option);
}

/**
 * gst_buffer_pool_config_get_params:
 * @config: (transfer none): a #GstBufferPool configuration
 * @caps: (out) (transfer none) (allow-none): the caps of buffers
 * @size: (out) (allow-none): the size of each buffer, not including prefix and padding
 * @min_buffers: (out) (allow-none): the minimum amount of buffers to allocate.
 * @max_buffers: (out) (allow-none): the maximum amount of buffers to allocate or 0 for unlimited.
 *
 * Get the configuration values from @config.
 *
 * Returns: %TRUE if all parameters could be fetched.
 */
gboolean
gst_buffer_pool_config_get_params (GstStructure * config, GstCaps ** caps,
    guint * size, guint * min_buffers, guint * max_buffers)
{
  g_return_val_if_fail (config != NULL, FALSE);

  gst_mini_object_pool_config_get_params (config, min_buffers, max_buffers);

  if (caps) {
    *caps = g_value_get_boxed (gst_structure_id_get_value (config,
            GST_QUARK (CAPS)));
  }
  return gst_structure_id_get (config,
      GST_QUARK (SIZE), G_TYPE_UINT, size, NULL);
}

/**
 * gst_buffer_pool_config_get_allocator:
 * @config: (transfer none): a #GstBufferPool configuration
 * @allocator: (out) (allow-none) (transfer none): a #GstAllocator, or %NULL
 * @params: (out) (allow-none): #GstAllocationParams, or %NULL
 *
 * Get the @allocator and @params from @config.
 *
 * Returns: %TRUE, if the values are set.
 */
gboolean
gst_buffer_pool_config_get_allocator (GstStructure * config,
    GstAllocator ** allocator, GstAllocationParams * params)
{
  g_return_val_if_fail (config != NULL, FALSE);

  if (allocator)
    *allocator = g_value_get_object (gst_structure_id_get_value (config,
            GST_QUARK (ALLOCATOR)));
  if (params) {
    GstAllocationParams *p;

    p = g_value_get_boxed (gst_structure_id_get_value (config,
            GST_QUARK (PARAMS)));
    if (p) {
      *params = *p;
    } else {
      gst_allocation_params_init (params);
    }
  }
  return TRUE;
}

/**
 * gst_buffer_pool_config_validate_params:
 * @config: (transfer none): a #GstBufferPool configuration
 * @caps: (transfer none): the excepted caps of buffers
 * @size: the expected size of each buffer, not including prefix and padding
 * @min_buffers: the expected minimum amount of buffers to allocate.
 * @max_buffers: the expect maximum amount of buffers to allocate or 0 for unlimited.
 *
 * Validate that changes made to @config are still valid in the context of the
 * expected parameters. This function is a helper that can be used to validate
 * changes made by a pool to a config when gst_buffer_pool_set_config()
 * returns %FALSE. This expects that @caps haven't changed and that
 * @min_buffers aren't lower then what we initially expected.
 * This does not check if options or allocator parameters are still valid,
 * won't check if size have changed, since changing the size is valid to adapt
 * padding.
 *
 * Since: 1.4
 *
 * Returns: %TRUE, if the parameters are valid in this context.
 */
gboolean
gst_buffer_pool_config_validate_params (GstStructure * config, GstCaps * caps,
    guint size, guint min_buffers, guint max_buffers)
{
  GstCaps *newcaps;
  guint newsize, newmin;
  gboolean ret = FALSE;

  g_return_val_if_fail (config != NULL, FALSE);

  gst_buffer_pool_config_get_params (config, &newcaps, &newsize, &newmin, NULL);

  if (gst_caps_is_equal (caps, newcaps) && (newsize >= size))
    ret = TRUE;

  ret &=
      gst_mini_object_pool_config_validate_params (config, min_buffers,
      max_buffers);

  return ret;
}

static gboolean
remove_meta_unpooled (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  if (!GST_META_FLAG_IS_SET (*meta, GST_META_FLAG_POOLED)) {
    GST_META_FLAG_UNSET (*meta, GST_META_FLAG_LOCKED);
    *meta = NULL;
  }
  return TRUE;
}

static void
default_reset_buffer (GstMiniObjectPool * base, GstMiniObject * object)
{
  GstBufferPool *pool = GST_BUFFER_POOL (base);
  GstBuffer *buffer = GST_BUFFER (object);

  GST_BUFFER_FLAGS (buffer) &= GST_BUFFER_FLAG_TAG_MEMORY;

  GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;

  /* if the memory is intact reset the size to the full size */
  if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY)) {
    gsize offset, maxsize;
    gst_buffer_get_sizes (buffer, &offset, &maxsize);
    /* check if we can resize to at least the pool configured size.  If not,
     * then this will fail internally in gst_buffer_resize().
     * default_release_buffer() will drop the buffer from the pool if the
     * sizes don't match */
    if (maxsize >= pool->priv->size) {
      gst_buffer_resize (buffer, -offset, pool->priv->size);
    } else {
      GST_WARNING_OBJECT (pool, "Buffer %p without the memory tag has "
          "maxsize (%" G_GSIZE_FORMAT ") that is smaller than the "
          "configured buffer pool size (%u). The buffer will be not be "
          "reused. This is most likely a bug in this GstBufferPool subclass",
          buffer, maxsize, pool->priv->size);
    }
  }

  /* remove all metadata without the POOLED flag */
  gst_buffer_foreach_meta (buffer, remove_meta_unpooled, pool);
}

/**
 * gst_buffer_pool_acquire_buffer:
 * @pool: a #GstBufferPool
 * @buffer: (out): a location for a #GstBuffer
 * @params: (transfer none) (allow-none): parameters.
 *
 * Acquire a buffer from @pool. @buffer should point to a memory location that
 * can hold a pointer to the new buffer.
 *
 * @params can be %NULL or contain optional parameters to influence the
 * allocation.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the pool is
 * inactive.
 */
GstFlowReturn
gst_buffer_pool_acquire_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstMiniObjectPoolAcquireParams * params)
{
  return gst_mini_object_pool_acquire_object (GST_MINI_OBJECT_POOL (pool),
      (GstMiniObject **) buffer, params);
}

static void
default_release_buffer (GstMiniObjectPool * base, GstMiniObject * object)
{
  GstBufferPool *pool = GST_BUFFER_POOL (base);
  GstBuffer *buffer = GST_BUFFER (object);

  GST_LOG_OBJECT (pool, "released buffer %p %d", buffer,
      GST_MINI_OBJECT_FLAGS (buffer));

  /* memory should be untouched */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY)))
    goto memory_tagged;

  /* size should have been reset. This is not a catch all, pool with
   * size requirement per memory should do their own check. */
  if (G_UNLIKELY (gst_buffer_get_size (buffer) != pool->priv->size))
    goto size_changed;

  /* all memory should be exclusive to this buffer (and thus be writable) */
  if (G_UNLIKELY (!gst_buffer_is_all_memory_writable (buffer)))
    goto not_writable;

  GST_MINI_OBJECT_POOL_CLASS (gst_buffer_pool_parent_class)->release_object
      (GST_MINI_OBJECT_POOL (pool), GST_MINI_OBJECT_CAST (buffer));
  return;

memory_tagged:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PERFORMANCE, pool,
        "discarding buffer %p: memory tag set", buffer);
    goto discard;
  }
size_changed:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PERFORMANCE, pool,
        "discarding buffer %p: size %" G_GSIZE_FORMAT " != %u",
        buffer, gst_buffer_get_size (buffer), pool->priv->size);
    goto discard;
  }
not_writable:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PERFORMANCE, pool,
        "discarding buffer %p: memory not writable", buffer);
    goto discard;
  }
discard:
  {
    gst_mini_object_pool_discard_object (GST_MINI_OBJECT_POOL (pool),
        GST_MINI_OBJECT_CAST (buffer));
    return;
  }
}

/**
 * gst_buffer_pool_release_buffer:
 * @pool: a #GstBufferPool
 * @buffer: (transfer full): a #GstBuffer
 *
 * Release @buffer to @pool. @buffer should have previously been allocated from
 * @pool with gst_buffer_pool_acquire_buffer().
 *
 * This function is usually called automatically when the last ref on @buffer
 * disappears.
 */
void
gst_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  gst_mini_object_pool_release_object (GST_MINI_OBJECT_POOL (pool),
      GST_MINI_OBJECT_CAST (buffer));
}

/**
 * gst_buffer_pool_set_flushing:
 * @pool: a #GstBufferPool
 * @flushing: whether to start or stop flushing
 *
 * Enable or disable the flushing state of a @pool without freeing or
 * allocating buffers.
 *
 * Since: 1.4
 */
void
gst_buffer_pool_set_flushing (GstBufferPool * pool, gboolean flushing)
{
  gst_mini_object_pool_set_flushing (GST_MINI_OBJECT_POOL (pool), flushing);
}

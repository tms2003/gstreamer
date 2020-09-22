/* GStreamer
 * Copyright (C) 2010-2020 Wim Taymans <wim.taymans@gmail.com>
 *                         Michael Gruner <michael.gruner@ridgerun.com>
 *
 * gstminiobjectpool.c: GstMiniObjectPool baseclass
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
 * SECTION:gstminiobjectpool
 * @title: GstMiniObjectPool
 * @short_description: Pool for mini objects
 * @see_also: #GstMiniObject
 * @see_also: #GstBufferPool
 *
 * A #GstMiniObjectPool is a base class for classes that can be used
 * to pre-allocate and recycle specific mini-objects of (typically)
 * the same type.
 *
 * A #GstMiniObjectPool is an abstract class so it's not directly
 * instanced. Instead, a specialized pool such as #GstBufferPool
 * should be constructed.
 *
 * Once a pool is created, extra options can be enabled with
 * gst_mini_object_pool_config_add_option(). The available options can
 * be retrieved with gst_mini_object_pool_get_options(). Some options
 * allow for additional configuration properties to be set. Configurations
 * are specific to each subclass.
 *
 * After the configuration structure has been configured,
 * gst_mini_object_pool_set_config() updates the configuration in the
 * pool. This can fail when the configuration structure is not
 * accepted.
 *
 * After the a pool has been configured, it can be activated with
 * gst_mini_object_pool_set_active(). This will preallocate the
 * configured resources in the pool.
 *
 * When the pool is active, gst_mini_object_pool_acquire_object() can be used
 * to retrieve a mini object from the pool. Typically, the specialized
 * pool will provide a more direct method. For example, the
 * #GstBufferPool provides gst_buffer_pool_acquire_buffer() wich
 * returns a #GstBuffer instead of a generic #GstMiniObject.
 *
 * Mini objects allocated from a pool will automatically be returned
 * to the pool with gst_mini_object_pool_release_object() when their refcount
 * drops to 0. Typically, the specialized pool will provide a more
 * direct method. For example, the #GstBufferPool provides
 * gst_buffer_pool_release_buffer() wich receives a #GstBuffer instead
 * of a generic #GstMiniObject.
 *
 * The pool can be deactivated again with gst_mini_object_pool_set_active().
 * All further gst_mini_object_pool_acquire_object() calls will return an error.
 * When all mini objects are returned to the pool they will be freed.
 *
 * Use gst_object_unref() to release the reference to a pool. If the
 * refcount of the pool reaches 0, the pool will be freed.
 */

#include "gst_private.h"
#include "glib-compat-private.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>

#include "gstatomicqueue.h"
#include "gstpoll.h"
#include "gstinfo.h"
#include "gstquark.h"
#include "gstvalue.h"

#include "gstminiobjectpool.h"

#ifdef G_OS_WIN32
#  ifndef EWOULDBLOCK
#  define EWOULDBLOCK EAGAIN    /* This is just to placate gcc */
#  endif
#endif /* G_OS_WIN32 */

GST_DEBUG_CATEGORY_STATIC (gst_mini_object_pool_debug);
#define GST_CAT_DEFAULT gst_mini_object_pool_debug

#define GST_MINI_OBJECT_POOL_LOCK(pool)   (g_rec_mutex_lock(&pool->priv->rec_lock))
#define GST_MINI_OBJECT_POOL_UNLOCK(pool) (g_rec_mutex_unlock(&pool->priv->rec_lock))

struct _GstMiniObjectPoolPrivate
{
  GstAtomicQueue *queue;
  GstPoll *poll;

  GRecMutex rec_lock;

  gboolean started;
  gboolean active;
  gint outstanding;             /* number of mini-objects that are in use */

  gboolean configured;
  GstStructure *config;

  guint size;
  guint min_objects;
  guint max_objects;
  guint cur_objects;
};

static void gst_mini_object_pool_finalize (GObject * object);

G_DEFINE_TYPE_WITH_PRIVATE (GstMiniObjectPool, gst_mini_object_pool,
    GST_TYPE_OBJECT);

static gboolean default_start (GstMiniObjectPool * pool);
static gboolean default_stop (GstMiniObjectPool * pool);
static gboolean default_set_config (GstMiniObjectPool * pool,
    GstStructure * config);
static GstFlowReturn default_acquire_object (GstMiniObjectPool * pool,
    GstMiniObject ** object, GstMiniObjectPoolAcquireParams * params);
static void default_free_object (GstMiniObjectPool * pool,
    GstMiniObject * buffer);
static void default_release_object (GstMiniObjectPool * pool,
    GstMiniObject * object);

static void
gst_mini_object_pool_class_init (GstMiniObjectPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_mini_object_pool_finalize;

  klass->start = default_start;
  klass->stop = default_stop;
  klass->set_config = default_set_config;
  klass->acquire_object = default_acquire_object;
  klass->reset_object = NULL;
  klass->alloc_object = NULL;
  klass->release_object = default_release_object;
  klass->free_object = default_free_object;

  GST_DEBUG_CATEGORY_INIT (gst_mini_object_pool_debug, "miniobjectpool", 0,
      "miniobjectpool debug");
}

static void
gst_mini_object_pool_init (GstMiniObjectPool * pool)
{
  GstMiniObjectPoolPrivate *priv;

  priv = pool->priv = gst_mini_object_pool_get_instance_private (pool);

  g_rec_mutex_init (&priv->rec_lock);

  priv->poll = gst_poll_new_timer ();
  priv->queue = gst_atomic_queue_new (16);
  pool->flushing = 1;
  priv->active = FALSE;
  priv->configured = FALSE;
  priv->started = FALSE;
  priv->config =
      gst_structure_new_id_empty (GST_QUARK (MINI_OBJECT_POOL_CONFIG));
  gst_mini_object_pool_config_set_params (priv->config, 0, 0);
  /* 1 control write for flushing - the flush token */
  gst_poll_write_control (priv->poll);
  /* 1 control write for marking that we are not waiting for poll - the wait token */
  gst_poll_write_control (priv->poll);

  GST_DEBUG_OBJECT (pool, "created");
}

static void
gst_mini_object_pool_finalize (GObject * object)
{
  GstMiniObjectPool *pool;
  GstMiniObjectPoolPrivate *priv;

  pool = GST_MINI_OBJECT_POOL_CAST (object);
  priv = pool->priv;

  GST_DEBUG_OBJECT (pool, "%p finalize", pool);

  gst_mini_object_pool_set_active (pool, FALSE);
  gst_atomic_queue_unref (priv->queue);
  gst_poll_free (priv->poll);
  gst_structure_free (priv->config);
  g_rec_mutex_clear (&priv->rec_lock);

  G_OBJECT_CLASS (gst_mini_object_pool_parent_class)->finalize (object);
}

static GstFlowReturn
do_alloc_object (GstMiniObjectPool * pool, GstMiniObject ** object,
    GstMiniObjectPoolAcquireParams * params)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;
  GstFlowReturn result;
  gint cur_objects, max_objects;
  GstMiniObjectPoolClass *pclass;

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  if (G_UNLIKELY (!pclass->alloc_object))
    goto no_function;

  max_objects = priv->max_objects;

  /* increment the allocation counter */
  cur_objects = g_atomic_int_add (&priv->cur_objects, 1);
  if (max_objects && cur_objects >= max_objects)
    goto max_reached;

  result = pclass->alloc_object (pool, object, params);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto alloc_failed;

  GST_LOG_OBJECT (pool, "allocated object %d/%d, %p", cur_objects,
      max_objects, *object);

  return result;

  /* ERRORS */
no_function:
  {
    GST_ERROR_OBJECT (pool, "no alloc function");
    return GST_FLOW_NOT_SUPPORTED;
  }
max_reached:
  {
    GST_DEBUG_OBJECT (pool, "max objects reached");
    g_atomic_int_add (&priv->cur_objects, -1);
    return GST_FLOW_EOS;
  }
alloc_failed:
  {
    GST_WARNING_OBJECT (pool, "alloc function failed");
    g_atomic_int_add (&priv->cur_objects, -1);
    return result;
  }
}

/* the default implementation for preallocating the mini-objectss in the pool */
static gboolean
default_start (GstMiniObjectPool * pool)
{
  guint i;
  GstMiniObjectPoolPrivate *priv = pool->priv;
  GstMiniObjectPoolClass *pclass;

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  /* we need to prealloc mini-objectss */
  for (i = 0; i < priv->min_objects; i++) {
    GstMiniObject *object;

    if (do_alloc_object (pool, &object, NULL) != GST_FLOW_OK)
      goto alloc_failed;

    /* release to the queue, we call the vmethod directly, we don't need to do
     * the other refcount handling right now. */
    if (G_LIKELY (pclass->release_object))
      pclass->release_object (pool, object);
  }
  return TRUE;

  /* ERRORS */
alloc_failed:
  {
    GST_WARNING_OBJECT (pool, "failed to allocate mini-object");
    return FALSE;
  }
}

/* must be called with the lock */
static gboolean
do_start (GstMiniObjectPool * pool)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;

  if (!priv->started) {
    GstMiniObjectPoolClass *pclass;

    pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

    GST_LOG_OBJECT (pool, "starting");
    /* start the pool, subclasses should allocate mini-objects and put them
     * in the queue */
    if (G_LIKELY (pclass->start)) {
      if (!pclass->start (pool))
        return FALSE;
    }
    priv->started = TRUE;
  }
  return TRUE;
}

static void
default_free_object (GstMiniObjectPool * pool, GstMiniObject * object)
{
  gst_mini_object_unref (object);
}

static void
do_free_object (GstMiniObjectPool * pool, GstMiniObject * object)
{
  GstMiniObjectPoolPrivate *priv;
  GstMiniObjectPoolClass *pclass;

  priv = pool->priv;
  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  g_atomic_int_add (&priv->cur_objects, -1);
  GST_LOG_OBJECT (pool, "freeing object %p (%u left)", object,
      priv->cur_objects);

  if (G_LIKELY (pclass->free_object))
    pclass->free_object (pool, object);
}

/* must be called with the lock */
static gboolean
default_stop (GstMiniObjectPool * pool)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;
  GstMiniObject *object;

  /* clear the pool */
  while ((object = gst_atomic_queue_pop (priv->queue))) {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* We put the object into the queue but did not finish writing control
         * yet, let's wait a bit and retry */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }
    do_free_object (pool, object);
  }
  return priv->cur_objects == 0;
}

/* must be called with the lock */
static gboolean
do_stop (GstMiniObjectPool * pool)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;

  if (priv->started) {
    GstMiniObjectPoolClass *pclass;

    pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

    GST_LOG_OBJECT (pool, "stopping");
    if (G_LIKELY (pclass->stop)) {
      if (!pclass->stop (pool))
        return FALSE;
    }
    priv->started = FALSE;
  }
  return TRUE;
}

/* must be called with the lock */
static void
do_set_flushing (GstMiniObjectPool * pool, gboolean flushing)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;
  GstMiniObjectPoolClass *pclass;

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  if (GST_MINI_OBJECT_POOL_IS_FLUSHING (pool) == flushing)
    return;

  if (flushing) {
    g_atomic_int_set (&pool->flushing, 1);
    /* Write the flush token to wake up any waiters */
    gst_poll_write_control (priv->poll);

    if (pclass->flush_start)
      pclass->flush_start (pool);
  } else {
    if (pclass->flush_stop)
      pclass->flush_stop (pool);

    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This should not really happen unless flushing and unflushing
         * happens on different threads. Let's wait a bit to get back flush
         * token from the thread that was setting it to flushing */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }

    g_atomic_int_set (&pool->flushing, 0);
  }
}

/**
 * gst_mini_object_pool_set_active:
 * @pool: a #GstMiniObjectPool
 * @active: the new active state
 *
 * Control the active state of @pool. When the pool is inactive, new calls to
 * gst_mini_object_pool_acquire_object() will return with %GST_FLOW_FLUSHING.
 *
 * Activating the pool will preallocate all resources in the pool based on
 * the configuration of the pool.
 *
 * Deactivating will free the resources again when there are no outstanding
 * objects. When there are outstanding objects, they will be freed as soon as
 * they are all returned to the pool.
 *
 * Returns: %FALSE when the pool was not configured or when preallocation of the
 * objects failed.
 */
gboolean
gst_mini_object_pool_set_active (GstMiniObjectPool * pool, gboolean active)
{
  gboolean res = TRUE;
  GstMiniObjectPoolPrivate *priv;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), FALSE);

  GST_LOG_OBJECT (pool, "active %d", active);

  priv = pool->priv;

  GST_MINI_OBJECT_POOL_LOCK (pool);
  /* just return if we are already in the right state */
  if (priv->active == active)
    goto was_ok;

  /* we need to be configured */
  if (!priv->configured)
    goto not_configured;

  if (active) {
    if (!do_start (pool))
      goto start_failed;

    /* flush_stop my release objects, setting to active to avoid running
     * do_stop while activating the pool */
    priv->active = TRUE;

    /* unset the flushing state now */
    do_set_flushing (pool, FALSE);
  } else {
    gint outstanding;

    /* set to flushing first */
    do_set_flushing (pool, TRUE);

    /* when all objects are in the pool, free them. Else they will be
     * freed when they are released */
    outstanding = g_atomic_int_get (&priv->outstanding);
    GST_LOG_OBJECT (pool, "outstanding objects %d", outstanding);
    if (outstanding == 0) {
      if (!do_stop (pool))
        goto stop_failed;
    }

    priv->active = FALSE;
  }
  GST_MINI_OBJECT_POOL_UNLOCK (pool);

  return res;

was_ok:
  {
    GST_DEBUG_OBJECT (pool, "pool was in the right state");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return TRUE;
  }
not_configured:
  {
    GST_ERROR_OBJECT (pool, "pool was not configured");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return FALSE;
  }
start_failed:
  {
    GST_ERROR_OBJECT (pool, "start failed");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return FALSE;
  }
stop_failed:
  {
    GST_WARNING_OBJECT (pool, "stop failed");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return FALSE;
  }
}

/**
 * gst_mini_object_pool_is_active:
 * @pool: a #GstMiniObjectPool
 *
 * Check if @pool is active. A pool can be activated with the
 * gst_mini_object_pool_set_active() call.
 *
 * Returns: %TRUE when the pool is active.
 */
gboolean
gst_mini_object_pool_is_active (GstMiniObjectPool * pool)
{
  gboolean res;

  GST_MINI_OBJECT_POOL_LOCK (pool);
  res = pool->priv->active;
  GST_MINI_OBJECT_POOL_UNLOCK (pool);

  return res;
}

static gboolean
default_set_config (GstMiniObjectPool * pool, GstStructure * config)
{
  GstMiniObjectPoolPrivate *priv = pool->priv;
  guint min_objects, max_objects;

  /* parse the config and keep around */
  if (!gst_mini_object_pool_config_get_params (config, &min_objects,
          &max_objects))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  priv->min_objects = min_objects;
  priv->max_objects = max_objects;
  priv->cur_objects = 0;

  return TRUE;

wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
}

/**
 * gst_mini_object_pool_set_config:
 * @pool: a #GstMiniObjectPool
 * @config: (transfer full): a #GstStructure
 *
 * Set the configuration of the pool. If the pool is already configured, and
 * the configuration haven't change, this function will return %TRUE. If the
 * pool is active, this method will return %FALSE and active configuration
 * will remain. Objects allocated form this pool must be returned or else this
 * function will do nothing and return %FALSE.
 *
 * @config is a #GstStructure that contains the configuration parameters for
 * the pool. A default and mandatory set of parameters can be configured with
 * gst_mini_object_pool_config_set_params() and
 *  gst_mini_object_pool_config_add_option().
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_mini_object_pool_get_config().
 *
 * This function takes ownership of @config.
 *
 * Returns: %TRUE when the configuration could be set.
 */
gboolean
gst_mini_object_pool_set_config (GstMiniObjectPool * pool,
    GstStructure * config)
{
  gboolean result;
  GstMiniObjectPoolClass *pclass;
  GstMiniObjectPoolPrivate *priv;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  priv = pool->priv;

  GST_MINI_OBJECT_POOL_LOCK (pool);

  /* nothing to do if config is unchanged */
  if (priv->configured && gst_structure_is_equal (config, priv->config))
    goto config_unchanged;

  /* can't change the settings when active */
  if (priv->active)
    goto was_active;

  /* we can't change when outstanding objects */
  if (g_atomic_int_get (&priv->outstanding) != 0)
    goto have_outstanding;

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  /* set the new config */
  if (G_LIKELY (pclass->set_config))
    result = pclass->set_config (pool, config);
  else
    result = FALSE;

  /* save the config regardless of the result so user can read back the
   * modified config and evaluate if the changes are acceptable */
  if (priv->config)
    gst_structure_free (priv->config);
  priv->config = config;

  if (result) {
    /* now we are configured */
    priv->configured = TRUE;
  }
  GST_MINI_OBJECT_POOL_UNLOCK (pool);

  return result;

config_unchanged:
  {
    gst_structure_free (config);
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return TRUE;
  }
  /* ERRORS */
was_active:
  {
    gst_structure_free (config);
    GST_INFO_OBJECT (pool, "can't change config, we are active");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return FALSE;
  }
have_outstanding:
  {
    gst_structure_free (config);
    GST_WARNING_OBJECT (pool, "can't change config, have outstanding objects");
    GST_MINI_OBJECT_POOL_UNLOCK (pool);
    return FALSE;
  }
}

/**
 * gst_mini_object_pool_get_config:
 * @pool: a #GstMiniObjectPool
 *
 * Get a copy of the current configuration of the pool. This configuration
 * can either be modified and used for the gst_mini_object_pool_set_config() call
 * or it must be freed after usage.
 *
 * Returns: (transfer full): a copy of the current configuration of @pool. use
 * gst_structure_free() after usage or gst_mini_object_pool_set_config().
 */
GstStructure *
gst_mini_object_pool_get_config (GstMiniObjectPool * pool)
{
  GstStructure *result;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), NULL);

  GST_MINI_OBJECT_POOL_LOCK (pool);
  result = gst_structure_copy (pool->priv->config);
  GST_MINI_OBJECT_POOL_UNLOCK (pool);

  return result;
}

static const gchar *empty_option[] = { NULL };

/**
 * gst_mini_object_pool_get_options:
 * @pool: a #GstMiniObjectPool
 *
 * Get a %NULL terminated array of string with supported pool options for
 * @pool. An option would typically be enabled with
 * gst_mini_object_pool_config_add_option().
 *
 * Returns: (array zero-terminated=1) (transfer none): a %NULL terminated array
 *          of strings.
 */
const gchar **
gst_mini_object_pool_get_options (GstMiniObjectPool * pool)
{
  GstMiniObjectPoolClass *pclass;
  const gchar **result;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), NULL);

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  if (G_LIKELY (pclass->get_options)) {
    if ((result = pclass->get_options (pool)) == NULL)
      goto invalid_result;
  } else
    result = empty_option;

  return result;

  /* ERROR */
invalid_result:
  {
    g_warning ("pool subclass returned NULL options");
    return empty_option;
  }
}

/**
 * gst_mini_object_pool_has_option:
 * @pool: a #GstMiniObjectPool
 * @option: an option
 *
 * Check if the pool supports @option.
 *
 * Returns: %TRUE if the pool contains @option.
 */
gboolean
gst_mini_object_pool_has_option (GstMiniObjectPool * pool, const gchar * option)
{
  guint i;
  const gchar **options;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), FALSE);
  g_return_val_if_fail (option != NULL, FALSE);

  options = gst_mini_object_pool_get_options (pool);

  for (i = 0; options[i]; i++) {
    if (g_str_equal (options[i], option))
      return TRUE;
  }
  return FALSE;
}

/**
 * gst_mini_object_pool_config_set_params:
 * @config: a #GstMiniObjectPool configuration
 * @min_objects: the minimum amount of mini-objects to allocate.
 * @max_objects: the maximum amount of mini-objects to allocate or 0 for unlimited.
 *
 * Configure @config with the given parameters.
 */
void
gst_mini_object_pool_config_set_params (GstStructure * config,
    guint min_objects, guint max_objects)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (max_objects == 0 || min_objects <= max_objects);

  gst_structure_id_set (config, GST_QUARK (MIN_OBJECTS), G_TYPE_UINT,
      min_objects, GST_QUARK (MAX_OBJECTS), G_TYPE_UINT, max_objects, NULL);
}

/**
 * gst_mini_object_pool_config_add_option:
 * @config: a #GstMiniObjectPool configuration
 * @option: an option to add
 *
 * Enabled the option in @config. This will instruct the pool to enable
 * the specified option on the mini-objects that it allocates.
 *
 * The supported options by each pool can be retrieved with
 * gst_mini_object_pool_get_options().
 */
void
gst_mini_object_pool_config_add_option (GstStructure * config,
    const gchar * option)
{
  const GValue *value;
  GValue option_value = { 0, };
  guint i, len;

  g_return_if_fail (config != NULL);

  value = gst_structure_id_get_value (config, GST_QUARK (OPTIONS));
  if (value) {
    len = gst_value_array_get_size (value);
    for (i = 0; i < len; ++i) {
      const GValue *nth_val = gst_value_array_get_value (value, i);

      if (g_str_equal (option, g_value_get_string (nth_val)))
        return;
    }
  } else {
    GValue new_array_val = { 0, };

    g_value_init (&new_array_val, GST_TYPE_ARRAY);
    gst_structure_id_take_value (config, GST_QUARK (OPTIONS), &new_array_val);
    value = gst_structure_id_get_value (config, GST_QUARK (OPTIONS));
  }
  g_value_init (&option_value, G_TYPE_STRING);
  g_value_set_string (&option_value, option);
  gst_value_array_append_and_take_value ((GValue *) value, &option_value);
}

/**
 * gst_mini_object_pool_config_n_options:
 * @config: a #GstMiniObjectPool configuration
 *
 * Retrieve the number of values currently stored in the options array of the
 * @config structure.
 *
 * Returns: the options array size as a #guint.
 */
guint
gst_mini_object_pool_config_n_options (GstStructure * config)
{
  const GValue *value;
  guint size = 0;

  g_return_val_if_fail (config != NULL, 0);

  value = gst_structure_id_get_value (config, GST_QUARK (OPTIONS));
  if (value) {
    size = gst_value_array_get_size (value);
  }
  return size;
}

/**
 * gst_mini_object_pool_config_get_option:
 * @config: a #GstMiniObjectPool configuration
 * @index: position in the option array to read
 *
 * Parse an available @config and get the option at @index of the options API
 * array.
 *
 * Returns: a #gchar of the option at @index.
 */
const gchar *
gst_mini_object_pool_config_get_option (GstStructure * config, guint index)
{
  const GValue *value;
  const gchar *ret = NULL;

  g_return_val_if_fail (config != NULL, 0);

  value = gst_structure_id_get_value (config, GST_QUARK (OPTIONS));
  if (value) {
    const GValue *option_value;

    option_value = gst_value_array_get_value (value, index);
    if (option_value)
      ret = g_value_get_string (option_value);
  }
  return ret;
}

/**
 * gst_mini_object_pool_config_has_option:
 * @config: a #GstMiniObjectPool configuration
 * @option: an option
 *
 * Check if @config contains @option.
 *
 * Returns: %TRUE if the options array contains @option.
 */
gboolean
gst_mini_object_pool_config_has_option (GstStructure * config,
    const gchar * option)
{
  const GValue *value;
  guint i, len;

  g_return_val_if_fail (config != NULL, 0);

  value = gst_structure_id_get_value (config, GST_QUARK (OPTIONS));
  if (value) {
    len = gst_value_array_get_size (value);
    for (i = 0; i < len; ++i) {
      const GValue *nth_val = gst_value_array_get_value (value, i);

      if (g_str_equal (option, g_value_get_string (nth_val)))
        return TRUE;
    }
  }
  return FALSE;
}

/**
 * gst_mini_object_pool_config_get_params:
 * @config: (transfer none): a #GstMiniObjectPool configuration
 * @min_objects: (out) (allow-none): the minimum amount of mini-objects to allocate.
 * @max_objects: (out) (allow-none): the maximum amount of mini-objects to allocate
 *     or 0 for unlimited.
 *
 * Get the configuration values from @config.
 *
 * Returns: %TRUE if all parameters could be fetched.
 */
gboolean
gst_mini_object_pool_config_get_params (GstStructure * config,
    guint * min_objects, guint * max_objects)
{
  g_return_val_if_fail (config != NULL, FALSE);

  return gst_structure_id_get (config,
      GST_QUARK (MIN_OBJECTS), G_TYPE_UINT, min_objects,
      GST_QUARK (MAX_OBJECTS), G_TYPE_UINT, max_objects, NULL);
}

/**
 * gst_mini_object_pool_config_validate_params:
 * @config: (transfer none): a #GstMiniObjectPool configuration
 * @min_objects: the expected minimum amount of objects to allocate.
 * @max_objects: the expect maximum amount of objects to allocate or 0 for unlimited.
 *
 * Validate that changes made to @config are still valid in the context of the
 * expected parameters. This function is a helper that can be used to validate
 * changes made by a pool to a config when gst_mini_object_pool_set_config()
 * returns %FALSE. This expects that @caps haven't changed and that
 * @min_objects aren't lower then what we initially expected.
 * This does not check if options or allocator parameters are still valid,
 * won't check if size have changed, since changing the size is valid to adapt
 * padding.
 *
 * Returns: %TRUE, if the parameters are valid in this context.
 */
gboolean
gst_mini_object_pool_config_validate_params (GstStructure * config,
    guint min_objects, G_GNUC_UNUSED guint max_objects)
{
  guint newmin;
  gboolean ret = FALSE;

  g_return_val_if_fail (config != NULL, FALSE);

  gst_mini_object_pool_config_get_params (config, &newmin, NULL);

  if (newmin >= min_objects)
    ret = TRUE;

  return ret;
}

static GstFlowReturn
default_acquire_object (GstMiniObjectPool * pool, GstMiniObject ** object,
    GstMiniObjectPoolAcquireParams * params)
{
  GstFlowReturn result;
  GstMiniObjectPoolPrivate *priv = pool->priv;

  while (TRUE) {
    if (G_UNLIKELY (GST_MINI_OBJECT_POOL_IS_FLUSHING (pool)))
      goto flushing;

    /* try to get a mini-object from the queue */
    *object = gst_atomic_queue_pop (priv->queue);
    if (G_LIKELY (*object)) {
      while (!gst_poll_read_control (priv->poll)) {
        if (errno == EWOULDBLOCK) {
          /* We put the object into the queue but did not finish writing control
           * yet, let's wait a bit and retry */
          g_thread_yield ();
          continue;
        } else {
          /* Critical error but GstPoll already complained */
          break;
        }
      }
      result = GST_FLOW_OK;
      GST_LOG_OBJECT (pool, "acquired mini-object %p", *object);
      break;
    }

    /* no object, try to allocate some more */
    GST_LOG_OBJECT (pool, "no mini-object, trying to allocate");
    result = do_alloc_object (pool, object, params);
    if (G_LIKELY (result == GST_FLOW_OK))
      /* we have a object, return it */
      break;

    if (G_UNLIKELY (result != GST_FLOW_EOS))
      /* something went wrong, return error */
      break;

    /* check if we need to wait */
    if (params && (params->flags & GST_MINI_OBJECT_POOL_ACQUIRE_FLAG_DONTWAIT)) {
      GST_LOG_OBJECT (pool, "no more objects");
      break;
    }

    /* now we release the control socket, we wait for a object release or
     * flushing */
    if (!gst_poll_read_control (pool->priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This means that we have two threads trying to allocate objects
         * already, and the other one already got the wait token. This
         * means that we only have to wait for the poll now and not write the
         * token afterwards: we will be woken up once the other thread is
         * woken up and that one will write the wait token it removed */
        GST_LOG_OBJECT (pool, "waiting for free objects or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      } else {
        /* This is a critical error, GstPoll already gave a warning */
        result = GST_FLOW_ERROR;
        break;
      }
    } else {
      /* We're the first thread waiting, we got the wait token and have to
       * write it again later
       * OR
       * We're a second thread and just consumed the flush token and block all
       * other threads, in which case we must not wait and give it back
       * immediately */
      if (!GST_MINI_OBJECT_POOL_IS_FLUSHING (pool)) {
        GST_LOG_OBJECT (pool, "waiting for free mini-objects or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      }
      gst_poll_write_control (pool->priv->poll);
    }
  }

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (pool, "we are flushing");
    return GST_FLOW_FLUSHING;
  }
}

static inline void
dec_outstanding (GstMiniObjectPool * pool)
{
  if (g_atomic_int_dec_and_test (&pool->priv->outstanding)) {
    /* all mini-objects are returned to the pool, see if we need to free them */
    if (GST_MINI_OBJECT_POOL_IS_FLUSHING (pool)) {
      /* take the lock so that set_active is not run concurrently */
      GST_MINI_OBJECT_POOL_LOCK (pool);
      /* now that we have the lock, check if we have been de-activated with
       * outstanding mini-objects */
      if (!pool->priv->active)
        do_stop (pool);

      GST_MINI_OBJECT_POOL_UNLOCK (pool);
    }
  }
}

/**
 * gst_mini_object_pool_acquire_object:
 * @pool: a #GstMiniObjectPool
 * @object: (out): a location for a #GstMiniObject
 * @params: (transfer none) (allow-none): parameters.
 *
 * Acquire a mini-object from @pool. @object should point to a memory location that
 * can hold a pointer to the new mini-object.
 *
 * @params can be %NULL or contain optional parameters to influence the
 * allocation.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the pool is
 * inactive.
 */
GstFlowReturn
gst_mini_object_pool_acquire_object (GstMiniObjectPool * pool,
    GstMiniObject ** object, GstMiniObjectPoolAcquireParams * params)
{
  GstMiniObjectPoolClass *pclass;
  GstFlowReturn result;

  g_return_val_if_fail (GST_IS_MINI_OBJECT_POOL (pool), GST_FLOW_ERROR);
  g_return_val_if_fail (object != NULL, GST_FLOW_ERROR);

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  /* assume we'll have one more outstanding object we need to do that so
   * that concurrent set_active doesn't clear the objects */
  g_atomic_int_inc (&pool->priv->outstanding);

  if (G_LIKELY (pclass->acquire_object))
    result = pclass->acquire_object (pool, object, params);
  else
    result = GST_FLOW_NOT_SUPPORTED;

  if (G_LIKELY (result == GST_FLOW_OK)) {
    /* all objects from the pool point to the pool and have the refcount of the
     * pool incremented */
    (*object)->pool = gst_object_ref (pool);
  } else {
    dec_outstanding (pool);
  }

  return result;
}

static void
default_release_object (GstMiniObjectPool * pool, GstMiniObject * object)
{
  GST_LOG_OBJECT (pool, "released object %p %d", object,
      GST_MINI_OBJECT_FLAGS (object));

  /* keep it around in our queue */
  gst_atomic_queue_push (pool->priv->queue, object);
  gst_poll_write_control (pool->priv->poll);
}

/**
 * gst_mini_object_pool_release_object:
 * @pool: a #GstMiniObjectPool
 * @object: (transfer full): a #GstMiniObject
 *
 * Release @object to @pool. @object should have previously been allocated from
 * @pool with gst_mini_object_pool_acquire_object().
 *
 * This function is usually called automatically when the last ref on @object
 * disappears.
 */
void
gst_mini_object_pool_release_object (GstMiniObjectPool * pool,
    GstMiniObject * object)
{
  GstMiniObjectPoolClass *pclass;

  g_return_if_fail (GST_IS_MINI_OBJECT_POOL (pool));
  g_return_if_fail (object != NULL);

  /* check that the object is ours, all mini-objects returned to the pool have the
   * pool member set to NULL and the pool refcount decreased */
  if (!g_atomic_pointer_compare_and_exchange (&object->pool, pool, NULL))
    return;

  pclass = GST_MINI_OBJECT_POOL_GET_CLASS (pool);

  /* reset the object when needed */
  if (G_LIKELY (pclass->reset_object))
    pclass->reset_object (pool, object);

  if (G_LIKELY (pclass->release_object))
    pclass->release_object (pool, object);

  dec_outstanding (pool);

  /* decrease the refcount that the object had to us */
  gst_object_unref (pool);
}

/**
 * gst_mini_object_pool_set_flushing:
 * @pool: a #GstMiniObjectPool
 * @flushing: whether to start or stop flushing
 *
 * Enable or disable the flushing state of a @pool without freeing or
 * allocating mini-objects.
 */
void
gst_mini_object_pool_set_flushing (GstMiniObjectPool * pool, gboolean flushing)
{
  GstMiniObjectPoolPrivate *priv;

  g_return_if_fail (GST_IS_MINI_OBJECT_POOL (pool));

  GST_LOG_OBJECT (pool, "flushing %d", flushing);

  priv = pool->priv;

  GST_MINI_OBJECT_POOL_LOCK (pool);

  if (!priv->active) {
    GST_WARNING_OBJECT (pool, "can't change flushing state of inactive pool");
    goto done;
  }

  do_set_flushing (pool, flushing);

done:
  GST_MINI_OBJECT_POOL_UNLOCK (pool);
}

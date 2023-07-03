/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalysismeta.c
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstanalysismeta.h"

GST_DEBUG_CATEGORY_STATIC (an_relation_meta_debug);
#define GST_CAT_AN_RELATION an_relation_meta_debug

static char invalid_relatable_type_name[] = "_invalid";

/**
 * gst_analytic_relatable_mtd_get_type:
 * @instance: Instance of #GstAnalyticRelatableMtd 
 * Get analysis result type.
 *
 * Returns: quark associated with type.
 *
 * Since: 1.23
 */
GQuark
gst_analytic_relatable_mtd_get_type (GstAnalyticRelatableMtd * handle)
{
  GstAnalyticRelatableMtdData *rlt;
  rlt = gst_analytic_relation_meta_get_relatable_mtd_data (handle->ptr,
      handle->id);
  if (rlt == NULL) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return g_quark_from_static_string (invalid_relatable_type_name);
  }

  return rlt->analysis_type;
}

/**
 * gst_analytic_relatable_mtd_get_id:
 * @instance: Instance of #GstAnalyticRelatableMtd 
 * Get instance id
 *
 * Returns: Id of @instance
 *
 * Since: 1.23
 */
guint
gst_analytic_relatable_mtd_get_id (GstAnalyticRelatableMtd * handle)
{
  return handle->id;
}

/**
 * gst_analytic_relatable_mtd_get_size:
 * @instance Instance of #GstAnalyticRelatableMtd
 * Get instance size
 *
 * Returns: Size (in bytes) of this instance or 0 on failure.
 *
 * Since: 1.23
 */
gsize
gst_analytic_relatable_mtd_get_size (GstAnalyticRelatableMtd * handle)
{
  GstAnalyticRelatableMtdData *rlt;
  rlt = gst_analytic_relation_meta_get_relatable_mtd_data (handle->ptr,
      handle->id);
  if (rlt == NULL) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return 0;
  }

  return rlt->size;
}

/**
 * GstAnalyticRelationMetaPriv:
 * Structure storing analytic metadata data and their relations.
 * (content analysis-meta) attached to #GstBuffer.
 * 
 * Since: 1.23
 */
typedef struct _GstAnalyticRelationMetaPriv
{

  /* Adjacency-matrix */
  guint8 **adj_mat;

  /* Lookup (offset relative to analysis_results) of relatable metadata */
  gsize *relatable_mtd_data_lookup;

  /* Relation order */
  gsize rel_order;

  /* Increment used when relation order grow */
  gsize rel_order_increment;

  /* R/W lock to synchronize access to adj_mat, rel_order */
  GRWLock relations_rw_lock;

  /* Analysis metadata based on GstAnalyticRelatableMtdData */
  gint8 *analysis_results;

  /* Current writing location in analysis_results buffer */
  gsize offset;

  /* Size of analysis_results buffer */
  gsize max_size;

  /* Increment of analysis_results */
  gsize max_size_increment;

  /* Number of analytic metadata (GstAnalyticRelatableMtdData) in 
   * analysis_results */
  gsize length;

} GstAnalyticRelationMetaPriv;

/**
 * gst_analytic_relation_get_length:
 * @instance Instance of #GstAnalyticRelationMeta
 * Get number of relatable meta attached to instance
 *
 * Returns: Number of analysis-meta attached to this
 * instance.
 * Since: 1.23
 */
gsize
gst_analytic_relation_get_length (GstAnalyticRelationMeta * instance)
{
  gsize rv;
  GstAnalyticRelationMetaPriv *priv;
  g_return_val_if_fail (instance, 0);
  g_return_val_if_fail (instance->priv, 0);

  priv = (GstAnalyticRelationMetaPriv *) instance->priv;
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  rv = priv->length;
  g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  return rv;
}

/**
 * gst_analytic_relation_adj_mat_create:
 * @order: Order or the adjacency-matrix to create.
 * Create a new adjacency-matrix (array of MxN where M and N are equal
 * to order).
 *
 * Returns: new adjacency-matrix
 *
 * Since: 1.23
 */
static guint8 **
gst_analytic_relation_adj_mat_create (gsize order)
{
  guint8 **adj_mat, *data;
  gsize sz = sizeof (guint8 *) * order + sizeof (guint8) * order * order;
  adj_mat = (guint8 **) g_malloc0 (sz);
  data = (guint8 *) (adj_mat + order);
  for (gsize r = 0; r < order; r++) {
    adj_mat[r] = (data + order * r);
  }
  return adj_mat;
}

/**
 * gst_analytic_relation_adj_mat_dup:
 * @adj_mat: Adjcency-matrix (array or MxN)
 * @order: Order of the existing matrix
 * @new_order: Order of the matrix to create
 * Duplicate adj_mat to a newly allocated array new_order x new_order dimension
 * while keep values of adj_mat at the same indexes in the new array.
 *
 * Returns: New adjacency matrix with maintained values.
 *
 * Since: 1.23
 */
static guint8 **
gst_analytic_relation_adj_mat_dup (guint8 ** adj_mat, gsize order,
    gsize new_order)
{
  guint8 **new_adj_mat = gst_analytic_relation_adj_mat_create (new_order);
  for (gsize r = 0; r < order; r++) {
    memcpy (new_adj_mat[r], adj_mat[r], sizeof (guint8) * order);
  }
  return new_adj_mat;
}

/**
 * gst_analytic_relation_meta_api_get_type:
 * Returns: GType of GstAnalyticRelationMeta
 *
 * Since: 1.23
 */
GType
gst_analytic_relation_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { NULL };
  if (g_once_init_enter (&type)) {
    GType newType =
        gst_meta_api_type_register ("GstAnalyticRelationMetaAPI", tags);
    GST_DEBUG_CATEGORY_INIT (an_relation_meta_debug, "anrelmeta",
        GST_DEBUG_FG_BLACK, "Content analysis meta relations meta");
    g_once_init_leave (&type, newType);
  }
  return type;
}

static gboolean
gst_analytic_relation_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstAnalyticRelationMeta *an_rel_meta = (GstAnalyticRelationMeta *) meta;
  GstAnalyticRelationMetaPriv *priv = NULL;
  GstAnalyticRelationMetaInitParams *rel_params = params;
  an_rel_meta->next_id = 0;

  if (!params) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return FALSE;
  }

  GST_CAT_TRACE (GST_CAT_AN_RELATION, "Relation order:%lu",
      *((gsize *) params));

  priv = g_slice_new (GstAnalyticRelationMetaPriv);
  priv->rel_order_increment = rel_params->initial_relation_order;
  priv->rel_order = priv->rel_order_increment;
  if (priv->rel_order > 0) {
    priv->adj_mat = gst_analytic_relation_adj_mat_create (priv->rel_order);
    priv->relatable_mtd_data_lookup =
        g_malloc0 (sizeof (gpointer) * priv->rel_order);
  }
  priv->offset = 0;
  priv->max_size = priv->max_size_increment = rel_params->initial_buf_size;
  priv->analysis_results = g_malloc (rel_params->initial_buf_size);
  priv->length = 0;

  g_rw_lock_init (&priv->relations_rw_lock);
  an_rel_meta->priv = priv;

  GST_CAT_DEBUG (GST_CAT_AN_RELATION,
      "Content analysis meta-relation meta(%p, order=%lu) created for"
      "buffer(%p)",
      (gpointer) an_rel_meta, *(gsize *) params, (gpointer) buffer);
  return TRUE;
}

static void
gst_analytic_relation_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstAnalyticRelationMeta *an_rel_meta = (GstAnalyticRelationMeta *) meta;
  GstAnalyticRelationMetaPriv *priv = NULL;
  GST_CAT_TRACE (GST_CAT_AN_RELATION,
      "Content analysis meta-data(%p) freed for buffer(%p)",
      (gpointer) an_rel_meta, (gpointer) buffer);

  if (an_rel_meta) {
    priv = an_rel_meta->priv;
    if (priv) {
      if (priv->analysis_results)
        g_free (priv->analysis_results);

      if (priv->adj_mat)
        g_free (priv->adj_mat);

      if (priv->relatable_mtd_data_lookup) {
        g_free (priv->relatable_mtd_data_lookup);
        priv->relatable_mtd_data_lookup = NULL;
      }

      g_rw_lock_clear (&priv->relations_rw_lock);
      g_slice_free (GstAnalyticRelationMetaPriv, an_rel_meta->priv);
    }
  }
}

const GstMetaInfo *
gst_analytic_relation_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;
  if (g_once_init_enter ((GstMetaInfo **) & info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_ANALYTIC_RELATION_META_API_TYPE,
        "GstAnalyticRelationMeta",
        sizeof (GstAnalyticRelationMeta),
        gst_analytic_relation_meta_init,
        gst_analytic_relation_meta_free,
        NULL);
    g_once_init_leave ((GstMetaInfo **) & info, (GstMetaInfo *) meta);
  }
  return info;
}

/**
 * gst_analytic_relation_meta_bfs:
 * @start: start vertex
 * @adj_mat: graph's adjacency matrix
 * @adj_mat_order: order of the adjacency matrix (number of vertex in the graph)
 * @edge_mask: allow to select edge type we are interested by.
 * @max_span: Maximum number of edge to traverse from start vertex while
 * exploring graph.
 * @level: array of at least @adj_mat_order elements that will be filled with
 *    number of edge to traverse to reach @start from the vertex identified by
 *    the array index. (Ex: start=1 and level[3]=2, mean we need to traverse 2
 *    edges from vertex 2 to vertex 3. A value of -1 in @level mean this vertex
 *    is on reachable considering @edge_mask, @max_span and @adj_mat. @parent:
 *    array of at least @adj_mat order elements that will be filled with
 *    shortest path information. 
 * 
 * Define shortest path from vertex X and vertex @start, where X is the index of
 * @parent array. To find each node on the path we need to recursively do
 * ...parent[parent[parent[X]]] until value is @start. Value at index Y equal to
 * -1 mean there's no path from vertex Y to vertex @start. 
 *
 * Since: 1.23
 */
static void
gst_analytic_relation_meta_bfs (gint start, const guint8 ** adj_mat,
    gsize adj_mat_order, guint8 edge_mask, gsize max_span, gint * level,
    gint * parent)
{
  GSList *frontier = NULL;
  GSList *iter;
  GSList *next_frontier;
  gsize i = 1;
  memset (level, -1, sizeof (gint) * adj_mat_order);
  memset (parent, -1, sizeof (gint) * adj_mat_order);

  GST_CAT_TRACE (GST_CAT_AN_RELATION,
      "Performing bfs to find relation(%x) starting from %d with less than %lu"
      " edges from start", edge_mask, start, max_span);

  // vertex that has a relation with itself 
  if (adj_mat[start][start] & edge_mask) {
    level[start] = 0;
  }

  frontier = g_slist_prepend (frontier, GINT_TO_POINTER (start));

  while (frontier && i <= max_span) {
    next_frontier = NULL;
    for (iter = frontier; iter; iter = iter->next) {
      for (gsize j = 0; j < adj_mat_order; j++) {
        if (adj_mat[(gsize) GPOINTER_TO_INT (iter->data)][j] & edge_mask) {
          if (level[j] == -1) {
            level[j] = i;
            parent[j] = GPOINTER_TO_INT (iter->data);
            GST_CAT_TRACE (GST_CAT_AN_RELATION, "Parent of %lu is %d", j,
                parent[j]);
            next_frontier =
                g_slist_prepend (next_frontier, GINT_TO_POINTER ((gint) j));
          }
        }
      }
    }
    g_slist_free (frontier);
    frontier = next_frontier;
  }
  g_slist_free (frontier);
}

/**
 * gst_analytic_relation_meta_get_next_id:
 * @meta a #GstAnalyticRelationMeta from which we want to get next id.
 *
 * Get next id and prepare for future request.
 *
 * Returns: next id
 *
 * Since: 1.23
 */
gint
gst_analytic_relation_meta_get_next_id (GstAnalyticRelationMeta * meta)
{
  if (!meta) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return -1;
  }
  return g_atomic_int_add (&meta->next_id, 1);
}

/**
 * gst_analytic_relation_meta_get_relation:
 * @meta: (transfer none): a #GstAnalyticRelationMeta 
 * @an_meta_first: Id of first analysis-meta 
 * @an_meta_second: Id of second  analysis-meta
 *
 *
 * Returns: relation description between first and second analysis-meta.
 * Get relations between first and second analysis-meta.
 *
 * Since: 1.23
 */
GstAnalyticRelTypes
gst_analytic_relation_meta_get_relation (GstAnalyticRelationMeta * meta,
    gint an_meta_first, gint an_meta_second)
{
  GstAnalyticRelTypes types = GST_ANALYTIC_REL_TYPE_NONE;
  GstAnalyticRelationMetaPriv *priv = NULL;
  g_return_val_if_fail (meta, GST_ANALYTIC_REL_TYPE_NONE);
  g_return_val_if_fail (meta->priv, GST_ANALYTIC_REL_TYPE_NONE);

  priv = (GstAnalyticRelationMetaPriv *) meta->priv;
  g_return_val_if_fail (priv->adj_mat != NULL, GST_ANALYTIC_REL_TYPE_NONE);
  if (priv->rel_order > an_meta_first && priv->rel_order > an_meta_second) {
    g_rw_lock_reader_lock (&priv->relations_rw_lock);
    types = priv->adj_mat[an_meta_first][an_meta_second];
    g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  } else {
    GST_CAT_ERROR (GST_CAT_AN_RELATION,
        "an_meta_first(%i) and an_meta_second(%i) must be inferior to %lu",
        an_meta_first, an_meta_second, priv->rel_order);
  }
  return types;
}

/**
 * gst_analytic_relation_meta_set_relation:
 * @meta: (transfer none): Parameter to receive new maximum number of 
 *    analysis-meta described by relation.
 * @type: a #GstAnalyticRelTypes defining relation between two analysis-meta 
 * @an_meta_first: (transfer none) : first #GstAnalyticRelatableMtd
 * @an_meta_second: (transfer none) : second #GstAnalyticRelatableMtd
 *
 * Describe the relation (#GstAnalyticRelTypes) between @an_meta_first and 
 *    @an_meta_second.
 * Ids must have been obtained from @meta using 
 *    #gst_analytic_relation_meta_get_next_id.
 *
 * Returns: <0 on failure, 
 *          0 on success,
 *          >0 if @new_max_an_meta was updated. Caller should interpret 
 *                @new_max_an_meta update as inefficacy and if possible use a 
 *                mechanism to inform subsequent call to 
 *                gst_buffer_add_analytic_relation_meta_full to use this value.
 * 
 * Since: 1.23
 */
gint
gst_analytic_relation_meta_set_relation (GstAnalyticRelationMeta * meta,
    GstAnalyticRelTypes type, GstAnalyticRelatableMtd * an_meta_first,
    GstAnalyticRelatableMtd * an_meta_second)
{
  GstAnalyticRelationMetaPriv *priv;

  priv = (GstAnalyticRelationMetaPriv *) meta->priv;
  g_rw_lock_writer_lock (&priv->relations_rw_lock);
  g_return_val_if_fail (meta && meta->priv, -EINVAL);
  if (an_meta_first->id >= priv->rel_order
      || an_meta_second->id >= priv->rel_order) {
    g_rw_lock_writer_unlock (&priv->relations_rw_lock);
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return -EINVAL;
  }
  priv->adj_mat[an_meta_first->id][an_meta_second->id] = type;
  g_rw_lock_writer_unlock (&priv->relations_rw_lock);
  GST_CAT_TRACE (GST_CAT_AN_RELATION,
      "Relation %x set between %d and %d",
      type, an_meta_first->id, an_meta_second->id);
  return 0;
}

/**
 * gst_analytic_relation_meta_exist:
 * @rmeta: (transfer none): a #GstAnalyticRelationMeta describing analysis-meta 
 *    relation
 * @an_meta_first: First analysis-meta 
 * @an_meta_second: Second analysis-meta
 * @max_relation_span: Maximum number of relation between @an_meta_first_id and
 *    @an_meta_second_id.
 *    A value of 1 mean only only consider direct relation.
 * @cond_types: condition on relation types. 
 * @relations_path:(transfer full): If not null this list will be filled with 
 *    relation path between @an_meta_first_id and @an_meta_second_id. List value
 *    should be access with.
 *
 * Verify existence of relation(s) between @an_meta_first and
 * @an_meta_second according to relation condition @cond_types. It optionally
 * also return a shortest path of relations ( compliant with @cond_types)
 * between @an_meta_first_id and @an_meta_second_id.
 *
 * Returns: TRUE if a relation between exit between an_meta_first_id and 
 *  an_meta_second_id, otherwise FALSE.
 *
 * Since 1.23
 */
gboolean
gst_analytic_relation_meta_exist (GstAnalyticRelationMeta * rmeta,
    GstAnalyticRelatableMtd * an_meta_first,
    GstAnalyticRelatableMtd * an_meta_second,
    gint max_relation_span,
    GstAnalyticRelTypes cond_types, GSList ** relations_path)
{
  gboolean rv = FALSE;
  guint8 **adj_mat;
  gsize adj_mat_order, span;
  GstAnalyticRelationMetaPriv *priv = NULL;
  GSList *path = NULL;
  gint *level;
  gint *parent;
  g_return_val_if_fail (rmeta && rmeta->priv, FALSE);

  if (!rmeta) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return EINVAL;
  }
  priv = rmeta->priv;
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  adj_mat_order = priv->rel_order;

  if (adj_mat_order < (an_meta_first->id + 1)
      || adj_mat_order < (an_meta_second->id + 1)) {

    GST_CAT_DEBUG (GST_CAT_AN_RELATION,
        "Testing relation existence for analysis-meta that have no index in "
        "adj-mat.");

    g_rw_lock_reader_unlock (&priv->relations_rw_lock);
    return FALSE;
  }

  adj_mat = priv->adj_mat;
  if (max_relation_span < 0) {
    span = G_MAXSIZE;
  }
  // If we're only considering the direct relation (@max_relation_span <= 1) we can directly read the 
  // adjacency-matrix, 
  if (max_relation_span == 0 || max_relation_span == 1) {
    rv = (adj_mat[an_meta_first->id][an_meta_second->id] & cond_types) != 0;
    if (rv && relations_path) {
      path = g_slist_prepend (path, GINT_TO_POINTER (an_meta_second->id));
      path = g_slist_prepend (path, GINT_TO_POINTER (an_meta_first->id));
      *relations_path = path;
    }
  } else {

    level = g_slice_alloc (sizeof (gint) * adj_mat_order);
    parent = g_slice_alloc (sizeof (gint) * adj_mat_order);
    gst_analytic_relation_meta_bfs (an_meta_first->id,
        (const guint8 **) adj_mat, adj_mat_order, cond_types, span, level,
        parent);

    GST_CAT_TRACE (GST_CAT_AN_RELATION, "Adj order:%ld", adj_mat_order);

    rv = level[an_meta_second->id] != -1;
    if (rv && relations_path) {
      gint i = parent[an_meta_second->id];
      if (i != -1) {
        path = g_slist_prepend (path, GINT_TO_POINTER (an_meta_second->id));
        while (i != -1 && i != an_meta_second->id) {
          GST_CAT_TRACE (GST_CAT_AN_RELATION, "Relation parent of %d", i);
          path = g_slist_prepend (path, GINT_TO_POINTER (i));
          i = parent[i];
        }
      }
      *relations_path = path;
    }

    g_slice_free1 (sizeof (gint) * adj_mat_order, level);
    g_slice_free1 (sizeof (gint) * adj_mat_order, parent);
  }
  g_rw_lock_reader_unlock (&priv->relations_rw_lock);

  GST_CAT_TRACE (GST_CAT_AN_RELATION,
      "Relation %x between %d and %d %s",
      cond_types, an_meta_first->id, an_meta_second->id,
      rv ? "exist" : "does not exist");
  return rv;
}


/**
 * gst_buffer_add_analytic_relation_meta:
 * @buffer: (transfer none): a #GstBuffer
 * @init_params: Stucture defining initial maximum relation order and size.
 *
 * Attach a analysis-results-meta-relation  meta (#GstAnalyticRelationMeta)to @buffer.
 *
 * A #GstAnalyticRelationMeta is a metadata describing relation between other 
 * analysis meta. It's more efficient to use #gst_buffer_add_analytic_relation_meta_full
 * and providing the maximum number of analysis meta that will attached to a buffer.
 *
 * Returns: (transfer none) (nullable) : Newly attached #GstAnalyticRelationMeta
 *
 * Since 1.23
 */
GstAnalyticRelationMeta *
gst_buffer_add_analytic_relation_meta (GstBuffer * buffer)
{
  GstAnalyticRelationMetaInitParams init_params = { 5, 1024 };
  return gst_buffer_add_analytic_relation_meta_full (buffer, &init_params);
}

/**
 * gst_buffer_add_analytic_relation_meta_full:
 * @buffer: (transfer none): a #GstBuffer
 * @init_params: Initialization parameters
 *
 * Attache a analysis-results relation-meta (#GstAnalyticRelationMeta) to @buffer.
 *
 * A #GstAnalyticRelationMeta is a metadata describing relation between other 
 * analysis meta.
 *
 * Returns: (transfer none) (nullable) : Newly attached #GstAnalyticRelationMeta
 *
 * Since 1.23
 */
GstAnalyticRelationMeta *
gst_buffer_add_analytic_relation_meta_full (GstBuffer * buffer,
    GstAnalyticRelationMetaInitParams * init_params)
{
  GstAnalyticRelationMeta *meta = NULL;

  if (!buffer) {
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return NULL;
  }
  // We only want one relation-meta on a buffer, will check if one already
  // exist.
  meta = (GstAnalyticRelationMeta *) gst_buffer_get_meta (buffer,
      GST_ANALYTIC_RELATION_META_API_TYPE);

  if (!meta) {
    meta =
        (GstAnalyticRelationMeta *) gst_buffer_add_meta (buffer,
        GST_ANALYTIC_RELATION_META_INFO, init_params);
    g_return_val_if_fail (meta != NULL, NULL);
  }

  return meta;
}

/**
 * gst_analytic_relation_meta_add_relatable_mtd
 * @meta: Instance
 * @type: Type of relatable (#GstAnalyticRelatableMtd)
 * @size: Size required
 * @new_max_relation_order: Updated max relation order.
 * @new_max_size: Updated max size.
 * @rlt_mtd: Updated handle
 * Add a relatable metadata to @meta. This method is meant to be used by
 * new struct sub-classing GstAnalyticRelatableMtd.
 *
 * Returns: New GstAnalyticRelatableMtdData instance.
 *
 * Since 1.23
 */
GstAnalyticRelatableMtdData *
gst_analytic_relation_meta_add_relatable_mtd (GstAnalyticRelationMeta * meta,
    GQuark type, gsize size, gsize * new_max_relation_order,
    gsize * new_max_size, GstAnalyticRelatableMtd * rlt_mtd)
{
  GstAnalyticRelationMetaPriv *priv = meta->priv;
  gsize new_size = size + priv->offset;
  GstAnalyticRelatableMtdData *dest = NULL;
  gpointer mem;
  guint8 **new_adj_mat;
  gsize new_mem_cap, new_rel_order;
  GST_CAT_TRACE (GST_CAT_AN_RELATION, "Adding relatable metadata to rmeta %p",
      meta);

  g_rw_lock_writer_lock (&priv->relations_rw_lock);
  if (new_size > priv->max_size) {
    if (new_size > priv->max_size_increment + priv->offset) {
      new_mem_cap = new_size;
    } else {
      new_mem_cap = priv->max_size + priv->max_size_increment;
    }

    if ((mem = g_realloc (priv->analysis_results, new_mem_cap)) == NULL) {
      GST_CAT_ERROR (GST_CAT_AN_RELATION, "No memory, failed to resize");
      g_rw_lock_writer_unlock (&priv->relations_rw_lock);
      return NULL;
    }

    priv->max_size = new_mem_cap;
    priv->analysis_results = mem;
    if (new_max_size) {
      *new_max_size = new_mem_cap;
    }
  }

  if (priv->length >= priv->rel_order) {
    new_rel_order = priv->rel_order + priv->rel_order_increment;
    new_adj_mat = gst_analytic_relation_adj_mat_dup (priv->adj_mat,
        priv->rel_order, new_rel_order);
    g_free (priv->adj_mat);
    priv->adj_mat = new_adj_mat;
    if ((mem =
            g_realloc (priv->relatable_mtd_data_lookup,
                sizeof (gpointer) * new_rel_order))
        == NULL) {
      GST_CAT_ERROR (GST_CAT_AN_RELATION, "No memory, failed to resize");
      g_rw_lock_writer_unlock (&priv->relations_rw_lock);
      return NULL;
    }
    priv->relatable_mtd_data_lookup = mem;
    priv->rel_order = new_rel_order;
    if (new_max_relation_order) {
      *new_max_relation_order = new_rel_order;
    }
  }
  if (new_size <= priv->max_size && (priv->length < priv->rel_order)) {
    dest =
        (GstAnalyticRelatableMtdData *) (priv->analysis_results + priv->offset);
    dest->analysis_type = type;
    dest->id = gst_analytic_relation_meta_get_next_id (meta);
    dest->size = size;
    priv->relatable_mtd_data_lookup[dest->id] = priv->offset;
    priv->offset += dest->size;
    priv->length++;
    rlt_mtd->id = dest->id;
    rlt_mtd->ptr = meta;
    GST_CAT_TRACE (GST_CAT_AN_RELATION, "Add %p relatable type=%s (%lu / %lu).",
        dest, g_quark_to_string (type), new_size, priv->max_size);
  } else {
    GST_CAT_ERROR (GST_CAT_AN_RELATION,
        "Failed to add relatable, out-of-space (%lu / %lu).", new_size,
        priv->max_size);
  }
  g_rw_lock_writer_unlock (&priv->relations_rw_lock);
  return dest;
}

/**
 * gst_analytic_relation_meta_get_relatable_mtd
 * @meta: Instance of GstAnalyticRelationMeta
 * @an_meta_id: Id of GstAnalyticRelatableMtd instance to retrieve
 * @rlt: (GstAnalyticRelatableMtd*): Handle to relatable meta requested
 * Returns: TRUE if successful.
 *
 * Since 1.23
 */
gboolean
gst_analytic_relation_meta_get_relatable_mtd (GstAnalyticRelationMeta * meta,
    gint an_meta_id, GstAnalyticRelatableMtd * rlt)
{
  g_return_val_if_fail (meta && meta->priv, FALSE);
  GstAnalyticRelationMetaPriv *priv = meta->priv;
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  if (an_meta_id < 0 || an_meta_id >= priv->rel_order) {
    rlt->ptr = NULL;
    g_rw_lock_reader_unlock (&priv->relations_rw_lock);
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return FALSE;
  }
  rlt->ptr = meta;
  rlt->id = an_meta_id;
  g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  return TRUE;
}

/**
 * gst_analytic_relation_meta_get_relatable_mtd_data
 * @meta: Instance of GstAnalyticRelationMeta
 * @an_meta_id: Id of GstAnalyticRelatableMtd instance to retrieve
 *
 * Returns:(nullable):GstAnalyticRelatableMtd: Instance of GstAnalyticRelatableMtd
 *
 * Since 1.23
 */
GstAnalyticRelatableMtdData *
gst_analytic_relation_meta_get_relatable_mtd_data (GstAnalyticRelationMeta *
    meta, gint an_meta_id)
{
  GstAnalyticRelatableMtdData *rv;
  g_return_val_if_fail (meta && meta->priv, NULL);
  GstAnalyticRelationMetaPriv *priv = meta->priv;
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  if (an_meta_id < 0 || an_meta_id >= priv->rel_order) {
    g_rw_lock_reader_unlock (&priv->relations_rw_lock);
    GST_CAT_ERROR (GST_CAT_AN_RELATION, "Invalid parameter");
    return NULL;
  }
  rv = (GstAnalyticRelatableMtdData *)
      (priv->relatable_mtd_data_lookup[an_meta_id] + priv->analysis_results);
  g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  return rv;
}


/**
 * gst_analytic_relation_meta_get_direct_related:
 * @meta: GstAnalyticRelationMeta instance where to query for 
 *    GstAnalyticRelatableMtd.
 * @an_meta_id: Id of GstAnalyticRelatableMtd involved in relation to query
 * @relation_type: Type of relation to filter on.
 * @relatable_type: Type of GstAnalyticRelatableMtd to filter on. 
 * @state: Opaque data to store state of the query.
 * @rlt_mtd: Handle updated to directly related relatable meta.
 *
 * Returns:(nullable): GstAnalyticRelatableMtd that fit all criteria.
 *
 * Since 1.23
 */
gboolean
gst_analytic_relation_meta_get_direct_related (GstAnalyticRelationMeta * meta,
    gint an_meta_id, GstAnalyticRelTypes relation_type, GQuark relatable_type,
    gpointer * state, GstAnalyticRelatableMtd * rlt_mtd)
{
  guint8 **adj_mat;
  gsize adj_mat_order;
  GstAnalyticRelationMeta *rmeta = (GstAnalyticRelationMeta *) meta;
  GstAnalyticRelationMetaPriv *priv = NULL;
  GstAnalyticRelatableMtdData *rlt_mtd_data;
  gsize i;

  GST_CAT_TRACE (GST_CAT_AN_RELATION,
      "Looking for %s related to %d by %d", g_quark_to_string (relatable_type),
      an_meta_id, relation_type);

  g_return_val_if_fail (rmeta != NULL, FALSE);

  if (state) {
    if (*state) {
      i = ~G_MINSSIZE & (GPOINTER_TO_SIZE (*state) + 1);
    } else {
      i = 0;
      *state = GSIZE_TO_POINTER (G_MINSSIZE | i);
    }
  } else {
    i = 0;
  }

  priv = rmeta->priv;
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  adj_mat_order = priv->rel_order;

  if (adj_mat_order < (an_meta_id + 1)) {
    GST_CAT_DEBUG (GST_CAT_AN_RELATION,
        "Testing relation existence for analysis-meta that have no index in "
        "adj-mat.");
    g_rw_lock_reader_unlock (&priv->relations_rw_lock);
    return FALSE;
  }

  rlt_mtd->ptr = rmeta;
  adj_mat = priv->adj_mat;
  for (; i < adj_mat_order; i++) {
    if (adj_mat[an_meta_id][i] & relation_type) {
      rlt_mtd_data = (GstAnalyticRelatableMtdData *)
          (priv->relatable_mtd_data_lookup[i] + priv->analysis_results);
      rlt_mtd->id = rlt_mtd_data->id;
      if (gst_analytic_relatable_mtd_get_type (rlt_mtd) == relatable_type) {
        if (state) {
          *state = GSIZE_TO_POINTER (G_MINSSIZE | i);
        }
        GST_CAT_TRACE (GST_CAT_AN_RELATION, "Found match at %" G_GSIZE_FORMAT,
            i);
        break;
      }
      rlt_mtd = NULL;
    }
  }

  g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  return rlt_mtd != NULL;
}

/**
 * gst_analytic_relation_meta_iterate:
 * @meta: Instance of GstAnalyticRelationMeta
 * @state: Opaque data to store iteration state
 * @relatable_type: Type of GstAnalyticRelatableMtd to iterate on.
 * @rlt_mtd: Handle updated to iterated GstAnalyticRelatableMtd.
 *
 * Returns: FALSE if end was reached and iteration is completed. 
 *
 * Since 1.23
 */
gboolean
gst_analytic_relation_meta_iterate (GstAnalyticRelationMeta * meta,
    gpointer * state, GQuark relatable_type, GstAnalyticRelatableMtd * rlt_mtd)
{
  GstAnalyticRelationMetaPriv *priv = meta->priv;
  gsize index;
  gsize len = gst_analytic_relation_get_length (meta);
  GstAnalyticRelatableMtdData *rlt_mtd_data = NULL;

  g_return_val_if_fail (rlt_mtd != NULL, FALSE);

  if (*state) {
    index = ~G_MINSSIZE & (GPOINTER_TO_SIZE (*state) + 1);
  } else {
    index = 0;
    *state = GSIZE_TO_POINTER (G_MINSSIZE | index);
  }
  g_rw_lock_reader_lock (&priv->relations_rw_lock);
  for (; index < len; index++) {
    rlt_mtd_data = (GstAnalyticRelatableMtdData *)
        (priv->relatable_mtd_data_lookup[index] + priv->analysis_results);
    rlt_mtd->id = rlt_mtd_data->id;
    rlt_mtd->ptr = meta;
    if (gst_analytic_relatable_mtd_get_type (rlt_mtd) == relatable_type) {
      *state = GSIZE_TO_POINTER (G_MINSSIZE | index);
      g_rw_lock_reader_unlock (&priv->relations_rw_lock);
      return TRUE;
    }
  }

  g_rw_lock_reader_unlock (&priv->relations_rw_lock);
  return FALSE;
}

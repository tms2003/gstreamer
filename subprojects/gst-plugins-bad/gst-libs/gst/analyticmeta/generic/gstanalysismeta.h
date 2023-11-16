/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalysismeta.h
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

#ifndef __GST_ANALYSIS_META_H__
#define __GST_ANALYSIS_META_H__

#include <gst/gst.h>
#include <gst/analyticmeta/analytic-meta-prelude.h>

G_BEGIN_DECLS

#define GST_INF_RELATION_SPAN -1

#define GST_AN_RELATION_META_TAG "GST-ANALYSIS-RELATION-META-TAG"
typedef struct _GstAnalyticRelatableMtd GstAnalyticRelatableMtd;

#define GST_RELATABLE_META_CAST(relatable_meta) \
    ((GstAnalyticRelatableMtd *)(relatable_meta))

typedef struct _GstAnalyticRelatableMtdData GstAnalyticRelatableMtdData;
typedef struct _GstAnalyticRelationMeta GstAnalyticRelationMeta;

/**
 * GstAnalyticRelatableMtd:
 * @analysis_type: Identify the type of analysis-metadata
 * @id: Instance identifier.
 * @size: Size in bytes of the instance
 *
 * Opaque base structure for analysis-metadata that can be placed in relation.
 */
struct _GstAnalyticRelatableMtd
{
  /* <private> */
  guint id;
  GstAnalyticRelationMeta *ptr;
};

/**
 * GstAnalyticRelatableMtdData:
 * @analysis_type: Identify the type of analysis-metadata
 * @id: Instance identifier.
 * @size: Size in bytes of the instance
 *
 * Opaque base structure for analysis-metadata that can be placed in relation.
 */
struct _GstAnalyticRelatableMtdData
{
  GQuark analysis_type;
  guint id;
  gsize size;
};

GST_ANALYTIC_META_API
GQuark gst_analytic_relatable_mtd_get_type (GstAnalyticRelatableMtd * instance);

GST_ANALYTIC_META_API
guint gst_analytic_relatable_mtd_get_id (GstAnalyticRelatableMtd * instance);

GST_ANALYTIC_META_API
gsize gst_analytic_relatable_mtd_get_size (GstAnalyticRelatableMtd * instance);

typedef struct _GstAnalyticRelationMetaInitParams
GstAnalyticRelationMetaInitParams;

#define GST_ANALYTIC_RELATION_META_API_TYPE \
  (gst_analytic_relation_meta_api_get_type())

#define GST_ANALYTIC_RELATION_META_INFO \
  (gst_analytic_relation_meta_get_info())

/**
 * GstAnalyticRelTypes:
 * @GST_ANALYTIC_REL_TYPE_NONE: No relation
 * @GST_ANALYTIC_REL_TYPE_IS_PART_OF: First analysis-meta is part of second analysis-meta
 * @GST_ANALYTIC_REL_TYPE_CONTAIN: First analysis-meta contain second analysis-meta.
 * @GST_ANALYTIC_REL_TYPE_RELATE: First analysis-meta relate to second analysis-meta.
 * @GST_ANALYTIC_REL_TYPE_LAST: reserved
 */
typedef enum
{
  GST_ANALYTIC_REL_TYPE_NONE = 0,
  GST_ANALYTIC_REL_TYPE_IS_PART_OF = (1 << 1),
  GST_ANALYTIC_REL_TYPE_CONTAIN = (1 << 2),
  GST_ANALYTIC_REL_TYPE_RELATE_TO = (1 << 3),
  GST_ANALYTIC_REL_TYPE_LAST = (1 << 4)
} GstAnalyticRelTypes;

/**
 * GstAnalyticRelationMeta:
 * @parent_meta: #GstMeta
 *
 * Meta storing analysis-metadata relation information.
 */
struct _GstAnalyticRelationMeta
{
  GstMeta parent_meta;

  /*< private > */
  guint next_id;
  gpointer priv;
};

/**
 * GstAnalyticRelationMetaInitParams:
 * @initial_relation_order: Initial relations order.
 * @initial_buf_size: Buffer size in bytes to store relatable metadata
 *
 * GstAnalyticRelationMeta initialization parameters.
 */
struct _GstAnalyticRelationMetaInitParams
{
  gsize initial_relation_order;
  gsize initial_buf_size;
};

GST_ANALYTIC_META_API 
GType gst_analytic_relation_meta_api_get_type (void);

GST_ANALYTIC_META_API
const GstMetaInfo *gst_analytic_relation_meta_get_info (void);

GST_ANALYTIC_META_API
gsize gst_analytic_relation_get_length (GstAnalyticRelationMeta * instance);

GST_ANALYTIC_META_API
GstAnalyticRelTypes gst_analytic_relation_meta_get_relation (
    GstAnalyticRelationMeta * meta, gint an_meta_first_id,
    gint an_meta_second_id);

GST_ANALYTIC_META_API
gint gst_analytic_relation_meta_set_relation (GstAnalyticRelationMeta * meta,
    GstAnalyticRelTypes type, GstAnalyticRelatableMtd * an_meta_first,
    GstAnalyticRelatableMtd * an_meta_second);

GST_ANALYTIC_META_API
gboolean gst_analytic_relation_meta_exist (GstAnalyticRelationMeta * meta,
    GstAnalyticRelatableMtd * an_meta_first, GstAnalyticRelatableMtd * an_meta_second, gint max_relation_span,
    GstAnalyticRelTypes cond_types, GSList ** relations_path);

GST_ANALYTIC_META_API
GstAnalyticRelationMeta * gst_buffer_add_analytic_relation_meta (GstBuffer *
    buffer);

GST_ANALYTIC_META_API
GstAnalyticRelationMeta * gst_buffer_add_analytic_relation_meta_full (
    GstBuffer * buffer, GstAnalyticRelationMetaInitParams * init_params);

GST_ANALYTIC_META_API
gint gst_analytic_relation_meta_get_next_id (GstAnalyticRelationMeta * meta);

GST_ANALYTIC_META_API
GstAnalyticRelatableMtdData * gst_analytic_relation_meta_add_relatable_mtd (
    GstAnalyticRelationMeta * meta, GQuark type, gsize size,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticRelatableMtd *rlt_mtd);

GST_ANALYTIC_META_API
gboolean gst_analytic_relation_meta_get_relatable_mtd (
    GstAnalyticRelationMeta * meta, gint an_meta_id,
    GstAnalyticRelatableMtd * rlt);

GST_ANALYTIC_META_API
GstAnalyticRelatableMtdData * gst_analytic_relation_meta_get_relatable_mtd_data (
    GstAnalyticRelationMeta * meta, gint an_meta_id);


GST_ANALYTIC_META_API
gboolean gst_analytic_relation_meta_iterate (
    GstAnalyticRelationMeta * meta, gpointer * state, GQuark relatable_type,
    GstAnalyticRelatableMtd * rlt_mtd);

GST_ANALYTIC_META_API
gboolean gst_analytic_relation_meta_get_direct_related (
    GstAnalyticRelationMeta * meta, gint an_meta_id,
    GstAnalyticRelTypes relation_type, GQuark relatable_type, gpointer * state,
    GstAnalyticRelatableMtd * rlt_mtd);

G_END_DECLS
#endif // __GST_ANALYSIS_META_H__

/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalysisclassificationmtd.h
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

#ifndef __GST_ANALYSIS_CLASSIFICATION_H__
#define __GST_ANALYSIS_CLASSIFICATION_H__

#include <gst/gst.h>
#include <gst/analyticmeta/analytic-meta-prelude.h>
#include <gst/analyticmeta/generic/gstanalysismeta.h>

G_BEGIN_DECLS

typedef GstAnalyticRelatableMtd GstAnalyticClsMtd;

GST_ANALYTIC_META_API 
GQuark gst_analytic_cls_mtd_get_type_quark (void);

GST_ANALYTIC_META_API 
const gchar *gst_analytic_cls_mtd_get_type_name (void);

GST_ANALYTIC_META_API
gfloat gst_analytic_cls_mtd_get_level (GstAnalyticClsMtd * handle,
    gint index);

GST_ANALYTIC_META_API
gint gst_analytic_cls_mtd_get_index_by_quark (GstAnalyticClsMtd * instance,
    GQuark quark);

GST_ANALYTIC_META_API 
gsize gst_analytic_cls_mtd_get_length (GstAnalyticClsMtd * instance);

GST_ANALYTIC_META_API
GQuark gst_analytic_cls_mtd_get_quark (GstAnalyticClsMtd * mtd, gint index);

GST_ANALYTIC_META_API
gboolean
gst_analytic_relation_add_analytic_cls_mtd (GstAnalyticRelationMeta *
    instance, gfloat * confidence_levels, gsize length, GQuark * class_quarks,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticClsMtd * rlt_mtd);

GST_ANALYTIC_META_API
gboolean
gst_analytic_relation_add_one_analytic_cls_mtd (GstAnalyticRelationMeta *
    instance, gfloat confidence_level, GQuark class_quark,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticClsMtd * rlt_mtd);

G_END_DECLS
#endif // __GST_ANALYSIS_CLASSIFICATION_H__

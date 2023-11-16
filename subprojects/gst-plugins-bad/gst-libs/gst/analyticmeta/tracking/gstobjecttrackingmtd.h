/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstobjecttrackingmtd.h
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

#ifndef __GST_OBJECT_TRACKING_MTD__
#define __GST_OBJECT_TRACKING_MTD__

#include <gst/gst.h>
#include <gst/analyticmeta/analytic-meta-prelude.h>
#include <gst/analyticmeta/generic/gstanalysismeta.h>

G_BEGIN_DECLS 
typedef GstAnalyticRelatableMtd GstAnalyticTrackMtd;

GST_ANALYTIC_META_API 
GQuark gst_analytic_track_mtd_get_type_quark (void);

GST_ANALYTIC_META_API 
const gchar *gst_analytic_track_mtd_get_type_name (void);

GST_ANALYTIC_META_API 
gboolean gst_analytic_track_mtd_update_last_seen (GstAnalyticTrackMtd * instance,
    GstClockTime last_seen);

GST_ANALYTIC_META_API 
gboolean gst_analytic_track_mtd_set_lost (GstAnalyticTrackMtd * instance);

GST_ANALYTIC_META_API
gboolean gst_analytic_track_mtd_get_track_info (GstAnalyticTrackMtd * instance,
    guint64 * track_id, GstClockTime * track_first_seen, GstClockTime *
    track_last_seen, gboolean * track_lost);

GST_ANALYTIC_META_API 
gboolean gst_analytic_relation_add_analytic_track_mtd (
    GstAnalyticRelationMeta * instance, guint64 track_id, 
    GstClockTime track_first_seen, gsize * new_max_relation_order,
    gsize * new_max_size, GstAnalyticRelatableMtd * rlt_mtd);

G_END_DECLS
#endif // __GST_OBJECT_TRACKING_MTD__

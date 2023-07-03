/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstobjecttrackingmtd.c
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

#include "gstobjecttrackingmtd.h"

#define GST_RELATABLE_MTD_TRACK_TYPE_NAME "object-tracking"

typedef struct _GstAnalyticTrackMtdData GstAnalyticTrackMtdData;

/**
 * GstAnalyticTrackMtd:
 * @parent: parent #GstAnalyticMtd
 * @track_id: Track identifier
 * @track_first_seen: Track creation time
 * @track_last_seen: Track last observation
 * @track_lost: Track lost
 *
 * Store information on results of object tracking
 *
 * Since: 1.23
 */
struct _GstAnalyticTrackMtdData
{
  GstAnalyticRelatableMtd parent;
  guint64 track_id;
  GstClockTime track_first_seen;
  GstClockTime track_last_seen;
  gboolean track_lost;
};


static char relatable_type[] = GST_RELATABLE_MTD_TRACK_TYPE_NAME;

static GstAnalyticTrackMtdData *
gst_analytic_track_mtd_get_data (GstAnalyticTrackMtd * instance)
{
  GstAnalyticRelatableMtdData *rlt_data =
      gst_analytic_relation_meta_get_relatable_mtd_data (instance->ptr,
      instance->id);
  g_assert (rlt_data);

  return (GstAnalyticTrackMtdData *) rlt_data;
}

/**
 * gst_analytic_track_mtd_get_type_quark:
 * Returns: Quark representing the type of GstAnalyticRelatableMtd
 *
 * Get the quark identifying the relatable type
 *
 * Since: 1.23
 */
GQuark
gst_analytic_track_mtd_get_type_quark (void)
{
  return g_quark_from_static_string (relatable_type);
}

/**
 * gst_an_od_mtd_get_type_name:
 * Returns: #GstAnalyticRelatableMtd type name.
 *
 * Get the name identifying relatable type name
 *
 * Since: 1.23
 */
const gchar *
gst_analytic_track_mtd_get_type_name (void)
{
  return GST_RELATABLE_MTD_TRACK_TYPE_NAME;
}

/**
 * gst_analytic_track_mtd_update_last_seen:
 * @instance: GstAnalyticTrackMtd instance
 * @last_seen: Timestamp of last time this object was tracked
 *
 * Since: 1.23
 */
gboolean
gst_analytic_track_mtd_update_last_seen (GstAnalyticTrackMtd * instance,
    GstClockTime last_seen)
{
  GstAnalyticTrackMtdData *trk_mtd_data;
  g_return_val_if_fail (instance, FALSE);
  trk_mtd_data = gst_analytic_track_mtd_get_data (instance);
  g_return_val_if_fail (trk_mtd_data != NULL, FALSE);

  trk_mtd_data->track_last_seen = last_seen;
  return TRUE;
}

/**
 * gst_analytic_track_mtd_set_lost:
 * @instance: Instance of GstAnalyticTrackMtd.  
 * Set tracking to lost
 *
 * Returns: Update successful
 *
 * Since: 1.23
 */
gboolean
gst_analytic_track_mtd_set_lost (GstAnalyticTrackMtd * instance)
{
  GstAnalyticTrackMtdData *trk_mtd_data;
  g_return_val_if_fail (instance, FALSE);
  trk_mtd_data = gst_analytic_track_mtd_get_data (instance);
  g_return_val_if_fail (trk_mtd_data != NULL, FALSE);
  trk_mtd_data->track_lost = TRUE;
  return TRUE;
}

/**
 * gst_analytic_track_mtd_get_track_info:
 * @instance: Instance of tracking metadata
 * @track_id: Updated tracking id
 * @track_first_seen: Updated timestamp of the track first observation.
 * @track_last_seen: Updated timestamp of the track last observation.
 * Retrieve tracking information.
 *
 * Returns: Successfully retrieved info.
 *
 * Since: 1.23
 */
gboolean
gst_analytic_track_mtd_get_track_info (GstAnalyticTrackMtd * instance,
    guint64 * track_id, GstClockTime * track_first_seen, GstClockTime *
    track_last_seen, gboolean * track_lost)
{
  GstAnalyticTrackMtdData *trk_mtd_data;
  g_return_val_if_fail (instance, FALSE);
  trk_mtd_data = gst_analytic_track_mtd_get_data (instance);

  g_return_val_if_fail (trk_mtd_data != NULL, FALSE);

  if (track_id)
    *track_id = trk_mtd_data->track_id;
  if (track_first_seen)
    *track_first_seen = trk_mtd_data->track_first_seen;
  if (track_last_seen)
    *track_last_seen = trk_mtd_data->track_last_seen;
  if (track_lost)
    *track_lost = trk_mtd_data->track_lost;

  return TRUE;
}


/**
 * gst_analytic_relation_add_analytic_track_mtd:
 * @instance: Instance of GstAnalyticRelationMeta where to add tracking mtd
 * @track_id: Tracking id
 * @track_first_seen: Timestamp of first time the object was observed.
 * @trk_mtd: Handle updated with newly added tracking meta.
 * Add an analytic tracking metadata to @instance.
 * Returns: Added successfully
 *
 * Since: 1.23
 */
gboolean
gst_analytic_relation_add_analytic_track_mtd (GstAnalyticRelationMeta *
    instance, guint64 track_id, GstClockTime track_first_seen,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticTrackMtd * trk_mtd)
{
  g_return_val_if_fail (instance, FALSE);

  GQuark relatable_type = gst_analytic_track_mtd_get_type_quark ();
  gsize size = sizeof (GstAnalyticTrackMtd);
  GstAnalyticTrackMtdData *trk_mtd_data = (GstAnalyticTrackMtdData *)
      gst_analytic_relation_meta_add_relatable_mtd (instance,
      relatable_type, size, new_max_relation_order, new_max_size, trk_mtd);

  if (trk_mtd_data) {
    trk_mtd_data->track_id = track_id;
    trk_mtd_data->track_first_seen = track_first_seen;
    trk_mtd_data->track_last_seen = track_first_seen;
    trk_mtd_data->track_lost = FALSE;
  }
  return trk_mtd_data != NULL;
}

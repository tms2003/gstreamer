/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstobjectdetectionmtd.c
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

#include "gstobjectdetectionmtd.h"

#define GST_RELATABLE_MTD_OD_TYPE_NAME "object-detection"

static char relatable_type[] = GST_RELATABLE_MTD_OD_TYPE_NAME;

typedef struct _GstAnalyticODMtdData GstAnalyticODMtdData;

/**
 * GstAnalyticODMtd:
 * @parent: parent #GstAnalyticMtd
 * @object_type: Type of object
 * @x: x component of upper-left corner
 * @y: y component of upper-left corner
 * @w: bounding box width
 * @h: bounding box height
 * @location_confidence_lvl: Confidence on object location
 *
 * Store information on results of object detection
 *
 * Since: 1.23
 */
struct _GstAnalyticODMtdData
{
  GstAnalyticRelatableMtdData parent;
  GQuark object_type;
  guint x;
  guint y;
  guint w;
  guint h;
  gfloat location_confidence_lvl;
};


/**
 * GstAnalyticODMtd:
 * Get a quark that represent object-detection metadata type
 * Returns: Quark of #GstAnalyticRelatableMtd type
 *
 * Since: 1.23
 */
GQuark
gst_analytic_od_mtd_get_type_quark (void)
{
  return g_quark_from_static_string (relatable_type);
}

/**
 * gst_analytic_od_mtd_get_type_name:
 * Get a text representing object-detection metadata type.
 * Returns: #GstAnalyticRelatableMtd type name.
 *
 * Since: 1.23
 */
const gchar *
gst_analytic_od_mtd_get_type_name (void)
{
  return GST_RELATABLE_MTD_OD_TYPE_NAME;
}

static GstAnalyticODMtdData *
gst_analytic_od_mtd_get_data (GstAnalyticODMtd * instance)
{
  GstAnalyticRelatableMtdData *rlt_data =
      gst_analytic_relation_meta_get_relatable_mtd_data (instance->ptr,
      instance->id);
  g_assert (rlt_data);

  return (GstAnalyticODMtdData *) rlt_data;
}

/**
 * gst_analytic_od_mtd_get_location:
 * @instance: instance
 * @x: x component of upper-left corner of the object location
 * @y: y component of upper-left corner of the object location
 * @w: bounding box width of the object location
 * @h: bounding box height of the object location
 * Retrieve location and location confidence level.
 * Returns: TRUE on success, otherwise FALSE.
 *
 * Since: 1.23
 */
gboolean
gst_analytic_od_mtd_get_location (GstAnalyticODMtd * instance,
    guint * x, guint * y, guint * w, guint * h, gfloat * loc_conf_lvl)
{
  g_return_val_if_fail (instance && x && y && w && h, FALSE);
  GstAnalyticODMtdData *data;
  data = gst_analytic_od_mtd_get_data (instance);
  g_return_val_if_fail (data != NULL, FALSE);

  if (data) {
    *x = data->x;
    *y = data->y;
    *w = data->w;
    *h = data->h;

    if (loc_conf_lvl) {
      *loc_conf_lvl = data->location_confidence_lvl;
    }
  }

  return TRUE;
}

/**
 * gst_analytic_od_mtd_get_type:
 * @instance: Instance
 * Quark of the class of object associated with this location.
 * Returns: Quark different from on success and 0 on failure.
 *
 * Since: 1.23
 */
GQuark
gst_analytic_od_mtd_get_type (GstAnalyticODMtd * instance)
{
  GstAnalyticODMtdData *data;
  g_return_val_if_fail (instance != NULL, 0);
  data = gst_analytic_od_mtd_get_data (instance);
  g_return_val_if_fail (data != NULL, 0);
  return data->object_type;
}

/**
 * gst_analytic_relation_add_analytic_od_mtd:
 * @instance: Instance of #GstAnalyticRelationMeta where to add classification instance
 * @type: Quark of the object type
 * @x: x component of bounding box upper-left corner
 * @y: y component of bounding box upper-left corner
 * @w: bounding box width
 * @h: bounding box height
 * @loc_conf_lvl: confidence level on the object location
 * @od_mtd: Handle updated with newly added object detection meta.
 * Add an object-detetion metadata to @instance.
 * Returns: Added successfully
 *
 * Since: 1.23
 */
gboolean
gst_analytic_relation_add_analytic_od_mtd (GstAnalyticRelationMeta * instance,
    GQuark type, guint x, guint y, guint w, guint h, gfloat loc_conf_lvl,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticODMtd * od_mtd)
{
  g_return_val_if_fail (instance != NULL, FALSE);
  GQuark relatable_type = gst_analytic_od_mtd_get_type_quark ();
  gsize size = sizeof (GstAnalyticODMtdData);
  GstAnalyticODMtdData *od_mtd_data = (GstAnalyticODMtdData *)
      gst_analytic_relation_meta_add_relatable_mtd (instance,
      relatable_type, size, new_max_relation_order, new_max_size, od_mtd);
  if (od_mtd_data) {
    od_mtd_data->x = x;
    od_mtd_data->y = y;
    od_mtd_data->w = w;
    od_mtd_data->h = h;
    od_mtd_data->location_confidence_lvl = loc_conf_lvl;
    od_mtd_data->object_type = type;
  }
  return od_mtd_data != NULL;
}

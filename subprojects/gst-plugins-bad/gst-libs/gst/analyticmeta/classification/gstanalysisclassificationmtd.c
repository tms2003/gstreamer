/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalysisclassificationmtd.c
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

#include "gstanalysisclassificationmtd.h"

#define GST_RELATABLE_MTD_CLASSIFICATION_TYPE_NAME "classification"

static char relatable_type[] = GST_RELATABLE_MTD_CLASSIFICATION_TYPE_NAME;

typedef struct _GstAnalyticClsConfLvlAndClass GstAnalyticClsConfLvlAndClass;
typedef struct _GstAnalyticClsMtdData GstAnalyticClsMtdData;

struct _GstAnalyticClsConfLvlAndClass
{
  GQuark class;
  gfloat confidence_levels;
};

/**
 * GstAnalyticClsMtd:
 * @parent: parent
 * @length: classes and confidence levels count
 * @class_quarks: (array length=length): Array of quark representing a class
 * @confidence_levels: (array length=length): Array of confidence levels for 
 * each class in @class_quarks. 
 *
 * Information on results of a classification of buffer content.
 *
 * Since: 1.23
 */
struct _GstAnalyticClsMtdData
{
  GstAnalyticRelatableMtdData parent;
  gsize length;
  GstAnalyticClsConfLvlAndClass confidence_levels_and_classes[];        // Must be last
};


static GstAnalyticClsMtdData *
gst_analytic_cls_mtd_get_data (GstAnalyticClsMtd * instance)
{
  GstAnalyticRelatableMtdData *rlt_data =
      gst_analytic_relation_meta_get_relatable_mtd_data (instance->ptr,
      instance->id);
  g_assert (rlt_data);

  return (GstAnalyticClsMtdData *) rlt_data;
}

/**
 * gst_analytic_cls_mtd_get_type_quark:
 * Get a quark identifying #GstAnalyticRelatableMtd type.
 * Returns: Quark of #GstAnalyticRelatableMtd type
 *
 * Since: 1.23
 */
GQuark
gst_analytic_cls_mtd_get_type_quark (void)
{
  return g_quark_from_static_string (relatable_type);
}

/**
 * gst_analytic_cls_mtd_get_type_name:
 * Get the static string representing #GstAnalyticRelatableMtd type.
 * Returns: #GstAnalyticRelatableMtd type name.
 *
 * Since: 1.23
 */
const gchar *
gst_analytic_cls_mtd_get_type_name (void)
{
  return GST_RELATABLE_MTD_CLASSIFICATION_TYPE_NAME;
}

/**
 * gst_analytic_cls_mtd_get_level:
 * @instance: instance handle
 * @index: Object class index
 *
 * Get confidence level for class at @index
 * Returns: confidence level for @index, <0.0 if the call failed.
 *
 * Since: 1.23
 */
gfloat
gst_analytic_cls_mtd_get_level (GstAnalyticClsMtd * instance, gint index)
{
  g_assert (instance);
  g_assert (index >= 0);
  g_assert (instance->ptr != NULL);
  GstAnalyticClsMtdData *cls_mtd_data;
  cls_mtd_data = gst_analytic_cls_mtd_get_data (instance);
  g_return_val_if_fail (cls_mtd_data != NULL, -1.0);
  g_return_val_if_fail (cls_mtd_data->length > index, -1.0);
  return cls_mtd_data->confidence_levels_and_classes[index].confidence_levels;
}

/**
 * gst_analytic_cls_mtd_get_index_by_quark:
 * @instance: Instance
 * @quark: Quark of the class
 * Get index of class represented by @quark
 * Returns: index of the class associated with @quarks ( and label) or
 *     a negative value on failure.
 *
 * Since: 1.23
 */
gint
gst_analytic_cls_mtd_get_index_by_quark (GstAnalyticClsMtd * instance,
    GQuark quark)
{
  g_assert (instance);

  GstAnalyticClsMtdData *cls_mtd_data;
  cls_mtd_data = gst_analytic_cls_mtd_get_data (instance);
  g_return_val_if_fail (cls_mtd_data != NULL, -1);

  for (gint i = 0; i < cls_mtd_data->length; i++) {
    if (quark == cls_mtd_data->confidence_levels_and_classes[i].class) {
      return i;
    }
  }
  return -1;
}

/**
 * gst_analytic_cls_mtd_get_length:
 * @instance: Instance
 * Get number of classes
 * Returns: Number of classes in this classification instance
 *
 * Since: 1.23
 */
gsize
gst_analytic_cls_mtd_get_length (GstAnalyticClsMtd * instance)
{
  GstAnalyticClsMtdData *cls_mtd_data;
  cls_mtd_data = gst_analytic_cls_mtd_get_data (instance);
  g_return_val_if_fail (cls_mtd_data != NULL, 0);
  return cls_mtd_data->length;
}

/**
 * gst_analytic_cls_mtd_get_quark:
 * @instance: Instance
 * @index: index of the class
 * Get quark of the class at @index
 * Returns: Quark of this class (label) associated with @index
 *
 * Since: 1.23
 */
GQuark
gst_analytic_cls_mtd_get_quark (GstAnalyticClsMtd * instance, gint index)
{
  GstAnalyticClsMtdData *cls_mtd_data;
  g_assert (instance);
  cls_mtd_data = gst_analytic_cls_mtd_get_data (instance);
  g_return_val_if_fail (cls_mtd_data != NULL, 0);
  return cls_mtd_data->confidence_levels_and_classes[index].class;
}

/**
 * gst_analytic_relation_add_analytic_cls_mtd:
 * @instance: Instance of #GstAnalyticRelationMeta where to add classification instance
 * @confidence_levels: Array (#gfloat) of confidence levels
 * @length: length of @confidence_levels
 * @class_quarks: (Array of length=length of #GQuark) labels of this 
 *    classification. Order define index, quark, labels relation. This array
 *    need to exist as long has this classification meta exist.
 * @new_max_relation_order: New maximum relation order
 * @new_max_size:  New maximum size
 * @cls_mtd: Handle updated to newly added classification meta.
 *
 * Add analytic classification metadata to @instance.
 * Returns: Added successfully
 *
 * Since: 1.23
 */
gboolean
gst_analytic_relation_add_analytic_cls_mtd (GstAnalyticRelationMeta * instance,
    gfloat * confidence_levels, gsize length, GQuark * class_quarks,
    gsize * new_max_relation_order, gsize * new_max_size,
    GstAnalyticClsMtd * cls_mtd)
{
  GQuark relatable_type = gst_analytic_cls_mtd_get_type_quark ();
  g_assert (instance);
  gsize confidence_levels_size =
      (sizeof (GstAnalyticClsConfLvlAndClass) * length);
  gsize size = sizeof (GstAnalyticClsMtdData) + confidence_levels_size;
  GstAnalyticClsConfLvlAndClass *conf_lvls_and_classes;

  GstAnalyticClsMtdData *cls_mtd_data = (GstAnalyticClsMtdData *)
      gst_analytic_relation_meta_add_relatable_mtd (instance,
      relatable_type, size, new_max_relation_order, new_max_size, cls_mtd);
  if (cls_mtd_data) {
    cls_mtd_data->length = length;
    for (gsize i = 0; i < length; i++) {
      conf_lvls_and_classes = &(cls_mtd_data->confidence_levels_and_classes[i]);
      conf_lvls_and_classes->class = class_quarks[i];
      conf_lvls_and_classes->confidence_levels = confidence_levels[i];
    }
  }
  return cls_mtd_data != NULL;
}

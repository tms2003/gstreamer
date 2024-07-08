/* GStreamer
 * Copyright (C) 2024 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticssegmentmeta.c
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

#include "gstanalyticssegmentationmtd.h"
#include <gst/video/video-info.h>

static void
gst_analytics_segmentation_mtd_clear (GstBuffer * buffer,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsMtd * mtd);

static gboolean
gst_analytics_segmentation_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data);

static const GstAnalyticsMtdImpl segmentation_impl = {
  "segmentation",
  gst_analytics_segmentation_mtd_transform,
  gst_analytics_segmentation_mtd_clear
};

/**
 * GstAnalyticsSegMtdData:
 * @video_info: Video info describing mask storage
 * @masks: #GstBuffer used to store segmentation masks
 *
 * Store segmentation results where each value represent a group to which
 * belong the corresponding pixel from original image where segmentation was
 * performed. All values equal in @masks form a mask defining all the
 * pixel belonging to the same segmented region from the original image. The
 * @video_info is a description of the @masks, where masks resolution, padding,
 * format, ... The format of @video_info has a special meaning in the context
 * of the mask, GRAY8 mean that @masks value can take 256 values which mean
 * 256 segmented region can be represented.
 *
 * Since: 1.24
 */
typedef struct GstAnalyticsSegMtdData
{
  GstSegmentationType type;
  GstBuffer *masks;
  gint x;
  gint y;
} GstAnalyticsSegMtdData;

/**
 * gst_analytics_segmentation_mtd_get_mtd_type:
 *
 * Get an instance of #GstAnalyticsMtdType that represent segmentation
 * metadata type.
 *
 * Returns: A #GstAnalyticsMtdType type
 *
 * Since: 1.24
 */
GstAnalyticsMtdType
gst_analytics_segmentation_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & segmentation_impl;
}

/**
 * gst_analytics_segmentation_mtd_get_mask:
 * @handle: Instance
 * @buffer: Buffer containing segmentation masks.
 * @vinfo: (out caller-allocates)(not nullable): Masks data stored in
 * video info
 *
 * Retrieve segmentation mask data. See #GstAnalyticsSegMtdData for more
 * details.
 *
 * Returns: TRUE on success, otherwise FALSE.
 *
 * Since: 1.24
 */
gboolean
gst_analytics_segmentation_mtd_get_mask (GstAnalyticsSegmentationMtd * handle,
    GstBuffer ** buffer)
{
  GstAnalyticsSegMtdData *mtddata;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, FALSE);

  *buffer = gst_buffer_ref (mtddata->masks);
  return TRUE;
}

/**
 * gst_analytics_relation_meta_add_segmentation_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add segmentation
 * instance.
 * @buffer:(in)(transfer full): Buffer containing segmentation masks. @buffer
 * must have a #GstVideoMeta attached
 * @segmentation_type:(in): Segmentation type
 * @segmentation_mtd:(out)(not nullable): Handle update with newly added segmenation meta.
 *
 * Add analytics segmentation metadata to @instance. See #GstAnalyticsSegMtdData for more details.
 *
 * Returns: TRUE if added successfully, otherwise FALSE
 *
 * Since: 1.24
 */
gboolean
gst_analytics_relation_meta_add_segmentation_mtd (GstAnalyticsRelationMeta *
    instance, GstBuffer * buffer, GstSegmentationType segmentation_type,
    GstAnalyticsSegmentationMtd * segmentation_mtd)
{
  const gsize size = sizeof (GstAnalyticsSegMtdData);
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
  g_return_val_if_fail (vmeta != NULL, FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (vmeta->format == GST_VIDEO_FORMAT_GRAY8 ||
      vmeta->format == GST_VIDEO_FORMAT_GRAY16_BE ||
      vmeta->format == GST_VIDEO_FORMAT_GRAY16_LE, FALSE);

  GstAnalyticsSegMtdData *mtddata = NULL;
  mtddata =
      (GstAnalyticsSegMtdData *) gst_analytics_relation_meta_add_mtd (instance,
      &segmentation_impl, size, segmentation_mtd);


  if (mtddata) {
    mtddata->masks = buffer;
    mtddata->type = segmentation_type;
  }

  return mtddata != NULL;
}

/**
 * gst_analytics_relation_meta_add_segmentation_region_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add segmentation
 * instance.
 * @buffer:(in)(transfer full): Buffer containing segmentation masks for a
 * region of the image.
 * @vinfo: (in): #GstVideoInfo describing masks data. Note that only GST_VIDEO_FORMAT_GRAY8, GST_VIDEO_FORMAT_GRAY16_BE, GST_VIDEO_FORMAT_GRAY16_LE are supported and each components represent an identifier for the segmentation group where the corresponding pixel from the region in the original buffer belongs to.
 * @x:(in): Region left most coordinate described by the mask
 * @y:(in): Region top most coordinate described by the mask.
 * @segmentation_type:(in): Segmentation type
 * @segmentation_mtd:(out)(not nullable): Handle update with newly added segmenation meta.
 *
 * Add analytics segmentation region metadata to @instance. See #GstAnalyticsSegMtdData for more details.
 *
 * Returns: TRUE if added successfully, otherwise FALSE
 *
 * Since: 1.24
 */
gboolean
    gst_analytics_relation_meta_add_segmentation_region_mtd
    (GstAnalyticsRelationMeta * instance, GstBuffer * buffer, gint x, gint y,
    GstSegmentationType segmentation_type,
    GstAnalyticsSegmentationMtd * segmentation_mtd) {
  GstAnalyticsSegMtdData *mtddata = NULL;
  if (gst_analytics_relation_meta_add_segmentation_mtd (instance, buffer,
          segmentation_type, segmentation_mtd) == TRUE) {
    mtddata = gst_analytics_relation_meta_get_mtd_data (segmentation_mtd->meta,
        segmentation_mtd->id);
    mtddata->x = x;
    mtddata->y = y;
  }

  return mtddata != NULL;
}

static void
gst_analytics_segmentation_mtd_clear (GstBuffer * buffer,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsMtd * mtd)
{
  GstAnalyticsSegMtdData *segdata;
  segdata = gst_analytics_relation_meta_get_mtd_data (rmeta, mtd->id);
  g_return_if_fail (segdata != NULL);
  gst_clear_buffer (&segdata->masks);
}

static gboolean
gst_analytics_segmentation_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{

  if (GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    GstAnalyticsSegMtdData *segdata =
        gst_analytics_relation_meta_get_mtd_data (transmtd->meta, transmtd->id);

    gst_buffer_ref (segdata->masks);

  }

  return TRUE;
}

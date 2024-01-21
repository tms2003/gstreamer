/* GStreamer
 * Copyright (C) 2024 Benjamin Desef <projekter-git@yahoo.de>
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

#ifndef _GST_KMS_COMPOSITOR_H_
#define _GST_KMS_COMPOSITOR_H_

#include <gst/gst.h>
#include <gst/video/gstvideoaggregator.h>
#include <drm_mode.h>
#include <xf86drmMode.h>

G_BEGIN_DECLS

#define GST_TYPE_KMS_COMPOSITOR (gst_kms_compositor_get_type())
#define GST_KMS_COMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_KMS_COMPOSITOR, GstKMSCompositor))
#define GST_KMS_COMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_KMS_COMPOSITOR, GstKMSCompositorClass))
#define GST_IS_KMS_COMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_KMS_COMPOSITOR))
#define GST_IS_KMS_COMPOSITOR_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_KMS_COMPOSITOR))
typedef struct _GstKMSCompositor GstKMSCompositor;
typedef struct _GstKMSCompositorClass GstKMSCompositorClass;

struct _GstKMSCompositor
{
  GstVideoAggregator videoaggregator;

  gint fd;
  gint conn_id;
  gint crtc_id;
  guint32 pipe;
  gboolean force_dma;

  // kms data
  guint mode_id;
  gboolean need_modesetting;
  drmModeRes *resources;
  struct drm_mode_atomic atomic;

  // capabilities
  gboolean has_prime_import;
  gboolean has_prime_export;

  GstStructure *connector_props;
  GstCaps *allowed_src_caps;
  GstVideoInfo src_vinfo;
  GstBufferPool *src_pool;
  gboolean src_needs_copy;
  GstBuffer *src_buffer;
  GstAllocator *allocator;

  gchar *devname;
  gchar *bus_id;

  gboolean is_internal_fd;
};

struct _GstKMSCompositorClass
{
  GstVideoAggregatorClass parent;
};

GType gst_kms_compositor_get_type(void);
GST_ELEMENT_REGISTER_DECLARE (kmscompositor);
void gst_kms_compositor_ensure_kms_allocator(GstKMSCompositor *self);
GstBufferPool *gst_kms_compositor_create_pool(GstObject *self, GstKMSCompositor *comp, GstCaps *caps, gsize size);

G_END_DECLS

#endif

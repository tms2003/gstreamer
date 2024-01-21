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

#ifndef _GST_KMS_COMPOSITOR_PAD_H_
#define _GST_KMS_COMPOSITOR_PAD_H_

#include <gst/gst.h>
#include <gst/video/gstvideoaggregator.h>
#include <xf86drmMode.h>

G_BEGIN_DECLS

#define GST_TYPE_KMS_COMPOSITOR_PAD (gst_kms_compositor_pad_get_type())
#define GST_KMS_COMPOSITOR_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_KMS_COMPOSITOR_PAD, GstKMSCompositorPad))
#define GST_KMS_COMPOSITOR_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_KMS_COMPOSITOR, GstKMSCompositorPadClass))
#define GST_IS_KMS_COMPOSITOR_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_KMS_COMPOSITOR_PAD))
#define GST_IS_KMS_COMPOSITOR_PAD_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_KMS_COMPOSITOR_PAD))
typedef struct _GstKMSCompositorPad GstKMSCompositorPad;
typedef struct _GstKMSCompositorPadClass GstKMSCompositorPadClass;

/**
 * GstKMSCompositorBlendMode
 * @KMS_COMPOSITOR_BLEND_NONE: Blend formula that ignores the pixel alpha:
 *                            plane_alpha * fg.rgb + (1 - plane_alpha) * bg.rgb
 * @KMS_COMPOSITOR_BLEND_PREMULTIPLIED: Blend formula that assumes the pixel color
 *                                     values have been already pre-multiplied with
 *                                     the alpha channel values:
 *                                     plane_alpha * fg.rgb +
 *                                     (1 - (plane_alpha * fg.alpha)) * bg.rgb
 * @KMS_COMPOSITOR_BLEND_COVERAGE: Blend formula that assumes the pixel color values
 *                                have not been pre-multiplied and will do so when
 *                                blending them to the background color values:
 *                                plane_alpha * fg.alpha * fg.rgb +
 *                                (1 - (plane_alpha * fg.alpha)) * bg.rgb
 *
 * The different pixel blend modes that can be used by kmscompositor.
 *
 * See https://dri.freedesktop.org/docs/drm/gpu/drm-kms.html#plane-composition-properties.
 */
typedef enum
{
    KMS_COMPOSITOR_BLEND_NONE,
    KMS_COMPOSITOR_BLEND_PREMULTIPLIED,
    KMS_COMPOSITOR_BLEND_COVERAGE
} GstKMSCompositorBlendMode;

typedef enum
{
    KMS_COMPOSITOR_ROTATE_0 = 1 << 0,
    KMS_COMPOSITOR_ROTATE_90 = 1 << 1,
    KMS_COMPOSITOR_ROTATE_180 = 1 << 2,
    KMS_COMPOSITOR_ROTATE_270 = 1 << 3,
    KMS_COMPOSITOR_REFLECT_X = 1 << 4,
    KMS_COMPOSITOR_REFLECT_Y = 1 << 5
} GstKMSCompositorRotation;

#define PROPS_PER_PLANE 14

struct _GstKMSCompositorPad
{
    GstVideoAggregatorPad aggregator_pad;

    /* private */
    guint32 kms_ids[PROPS_PER_PLANE];
    GstCaps *allowed_caps;
    GstBufferPool *pool;

    /* properties */
    GstVideoInfo vinfo;
    gint plane_id;
    gint xpos, ypos;
    gint width, height;
    gint src_x, src_y;
    gint src_width, src_height;
    guint16 alpha;
    GstKMSCompositorBlendMode blend;
    GstKMSCompositorRotation rotation;
    guint zorder; // TODO: how to integrate this with the zorder from GstVideoAggregatorPad?

    /* fixed by plane */
    gboolean primary;
    gboolean zorder_mutable;
};

struct _GstKMSCompositorPadClass
{
    GstVideoAggregatorPadClass parent;
};

GType gst_kms_compositor_pad_get_type(void);
gboolean gst_kms_compositor_pad_assign_plane(GstKMSCompositorPad *self, gint fd, drmModeRes *res,
                                             drmModeObjectPropertiesPtr properties,
                                             drmModePlane *plane);
GstBuffer *gst_kms_compositor_pad_get_input_buffer(GstKMSCompositorPad *self, GstBuffer *inbuf);

G_END_DECLS

#endif

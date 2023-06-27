/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/video/video.h>
#include <gst/base/base.h>
#include <gst/textlayoutoverlay/textlayoutoverlay-prelude.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY             (gst_base_text_layout_overlay_get_type())
#define GST_BASE_TEXT_LAYOUT_OVERLAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY,GstBaseTextLayoutOverlay))
#define GST_BASE_TEXT_LAYOUT_OVERLAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY,GstBaseTextLayoutOverlayClass))
#define GST_BASE_TEXT_LAYOUT_OVERLAY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY,GstBaseTextLayoutOverlayClass))
#define GST_IS_BASE_TEXT_LAYOUT_OVERLAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY))
#define GST_IS_BASE_TEXT_LAYOUT_OVERLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BASE_TEXT_LAYOUT_OVERLAY))

typedef struct _GstBaseTextLayoutOverlay GstBaseTextLayoutOverlay;
typedef struct _GstBaseTextLayoutOverlayClass GstBaseTextLayoutOverlayClass;
typedef struct _GstBaseTextLayoutOverlayPrivate GstBaseTextLayoutOverlayPrivate;

/**
 * GstBaseTextLayoutOverlay:
 *
 * Since: 1.24
 */
struct _GstBaseTextLayoutOverlay
{
  GstBaseTransform parent;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  /*< private >*/
  GstBaseTextLayoutOverlayPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBaseTextLayoutOverlayClass:
 *
 * Since: 1.24
 */
struct _GstBaseTextLayoutOverlayClass
{
  GstBaseTransformClass parent_class;

  /**
   * GstBaseTextLayoutOverlayClass::set_info
   * @overlay: a #GstBaseTextLayoutOverlay
   * @in_caps: (transfer none): a #GstCaps
   * @in_info: a #GstVideoInfo
   * @out_caps: (transfer none): a #GstCaps
   * @out_info: a #GstVideoInfo
   *
   * Optional, called with the negotiated caps and video info
   *
   * Since: 1.24
   */
  gboolean        (*set_info)        (GstBaseTextLayoutOverlay * overlay,
                                      GstCaps * in_caps,
                                      const GstVideoInfo * in_info,
                                      GstCaps * out_caps,
                                      const GstVideoInfo * out_info);

  /**
   * GstBaseTextLayoutOverlayClass::process_input:
   * @overlay: a #GstBaseTextLayoutOverlay
   * @buffer: (transfer none): an input #GstBuffer
   *
   * Optional, called before processing input buffer regardless of passthrough
   * status. Subclass that implement this must chain up to the parent
   * unless failed to process the input.
   *
   * Since: 1.24
   */
  GstFlowReturn   (*process_input)   (GstBaseTextLayoutOverlay * overlay,
                                      GstBuffer * buffer);

  /**
   * GstBaseTextLayoutOverlayClass::generate_layout:
   * @overlay: a #GstBaseTextLayoutOverlay
   * @text: (nullable): an user specified text
   * @buffer: (transfer none): a #GstBuffer
   * @layout: (out) (transfer full) (nullable): a location for a #GstTextLayout
   *
   * Generates a #GstTextLayout object.
   *
   * Since: 1.24
   */
  GstFlowReturn   (*generate_layout) (GstBaseTextLayoutOverlay * overlay,
                                      const gchar * text,
                                      GstBuffer * buffer,
                                      GstTextLayout ** layout);

  /**
   * GstBaseTextLayoutOverlayClass::accept_attribute:
   * @overlay: a #GstBaseTextLayoutOverlay
   * @attr: a #GstTextAttr
   *
   * Called to query if subclass can accept @attr or not
   *
   * Returns: %TRUE if subclass can accept the @attr, otherwise %FALSE
   *
   * Since: 1.24
   */
  gboolean        (*accept_attribute) (GstBaseTextLayoutOverlay * overlay,
                                       GstTextAttr * attr);

  /**
   * GstBaseTextLayoutOverlayClass::generate_output:
   * @overlay: a #GstBaseTextLayoutOverlay
   * @layout: (transfer none): a #GstTextLayout
   * @in_buf: (transfer none): a #GstBuffer
   * @buffer: (out) (transfer full): a #GstBuffer
   *
   * Generates output buffer using @layout and @in_buf
   *
   * Since: 1.24
   */
  GstFlowReturn   (*generate_output) (GstBaseTextLayoutOverlay * overlay,
                                      GstTextLayout * layout,
                                      GstBuffer * in_buf,
                                      GstBuffer ** out_buf);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_TEXT_LAYOUT_OVERLAY_API
GType gst_base_text_layout_overlay_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstBaseTextLayoutOverlay, gst_object_unref)

G_END_DECLS

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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>
#include <gst/allocators/gstdmabuf.h>

#include "gstkmscompositorpad.h"
#include "gstkmscompositor.h"

#include <gstkmsutils.h>
#include <gstkmsallocator.h>

GST_DEBUG_CATEGORY_STATIC (gst_kms_compositor_debug);
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);
#define GST_CAT_DEFAULT gst_kms_compositor_debug

#define parent_class gst_kms_compositor_pad_parent_class

#define GST_TYPE_KMS_COMPOSITOR_BLEND (gst_kms_compositor_blend_get_type())
static GType
gst_kms_compositor_blend_get_type (void)
{
  static GType kms_compositor_blend_type = 0;

  static const GEnumValue kms_compositor_blend[] = {
    {KMS_COMPOSITOR_BLEND_NONE, "None", "none"},
    {KMS_COMPOSITOR_BLEND_PREMULTIPLIED, "Premultiplied", "pre"},
    {KMS_COMPOSITOR_BLEND_COVERAGE, "Coverage", "cov"},
    {0, NULL, NULL},
  };

  if (!kms_compositor_blend_type) {
    kms_compositor_blend_type =
        g_enum_register_static ("GstKMSCompositorBlend", kms_compositor_blend);
  }
  return kms_compositor_blend_type;
}

#define C_ENUM(v) ((guint)v)

#define GST_TYPE_KMS_COMPOSITOR_ROTATION (gst_kms_compositor_rotation_get_type())
static GType
gst_kms_compositor_rotation_get_type (void)
{
  static gsize id = 0;

  static const GFlagsValue kms_compositor_rotation[] = {
    {C_ENUM (KMS_COMPOSITOR_ROTATE_0), "KMS_COMPOSITOR_ROTATE_0", "0"},
    {C_ENUM (KMS_COMPOSITOR_ROTATE_90), "KMS_COMPOSITOR_ROTATE_90", "90"},
    {C_ENUM (KMS_COMPOSITOR_ROTATE_180), "KMS_COMPOSITOR_ROTATE_180", "180"},
    {C_ENUM (KMS_COMPOSITOR_ROTATE_270), "KMS_COMPOSITOR_ROTATE_270", "270"},
    {C_ENUM (KMS_COMPOSITOR_REFLECT_X), "KMS_COMPOSITOR_REFLECT_X", "x"},
    {C_ENUM (KMS_COMPOSITOR_REFLECT_Y), "KMS_COMPOSITOR_REFLECT_Y", "y"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id = g_flags_register_static ("GstKMSCompositorRotation",
        kms_compositor_rotation);
    g_once_init_leave ((gsize *) & id, _id);
  }
  return id;
}

#define DEFAULT_PAD_PLANE_ID -1
#define DEFAULT_PAD_XPOS 0
#define DEFAULT_PAD_YPOS 0
#define DEFAULT_PAD_WIDTH -1
#define DEFAULT_PAD_HEIGHT -1
#define DEFAULT_PAD_SRC_X 0
#define DEFAULT_PAD_SRC_Y 0
#define DEFAULT_PAD_SRC_WIDTH -1
#define DEFAULT_PAD_SRC_HEIGHT -1
#define DEFAULT_PAD_ALPHA 1.
#define DEFAULT_PAD_BLEND KMS_COMPOSITOR_BLEND_COVERAGE
#define DEFAULT_PAD_ROTATION KMS_COMPOSITOR_ROTATE_0

enum
{
  PROP_PAD_PLANE_ID = 1,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_SRC_X,
  PROP_PAD_SRC_Y,
  PROP_PAD_SRC_WIDTH,
  PROP_PAD_SRC_HEIGHT,
  PROP_PAD_ALPHA,
  PROP_PAD_BLEND,
  PROP_PAD_ROTATION,
  PROP_PAD_PRIMARY,
  PROP_PAD_ZORDER_MUTABLE,
  PROP_PAD_N,
  PROP_PAD_ZORDER,              /* overwritten, not for initialization */
};

static GParamSpec *g_properties_pad[PROP_PAD_N] = {
  NULL,
};

G_DEFINE_TYPE_WITH_CODE (GstKMSCompositorPad, gst_kms_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD,
    GST_DEBUG_CATEGORY_GET (gst_kms_compositor_debug, "kmscompositor");
    GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE"));

gboolean
gst_kms_compositor_pad_assign_plane (GstKMSCompositorPad * self, gint fd,
    drmModeRes * res, drmModeObjectProperties * properties,
    drmModePlane * plane)
{
  GstCaps *in_caps;
  guint32 i;

  /* warning: self will not have a parent here yet! */
  if (self->allowed_caps)
    return TRUE;
  if (!res)
    return FALSE;
  GST_INFO_OBJECT (self, "assigning plane id %d to new pad", plane->plane_id);

  {
    /* keep in sync with gst_kms_compositor_aggregate_frames */
    static const char *prop_names[PROPS_PER_PLANE] = {
      "FB_ID", "CRTC_ID", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H", "SRC_X",
      "SRC_Y", "SRC_W", "SRC_H", "alpha", "pixel blend mode", "rotation",
      "zpos"
    };                          /* zpos must be last! */
    guint found_props = 0;

    for (i = 0; i < properties->count_props; ++i) {
      /* we only need rudimenatry property information */
      struct drm_mode_get_property prop;

      prop.count_values = prop.count_enum_blobs = 0;
      prop.prop_id = properties->props[i];
      if (!drmIoctl (fd, DRM_IOCTL_MODE_GETPROPERTY, &prop)) {
        if (!strcmp (prop.name, "type")) {
          self->primary = properties->prop_values[i] == DRM_PLANE_TYPE_PRIMARY;
        } else {
          for (guint j = 0; j < PROPS_PER_PLANE; ++j) {
            if (!self->kms_ids[j] && !strcmp (prop.name, prop_names[j])) {
              self->kms_ids[j] = prop.prop_id;
              ++found_props;
              if (j == PROPS_PER_PLANE - 1) {
                /* this is the zpos property (not the only reason why zpos must
                 * be last!) */
                self->zorder_mutable = !(prop.flags & DRM_MODE_PROP_IMMUTABLE);
                self->zorder = properties->prop_values[i];
              }
              break;
            }
          }
        }
      }
    }
    if (found_props != PROPS_PER_PLANE) {
      GST_ERROR_OBJECT (self,
          "Not all plane properties were returned by the driver");
      return FALSE;
    }
  }

  in_caps = gst_caps_new_empty ();
  if (!in_caps)
    return FALSE;
  self->plane_id = plane->plane_id;
  for (i = 0; i < plane->count_formats; ++i) {
    GstVideoFormat fmt = gst_video_format_from_drm (plane->formats[i]);
    const gchar *format;
    GstCaps *caps;

    if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_INFO_OBJECT (self, "ignoring format %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (plane->formats[i]));
      continue;
    }

    format = gst_video_format_to_string (fmt);
    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, format,
        "width", GST_TYPE_INT_RANGE, res->min_width, res->max_width,
        "height", GST_TYPE_INT_RANGE, res->min_height, res->max_height,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    if (caps)
      in_caps = gst_caps_merge (in_caps, caps);
  }
  in_caps = gst_caps_simplify (in_caps);
  if (gst_caps_is_empty (in_caps)) {
    GST_DEBUG_OBJECT (self, "allowed caps is empty");
    gst_caps_unref (in_caps);
    return FALSE;
  }
  self->allowed_caps = in_caps;
  GST_DEBUG_OBJECT (self, "allowed caps = %" GST_PTR_FORMAT, in_caps);
  return TRUE;
}

static GstBuffer *
gst_kms_compositor_pad_copy_to_dumb_buffer (GstKMSCompositorPad * self,
    GstBuffer * inbuf)
{
  GstBuffer *buf;
  GstFlowReturn ret;
  GstVideoFrame inframe, outframe;
  gboolean success;

  if (!self->pool) {
    GstVideoInfo vinfo = self->vinfo;
    GstVideoMeta *vmeta = gst_buffer_get_video_meta (inbuf);
    GstCaps *caps;
    GstBufferPool *pool;

    if (vmeta) {
      vinfo.width = vmeta->width;
      vinfo.height = vmeta->height;
    }

    caps = gst_video_info_to_caps (&vinfo);
    pool = gst_kms_compositor_create_pool (GST_OBJECT (self),
        GST_KMS_COMPOSITOR (gst_pad_get_parent (self)), caps,
        gst_buffer_get_size (inbuf));
    gst_caps_unref (caps);
    if (!pool)
      return NULL;

    if (!gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (self, "failed to activate buffer pool");
      gst_object_unref (pool);
      return FALSE;
    }

    self->pool = pool;
  }

  ret = gst_buffer_pool_acquire_buffer (self->pool, &buf, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "allocation failed: failed to create buffer");
    return NULL;
  }

  if (!gst_video_frame_map (&inframe, &self->vinfo, inbuf, GST_MAP_READ)) {
    GST_WARNING_OBJECT (self, "failed to map buffer");
    goto done;
  }

  if (!gst_video_frame_map (&outframe, &self->vinfo, buf, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&inframe);
    GST_WARNING_OBJECT (self, "failed to map buffer");
    goto done;
  }

  success = gst_video_frame_copy (&outframe, &inframe);
  gst_video_frame_unmap (&outframe);
  gst_video_frame_unmap (&inframe);
  if (!success) {
    GST_WARNING_OBJECT (self, "failed to upload buffer");
    goto done;
  }

  return buf;

done:
  gst_buffer_unref (buf);
  return NULL;
}

static gboolean
gst_kms_compositor_pad_import_dmabuf (GstKMSCompositorPad * self,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstKMSCompositor *comp = GST_KMS_COMPOSITOR (gst_pad_get_parent (self));
  GstVideoInfo *vinfo = &self->vinfo;
  guint n_planes, n_mem;
  GstVideoMeta *meta;
  guint mems_idx[GST_VIDEO_MAX_PLANES];
  gsize mems_skip[GST_VIDEO_MAX_PLANES];
  GstMemory *mems[GST_VIDEO_MAX_PLANES];
  guint i;
  GstKMSMemory *kmsmem;

  if (!comp->has_prime_import)
    return FALSE;

  /* This will eliminate most non-dmabuf out there */
  if (!gst_is_dmabuf_memory (gst_buffer_peek_memory (inbuf, 0)))
    return FALSE;

  n_planes = GST_VIDEO_INFO_N_PLANES (vinfo);
  n_mem = gst_buffer_n_memory (inbuf);
  meta = gst_buffer_get_video_meta (inbuf);

  GST_TRACE_OBJECT (self, "Found a dmabuf with %u planes and %u memories",
      n_planes, n_mem);

  /* We cannot have multiple dmabuf per plane */
  if (n_mem > n_planes)
    return FALSE;
  g_assert (n_planes != 0);

  /* Update video info based on video meta */
  if (meta) {
    GST_VIDEO_INFO_WIDTH (vinfo) = meta->width;
    GST_VIDEO_INFO_HEIGHT (vinfo) = meta->height;

    for (guint i = 0; i < meta->n_planes; i++) {
      GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i) = meta->offset[i];
      GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i) = meta->stride[i];
    }
  }

  /* Find and validate all memories */
  for (i = 0; i < n_planes; i++) {
    guint length;

    if (!gst_buffer_find_memory (inbuf,
            GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i), 1,
            &mems_idx[i], &length, &mems_skip[i]))
      return FALSE;

    mems[i] = gst_buffer_peek_memory (inbuf, mems_idx[i]);

    /* adjust for memory offset, in case data does not
     * start from byte 0 in the dmabuf fd */
    mems_skip[i] += mems[i]->offset;

    /* And all memory found must be dmabuf */
    if (!gst_is_dmabuf_memory (mems[i]))
      return FALSE;
  }

  gst_kms_compositor_ensure_kms_allocator (comp);

  kmsmem = (GstKMSMemory *) gst_kms_allocator_get_cached (mems[0]);
  if (kmsmem) {
    GST_LOG_OBJECT (self, "found KMS mem %p in DMABuf mem %p with fb id = %d",
        kmsmem, mems[0], kmsmem->fb_id);
  } else {
    gint prime_fds[GST_VIDEO_MAX_PLANES] = {
      0,
    };
    for (i = 0; i < n_planes; i++)
      prime_fds[i] = gst_dmabuf_memory_get_fd (mems[i]);

    GST_LOG_OBJECT (self, "found these prime ids: %d, %d, %d, %d", prime_fds[0],
        prime_fds[1], prime_fds[2], prime_fds[3]);

    kmsmem = gst_kms_allocator_dmabuf_import (comp->allocator,
        prime_fds, n_planes, mems_skip, vinfo);
    if (!kmsmem)
      return FALSE;

    GST_LOG_OBJECT (self, "setting KMS mem %p to DMABuf mem %p with fb id = %d",
        kmsmem, mems[0], kmsmem->fb_id);
    gst_kms_allocator_cache (comp->allocator, mems[0],
        GST_MEMORY_CAST (kmsmem));
  }

  *outbuf = gst_buffer_new ();
  if (!*outbuf)
    return FALSE;
  gst_buffer_append_memory (*outbuf, gst_memory_ref (GST_MEMORY_CAST (kmsmem)));
  gst_buffer_add_parent_buffer_meta (*outbuf, inbuf);

  return TRUE;
}

GstBuffer *
gst_kms_compositor_pad_get_input_buffer (GstKMSCompositorPad * self,
    GstBuffer * inbuf)
{
  GstMemory *mem = gst_buffer_peek_memory (inbuf, 0);
  GstBuffer *buf;

  if (!mem)
    return NULL;

  if (gst_is_kms_memory (mem))
    return gst_buffer_ref (inbuf);

  if (gst_kms_compositor_pad_import_dmabuf (self, inbuf, &buf)) {
    return buf;
  } else {
    GST_CAT_INFO_OBJECT (CAT_PERFORMANCE, self, "frame copy");
    return gst_kms_compositor_pad_copy_to_dumb_buffer (self, inbuf);
  }
  /* metadata will be explictly copied in caller */
}

static void
gst_kms_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKMSCompositorPad *self = GST_KMS_COMPOSITOR_PAD (object);

  GST_DEBUG_OBJECT (self, "pad get_property");

  switch (prop_id) {
    case PROP_PAD_PLANE_ID:
      g_value_set_int (value, self->plane_id);
      break;
    case PROP_PAD_XPOS:
      g_value_set_int (value, self->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, self->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, self->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, self->height);
      break;
    case PROP_PAD_SRC_X:
      g_value_set_int (value, self->src_x);
      break;
    case PROP_PAD_SRC_Y:
      g_value_set_int (value, self->src_y);
      break;
    case PROP_PAD_SRC_WIDTH:
      g_value_set_int (value, self->src_width);
      break;
    case PROP_PAD_SRC_HEIGHT:
      g_value_set_int (value, self->src_height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, self->alpha / (gdouble) 0xFFFF);
      break;
    case PROP_PAD_BLEND:
      g_value_set_enum (value, self->blend);
      break;
    case PROP_PAD_ROTATION:
      g_value_set_flags (value, self->rotation);
      break;
    case PROP_PAD_ZORDER:
      /* TODO: remove zorder and request value from parent */
      g_value_set_uint (value, self->zorder);
      break;
    case PROP_PAD_PRIMARY:
      g_value_set_boolean (value, self->primary);
      break;
    case PROP_PAD_ZORDER_MUTABLE:
      g_value_set_boolean (value, self->zorder_mutable);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKMSCompositorPad *self = GST_KMS_COMPOSITOR_PAD (object);

  GST_DEBUG_OBJECT (self, "pad set_property");

  switch (prop_id) {
    case PROP_PAD_XPOS:
      self->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      self->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_WIDTH:
      self->width = g_value_get_int (value);
      break;
    case PROP_PAD_HEIGHT:
      self->height = g_value_get_int (value);
      break;
    case PROP_PAD_SRC_X:
      self->src_x = g_value_get_int (value);
      break;
    case PROP_PAD_SRC_Y:
      self->src_y = g_value_get_int (value);
      break;
    case PROP_PAD_SRC_WIDTH:
      self->src_width = g_value_get_int (value);
      break;
    case PROP_PAD_SRC_HEIGHT:
      self->src_height = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      self->alpha = (guint16) (0xFFFF * g_value_get_double (value));
      break;
    case PROP_PAD_BLEND:
      self->blend = g_value_get_enum (value);
      break;
    case PROP_PAD_ZORDER:
      if (self->zorder_mutable)
        self->zorder = g_value_get_uint (value);
      else
        GST_WARNING_OBJECT (self, "zorder is not mutable for this plane");
      break;
    case PROP_PAD_ROTATION:
      self->rotation = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_compositor_pad_init (GstKMSCompositorPad * self)
{
  gst_video_info_init (&self->vinfo);
  self->plane_id = DEFAULT_PAD_PLANE_ID;
  self->xpos = DEFAULT_PAD_XPOS;
  self->ypos = DEFAULT_PAD_YPOS;
  self->width = DEFAULT_PAD_WIDTH;
  self->height = DEFAULT_PAD_HEIGHT;
  self->src_x = DEFAULT_PAD_SRC_X;
  self->src_y = DEFAULT_PAD_SRC_Y;
  self->src_width = DEFAULT_PAD_SRC_WIDTH;
  self->src_height = DEFAULT_PAD_SRC_HEIGHT;
  self->alpha = (guint16) (0xFFFF * DEFAULT_PAD_ALPHA);
  self->blend = DEFAULT_PAD_BLEND;
  self->rotation = DEFAULT_PAD_ROTATION;
}

static void
gst_kms_compositor_pad_finalize (GObject * object)
{
  GstKMSCompositorPad *self = GST_KMS_COMPOSITOR_PAD (object);

  GST_DEBUG_OBJECT (self, "finalize");
  gst_caps_replace (&self->allowed_caps, NULL);
  gst_object_replace ((GstObject **) & self->pool, NULL);
}

static void
gst_kms_compositor_pad_class_init (GstKMSCompositorPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_kms_compositor_pad_set_property;
  gobject_class->get_property = gst_kms_compositor_pad_get_property;
  gobject_class->finalize = gst_kms_compositor_pad_finalize;

  g_properties_pad[PROP_PAD_PLANE_ID] = g_param_spec_int ("plane-id",
      "Plane ID", "DRM plane id", 0, G_MAXINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_properties_pad[PROP_PAD_XPOS] = g_param_spec_int ("xpos",
      "X Position", "X Position of the picture",
      G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_YPOS] = g_param_spec_int ("ypos",
      "Y Position", "Y Position of the picture",
      G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_WIDTH] = g_param_spec_int ("width",
      "Width of the picture",
      "Width of the picture (-1 will use device defaults)", -1, G_MAXINT,
      DEFAULT_PAD_WIDTH,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_HEIGHT] =
      g_param_spec_int ("height", "Height of the picture",
      "Height of the picture (-1 will use device defaults)", -1, G_MAXINT,
      DEFAULT_PAD_HEIGHT,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  g_properties_pad[PROP_PAD_SRC_X] = g_param_spec_int ("src-x",
      "Source X Position",
      "Left position of visible portion of plane within plane (in 16.16 fixed point).",
      0, G_MAXINT, DEFAULT_PAD_SRC_X,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_SRC_Y] = g_param_spec_int ("src-y",
      "Source Y Position",
      "Upper position of visible portion of plane within plane (in 16.16 fixed point).",
      0, G_MAXINT, DEFAULT_PAD_SRC_Y,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_SRC_WIDTH] = g_param_spec_int ("src-width",
      "Source Width", "Width of visible portion of plane (in 16.16)",
      -1, G_MAXINT, DEFAULT_PAD_SRC_WIDTH,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_SRC_HEIGHT] = g_param_spec_int ("src-height",
      "Source Height", "Height of visible portion of plane (in 16.16)",
      -1, G_MAXINT, DEFAULT_PAD_SRC_HEIGHT,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  g_properties_pad[PROP_PAD_ALPHA] = g_param_spec_double ("alpha",
      "Alpha", "Alpha of the picture",
      0.0, 1.0, DEFAULT_PAD_ALPHA,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_BLEND] = g_param_spec_enum ("blend", "Blend",
      "Pixel blend mode",
      GST_TYPE_KMS_COMPOSITOR_BLEND, DEFAULT_PAD_BLEND,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_ROTATION] =
      g_param_spec_flags ("rotation", "Rotation", "Rotation of the plane",
      GST_TYPE_KMS_COMPOSITOR_ROTATION, DEFAULT_PAD_ROTATION,
      G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_override_property (gobject_class, PROP_PAD_ZORDER, "zorder");
  g_properties_pad[PROP_PAD_PRIMARY] =
      g_param_spec_boolean ("primary", "Primary plane",
      "Indicates whether this is a primary or overlay plane", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_properties_pad[PROP_PAD_ZORDER_MUTABLE] =
      g_param_spec_boolean ("zorder-mutable", "Mutability of zorder",
      "Indicates whether the zorder property can be set (plane-dependent)",
      FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_PAD_N,
      g_properties_pad);
}

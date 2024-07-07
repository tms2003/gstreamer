/* GStreamer
 *
 * Copyright (C) 2016 Igalia
 *
 * Authors:
 *  Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
 *  Javier Martin <javiermartin@by.com.es>
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
 *
 */

/**
 * SECTION:element-kmssink
 * @title: kmssink
 * @short_description: A KMS/DRM based video sink
 *
 * kmssink is a simple video sink that renders video frames directly
 * in a plane of a DRM device.
 *
 * In advance usage, the behaviour of kmssink can be change using the
 * supported properties. Note that plane and connectors IDs and properties can
 * be enumerated using the modetest command line tool.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! kmssink
 * gst-launch-1.0 videotestsrc ! kmssink plane-properties=s,rotation=4
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video-color.h>
#include <gst/allocators/gstdmabuf.h>

#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <string.h>

#include "gstkmssink.h"
#include "gstkmsutils.h"
#include "gstkmsbufferpool.h"
#include "gstkmsallocator.h"

#ifdef HAVE_DRM_HDR
#include <math.h>
#include "gstkmsedid.h"
#endif

#define GST_PLUGIN_NAME "kmssink"
#define GST_PLUGIN_DESC "Video sink using the Linux kernel mode setting API"

#define cache_drm_properties(TYPE, filed, drm_fd)                       \
  do {                                                                  \
    drmModeObjectProperties *props; gint i;                             \
    props = drmModeObjectGetProperties (drm_fd, filed##_id,             \
      DRM_MODE_OBJECT_##TYPE);                                          \
    g_return_val_if_fail (props, FALSE);                                \
    for (i = 0; i < props->count_props; i++) {                          \
      GQuark key_id; drmModePropertyRes *prop;                          \
      prop = drmModeGetProperty (drm_fd, props->props[i]);              \
      if (!prop)                                                        \
        continue;                                                       \
      key_id = g_quark_from_static_string (prop->name);                 \
      g_datalist_id_set_data_full (&(filed##_props), key_id, prop,      \
                      destroy_drm_prop);                                \
    }                                                                   \
    drmModeFreeObjectProperties(props);                                 \
  } while (0)

#define add_drm_property(TYPE, req, _name, value)                       \
  do {                                                                  \
    GQuark key_id; drmModePropertyRes *prop;                            \
    key_id = g_quark_try_string (_name);                                \
    if (!key_id) {                                                      \
      return  -1;                                                       \
    }                                                                   \
    prop = g_datalist_id_get_data (&(TYPE##_props), key_id);            \
    if (!prop) {                                                        \
      return -1;                                                        \
    }                                                                   \
    ret = drmModeAtomicAddProperty (req, TYPE##_id,                     \
                                    prop->prop_id, value);              \
  } while (0)

#define get_drm_property(TYPE, _name, _value)                           \
  do {                                                                  \
    GQuark key_id;                                                      \
    key_id = g_quark_try_string (_name);                                \
    if (!key_id) {                                                      \
      return FALSE;                                                     \
    }                                                                   \
    _value = g_datalist_id_get_data (&(TYPE##_props), key_id);          \
    if (!_value) {                                                      \
      return FALSE;                                                     \
    }                                                                   \
  } while (0)

GST_DEBUG_CATEGORY_STATIC (gst_kms_sink_debug);
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);
#define GST_CAT_DEFAULT gst_kms_sink_debug

static gboolean gst_kms_sink_wait_event (GstKMSSink * self);
static GstFlowReturn gst_kms_sink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static void gst_kms_sink_video_overlay_init (GstVideoOverlayInterface * iface);
static void gst_kms_sink_drain (GstKMSSink * self);

#define parent_class gst_kms_sink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstKMSSink, gst_kms_sink, GST_TYPE_VIDEO_SINK,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_PLUGIN_NAME, 0,
        GST_PLUGIN_DESC);
    GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_kms_sink_video_overlay_init));
GST_ELEMENT_REGISTER_DEFINE (kmssink, GST_PLUGIN_NAME, GST_RANK_SECONDARY,
    GST_TYPE_KMS_SINK);

enum
{
  PROP_DRIVER_NAME = 1,
  PROP_BUS_ID,
  PROP_CONNECTOR_ID,
  PROP_PLANE_ID,
  PROP_FORCE_MODESETTING,
  PROP_RESTORE_CRTC,
  PROP_CAN_SCALE,
  PROP_DISPLAY_WIDTH,
  PROP_DISPLAY_HEIGHT,
  PROP_CONNECTOR_PROPS,
  PROP_PLANE_PROPS,
  PROP_FD,
  PROP_SKIP_VSYNC,
  PROP_FORCE_LEGACY,
  PROP_N,
};

#ifdef HAVE_DRM_HDR
enum hdmi_metadata_type
{
  HDMI_STATIC_METADATA_TYPE1 = 0,
};
enum hdmi_eotf
{
  HDMI_EOTF_TRADITIONAL_GAMMA_SDR = 0,
  HDMI_EOTF_TRADITIONAL_GAMMA_HDR,
  HDMI_EOTF_SMPTE_ST2084,
  HDMI_EOTF_BT_2100_HLG,
};
#endif

static GParamSpec *g_properties[PROP_N] = { NULL, };

static void
destroy_drm_prop (gpointer data)
{
  drmModeFreeProperty ((drmModePropertyPtr) data);
}

static gboolean
cache_conn_properties (GstKMSSink * self)
{
  gboolean ret = TRUE;
  gint conn_id = self->conn_id;
  GData *conn_props = self->conn_props;

  cache_drm_properties (CONNECTOR, conn, self->fd);
  return ret;
}

static gboolean
cache_crtc_properties (GstKMSSink * self)
{
  gboolean ret = TRUE;
  gint crtc_id = self->crtc_id;
  GData *crtc_props = self->conn_props;

  cache_drm_properties (CRTC, crtc, self->fd);
  return ret;
}

static gboolean
cache_plane_properties (GstKMSSink * self, gint plane_id)
{
  GData *plane_props = NULL;
  g_datalist_init (&plane_props);

  cache_drm_properties (PLANE, plane, self->fd);

  g_hash_table_insert (self->plane_res, GUINT_TO_POINTER (plane_id),
      plane_props);

  return TRUE;
}

static void
destroy_fmt_slist (gpointer data)
{
  g_slist_free ((GSList *) data);
}

static gboolean
cache_planes_properties (GstKMSSink * self, drmModePlaneRes * pres)
{
  int i;

  self->plane_res = g_hash_table_new (g_direct_hash, g_direct_equal);

  for (i = 0; i < pres->count_planes; i++) {
    GData *props = NULL;
    GSList *formats = NULL;
    drmModePlane *plane = NULL;
    int j;

    plane = drmModeGetPlane (self->fd, pres->planes[i]);
    if (!plane)
      continue;

    cache_plane_properties (self, plane->plane_id);

    props = g_hash_table_lookup (self->plane_res,
        GUINT_TO_POINTER (plane->plane_id));
    if (!props) {
      drmModeFreePlane (plane);
      continue;
    }

    /*
     * TODO: store drm modifier in the future, the below way only for
     * those platform without a modifier in FB
     */
    for (j = 0; j < plane->count_formats; j++)
      formats = g_slist_prepend (formats, GUINT_TO_POINTER (plane->formats[j]));

    g_datalist_id_set_data_full (&props,
        g_quark_from_static_string ("fmt"), formats, destroy_fmt_slist);
    g_datalist_id_set_data (&props,
        g_quark_from_static_string ("possible_crtcs"),
        GUINT_TO_POINTER (plane->possible_crtcs));

    drmModeFreePlane (plane);
  }

  return TRUE;
}

static gint
add_connection_property (GstKMSSink * self, drmModeAtomicReq * req,
    const gchar * name, guint64 value)
{
  gint ret = 0;
  gint conn_id = self->conn_id;
  GData *conn_props = self->conn_props;

  add_drm_property (conn, req, name, value);
  return ret;
}

static gint
add_crtc_property (GstKMSSink * self, drmModeAtomicReq * req,
    const gchar * name, guint64 value)
{
  gint ret = 0;
  gint crtc_id = self->crtc_id;
  GData *crtc_props = self->conn_props;

  add_drm_property (crtc, req, name, value);
  return ret;
}

static gint
add_plane_property (GstKMSSink * self, drmModeAtomicReq * req, gint plane_id,
    const gchar * name, guint64 value)
{
  gint ret = 0;
  GData *plane_props = g_hash_table_lookup (self->plane_res,
      GUINT_TO_POINTER (plane_id));

  add_drm_property (plane, req, name, value);
  return ret;
}

static gboolean
set_drm_property (GstKMSSink * self, guint32 object, guint32 object_type,
    const gchar * prop_name, guint64 value);

#ifdef HAVE_DRM_HDR
static void
gst_kms_populate_infoframe (struct hdr_output_metadata *pinfo_frame,
    GstVideoMasteringDisplayInfo * p_hdr_minfo,
    GstVideoContentLightLevel * p_hdr_cll,
    gchar colorimetry, gboolean clear_it_out)
{
  /* From CTA-861.3:
   * When a source is transmitting the Dynamic Range and Mastering InfoFrame,
   * it shall signal the end of Dynamic Range... by sending a ... InfoFrame with
   * the EOTF field to '0', the Static_Metadata_Descriptor_ID field set to '0',
   * and the fields of the Static_Metadata_Descriptor set to unknown (0)...
   *
   * See also https://dri.freedesktop.org/docs/drm/gpu/drm-uapi.html
   */
  if (clear_it_out) {
    /* Static_Metadata_Descriptor_ID */
    pinfo_frame->metadata_type = 0;
    (void) memset ((void *) &pinfo_frame->hdmi_metadata_type1, 0,
        sizeof (pinfo_frame->hdmi_metadata_type1));
    return;
  } else {
    pinfo_frame->metadata_type = HDMI_STATIC_METADATA_TYPE1;
    pinfo_frame->hdmi_metadata_type1.eotf = colorimetry;
    pinfo_frame->hdmi_metadata_type1.metadata_type = HDMI_STATIC_METADATA_TYPE1;
  }

  /* For HDR Infoframe see CTA-861-G, Section 6.9.1
   * SEI message is in units of 0.0001 cd/m2, HDMI is units of 1 cd/m2 - see
   * x265 specs */
  pinfo_frame->hdmi_metadata_type1.max_display_mastering_luminance =
      round (p_hdr_minfo->max_display_mastering_luminance / 10000.0);
  pinfo_frame->hdmi_metadata_type1.min_display_mastering_luminance =
      p_hdr_minfo->min_display_mastering_luminance;

  pinfo_frame->hdmi_metadata_type1.max_cll = p_hdr_cll->max_content_light_level;
  pinfo_frame->hdmi_metadata_type1.max_fall =
      p_hdr_cll->max_frame_average_light_level;

  for (int i = 0; i < 3; i++) {
    pinfo_frame->hdmi_metadata_type1.display_primaries[i].x =
        p_hdr_minfo->display_primaries[i].x;
    pinfo_frame->hdmi_metadata_type1.display_primaries[i].y =
        p_hdr_minfo->display_primaries[i].y;
  }

  pinfo_frame->hdmi_metadata_type1.white_point.x = p_hdr_minfo->white_point.x;
  pinfo_frame->hdmi_metadata_type1.white_point.y = p_hdr_minfo->white_point.y;
}

static void
gst_kms_push_hdr_infoframe (GstKMSSink * self, drmModeAtomicReq * req)
{
  struct hdr_output_metadata info_frame;
  int drm_fd = self->fd;
  int ret = 0;

  if (!self->has_hdr_prop || !self->has_hdr_info)
    return;

  if (!self->hdr_blob_id) {
    gst_kms_populate_infoframe (&info_frame, &self->hdr_minfo, &self->hdr_cll,
        self->colorimetry, FALSE);

    ret = drmModeCreatePropertyBlob (drm_fd, &info_frame,
        sizeof (struct hdr_output_metadata), &self->hdr_blob_id);
    if (ret) {
      GST_ERROR_OBJECT (self, "Failed to drmModeCreatePropertyBlob %d %s",
          errno, g_strerror (errno));
      return;
    }
  }

  add_connection_property (self, req, "HDR_OUTPUT_METADATA", self->hdr_blob_id);
}

static void
gst_kms_push_hdr_infoframe_legacy (GstKMSSink * self, gboolean clear_it_out)
{
  struct hdr_output_metadata info_frame;
  int drm_fd = self->fd;
  uint32_t conn_id = self->conn_id;
  uint32_t hdr_blob_id;
  int ret = 0;


  if (!self->has_hdr_prop || !self->has_hdr_info || (!clear_it_out
          && self->has_sent_hdrif)) {
    return;
  }

  if (clear_it_out)
    GST_INFO ("Clearing HDR Infoframe on connector %d", conn_id);
  else
    GST_INFO ("Setting HDR Infoframe, if available on connector %d", conn_id);

  gst_kms_populate_infoframe (&info_frame, &self->hdr_minfo, &self->hdr_cll,
      self->colorimetry, clear_it_out);

  ret = drmModeCreatePropertyBlob (drm_fd, &info_frame,
      sizeof (struct hdr_output_metadata), &hdr_blob_id);
  if (!ret) {
    /* Use non-atomic property setting */
    ret = set_drm_property (self, conn_id, DRM_MODE_OBJECT_CONNECTOR,
        "HDR_OUTPUT_METADATA", hdr_blob_id);
    if (ret) {
      GST_ERROR_OBJECT (self, "drmModeObjectSetProperty result %d %d %s", ret,
          errno, g_strerror (errno));
    }
    drmModeDestroyPropertyBlob (drm_fd, hdr_blob_id);
  } else {
    GST_ERROR_OBJECT (self, "Failed to drmModeCreatePropertyBlob %d %s", errno,
        g_strerror (errno));
  }

  if (!ret) {
    GST_INFO ("Set HDR Infoframe on connector %d", conn_id);
    self->has_sent_hdrif = TRUE;        // Hooray!
  }
}

/* From an HDR10 stream caps:
 *
 * colorimetry=(string)bt2100-pq
 * content-light-level=(string)10000:166
 * mastering-display-info=(string)35400:14600:8500:39850:6550:2300:15635:16450:10000000:1
 */
static void
gst_kms_sink_set_hdr10_caps (GstKMSSink * self, GstCaps * caps)
{
  GstVideoMasteringDisplayInfo hdr_minfo;
  GstVideoContentLightLevel hdr_cll;
  GstStructure *structure;
  const gchar *colorimetry_s;
  GstVideoColorimetry colorimetry;
  gboolean has_hdr_eotf = FALSE;
  gboolean has_cll = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if ((colorimetry_s = gst_structure_get_string (structure,
              "colorimetry")) != NULL &&
      gst_video_colorimetry_from_string (&colorimetry, colorimetry_s)) {
    switch (colorimetry.transfer) {
      case GST_VIDEO_TRANSFER_SMPTE2084:
        self->colorimetry = HDMI_EOTF_SMPTE_ST2084;
        has_hdr_eotf = TRUE;
        GST_DEBUG ("Got HDR transfer value GST_VIDEO_TRANSFER_SMPTE2084: %u",
            self->colorimetry);
        break;
      case GST_VIDEO_TRANSFER_BT2020_10:
      case GST_VIDEO_TRANSFER_ARIB_STD_B67:
        self->colorimetry = HDMI_EOTF_BT_2100_HLG;
        has_hdr_eotf = TRUE;
        GST_DEBUG ("Got HDR transfer value HDMI_EOTF_BT_2100_HLG: %u",
            self->colorimetry);
        break;
      case GST_VIDEO_TRANSFER_BT709:
        self->colorimetry = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
        GST_DEBUG ("Got HDR transfer value GST_VIDEO_TRANSFER_BT709, "
            "not HDR: %u", self->colorimetry);
        break;
      default:
        /* not an HDMI and/or HDR colorimetry, we will ignore */
        GST_DEBUG ("Unsupported transfer function, no HDR: %u",
            colorimetry.transfer);
        self->has_hdr_info = FALSE;
        break;
    }
  }

  if (gst_video_mastering_display_info_from_caps (&hdr_minfo, caps)) {
    if (!gst_video_mastering_display_info_is_equal (&hdr_minfo,
            &self->hdr_minfo)) {
      self->hdr_minfo = hdr_minfo;
      self->has_hdr_info = TRUE;
      /* to send again */
      self->has_sent_hdrif = FALSE;
    }

    GST_DEBUG ("Got mastering info: "
        "min %u max %u wp %u %u dp[0] %u %u dp[1] %u %u dp[2] %u %u",
        self->hdr_minfo.min_display_mastering_luminance,
        self->hdr_minfo.max_display_mastering_luminance,
        self->hdr_minfo.white_point.x, self->hdr_minfo.white_point.y,
        self->hdr_minfo.display_primaries[0].x,
        self->hdr_minfo.display_primaries[0].y,
        self->hdr_minfo.display_primaries[1].x,
        self->hdr_minfo.display_primaries[1].y,
        self->hdr_minfo.display_primaries[2].x,
        self->hdr_minfo.display_primaries[2].y);

  } else {
    if (self->has_hdr_info == TRUE) {
      GST_WARNING ("Missing mastering display info");
    } else {
      self->has_hdr_info = FALSE;
    }

    gst_video_mastering_display_info_init (&self->hdr_minfo);
  }

  if (gst_video_content_light_level_from_caps (&hdr_cll, caps)) {
    GST_DEBUG ("Got content light level information: Max CLL: %u Max FALL: %u",
        hdr_cll.max_content_light_level, hdr_cll.max_frame_average_light_level);

    if (!gst_video_content_light_level_is_equal (&hdr_cll, &self->hdr_cll)) {
      self->hdr_cll = hdr_cll;
      self->has_hdr_info = TRUE;
      /* to send again */
      self->has_sent_hdrif = FALSE;
    }

    has_cll = TRUE;
  } else {
    gst_video_content_light_level_init (&self->hdr_cll);

    if (self->has_hdr_info == TRUE) {
      GST_WARNING ("Missing content light level info");
    }

    self->has_hdr_info = FALSE;
  }

  /* need all caps set */
  if ((has_hdr_eotf || has_cll) && !(has_hdr_eotf && has_cll)) {
    GST_ELEMENT_WARNING (self, STREAM, FORMAT,
        ("Stream doesn't have all HDR components needed"),
        ("Check stream caps"));

    self->has_hdr_info = FALSE;
  }
}

static gboolean
gst_kms_sink_check_hdr_props (GstKMSSink * self)
{
  struct gst_kms_hdr_static_metadata hdr_edid_info;
  GData *conn_props = self->conn_props;
  drmModePropertyPtr property = NULL;
  drmModePropertyBlobPtr blob;
  int res;

  get_drm_property (conn, "HDR_OUTPUT_METADATA", property);

  /* check the connected screen */
  get_drm_property (conn, "EDID", property);
  if (!(property->flags & DRM_MODE_PROP_BLOB))
    return FALSE;
  if (!property->count_blobs)
    return FALSE;

  memset (&hdr_edid_info, 0, sizeof (hdr_edid_info));
  blob = drmModeGetPropertyBlob (self->fd, property->blob_ids[0]);
  if (blob) {
    res = gst_kms_edid_parse (&hdr_edid_info, blob->data, blob->length);
    if (res) {
      hdr_edid_info.eotf = 0;
      hdr_edid_info.metadata_type = 0;
    }
    drmModeFreePropertyBlob (blob);

    GST_DEBUG_OBJECT (self, "EDID prop ID = %d", property->prop_id);
    /* only these two values are guaranteed to be populated for HDR */
    GST_DEBUG_OBJECT (self, "EDID EOTF = %u, metadata type = %u",
        hdr_edid_info.eotf, hdr_edid_info.metadata_type);
  } else {
    return FALSE;
  }

  if (hdr_edid_info.eotf && hdr_edid_info.metadata_type)
    return TRUE;

  return FALSE;
}
#endif /* HAVE_DRM_HDR */

static void
gst_kms_sink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height)
{
  GstKMSSink *self = GST_KMS_SINK (overlay);

  GST_DEBUG_OBJECT (self, "Setting render rectangle to (%d,%d) %dx%d", x, y,
      width, height);

  GST_OBJECT_LOCK (self);

  if (width == -1 && height == -1) {
    x = 0;
    y = 0;
    width = self->hdisplay;
    height = self->vdisplay;
  }

  if (width <= 0 || height <= 0)
    goto done;

  self->pending_rect.x = x;
  self->pending_rect.y = y;
  self->pending_rect.w = width;
  self->pending_rect.h = height;

  if (self->can_scale ||
      (self->render_rect.w == width && self->render_rect.h == height)) {
    self->render_rect = self->pending_rect;
  } else {
    self->reconfigure = TRUE;
    GST_DEBUG_OBJECT (self, "Waiting for new caps to apply render rectangle");
  }

done:
  GST_OBJECT_UNLOCK (self);
}

static void
gst_kms_sink_expose (GstVideoOverlay * overlay)
{
  GstKMSSink *self = GST_KMS_SINK (overlay);

  GST_DEBUG_OBJECT (overlay, "Expose called by application");

  if (!self->can_scale) {
    GST_OBJECT_LOCK (self);
    if (self->reconfigure) {
      GST_OBJECT_UNLOCK (self);
      GST_DEBUG_OBJECT (overlay, "Sending a reconfigure event");
      gst_pad_push_event (GST_BASE_SINK_PAD (self),
          gst_event_new_reconfigure ());
    } else {
      GST_DEBUG_OBJECT (overlay, "Applying new render rectangle");
      /* size of the rectangle does not change, only the (x,y) position changes */
      self->render_rect = self->pending_rect;
      GST_OBJECT_UNLOCK (self);
    }
  }

  gst_kms_sink_show_frame (GST_VIDEO_SINK (self), NULL);
}

static void
gst_kms_sink_video_overlay_init (GstVideoOverlayInterface * iface)
{
  iface->expose = gst_kms_sink_expose;
  iface->set_render_rectangle = gst_kms_sink_set_render_rectangle;
}

static int
kms_open (gchar ** driver)
{
  static const char *drivers[] = { "i915", "radeon", "nouveau", "vmwgfx",
    "exynos", "amdgpu", "imx-drm", "imx-lcdif", "rockchip", "atmel-hlcdc",
    "msm", "xlnx", "vc4", "meson", "stm", "sun4i-drm", "mxsfb-drm", "tegra",
    "tidss", "xilinx_drm",      /* DEPRECATED. Replaced by xlnx */
  };
  int i, fd = -1;

  for (i = 0; i < G_N_ELEMENTS (drivers); i++) {
    fd = drmOpen (drivers[i], NULL);
    if (fd >= 0) {
      if (driver)
        *driver = g_strdup (drivers[i]);
      break;
    }
  }

  return fd;
}

static gint
find_plane_for_crtc (GHashTable * plane_res, drmModeRes * res,
    drmModePlaneRes * pres, int crtc_id)
{
  int i, pipe;

  pipe = -1;
  for (i = 0; i < res->count_crtcs; i++) {
    if (crtc_id == res->crtcs[i]) {
      pipe = i;
      break;
    }
  }

  if (pipe == -1)
    return -1;

  for (i = 0; i < pres->count_planes; i++) {
    GData *plane = NULL;
    guint32 possible_crtcs = 0;

    plane = g_hash_table_lookup (plane_res, GUINT_TO_POINTER (pres->planes[i]));
    possible_crtcs =
        GPOINTER_TO_UINT (g_datalist_get_data (&plane, "possible_crtcs"));

    if (possible_crtcs & (1 << pipe))
      return pres->planes[i];
  }

  return -1;
}

static drmModeCrtc *
find_crtc_for_connector (int fd, drmModeRes * res, drmModeConnector * conn,
    guint * pipe)
{
  int i;
  int crtc_id;
  drmModeEncoder *enc;
  drmModeCrtc *crtc;
  guint32 crtcs_for_connector = 0;

  crtc_id = -1;
  for (i = 0; i < res->count_encoders; i++) {
    enc = drmModeGetEncoder (fd, res->encoders[i]);
    if (enc) {
      if (enc->encoder_id == conn->encoder_id) {
        crtc_id = enc->crtc_id;
        drmModeFreeEncoder (enc);
        break;
      }
      drmModeFreeEncoder (enc);
    }
  }

  /* If no active crtc was found, pick the first possible crtc */
  if (crtc_id == -1) {
    for (i = 0; i < conn->count_encoders; i++) {
      enc = drmModeGetEncoder (fd, conn->encoders[i]);
      crtcs_for_connector |= enc->possible_crtcs;
      drmModeFreeEncoder (enc);
    }

    if (crtcs_for_connector != 0)
      crtc_id = res->crtcs[ffs (crtcs_for_connector) - 1];
  }

  if (crtc_id == -1)
    return NULL;

  for (i = 0; i < res->count_crtcs; i++) {
    crtc = drmModeGetCrtc (fd, res->crtcs[i]);
    if (crtc) {
      if (crtc_id == crtc->crtc_id) {
        if (pipe)
          *pipe = i;
        return crtc;
      }
      drmModeFreeCrtc (crtc);
    }
  }

  return NULL;
}

static gboolean
connector_is_used (int fd, drmModeRes * res, drmModeConnector * conn)
{
  gboolean result;
  drmModeCrtc *crtc;

  result = FALSE;
  crtc = find_crtc_for_connector (fd, res, conn, NULL);
  if (crtc) {
    result = crtc->buffer_id != 0;
    drmModeFreeCrtc (crtc);
  }

  return result;
}

static drmModeConnector *
find_used_connector_by_type (int fd, drmModeRes * res, int type)
{
  int i;
  drmModeConnector *conn;

  conn = NULL;
  for (i = 0; i < res->count_connectors; i++) {
    conn = drmModeGetConnector (fd, res->connectors[i]);
    if (conn) {
      if ((conn->connector_type == type) && connector_is_used (fd, res, conn))
        return conn;
      drmModeFreeConnector (conn);
    }
  }

  return NULL;
}

static drmModeConnector *
find_first_used_connector (int fd, drmModeRes * res)
{
  int i;
  drmModeConnector *conn;

  conn = NULL;
  for (i = 0; i < res->count_connectors; i++) {
    conn = drmModeGetConnector (fd, res->connectors[i]);
    if (conn) {
      if (connector_is_used (fd, res, conn))
        return conn;
      drmModeFreeConnector (conn);
    }
  }

  return NULL;
}

static drmModeConnector *
find_main_monitor (int fd, drmModeRes * res)
{
  /* Find the LVDS and eDP connectors: those are the main screens. */
  static const int priority[] = { DRM_MODE_CONNECTOR_LVDS,
    DRM_MODE_CONNECTOR_eDP
  };
  int i;
  drmModeConnector *conn;

  conn = NULL;
  for (i = 0; !conn && i < G_N_ELEMENTS (priority); i++)
    conn = find_used_connector_by_type (fd, res, priority[i]);

  /* if we didn't find a connector, grab the first one in use */
  if (!conn)
    conn = find_first_used_connector (fd, res);

  /* if no connector is used, grab the first one */
  if (!conn)
    conn = drmModeGetConnector (fd, res->connectors[0]);

  return conn;
}

static void
log_drm_version (GstKMSSink * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  drmVersion *v;

  v = drmGetVersion (self->fd);
  if (v) {
    GST_INFO_OBJECT (self, "DRM v%d.%d.%d [%s — %s — %s]", v->version_major,
        v->version_minor, v->version_patchlevel, GST_STR_NULL (v->name),
        GST_STR_NULL (v->desc), GST_STR_NULL (v->date));
    drmFreeVersion (v);
  } else {
    GST_WARNING_OBJECT (self, "could not get driver information: %s",
        GST_STR_NULL (self->devname));
  }
#endif
  return;
}

static gboolean
get_drm_caps (GstKMSSink * self)
{
  gint ret;
  guint64 has_dumb_buffer;
  guint64 has_prime;
  guint64 has_async_page_flip;

  has_dumb_buffer = 0;
  ret = drmGetCap (self->fd, DRM_CAP_DUMB_BUFFER, &has_dumb_buffer);
  if (ret)
    GST_WARNING_OBJECT (self, "could not get dumb buffer capability");
  if (has_dumb_buffer == 0) {
    GST_ERROR_OBJECT (self, "driver cannot handle dumb buffers");
    return FALSE;
  }

  has_prime = 0;
  ret = drmGetCap (self->fd, DRM_CAP_PRIME, &has_prime);
  if (ret)
    GST_WARNING_OBJECT (self, "could not get prime capability");
  else {
    self->has_prime_import = (gboolean) (has_prime & DRM_PRIME_CAP_IMPORT);
    self->has_prime_export = (gboolean) (has_prime & DRM_PRIME_CAP_EXPORT);
  }

  has_async_page_flip = 0;
  ret = drmGetCap (self->fd, DRM_CAP_ASYNC_PAGE_FLIP, &has_async_page_flip);
  if (ret)
    GST_WARNING_OBJECT (self, "could not get async page flip capability");
  else
    self->has_async_page_flip = (gboolean) has_async_page_flip;

  GST_INFO_OBJECT (self,
      "prime import (%s) / prime export (%s) / legacy async page flip (%s)",
      self->has_prime_import ? "✓" : "✗",
      self->has_prime_export ? "✓" : "✗",
      self->has_async_page_flip ? "✓" : "✗");

  return TRUE;
}

static void
ensure_kms_allocator (GstKMSSink * self)
{
  if (self->allocator)
    return;
  self->allocator = gst_kms_allocator_new (self->fd);
}

static gint
apply_atomic_mode_setting (GstKMSSink * self, drmModeAtomicReqPtr req,
    drmModeModeInfoPtr mode)
{
  guint32 blob_id = 0;
  int err;

  err = drmModeCreatePropertyBlob (self->fd, mode, sizeof (*mode), &blob_id);
  if (err)
    goto create_mode_failed;

  add_connection_property (self, req, "CRTC_ID", self->crtc_id);

  add_crtc_property (self, req, "MODE_ID", blob_id);
  add_crtc_property (self, req, "ACTIVE", 1);

  err =
      drmModeAtomicCommit (self->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
  if (err)
    goto modesetting_failed;

bail:
  drmModeDestroyPropertyBlob (self->fd, blob_id);
  return err;
create_mode_failed:
  {
    GST_ERROR_OBJECT (self, "cannot create mode: %s", g_strerror (errno));
    return FALSE;
  }
modesetting_failed:
  {
    GST_ERROR_OBJECT (self, "Failed to set mode: %s", g_strerror (errno));
    drmModeDestroyPropertyBlob (self->fd, blob_id);
    goto bail;
  }
}

static drmModeModeInfoPtr
find_appropriate_mode (GstKMSSink * self, gint width, gint height)
{
  drmModeConnector *conn = NULL;
  drmModeModeInfo *mode = NULL;
  gint i;

  conn = drmModeGetConnector (self->fd, self->conn_id);
  if (!conn)
    goto connector_failed;

  for (i = 0; i < conn->count_modes; i++) {
    if (conn->modes[i].vdisplay == height && conn->modes[i].hdisplay == width) {
      mode = &conn->modes[i];
      break;
    }
  }
bail:
  if (conn)
    drmModeFreeConnector (conn);

  return mode;
connector_failed:
  {
    GST_ERROR_OBJECT (self, "Could not find a valid monitor connector");
    goto bail;
  }
}

static gboolean
configure_mode_setting (GstKMSSink * self, GstVideoInfo * vinfo)
{
  gboolean ret;
  int err;
  drmModeModeInfo *mode;
  guint32 fb_id;
  GstKMSMemory *kmsmem;

  ret = FALSE;
  mode = NULL;
  kmsmem = NULL;

  if (self->conn_id < 0)
    goto bail;

  GST_INFO_OBJECT (self, "configuring mode setting");

  ensure_kms_allocator (self);
  kmsmem = (GstKMSMemory *) gst_kms_allocator_bo_alloc (self->allocator, vinfo);
  if (!kmsmem)
    goto bo_failed;
  fb_id = kmsmem->fb_id;

  mode = find_appropriate_mode (self, GST_VIDEO_INFO_WIDTH (vinfo),
      GST_VIDEO_INFO_HEIGHT (vinfo));
  if (!mode)
    goto mode_failed;

  if (self->has_atomic) {
    drmModeAtomicReqPtr req = NULL;

    req = drmModeAtomicAlloc ();
    if (!req) {
      GST_ERROR_OBJECT (self, "can't allocator atomic request");
      goto bail;
    }

    add_plane_property (self, req, self->plane_id, "FB_ID", fb_id);
    add_plane_property (self, req, self->plane_id, "CRTC_ID", self->crtc_id);
    add_plane_property (self, req, self->plane_id, "SRC_X", 0);
    add_plane_property (self, req, self->plane_id, "SRC_Y", 0);
    add_plane_property (self, req, self->plane_id, "SRC_W",
        GST_VIDEO_INFO_WIDTH (vinfo) << 16);
    add_plane_property (self, req, self->plane_id, "SRC_H",
        GST_VIDEO_INFO_HEIGHT (vinfo) << 16);
    add_plane_property (self, req, self->plane_id, "CRTC_X", 0);
    add_plane_property (self, req, self->plane_id, "CRTC_Y", 0);
    add_plane_property (self, req, self->plane_id, "CRTC_W",
        GST_VIDEO_INFO_WIDTH (vinfo));
    add_plane_property (self, req, self->plane_id, "CRTC_H",
        GST_VIDEO_INFO_HEIGHT (vinfo));
    err = apply_atomic_mode_setting (self, req, mode);
  } else {
    err = drmModeSetCrtc (self->fd, self->crtc_id, fb_id, 0, 0,
        (uint32_t *) & self->conn_id, 1, mode);
  }
  if (err)
    goto modesetting_failed;

  g_clear_pointer (&self->tmp_kmsmem, gst_memory_unref);
  self->tmp_kmsmem = (GstMemory *) kmsmem;

  ret = TRUE;

bail:
  return ret;

  /* ERRORS */
bo_failed:
  {
    GST_ERROR_OBJECT (self,
        "failed to allocate buffer object for mode setting");
    goto bail;
  }
mode_failed:
  {
    GST_ERROR_OBJECT (self, "cannot find appropriate mode");
    goto bail;
  }
modesetting_failed:
  {
    GST_ERROR_OBJECT (self, "Failed to set mode: %s", g_strerror (errno));
    goto bail;
  }
}

static gboolean
ensure_allowed_caps (GstKMSSink * self, drmModeConnector * conn,
    gint plane_id, drmModeRes * res)
{
  GData *props = NULL;
  GSList *formats = NULL;

  GstCaps *out_caps, *tmp_caps, *caps;
  int i;
  GstVideoFormat fmt;
  gint count_modes;

  if (self->allowed_caps)
    return TRUE;

  out_caps = gst_caps_new_empty ();
  if (!out_caps)
    return FALSE;

  if (conn && self->modesetting_enabled)
    count_modes = conn->count_modes;
  else
    count_modes = 1;

  props = g_hash_table_lookup (self->plane_res, GUINT_TO_POINTER (plane_id));
  formats = g_datalist_id_get_data (&props, g_quark_from_static_string ("fmt"));

  for (i = 0; i < count_modes; i++) {
    const gchar *format = NULL;
    drmModeModeInfo *mode = NULL;
    GSList *iter = NULL;
    tmp_caps = gst_caps_new_empty ();
    if (!tmp_caps)
      return FALSE;

    mode = NULL;
    if (conn && self->modesetting_enabled)
      mode = &conn->modes[i];

    for (iter = formats; iter; iter = g_slist_next (iter)) {
      /* TODO: support drm modifier in the future */
      fmt = gst_video_format_from_drm (GPOINTER_TO_UINT (iter->data));
      if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_INFO_OBJECT (self, "ignoring format %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (GPOINTER_TO_UINT (iter->data)));
        continue;
      }

      format = gst_video_format_to_string (fmt);

      if (mode) {
        caps = gst_caps_new_simple ("video/x-raw",
            "format", G_TYPE_STRING, format,
            "width", G_TYPE_INT, mode->hdisplay,
            "height", G_TYPE_INT, mode->vdisplay,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
      } else {
        caps = gst_caps_new_simple ("video/x-raw",
            "format", G_TYPE_STRING, format,
            "width", GST_TYPE_INT_RANGE, res->min_width, res->max_width,
            "height", GST_TYPE_INT_RANGE, res->min_height, res->max_height,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
      }
      if (!caps)
        continue;

      tmp_caps = gst_caps_merge (tmp_caps, caps);
    }

    out_caps = gst_caps_merge (out_caps, gst_caps_simplify (tmp_caps));
  }

  if (gst_caps_is_empty (out_caps)) {
    GST_DEBUG_OBJECT (self, "allowed caps is empty");
    gst_caps_unref (out_caps);
    return FALSE;
  }

  self->allowed_caps = gst_caps_simplify (out_caps);

  GST_DEBUG_OBJECT (self, "allowed caps = %" GST_PTR_FORMAT,
      self->allowed_caps);

  return TRUE;
}

static gboolean
set_drm_property (GstKMSSink * self, guint32 object, guint32 object_type,
    const gchar * prop_name, guint64 value)
{
  gboolean ret = FALSE;
  GData *conn_props = self->conn_props;
  GData *plane_props = g_hash_table_lookup (self->plane_res,
      GUINT_TO_POINTER (self->plane_id));
  drmModePropertyPtr property = NULL;

  switch (object_type) {
    case DRM_MODE_OBJECT_CONNECTOR:
      get_drm_property (conn, prop_name, property);
      break;
    case DRM_MODE_OBJECT_PLANE:
      get_drm_property (plane, prop_name, property);
      break;
    default:
      return FALSE;
  }

  drmModeObjectSetProperty (self->fd, object, object_type,
      property->prop_id, value);
  ret = TRUE;

  return ret;
}

typedef struct
{
  GstKMSSink *self;
  guint obj_id;
  guint obj_type;
  const gchar *obj_type_str;
  drmModeAtomicReqPtr req;
} SetPropsIter;

static gboolean
set_obj_prop (GQuark field_id, const GValue * value, gpointer user_data)
{
  SetPropsIter *iter = user_data;
  GstKMSSink *self = iter->self;
  const gchar *name;
  guint64 v;
  /* FIXME: there is no way to check whether drm property is right in atomic */
  gint ret = -1;

  name = g_quark_to_string (field_id);

  if (G_VALUE_HOLDS (value, G_TYPE_INT))
    v = g_value_get_int (value);
  else if (G_VALUE_HOLDS (value, G_TYPE_UINT))
    v = g_value_get_uint (value);
  else if (G_VALUE_HOLDS (value, G_TYPE_INT64))
    v = g_value_get_int64 (value);
  else if (G_VALUE_HOLDS (value, G_TYPE_UINT64))
    v = g_value_get_uint64 (value);
  else {
    GST_WARNING_OBJECT (self,
        "'uint64' value expected for control '%s'.", name);
    return TRUE;
  }

  if (self->has_atomic) {
    switch (iter->obj_type) {
      case DRM_MODE_OBJECT_CONNECTOR:
        ret = add_connection_property (self, iter->req, name, v);
        break;
      case DRM_MODE_OBJECT_PLANE:
        ret = add_plane_property (self, iter->req, self->plane_id, name, v);
        break;
    }
  } else {
    ret = set_drm_property (self, iter->obj_id, iter->obj_type, name, v);
  }

  if (!ret) {
    GST_DEBUG_OBJECT (self,
        "Set %s property '%s' to %" G_GUINT64_FORMAT,
        iter->obj_type_str, name, v);
  } else {
    GST_WARNING_OBJECT (self,
        "Failed to set %s property '%s' to %" G_GUINT64_FORMAT,
        iter->obj_type_str, name, v);
  }

  return TRUE;
}

static void
gst_kms_sink_update_properties (SetPropsIter * iter, GstStructure * props)
{
  gst_structure_foreach (props, set_obj_prop, iter);
}

static void
gst_kms_sink_update_connector_properties (GstKMSSink * self,
    drmModeAtomicReq * req)
{
  SetPropsIter iter;

  if (!self->connector_props)
    return;

  iter.self = self;
  iter.req = req;
  iter.obj_id = self->conn_id;
  iter.obj_type = DRM_MODE_OBJECT_CONNECTOR;
  iter.obj_type_str = "connector";

  gst_kms_sink_update_properties (&iter, self->connector_props);
}

static void
gst_kms_sink_update_plane_properties (GstKMSSink * self, drmModeAtomicReq * req)
{
  SetPropsIter iter;

  if (!self->plane_props)
    return;

  iter.self = self;
  iter.req = req;
  iter.obj_id = self->plane_id;
  iter.obj_type = DRM_MODE_OBJECT_PLANE;
  iter.obj_type_str = "plane";

  gst_kms_sink_update_properties (&iter, self->plane_props);
}

static GstStateChangeReturn
gst_kms_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstKMSSink *self = GST_KMS_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_kms_sink_drain (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_kms_sink_start (GstBaseSink * bsink)
{
  GstKMSSink *self;
  drmModeRes *res;
  drmModeConnector *conn;
  drmModeCrtc *crtc;
  drmModePlaneRes *pres;
  gboolean universal_planes;
  guint32 plane_id;
  gboolean ret;

  self = GST_KMS_SINK (bsink);
  universal_planes = FALSE;
  ret = FALSE;
  res = NULL;
  conn = NULL;
  crtc = NULL;
  pres = NULL;

  /* open our own internal device fd if application did not supply its own */
  if (self->is_internal_fd) {
    if (self->devname || self->bus_id)
      self->fd = drmOpen (self->devname, self->bus_id);
    else
      self->fd = kms_open (&self->devname);
  }

  if (self->fd < 0)
    goto open_failed;

  log_drm_version (self);
  if (!get_drm_caps (self))
    goto bail;

  if (!self->force_legacy) {
    if (drmSetClientCap (self->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
      GST_WARNING_OBJECT (self, "could not set atomic capability");
      goto set_cap_failed;
    }
    self->has_atomic = 1;
    /* TODO: support atomic async page flip */
    self->has_async_page_flip = 0;
    /*
     * From kernel code, this would be enabled at the same time, it is not
     * to be enabled later.
     */
    universal_planes = TRUE;
  } else {
    /* unset here to save time on checking flag */
    self->has_atomic = 0;
  }

  res = drmModeGetResources (self->fd);
  if (!res)
    goto resources_failed;

  if (self->conn_id == -1)
    conn = find_main_monitor (self->fd, res);
  else
    conn = drmModeGetConnector (self->fd, self->conn_id);
  if (!conn)
    goto connector_failed;

  crtc = find_crtc_for_connector (self->fd, res, conn, &self->pipe);
  if (!crtc)
    goto crtc_failed;

  if (!crtc->mode_valid || self->modesetting_enabled) {
    GST_DEBUG_OBJECT (self, "enabling modesetting");
    self->modesetting_enabled = TRUE;
    universal_planes = TRUE;
  }

  if (crtc->mode_valid && self->modesetting_enabled && self->restore_crtc) {
    self->saved_crtc = (drmModeCrtc *) crtc;
  }

retry_find_plane:
  if (!self->has_atomic) {
    if (universal_planes &&
        drmSetClientCap (self->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
      goto set_cap_failed;
  }

  pres = drmModeGetPlaneResources (self->fd);
  if (!pres)
    goto plane_resources_failed;

  cache_planes_properties (self, pres);

  if (self->plane_id == -1)
    plane_id = find_plane_for_crtc (self->plane_res, res, pres, crtc->crtc_id);
  else
    plane_id = self->plane_id;

  if (!g_hash_table_lookup (self->plane_res, GUINT_TO_POINTER (plane_id)))
    goto plane_failed;

  if (!ensure_allowed_caps (self, conn, plane_id, res))
    goto allowed_caps_failed;

  self->conn_id = conn->connector_id;
  self->crtc_id = crtc->crtc_id;
  self->plane_id = plane_id;

  GST_INFO_OBJECT (self, "connector id = %d / crtc id = %d / plane id = %d",
      self->conn_id, self->crtc_id, self->plane_id);

  GST_OBJECT_LOCK (self);
  self->hdisplay = crtc->mode.hdisplay;
  self->vdisplay = crtc->mode.vdisplay;

  if (self->render_rect.w == 0 || self->render_rect.h == 0) {
    self->render_rect.x = 0;
    self->render_rect.y = 0;
    self->render_rect.w = self->hdisplay;
    self->render_rect.h = self->vdisplay;
  }

  self->pending_rect = self->render_rect;
  GST_OBJECT_UNLOCK (self);

  self->buffer_id = crtc->buffer_id;

  self->mm_width = conn->mmWidth;
  self->mm_height = conn->mmHeight;

  GST_INFO_OBJECT (self, "display size: pixels = %dx%d / millimeters = %dx%d",
      self->hdisplay, self->vdisplay, self->mm_width, self->mm_height);

  self->pollfd.fd = self->fd;
  gst_poll_add_fd (self->poll, &self->pollfd);
  gst_poll_fd_ctl_read (self->poll, &self->pollfd, TRUE);

  g_object_notify_by_pspec (G_OBJECT (self), g_properties[PROP_DISPLAY_WIDTH]);
  g_object_notify_by_pspec (G_OBJECT (self), g_properties[PROP_DISPLAY_HEIGHT]);

  cache_conn_properties (self);
  cache_crtc_properties (self);

  if (!self->has_atomic) {
    gst_kms_sink_update_connector_properties (self, NULL);
    gst_kms_sink_update_plane_properties (self, NULL);
  }
#ifdef HAVE_DRM_HDR
  if (gst_kms_sink_check_hdr_props (self))
    self->has_hdr_prop = TRUE;
  else
    GST_DEBUG_OBJECT (self, "No HDR support on target display");
#endif

  ret = TRUE;

bail:
  if (pres)
    drmModeFreePlaneResources (pres);
  if (crtc != self->saved_crtc)
    drmModeFreeCrtc (crtc);
  if (conn)
    drmModeFreeConnector (conn);
  if (res)
    drmModeFreeResources (res);

  if (!ret && self->fd >= 0) {
    if (self->is_internal_fd)
      drmClose (self->fd);
    self->fd = -1;
  }

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not open DRM module %s", GST_STR_NULL (self->devname)),
        ("reason: %s (%d)", g_strerror (errno), errno));
    return FALSE;
  }

resources_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("drmModeGetResources failed"),
        ("reason: %s (%d)", g_strerror (errno), errno));
    goto bail;
  }

connector_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("Could not find a valid monitor connector"), (NULL));
    goto bail;
  }

crtc_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("Could not find a crtc for connector"), (NULL));
    goto bail;
  }

set_cap_failed:
  {
    if (self->has_atomic) {
      GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
          ("Could not set atomic capability bit"), (NULL));
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
          ("Could not set universal planes capability bit"), (NULL));
    }
    goto bail;
  }

plane_resources_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("drmModeGetPlaneResources failed"),
        ("reason: %s (%d)", g_strerror (errno), errno));
    goto bail;
  }

plane_failed:
  {
    if (universal_planes) {
      GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
          ("Could not find a plane for crtc"), (NULL));
      goto bail;
    } else {
      universal_planes = TRUE;
      goto retry_find_plane;
    }
  }

allowed_caps_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("Could not get allowed GstCaps of device"),
        ("driver does not provide mode settings configuration"));
    goto bail;
  }
}

static gboolean
gst_kms_sink_stop (GstBaseSink * bsink)
{
  GstKMSSink *self;
  int err;

  self = GST_KMS_SINK (bsink);

  if (self->allocator)
    gst_kms_allocator_clear_cache (self->allocator);

  if (self->has_atomic)
    gst_kms_sink_wait_event (self);

  gst_buffer_replace (&self->last_buffer, NULL);
  gst_caps_replace (&self->allowed_caps, NULL);
  gst_object_replace ((GstObject **) & self->pool, NULL);
  gst_object_replace ((GstObject **) & self->allocator, NULL);

  gst_poll_remove_fd (self->poll, &self->pollfd);
  gst_poll_restart (self->poll);
  gst_poll_fd_init (&self->pollfd);
  g_hash_table_destroy (self->plane_res);

  if (self->saved_crtc) {
    drmModeCrtc *crtc = (drmModeCrtc *) self->saved_crtc;

    if (self->has_atomic) {
      drmModeAtomicReqPtr req = NULL;

      req = drmModeAtomicAlloc ();
      if (!req) {
        GST_ERROR_OBJECT (self, "can't allocator atomic request");
        err = 1;
        goto no_restore_crtc;
      }

      add_plane_property (self, req, self->plane_id, "FB_ID", crtc->buffer_id);
      add_plane_property (self, req, self->plane_id, "CRTC_ID", crtc->crtc_id);
      /* FIXME: there is no way to know these from its orignal buffer  */
      add_plane_property (self, req, self->plane_id, "SRC_X", 0);
      add_plane_property (self, req, self->plane_id, "SRC_Y", 0);
      add_plane_property (self, req, self->plane_id, "SRC_W", crtc->width);
      add_plane_property (self, req, self->plane_id, "SRC_H", crtc->height);

      add_plane_property (self, req, self->plane_id, "CRTC_X", crtc->x);
      add_plane_property (self, req, self->plane_id, "CRTC_Y", crtc->y);
      add_plane_property (self, req, self->plane_id, "CRTC_W", crtc->width);
      add_plane_property (self, req, self->plane_id, "CRTC_H", crtc->height);
      err = apply_atomic_mode_setting (self, req, &crtc->mode);
    } else {
      err = drmModeSetCrtc (self->fd, crtc->crtc_id, crtc->buffer_id, crtc->x,
          crtc->y, (uint32_t *) & self->conn_id, 1, &crtc->mode);
    }
  no_restore_crtc:
    if (err)
      GST_ERROR_OBJECT (self, "Failed to restore previous CRTC mode: %s",
          g_strerror (errno));

    drmModeFreeCrtc (crtc);
    self->saved_crtc = NULL;
  }
#if HAVE_DRM_HDR
  if (self->hdr_blob_id) {
    drmModeDestroyPropertyBlob (self->fd, self->hdr_blob_id);
    self->hdr_blob_id = 0;
  }
#endif

  if (self->fd >= 0) {
    if (self->is_internal_fd)
      drmClose (self->fd);
    self->fd = -1;
  }

  GST_OBJECT_LOCK (bsink);
  self->hdisplay = 0;
  self->vdisplay = 0;
  self->pending_rect.x = 0;
  self->pending_rect.y = 0;
  self->pending_rect.w = 0;
  self->pending_rect.h = 0;
  self->render_rect = self->pending_rect;
  GST_OBJECT_UNLOCK (bsink);

  g_object_notify_by_pspec (G_OBJECT (self), g_properties[PROP_DISPLAY_WIDTH]);
  g_object_notify_by_pspec (G_OBJECT (self), g_properties[PROP_DISPLAY_HEIGHT]);

  return TRUE;
}

static GstCaps *
gst_kms_sink_get_allowed_caps (GstKMSSink * self)
{
  if (!self->allowed_caps)
    return NULL;                /* base class will return the template caps */
  return gst_caps_ref (self->allowed_caps);
}

static GstCaps *
gst_kms_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstKMSSink *self;
  GstCaps *caps, *out_caps;
  GstStructure *s;
  guint dpy_par_n, dpy_par_d;

  self = GST_KMS_SINK (bsink);

  caps = gst_kms_sink_get_allowed_caps (self);
  if (!caps)
    return NULL;

  GST_OBJECT_LOCK (self);

  if (!self->can_scale) {
    out_caps = gst_caps_new_empty ();
    gst_video_calculate_device_ratio (self->hdisplay, self->vdisplay,
        self->mm_width, self->mm_height, &dpy_par_n, &dpy_par_d);

    s = gst_structure_copy (gst_caps_get_structure (caps, 0));
    gst_structure_set (s, "width", G_TYPE_INT, self->pending_rect.w,
        "height", G_TYPE_INT, self->pending_rect.h,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, dpy_par_n, dpy_par_d, NULL);

    gst_caps_append_structure (out_caps, s);

    out_caps = gst_caps_merge (out_caps, caps);
    caps = NULL;

    /* enforce our display aspect ratio */
    gst_caps_set_simple (out_caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        dpy_par_n, dpy_par_d, NULL);
  } else {
    out_caps = gst_caps_make_writable (caps);
    caps = NULL;
  }

  GST_OBJECT_UNLOCK (self);

  if (self->has_prime_import) {
    caps = gst_caps_copy (out_caps);
    gst_caps_set_features (caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DMABUF, NULL));
    out_caps = gst_caps_merge (out_caps, caps);
    caps = NULL;
  }

  GST_DEBUG_OBJECT (self, "Proposing caps %" GST_PTR_FORMAT, out_caps);

  if (filter) {
    caps = out_caps;
    out_caps = gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
  }

  return out_caps;
}

static GstBufferPool *
gst_kms_sink_create_pool (GstKMSSink * self, GstCaps * caps, gsize size,
    gint min)
{
  GstBufferPool *pool;
  GstStructure *config;

  pool = gst_kms_buffer_pool_new ();
  if (!pool)
    goto pool_failed;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  ensure_kms_allocator (self);
  gst_buffer_pool_config_set_allocator (config, self->allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config))
    goto config_failed;

  return pool;

  /* ERRORS */
pool_failed:
  {
    GST_ERROR_OBJECT (self, "failed to create buffer pool");
    return NULL;
  }
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return NULL;
  }
}

static gboolean
gst_kms_sink_calculate_display_ratio (GstKMSSink * self, GstVideoInfo * vinfo,
    gint * scaled_width, gint * scaled_height)
{
  guint dar_n, dar_d;
  guint video_width, video_height;
  guint video_par_n, video_par_d;
  guint dpy_par_n, dpy_par_d;

  video_width = GST_VIDEO_INFO_WIDTH (vinfo);
  video_height = GST_VIDEO_INFO_HEIGHT (vinfo);
  video_par_n = GST_VIDEO_INFO_PAR_N (vinfo);
  video_par_d = GST_VIDEO_INFO_PAR_D (vinfo);

  if (self->can_scale) {
    gst_video_calculate_device_ratio (self->hdisplay, self->vdisplay,
        self->mm_width, self->mm_height, &dpy_par_n, &dpy_par_d);
  } else {
    *scaled_width = video_width;
    *scaled_height = video_height;
    goto out;
  }

  if (!gst_video_calculate_display_ratio (&dar_n, &dar_d, video_width,
          video_height, video_par_n, video_par_d, dpy_par_n, dpy_par_d))
    return FALSE;

  GST_DEBUG_OBJECT (self, "video calculated display ratio: %d/%d", dar_n,
      dar_d);

  /* now find a width x height that respects this display ratio.
   * prefer those that have one of w/h the same as the incoming video
   * using wd / hd = dar_n / dar_d */

  /* start with same height, because of interlaced video */
  /* check hd / dar_d is an integer scale factor, and scale wd with the PAR */
  if (video_height % dar_d == 0) {
    GST_DEBUG_OBJECT (self, "keeping video height");
    *scaled_width = (guint)
        gst_util_uint64_scale_int (video_height, dar_n, dar_d);
    *scaled_height = video_height;
  } else if (video_width % dar_n == 0) {
    GST_DEBUG_OBJECT (self, "keeping video width");
    *scaled_width = video_width;
    *scaled_height = (guint)
        gst_util_uint64_scale_int (video_width, dar_d, dar_n);
  } else {
    GST_DEBUG_OBJECT (self, "approximating while keeping video height");
    *scaled_width = (guint)
        gst_util_uint64_scale_int (video_height, dar_n, dar_d);
    *scaled_height = video_height;
  }

out:
  GST_DEBUG_OBJECT (self, "scaling to %dx%d", *scaled_width, *scaled_height);

  return TRUE;
}

static gboolean
gst_kms_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstKMSSink *self;
  GstVideoInfo vinfo;

  self = GST_KMS_SINK (bsink);

  if (!gst_video_info_from_caps (&vinfo, caps))
    goto invalid_format;
  self->vinfo = vinfo;

  if (!gst_kms_sink_calculate_display_ratio (self, &vinfo,
          &GST_VIDEO_SINK_WIDTH (self), &GST_VIDEO_SINK_HEIGHT (self)))
    goto no_disp_ratio;

  if (GST_VIDEO_SINK_WIDTH (self) <= 0 || GST_VIDEO_SINK_HEIGHT (self) <= 0)
    goto invalid_size;

#ifdef HAVE_DRM_HDR
  gst_kms_sink_set_hdr10_caps (self, caps);
#endif

  /* discard dumb buffer pool */
  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    gst_object_unref (self->pool);
    self->pool = NULL;
  }

  if (self->modesetting_enabled && !configure_mode_setting (self, &vinfo))
    goto modesetting_failed;

  GST_OBJECT_LOCK (self);
  if (self->reconfigure) {
    self->reconfigure = FALSE;
    self->render_rect = self->pending_rect;
  }
  GST_OBJECT_UNLOCK (self);

  GST_DEBUG_OBJECT (self, "negotiated caps = %" GST_PTR_FORMAT, caps);

  return TRUE;

  /* ERRORS */
invalid_format:
  {
    GST_ERROR_OBJECT (self, "caps invalid");
    return FALSE;
  }

invalid_size:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("Invalid image size."));
    return FALSE;
  }

no_disp_ratio:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }

modesetting_failed:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("failed to configure video mode"));
    return FALSE;
  }

}

static gboolean
gst_kms_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstKMSSink *self;
  GstCaps *caps;
  gboolean need_pool;
  GstVideoInfo vinfo;
  GstBufferPool *pool;
  gsize size;

  self = GST_KMS_SINK (bsink);

  GST_DEBUG_OBJECT (self, "propose allocation");

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps)
    goto no_caps;
  if (!gst_video_info_from_caps (&vinfo, caps))
    goto invalid_caps;

  size = GST_VIDEO_INFO_SIZE (&vinfo);

  pool = NULL;
  if (need_pool) {
    pool = gst_kms_sink_create_pool (self, caps, size, 0);
    if (!pool)
      goto no_pool;

    /* Only export for pool used upstream */
    if (self->has_prime_export) {
      GstStructure *config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_KMS_PRIME_EXPORT);
      gst_buffer_pool_set_config (pool, config);
    }
  }

  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
no_pool:
  {
    /* Already warned in create_pool */
    return FALSE;
  }
}

static void
sync_handler (gint fd, guint frame, guint sec, guint usec, gpointer data)
{
  gboolean *waiting;

  waiting = data;
  *waiting = FALSE;
}

static gboolean
gst_kms_sink_sync (GstKMSSink * self)
{
  gint ret;
  gboolean waiting;
  drmEventContext evctxt = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler = sync_handler,
    .vblank_handler = sync_handler,
  };
  drmVBlank vbl = {
    .request = {
          .type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT,
          .sequence = 1,
          .signal = (gulong) & waiting,
        },
  };

  if (self->pipe == 1)
    vbl.request.type |= DRM_VBLANK_SECONDARY;
  else if (self->pipe > 1)
    vbl.request.type |= self->pipe << DRM_VBLANK_HIGH_CRTC_SHIFT;

  waiting = TRUE;
  if (!self->has_async_page_flip && !self->modesetting_enabled) {
    ret = drmWaitVBlank (self->fd, &vbl);
    if (ret)
      goto vblank_failed;
  } else {
    ret = drmModePageFlip (self->fd, self->crtc_id, self->buffer_id,
        DRM_MODE_PAGE_FLIP_EVENT, &waiting);
    if (ret)
      goto pageflip_failed;
  }

  while (waiting) {
    do {
      ret = gst_poll_wait (self->poll, 3 * GST_SECOND);
    } while (ret == -1 && (errno == EAGAIN || errno == EINTR));

    ret = drmHandleEvent (self->fd, &evctxt);
    if (ret)
      goto event_failed;
  }

  return TRUE;

  /* ERRORS */
vblank_failed:
  {
    GST_WARNING_OBJECT (self, "drmWaitVBlank failed: %s (%d)",
        g_strerror (errno), errno);
    return FALSE;
  }
pageflip_failed:
  {
    GST_WARNING_OBJECT (self, "drmModePageFlip failed: %s (%d)",
        g_strerror (errno), errno);
    return FALSE;
  }
event_failed:
  {
    GST_ERROR_OBJECT (self, "drmHandleEvent failed: %s (%d)",
        g_strerror (errno), errno);
    return FALSE;
  }
}

static void
atomic_flip_handler (gint fd, guint frame, guint sec, guint usec, guint crtc_id,
    gpointer data)
{
  GstKMSSink *self = GST_KMS_SINK (data);
  gst_buffer_replace (&self->last_buffer, NULL);
}

static gboolean
gst_kms_sink_wait_event (GstKMSSink * self)
{
  gint ret;
  drmEventContext evctxt = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler2 = atomic_flip_handler,
  };

  if (!self->has_async_page_flip)
    return TRUE;
  if (!self->last_buffer)
    return TRUE;

again:
  ret = gst_poll_wait (self->poll, 3 * GST_SECOND);
  if (G_UNLIKELY (ret < 0)) {
    switch (errno) {
      case EBUSY:
        return FALSE;
      case EAGAIN:
      case EINTR:
        goto again;
      case ENXIO:
        GST_WARNING_OBJECT (self, "This drm device doesn't support poll"
            "It doesn't make sense, disable async_page_flip");
        self->has_async_page_flip = FALSE;
      default:
        goto poll_failed;
    }
  }

  ret = drmHandleEvent (self->fd, &evctxt);
  if (ret)
    goto event_failed;

  return TRUE;;

  /* ERRORS */
poll_failed:
  {
    GST_ERROR_OBJECT (self, "poll failed: %s (%d)", strerror (-ret), ret);
    return FALSE;
  }
event_failed:
  {
    GST_ERROR_OBJECT (self, "drmHandleEvent failed: %s (%d)", strerror (-ret),
        ret);
    return FALSE;
  }
}

static gboolean
gst_kms_sink_import_dmabuf (GstKMSSink * self, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  gint prime_fds[GST_VIDEO_MAX_PLANES] = { 0, };
  GstVideoMeta *meta;
  guint i, n_mem, n_planes;
  GstKMSMemory *kmsmem;
  guint mems_idx[GST_VIDEO_MAX_PLANES];
  gsize mems_skip[GST_VIDEO_MAX_PLANES];
  GstMemory *mems[GST_VIDEO_MAX_PLANES];

  if (!self->has_prime_import)
    return FALSE;

  /* This will eliminate most non-dmabuf out there */
  if (!gst_is_dmabuf_memory (gst_buffer_peek_memory (inbuf, 0)))
    return FALSE;

  n_planes = GST_VIDEO_INFO_N_PLANES (&self->vinfo);
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
    GST_VIDEO_INFO_WIDTH (&self->vinfo) = meta->width;
    GST_VIDEO_INFO_HEIGHT (&self->vinfo) = meta->height;

    for (i = 0; i < meta->n_planes; i++) {
      GST_VIDEO_INFO_PLANE_OFFSET (&self->vinfo, i) = meta->offset[i];
      GST_VIDEO_INFO_PLANE_STRIDE (&self->vinfo, i) = meta->stride[i];
    }
  }

  /* Find and validate all memories */
  for (i = 0; i < n_planes; i++) {
    guint length;

    if (!gst_buffer_find_memory (inbuf,
            GST_VIDEO_INFO_PLANE_OFFSET (&self->vinfo, i), 1,
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

  ensure_kms_allocator (self);

  kmsmem = (GstKMSMemory *) gst_kms_allocator_get_cached (mems[0]);
  if (kmsmem) {
    GST_LOG_OBJECT (self, "found KMS mem %p in DMABuf mem %p with fb id = %d",
        kmsmem, mems[0], kmsmem->fb_id);
    goto wrap_mem;
  }

  for (i = 0; i < n_planes; i++)
    prime_fds[i] = gst_dmabuf_memory_get_fd (mems[i]);

  GST_LOG_OBJECT (self, "found these prime ids: %d, %d, %d, %d", prime_fds[0],
      prime_fds[1], prime_fds[2], prime_fds[3]);

  kmsmem = gst_kms_allocator_dmabuf_import (self->allocator,
      prime_fds, n_planes, mems_skip, &self->vinfo);
  if (!kmsmem)
    return FALSE;

  GST_LOG_OBJECT (self, "setting KMS mem %p to DMABuf mem %p with fb id = %d",
      kmsmem, mems[0], kmsmem->fb_id);
  gst_kms_allocator_cache (self->allocator, mems[0], GST_MEMORY_CAST (kmsmem));

wrap_mem:
  *outbuf = gst_buffer_new ();
  if (!*outbuf)
    return FALSE;
  gst_buffer_append_memory (*outbuf, gst_memory_ref (GST_MEMORY_CAST (kmsmem)));
  gst_buffer_add_parent_buffer_meta (*outbuf, inbuf);

  return TRUE;
}

static gboolean
ensure_internal_pool (GstKMSSink * self, GstVideoInfo * in_vinfo,
    GstBuffer * inbuf)
{
  GstBufferPool *pool;
  GstVideoInfo vinfo = *in_vinfo;
  GstVideoMeta *vmeta;
  GstCaps *caps;

  if (self->pool)
    return TRUE;

  /* When cropping, the caps matches the cropped rectangle width/height, but
   * we can retrieve the padded width/height from the VideoMeta (which is kept
   * intact when adding crop meta */
  if ((vmeta = gst_buffer_get_video_meta (inbuf))) {
    vinfo.width = vmeta->width;
    vinfo.height = vmeta->height;
  }

  caps = gst_video_info_to_caps (&vinfo);
  pool = gst_kms_sink_create_pool (self, caps, gst_buffer_get_size (inbuf), 2);
  gst_caps_unref (caps);

  if (!pool)
    return FALSE;

  if (!gst_buffer_pool_set_active (pool, TRUE))
    goto activate_pool_failed;

  self->pool = pool;
  return TRUE;

activate_pool_failed:
  {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate buffer pool"),
        ("failed to activate buffer pool"));
    gst_object_unref (pool);
    return FALSE;
  }

}

static GstBuffer *
gst_kms_sink_copy_to_dumb_buffer (GstKMSSink * self, GstVideoInfo * vinfo,
    GstBuffer * inbuf)
{
  GstFlowReturn ret;
  GstVideoFrame inframe, outframe;
  gboolean success;
  GstBuffer *buf = NULL;

  if (!ensure_internal_pool (self, vinfo, inbuf))
    goto bail;

  ret = gst_buffer_pool_acquire_buffer (self->pool, &buf, NULL);
  if (ret != GST_FLOW_OK)
    goto create_buffer_failed;

  if (!gst_video_frame_map (&inframe, vinfo, inbuf, GST_MAP_READ))
    goto error_map_src_buffer;

  if (!gst_video_frame_map (&outframe, vinfo, buf, GST_MAP_WRITE))
    goto error_map_dst_buffer;

  success = gst_video_frame_copy (&outframe, &inframe);
  gst_video_frame_unmap (&outframe);
  gst_video_frame_unmap (&inframe);
  if (!success)
    goto error_copy_buffer;

  return buf;

bail:
  {
    if (buf)
      gst_buffer_unref (buf);
    return NULL;
  }

  /* ERRORS */
create_buffer_failed:
  {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("allocation failed"),
        ("failed to create buffer"));
    return NULL;
  }
error_copy_buffer:
  {
    GST_WARNING_OBJECT (self, "failed to upload buffer");
    goto bail;
  }
error_map_dst_buffer:
  {
    gst_video_frame_unmap (&inframe);
    /* fall-through */
  }
error_map_src_buffer:
  {
    GST_WARNING_OBJECT (self, "failed to map buffer");
    goto bail;
  }
}

static GstBuffer *
gst_kms_sink_get_input_buffer (GstKMSSink * self, GstBuffer * inbuf)
{
  GstMemory *mem;
  GstBuffer *buf = NULL;

  mem = gst_buffer_peek_memory (inbuf, 0);
  if (!mem)
    return NULL;

  if (gst_is_kms_memory (mem))
    return gst_buffer_ref (inbuf);

  if (gst_kms_sink_import_dmabuf (self, inbuf, &buf))
    goto done;

  GST_CAT_INFO_OBJECT (CAT_PERFORMANCE, self, "frame copy");
  buf = gst_kms_sink_copy_to_dumb_buffer (self, &self->vinfo, inbuf);

done:
  /* Copy all the non-memory related metas, this way CropMeta will be
   * available upon GstVideoOverlay::expose calls. */
  if (buf)
    gst_buffer_copy_into (buf, inbuf, GST_BUFFER_COPY_METADATA, 0, -1);

  return buf;
}

static GstFlowReturn
gst_kms_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  gint ret;
  GstBuffer *buffer = NULL;
  guint32 fb_id;
  GstKMSSink *self;
  GstVideoInfo *vinfo;
  GstVideoCropMeta *crop;
  GstVideoRectangle src = { 0, };
  gint video_width, video_height;
  GstVideoRectangle dst = { 0, };
  GstVideoRectangle result;
  GstFlowReturn res;

  self = GST_KMS_SINK (vsink);

  res = GST_FLOW_ERROR;

  if (buf) {
    buffer = gst_kms_sink_get_input_buffer (self, buf);
    vinfo = &self->vinfo;
    video_width = src.w = GST_VIDEO_SINK_WIDTH (self);
    video_height = src.h = GST_VIDEO_SINK_HEIGHT (self);
  } else if (self->last_buffer) {
    buffer = gst_buffer_ref (self->last_buffer);
    vinfo = &self->last_vinfo;
    video_width = src.w = self->last_width;
    video_height = src.h = self->last_height;
  }

  /* Make sure buf is not used accidentally */
  buf = NULL;

  if (!buffer)
    return GST_FLOW_ERROR;
  fb_id = gst_kms_memory_get_fb_id (gst_buffer_peek_memory (buffer, 0));
  if (fb_id == 0)
    goto buffer_invalid;

  GST_TRACE_OBJECT (self, "displaying fb %d", fb_id);

  GST_OBJECT_LOCK (self);
  if ((!self->has_atomic) && self->modesetting_enabled) {
    self->buffer_id = fb_id;
    goto sync_frame;
  }

  if ((crop = gst_buffer_get_video_crop_meta (buffer))) {
    GstVideoInfo cropped_vinfo = *vinfo;

    cropped_vinfo.width = crop->width;
    cropped_vinfo.height = crop->height;

    if (!gst_kms_sink_calculate_display_ratio (self, &cropped_vinfo, &src.w,
            &src.h))
      goto no_disp_ratio;

    src.x = crop->x;
    src.y = crop->y;
  }

  dst.w = self->render_rect.w;
  dst.h = self->render_rect.h;

retry_set_plane:
  gst_video_sink_center_rect (src, dst, &result, self->can_scale);

  result.x += self->render_rect.x;
  result.y += self->render_rect.y;

  if (crop) {
    src.w = crop->width;
    src.h = crop->height;
  } else {
    src.w = video_width;
    src.h = video_height;
  }

  /* handle out of screen case */
  if ((result.x + result.w) > self->hdisplay)
    result.w = self->hdisplay - result.x;

  if ((result.y + result.h) > self->vdisplay)
    result.h = self->vdisplay - result.y;

  if (result.w <= 0 || result.h <= 0) {
    GST_WARNING_OBJECT (self, "video is out of display range");
    /* FIXME: I think it should throw an expection here */
    if (!self->has_atomic)
      goto sync_frame;
  }

  /* to make sure it can be show when driver don't support scale */
  if (!self->can_scale) {
    src.w = result.w;
    src.h = result.h;
  }

  if (self->has_atomic) {
    drmModeAtomicReqPtr req = NULL;
    guint32 flags = 0;

    /* Wait for the previous frame to complete redraw */
    if (!gst_kms_sink_wait_event (self))
      goto bail;

    req = drmModeAtomicAlloc ();
    if (!req) {
      GST_ERROR_OBJECT (self, "can't allocator atomic request");
      goto bail;
    }

    GST_TRACE_OBJECT (self,
        "drmModeAtomicCommit at (%i,%i) %ix%i sourcing at (%i,%i) %ix%i",
        result.x, result.y, result.w, result.h, src.x, src.y, src.w, src.h);

    add_plane_property (self, req, self->plane_id, "FB_ID", fb_id);
    add_plane_property (self, req, self->plane_id, "CRTC_ID", self->crtc_id);
    /* source/cropping coordinates are given in Q16 */
    add_plane_property (self, req, self->plane_id, "SRC_X", src.x << 16);
    add_plane_property (self, req, self->plane_id, "SRC_Y", src.y << 16);
    add_plane_property (self, req, self->plane_id, "SRC_W", src.w << 16);
    add_plane_property (self, req, self->plane_id, "SRC_H", src.h << 16);
    add_plane_property (self, req, self->plane_id, "CRTC_X", result.x);
    add_plane_property (self, req, self->plane_id, "CRTC_Y", result.y);
    add_plane_property (self, req, self->plane_id, "CRTC_W", result.w);
    add_plane_property (self, req, self->plane_id, "CRTC_H", result.h);
    /* TODO: add support for drm_color_encoding and drm_color_range */
#ifdef HAVE_DRM_HDR
    gst_kms_push_hdr_infoframe (self, req);
#endif
    gst_kms_sink_update_connector_properties (self, req);
    gst_kms_sink_update_plane_properties (self, req);

    /* assume all the drivers support page flip */
    flags |= DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    ret = drmModeAtomicCommit (self->fd, req, flags, (gpointer) self);
    if (ret)
      goto commit_plane_failed;
  } else {
#ifdef HAVE_DRM_HDR
    /* Send the HDR infoframes if appropriate */
    gst_kms_push_hdr_infoframe_legacy (self, FALSE);
#endif

    GST_TRACE_OBJECT (self,
        "drmModeSetPlane at (%i,%i) %ix%i sourcing at (%i,%i) %ix%i",
        result.x, result.y, result.w, result.h, src.x, src.y, src.w, src.h);

    ret = drmModeSetPlane (self->fd, self->plane_id, self->crtc_id, fb_id, 0,
        result.x, result.y, result.w, result.h,
        /* source/cropping coordinates are given in Q16 */
        src.x << 16, src.y << 16, src.w << 16, src.h << 16);
    if (ret) {
      if (self->can_scale) {
        self->can_scale = FALSE;
        goto retry_set_plane;
      }
      goto set_plane_failed;
    }

  sync_frame:
    /* Wait for the previous frame to complete redraw */
    if (!self->skip_vsync && !gst_kms_sink_sync (self)) {
      GST_OBJECT_UNLOCK (self);
      goto bail;
    }
  }

  /* Save the rendered buffer and its metadata in case a redraw is needed */
  if (buffer != self->last_buffer) {
    gst_buffer_replace (&self->last_buffer, buffer);
    self->last_width = GST_VIDEO_SINK_WIDTH (self);
    self->last_height = GST_VIDEO_SINK_HEIGHT (self);
    self->last_vinfo = self->vinfo;
  }
  g_clear_pointer (&self->tmp_kmsmem, gst_memory_unref);

  GST_OBJECT_UNLOCK (self);
  res = GST_FLOW_OK;

bail:
  gst_buffer_unref (buffer);
  return res;

  /* ERRORS */
buffer_invalid:
  {
    GST_ERROR_OBJECT (self, "invalid buffer: it doesn't have a fb id");
    goto bail;
  }
commit_plane_failed:
  {
    GST_OBJECT_UNLOCK (self);
    GST_DEBUG_OBJECT (self, "result = { %d, %d, %d, %d} / "
        "src = { %d, %d, %d %d } / dst = { %d, %d, %d %d }", result.x, result.y,
        result.w, result.h, src.x, src.y, src.w, src.h, dst.x, dst.y, dst.w,
        dst.h);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (NULL), ("drmModeAtomicCommit failed: %s (%d)", g_strerror (errno),
            errno));
    goto bail;
  }
set_plane_failed:
  {
    GST_OBJECT_UNLOCK (self);
    GST_DEBUG_OBJECT (self, "result = { %d, %d, %d, %d} / "
        "src = { %d, %d, %d %d } / dst = { %d, %d, %d %d }", result.x, result.y,
        result.w, result.h, src.x, src.y, src.w, src.h, dst.x, dst.y, dst.w,
        dst.h);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (NULL), ("drmModeSetPlane failed: %s (%d)", g_strerror (errno), errno));
    goto bail;
  }
no_disp_ratio:
  {
    GST_OBJECT_UNLOCK (self);
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    goto bail;
  }
}

static void
gst_kms_sink_drain (GstKMSSink * self)
{
  GstParentBufferMeta *parent_meta;

  if (self->has_atomic) {
    drmModeAtomicReqPtr req = NULL;

    req = drmModeAtomicAlloc ();

    /* commit an empty request to disable this plane */
    add_plane_property (self, req, self->plane_id, "FB_ID", 0);
    add_plane_property (self, req, self->plane_id, "CRTC_ID", 0);

    /* Do I need to check it here ? It should work for just close a plane */
    drmModeAtomicCommit (self->fd, req, 0, (gpointer) self);
    /*
     * We can drop the buffer safely here, the previous buffer should
     * have been scanout after the return of wait_event()
     */
    if (!gst_kms_sink_wait_event (self))
      GST_WARNING_OBJECT (self, "something went wrong with the driver");
    return;
  }

  if (!self->last_buffer)
    return;

  /* We only need to return the last_buffer if it depends on upstream buffer.
   * In this case, the last_buffer will have a GstParentBufferMeta set. */
  parent_meta = gst_buffer_get_parent_buffer_meta (self->last_buffer);
  if (parent_meta) {
    GstBuffer *dumb_buf, *last_buf;

    /* If this was imported from our dumb buffer pool we can safely skip the
     * drain */
    if (parent_meta->buffer->pool &&
        GST_IS_KMS_BUFFER_POOL (parent_meta->buffer->pool))
      return;

    GST_DEBUG_OBJECT (self, "draining");

    dumb_buf = gst_kms_sink_copy_to_dumb_buffer (self, &self->last_vinfo,
        parent_meta->buffer);
    last_buf = self->last_buffer;
    self->last_buffer = dumb_buf;

    gst_kms_allocator_clear_cache (self->allocator);
    gst_kms_sink_show_frame (GST_VIDEO_SINK (self), NULL);
    gst_buffer_unref (last_buf);
  }
}

static gboolean
gst_kms_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstKMSSink *self = GST_KMS_SINK (bsink);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    case GST_QUERY_DRAIN:
    {
      gst_kms_sink_drain (self);
      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
}

static void
_validate_and_set_external_fd (GstKMSSink * self, gint fd)
{
  if (self->devname) {
    GST_WARNING_OBJECT (self, "Can't set fd... %s already set.",
        g_param_spec_get_name (g_properties[PROP_DRIVER_NAME]));
    return;
  }

  if (self->bus_id) {
    GST_WARNING_OBJECT (self, "Can't set fd... %s already set.",
        g_param_spec_get_name (g_properties[PROP_BUS_ID]));
    return;
  }

  if (self->fd >= 0) {
    GST_WARNING_OBJECT (self, "Can't set fd... it is already set.");
    return;
  }

  if (fd >= 0) {
    self->devname = drmGetDeviceNameFromFd (fd);
    if (!self->devname) {
      GST_WARNING_OBJECT (self, "Failed to verify fd is a DRM fd.");
      return;
    }

    self->fd = fd;
    self->is_internal_fd = FALSE;
  }
}

static void
_invalidate_external_fd (GstKMSSink * self, GParamSpec * pspec)
{
  if (self->is_internal_fd)
    return;

  GST_WARNING_OBJECT (self, "Unsetting fd... %s has priority.",
      g_param_spec_get_name (pspec));

  self->fd = -1;
  self->is_internal_fd = TRUE;
}

static void
gst_kms_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKMSSink *sink;

  sink = GST_KMS_SINK (object);

  switch (prop_id) {
    case PROP_DRIVER_NAME:
      _invalidate_external_fd (sink, pspec);
      g_free (sink->devname);
      sink->devname = g_value_dup_string (value);
      break;
    case PROP_BUS_ID:
      _invalidate_external_fd (sink, pspec);
      g_free (sink->bus_id);
      sink->bus_id = g_value_dup_string (value);
      break;
    case PROP_CONNECTOR_ID:
      sink->conn_id = g_value_get_int (value);
      break;
    case PROP_PLANE_ID:
      sink->plane_id = g_value_get_int (value);
      break;
    case PROP_FORCE_MODESETTING:
      sink->modesetting_enabled = g_value_get_boolean (value);
      break;
    case PROP_RESTORE_CRTC:
      sink->restore_crtc = g_value_get_boolean (value);
      break;
    case PROP_CAN_SCALE:
      sink->can_scale = g_value_get_boolean (value);
      break;
    case PROP_CONNECTOR_PROPS:{
      const GstStructure *s = gst_value_get_structure (value);

      g_clear_pointer (&sink->connector_props, gst_structure_free);

      if (s)
        sink->connector_props = gst_structure_copy (s);

      break;
    }
    case PROP_PLANE_PROPS:{
      const GstStructure *s = gst_value_get_structure (value);

      g_clear_pointer (&sink->plane_props, gst_structure_free);

      if (s)
        sink->plane_props = gst_structure_copy (s);

      break;
    }
    case PROP_FD:
      _validate_and_set_external_fd (sink, g_value_get_int (value));
    case PROP_SKIP_VSYNC:
      sink->skip_vsync = g_value_get_boolean (value);
      break;
    case PROP_FORCE_LEGACY:
      sink->force_legacy = g_value_get_boolean (value);
      break;
    default:
      if (!gst_video_overlay_set_property (object, PROP_N, prop_id, value))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKMSSink *sink;

  sink = GST_KMS_SINK (object);

  switch (prop_id) {
    case PROP_DRIVER_NAME:
      g_value_set_string (value, sink->devname);
      break;
    case PROP_BUS_ID:
      g_value_set_string (value, sink->bus_id);
      break;
    case PROP_CONNECTOR_ID:
      g_value_set_int (value, sink->conn_id);
      break;
    case PROP_PLANE_ID:
      g_value_set_int (value, sink->plane_id);
      break;
    case PROP_FORCE_MODESETTING:
      g_value_set_boolean (value, sink->modesetting_enabled);
      break;
    case PROP_RESTORE_CRTC:
      g_value_set_boolean (value, sink->restore_crtc);
      break;
    case PROP_CAN_SCALE:
      g_value_set_boolean (value, sink->can_scale);
      break;
    case PROP_DISPLAY_WIDTH:
      GST_OBJECT_LOCK (sink);
      g_value_set_int (value, sink->hdisplay);
      GST_OBJECT_UNLOCK (sink);
      break;
    case PROP_DISPLAY_HEIGHT:
      GST_OBJECT_LOCK (sink);
      g_value_set_int (value, sink->vdisplay);
      GST_OBJECT_UNLOCK (sink);
      break;
    case PROP_CONNECTOR_PROPS:
      gst_value_set_structure (value, sink->connector_props);
      break;
    case PROP_PLANE_PROPS:
      gst_value_set_structure (value, sink->plane_props);
      break;
    case PROP_FD:
      g_value_set_int (value, sink->fd);
    case PROP_FORCE_LEGACY:
      g_value_set_boolean (value, sink->force_legacy);
      break;
    case PROP_SKIP_VSYNC:
      g_value_set_boolean (value, sink->skip_vsync);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_sink_finalize (GObject * object)
{
  GstKMSSink *sink;

  sink = GST_KMS_SINK (object);
  g_clear_pointer (&sink->devname, g_free);
  g_clear_pointer (&sink->bus_id, g_free);
  gst_poll_free (sink->poll);
  g_clear_pointer (&sink->connector_props, gst_structure_free);
  g_clear_pointer (&sink->plane_props, gst_structure_free);
  g_datalist_clear (&sink->conn_props);
  g_datalist_clear (&sink->crtc_props);
  g_clear_pointer (&sink->tmp_kmsmem, gst_memory_unref);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_kms_sink_init (GstKMSSink * sink)
{
  sink->fd = -1;
  sink->is_internal_fd = TRUE;
  sink->conn_id = -1;
  sink->plane_id = -1;
  g_datalist_init (&sink->conn_props);
  g_datalist_init (&sink->crtc_props);
  sink->can_scale = TRUE;
  gst_poll_fd_init (&sink->pollfd);
  sink->poll = gst_poll_new (TRUE);
  gst_video_info_init (&sink->vinfo);
  sink->skip_vsync = FALSE;

#ifdef HAVE_DRM_HDR
  sink->has_hdr_prop = FALSE;
  sink->has_hdr_info = FALSE;
  sink->has_sent_hdrif = FALSE;
  sink->hdr_blob_id = 0;
  sink->colorimetry = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
  gst_video_mastering_display_info_init (&sink->hdr_minfo);
  gst_video_content_light_level_init (&sink->hdr_cll);
#endif
}

static void
gst_kms_sink_class_init (GstKMSSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSinkClass *basesink_class;
  GstVideoSinkClass *videosink_class;
  GstCaps *caps;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesink_class = GST_BASE_SINK_CLASS (klass);
  videosink_class = GST_VIDEO_SINK_CLASS (klass);

  gst_element_class_set_static_metadata (element_class, "KMS video sink",
      "Sink/Video", GST_PLUGIN_DESC, "Víctor Jáquez <vjaquez@igalia.com>");

  caps = gst_kms_sink_caps_template_fill ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_kms_sink_change_state);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_kms_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_kms_sink_stop);
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_kms_sink_set_caps);
  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_kms_sink_get_caps);
  basesink_class->propose_allocation = gst_kms_sink_propose_allocation;
  basesink_class->query = gst_kms_sink_query;

  videosink_class->show_frame = gst_kms_sink_show_frame;

  gobject_class->finalize = gst_kms_sink_finalize;
  gobject_class->set_property = gst_kms_sink_set_property;
  gobject_class->get_property = gst_kms_sink_get_property;

  /**
   * kmssink:driver-name:
   *
   * If you have a system with multiple GPUs, you can choose which GPU
   * to use setting the DRM device driver name. Otherwise, the first
   * one from an internal list is used.
   */
  g_properties[PROP_DRIVER_NAME] = g_param_spec_string ("driver-name",
      "device name", "DRM device driver name", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:bus-id:
   *
   * If you have a system with multiple displays for the same driver-name,
   * you can choose which display to use by setting the DRM bus ID. Otherwise,
   * the driver decides which one.
   */
  g_properties[PROP_BUS_ID] = g_param_spec_string ("bus-id",
      "Bus ID", "DRM bus ID", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:connector-id:
   *
   * A GPU has several output connectors, for example: LVDS, VGA,
   * HDMI, etc. By default the first LVDS is tried, then the first
   * eDP, and at the end, the first connected one.
   */
  g_properties[PROP_CONNECTOR_ID] = g_param_spec_int ("connector-id",
      "Connector ID", "DRM connector id", -1, G_MAXINT32, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

   /**
   * kmssink:plane-id:
   *
   * There could be several planes associated with a CRTC.
   * By default the first plane that's possible to use with a given
   * CRTC is tried.
   */
  g_properties[PROP_PLANE_ID] = g_param_spec_int ("plane-id",
      "Plane ID", "DRM plane id", -1, G_MAXINT32, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:force-modesetting:
   *
   * If the output connector is already active, the sink automatically uses an
   * overlay plane. Enforce mode setting in the kms sink and output to the
   * base plane to override the automatic behavior.
   */
  g_properties[PROP_FORCE_MODESETTING] =
      g_param_spec_boolean ("force-modesetting", "Force modesetting",
      "When enabled, the sink try to configure the display mode", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:restore-crtc:
   *
   * Restore previous CRTC setting if new CRTC mode was set forcefully.
   * By default this is enabled if user set CRTC with a new mode on an already
   * active CRTC wich was having a valid mode.
   */
  g_properties[PROP_RESTORE_CRTC] =
      g_param_spec_boolean ("restore-crtc", "Restore CRTC mode",
      "When enabled and CRTC was set with a new mode, previous CRTC mode will"
      "be restored when going to NULL state.", TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:can-scale:
   *
   * User can tell kmssink if the driver can support scale.
   */
  g_properties[PROP_CAN_SCALE] =
      g_param_spec_boolean ("can-scale", "can scale",
      "User can tell kmssink if the driver can support scale", TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * kmssink:display-width
   *
   * Actual width of the display. This is read only and only available in
   * PAUSED and PLAYING state. It's meant to be used with
   * gst_video_overlay_set_render_rectangle() function.
   */
  g_properties[PROP_DISPLAY_WIDTH] =
      g_param_spec_int ("display-width", "Display Width",
      "Width of the display surface in pixels", 0, G_MAXINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * kmssink:display-height
   *
   * Actual height of the display. This is read only and only available in
   * PAUSED and PLAYING state. It's meant to be used with
   * gst_video_overlay_set_render_rectangle() function.
   */
  g_properties[PROP_DISPLAY_HEIGHT] =
      g_param_spec_int ("display-height", "Display Height",
      "Height of the display surface in pixels", 0, G_MAXINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * kmssink:connector-properties:
   *
   * Additional properties for the connector. Keys are strings and values
   * unsigned 64 bits integers.
   *
   * Since: 1.16
   */
  g_properties[PROP_CONNECTOR_PROPS] =
      g_param_spec_boxed ("connector-properties", "Connector Properties",
      "Additional properties for the connector",
      GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * kmssink:plane-properties:
   *
   * Additional properties for the plane. Keys are strings and values
   * unsigned 64 bits integers.
   *
   * Since: 1.16
   */
  g_properties[PROP_PLANE_PROPS] =
      g_param_spec_boxed ("plane-properties", "Connector Plane",
      "Additional properties for the plane",
      GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * kmssink:fd:
   *
   * You can supply your own DRM file descriptor.  By default, the sink will
   * open its own DRM file descriptor.
   *
   * Since: 1.22
   */
  g_properties[PROP_FD] =
      g_param_spec_int ("fd", "File Descriptor",
      "DRM file descriptor", -1, G_MAXINT, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  g_properties[PROP_FORCE_LEGACY] =
      g_param_spec_boolean ("legacy", "Legacy API",
      "Force to use legacy DRM API", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * kmssink:skip-vsync:
   *
   *  For some cases, to suppress internal vsync, which can drop framerate
   *  in half, set this to 1.
   *
   *  Since: 1.22
   */
  g_properties[PROP_SKIP_VSYNC] =
      g_param_spec_boolean ("skip-vsync", "Skip Internal VSync",
      "When enabled will not wait internally for vsync. "
      "Should be used for atomic drivers to avoid double vsync.", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (gobject_class, PROP_N, g_properties);

  gst_video_overlay_install_properties (gobject_class, PROP_N);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (kmssink, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, kms,
    GST_PLUGIN_DESC, plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)

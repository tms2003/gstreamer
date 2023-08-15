/* GStreamer Wayland Library
 *
 * Copyright (C) 2023 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwllinuxdmabuffeedback.h"

#include <xf86drm.h>
#include <drm_fourcc.h>
#include <sys/mman.h>

#include "linux-dmabuf-unstable-v1-client-protocol.h"

GST_DEBUG_CATEGORY (gst_wl_dmabuf_feedback_debug);
#define GST_CAT_DEFAULT gst_wl_dmabuf_feedback_debug

struct DmaBufFeedbackFormatTable
{
  guint32 size;
  struct
  {
    uint32_t fourcc;
    uint32_t unused;
    uint64_t modifier;
  } *data;
};

struct DmaBufFeedbackTranche
{
  dev_t target_device;

  gchar *primary_node;
  gchar *render_node;

  guint32 flags;
  GHashTable *formats;
};

struct DmaBufFeedback
{
  dev_t main_device;

  gchar *primary_node;
  gchar *render_node;

  struct DmaBufFeedbackFormatTable format_table;

  struct DmaBufFeedbackTranche *tranche_pending;
  GPtrArray *tranches;
};

struct _GstWlDmaBufFeedback
{
  GObject parent;

  struct DmaBufFeedback *feedback, *feedback_pending;
  struct zwp_linux_dmabuf_feedback_v1 *feedback_handle;
};

G_DEFINE_TYPE_WITH_CODE (GstWlDmaBufFeedback, gst_wl_dmabuf_feedback,
    G_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_wl_dmabuf_feedback_debug,
        "wldmabuffeedback", 0, "wldmabuffeedback library");
    );

enum
{
  SIGNAL_0,
  CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gint
set_dev_nodes (dev_t dev_id, gchar **primary_node, gchar **render_node)
{
  drmDevicePtr dev;
  gint res = drmGetDeviceFromDevId (dev_id, 0, &dev);

  if (res != 0) {
    GST_DEBUG ("Failed to get drm device info (%s)", g_strerror (res));
  }

  if (*primary_node)
    g_free (*primary_node);
  if (*render_node)
    g_free (*render_node);

  if (dev->available_nodes & (1 << DRM_NODE_PRIMARY))
    *primary_node = g_strdup (dev->nodes[DRM_NODE_PRIMARY]);
  if (dev->available_nodes & (1 << DRM_NODE_RENDER))
    *render_node = g_strdup (dev->nodes[DRM_NODE_RENDER]);

  drmFreeDevice (&dev);

  return res;
}

static void
dmabuf_feedback_format_table_init (struct DmaBufFeedbackFormatTable
    *format_table)
{
  memset (format_table, 0, sizeof (*format_table));
}

static struct DmaBufFeedbackTranche *
dmabuf_feedback_tranche_new (void)
{
  struct DmaBufFeedbackTranche *tranche =
      g_malloc0 (sizeof (struct DmaBufFeedbackTranche));
  tranche->formats =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_array_unref);
  return tranche;
}

static void
dmabuf_feedback_tranche_free (struct DmaBufFeedbackTranche *tranche)
{
  if (tranche->primary_node)
    g_free (tranche->primary_node);
  if (tranche->render_node)
    g_free (tranche->render_node);
  g_hash_table_destroy (tranche->formats);
  g_free (tranche);
}

static struct DmaBufFeedback *
dmabuf_feedback_new (void)
{
  struct DmaBufFeedback *feedback = g_malloc0 (sizeof (struct DmaBufFeedback));

  feedback->tranches =
      g_ptr_array_new_full (0, (GDestroyNotify) dmabuf_feedback_tranche_free);
  feedback->tranche_pending = dmabuf_feedback_tranche_new ();

  dmabuf_feedback_format_table_init (&feedback->format_table);

  return feedback;
}

static void
dmabuf_feedback_free (struct DmaBufFeedback *feedback)
{
  if (feedback->format_table.data && feedback->format_table.data != MAP_FAILED)
    munmap (feedback->format_table.data, feedback->format_table.size);

  dmabuf_feedback_tranche_free (feedback->tranche_pending);
  feedback->tranches =
      (GPtrArray *) g_ptr_array_free (feedback->tranches, TRUE);

  g_free (feedback);
}

static void
gst_wl_dmabuf_feedback_finalize (GObject *gobject)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (gobject);

  dmabuf_feedback_free (self->feedback_pending);
  if (self->feedback)
    dmabuf_feedback_free (self->feedback);

  if (self->feedback_handle)
    zwp_linux_dmabuf_feedback_v1_destroy (self->feedback_handle);

  G_OBJECT_CLASS (gst_wl_dmabuf_feedback_parent_class)->finalize (gobject);
}

static void
gst_wl_dmabuf_feedback_init (GstWlDmaBufFeedback *self)
{
  self->feedback_pending = dmabuf_feedback_new ();
}

static void
gst_wl_dmabuf_feedback_class_init (GstWlDmaBufFeedbackClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_dmabuf_feedback_finalize;

  signals[CHANGED] = g_signal_new ("changed",
      G_TYPE_FROM_CLASS (gobject_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

static void
handle_dmabuf_modifier (void *data,
    struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf, guint32 fourcc,
    guint32 modifier_hi, guint32 modifier_lo)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedbackTranche *tranche = feedback->tranche_pending;
  GArray *modifiers;
  guint64 modifier;

  modifier = ((guint64) modifier_hi << 32) | modifier_lo;

  if (!self->feedback)
    self->feedback = self->feedback_pending;

  GST_DEBUG_OBJECT (self,
      "modifier %" GST_FOURCC_FORMAT ":0x%016" G_GINT64_MODIFIER "x",
      GST_FOURCC_ARGS (fourcc), modifier);

  if (gst_video_dma_drm_fourcc_to_format (fourcc) != GST_VIDEO_FORMAT_UNKNOWN) {
    modifiers =
        g_hash_table_lookup (tranche->formats, GUINT_TO_POINTER (fourcc));
    if (!modifiers) {
      modifiers = g_array_new (FALSE, FALSE, sizeof (guint64));
      g_hash_table_insert (tranche->formats, GUINT_TO_POINTER (fourcc),
          modifiers);
    }
    g_array_append_val (modifiers, modifier);
  }
}

static void
handle_dmabuf_format (void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
    guint32 fourcc)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);

  GST_DEBUG_OBJECT (self, "format %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (fourcc));

  /* When the compositor lacks explicit modifier support
   * use implicit and linear for dumb buffers */
  if (zwp_linux_dmabuf_v1_get_version (zwp_linux_dmabuf) <
      ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
    handle_dmabuf_modifier (data, zwp_linux_dmabuf, fourcc,
        DRM_FORMAT_MOD_INVALID >> 32,
        DRM_FORMAT_MOD_INVALID & G_GUINT64_CONSTANT (0x0ffffffff));
    handle_dmabuf_modifier (data, zwp_linux_dmabuf, fourcc,
        DRM_FORMAT_MOD_LINEAR >> 32,
        DRM_FORMAT_MOD_LINEAR & G_GUINT64_CONSTANT (0x0ffffffff));
  }
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
  handle_dmabuf_format,
  handle_dmabuf_modifier,
};

static void
handle_dmabuf_feedback_done (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedback *feedback_old = self->feedback;
  guint i;

  set_dev_nodes (feedback->main_device, &feedback->primary_node,
      &feedback->render_node);

  for (i = 0; i < feedback->tranches->len; i++) {
    struct DmaBufFeedbackTranche *tranche =
        g_ptr_array_index (feedback->tranches, i);

    set_dev_nodes (tranche->target_device, &tranche->primary_node,
        &tranche->render_node);
  }

  self->feedback = feedback;

  if (feedback_old)
    dmabuf_feedback_free (feedback_old);

  self->feedback_pending = dmabuf_feedback_new ();

  g_signal_emit (self, signals[CHANGED], 0);
}

static void
handle_dmabuf_feedback_format_table (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
    int32_t fd, uint32_t size)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;

  feedback->format_table.size = size;
  feedback->format_table.data =
      mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

  if (feedback->format_table.data == MAP_FAILED)
    GST_ERROR_OBJECT (self, "Failed to mmap format table (%d): %s", errno,
        g_strerror (errno));

  close (fd);
}

static void
handle_dmabuf_feedback_main_device (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback, struct wl_array *dev)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;

  memcpy (&feedback->main_device, dev->data, sizeof (dev));
}

static void
handle_dmabuf_feedback_tranche_done (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedbackTranche *tranche = feedback->tranche_pending;

  g_ptr_array_add (feedback->tranches, tranche);

  feedback->tranche_pending = dmabuf_feedback_tranche_new ();
}

static void
handle_dmabuf_feedback_tranche_target_device (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback, struct wl_array *dev)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedbackTranche *tranche = feedback->tranche_pending;

  memcpy (&tranche->target_device, dev->data, sizeof (dev));
}

static void
handle_dmabuf_feedback_tranche_format (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
    struct wl_array *indices)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedbackTranche *tranche = feedback->tranche_pending;
  guint64 modifier;
  guint32 fourcc;
  uint16_t *index;

  /* If the compositor has not provided a format table for this feedback,
   * use the one from the previous feedback */
  if (feedback->format_table.data == NULL) {
    feedback->format_table = self->feedback->format_table;
    dmabuf_feedback_format_table_init (&self->feedback->format_table);
  }

  if (feedback->format_table.data == NULL) {
    GST_ERROR_OBJECT (self, "Compositor has not advertised format table");
    return;
  }
  if (feedback->format_table.data == MAP_FAILED) {
    GST_ERROR_OBJECT (self, "Format table could not be mapped");
    return;
  }

  wl_array_for_each (index, indices) {
    fourcc = feedback->format_table.data[*index].fourcc;
    modifier = feedback->format_table.data[*index].modifier;

    if (gst_video_dma_drm_fourcc_to_format (fourcc) != GST_VIDEO_FORMAT_UNKNOWN) {
      GArray *modifiers =
          g_hash_table_lookup (tranche->formats, GUINT_TO_POINTER (fourcc));
      if (!modifiers) {
        modifiers = g_array_new (FALSE, FALSE, sizeof (guint64));
        g_hash_table_insert (tranche->formats, GUINT_TO_POINTER (fourcc),
            modifiers);
      }
      g_array_append_val (modifiers, modifier);
      GST_DEBUG_OBJECT (self, "tranche %d, %" GST_FOURCC_FORMAT
          ":0x%016" G_GINT64_MODIFIER "x (%s)", feedback->tranches->len,
          GST_FOURCC_ARGS (fourcc), modifier,
          tranche->flags & ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT ?
          "scan out" : "");
    }
  }
}

static void
handle_dmabuf_feedback_tranche_flags (void *data,
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback, guint32 flags)
{
  GstWlDmaBufFeedback *self = GST_WL_DMABUF_FEEDBACK (data);
  struct DmaBufFeedback *feedback = self->feedback_pending;
  struct DmaBufFeedbackTranche *tranche = feedback->tranche_pending;

  tranche->flags = flags;
}

static const struct zwp_linux_dmabuf_feedback_v1_listener
    dmabuf_feedback_listener = {
  handle_dmabuf_feedback_done,
  handle_dmabuf_feedback_format_table,
  handle_dmabuf_feedback_main_device,
  handle_dmabuf_feedback_tranche_done,
  handle_dmabuf_feedback_tranche_target_device,
  handle_dmabuf_feedback_tranche_format,
  handle_dmabuf_feedback_tranche_flags,
};

GstWlDmaBufFeedback *
gst_wl_dmabuf_feedback_new_for_display (GstWlDisplay *display)
{
  GstWlDmaBufFeedback *self = g_object_new (GST_TYPE_WL_DMABUF_FEEDBACK, NULL);
  struct zwp_linux_dmabuf_v1 *dmabuf = gst_wl_display_get_dmabuf_v1 (display);

  g_return_val_if_fail (zwp_linux_dmabuf_v1_get_version (dmabuf) >=
      ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION, NULL);

  self->feedback_handle = zwp_linux_dmabuf_v1_get_default_feedback (dmabuf);

  zwp_linux_dmabuf_feedback_v1_add_listener (self->feedback_handle,
      &dmabuf_feedback_listener, self);

  return self;
}

GstWlDmaBufFeedback *
gst_wl_dmabuf_feedback_new_for_display_legacy (GstWlDisplay *display)
{
  GstWlDmaBufFeedback *self = g_object_new (GST_TYPE_WL_DMABUF_FEEDBACK, NULL);
  struct zwp_linux_dmabuf_v1 *dmabuf = gst_wl_display_get_dmabuf_v1 (display);
  struct DmaBufFeedback *feedback = self->feedback_pending;

  /* Only one tranche */
  feedback->tranche_pending = dmabuf_feedback_tranche_new ();
  g_ptr_array_add (feedback->tranches, feedback->tranche_pending);

  zwp_linux_dmabuf_v1_add_listener (dmabuf, &dmabuf_listener, self);

  return self;
}

gboolean
gst_wl_dmabuf_feedback_query_format_support (GstWlDmaBufFeedback *self,
    guint32 fourcc, guint64 modifier,
    gboolean *is_modifier, gboolean *is_implicit, gboolean *is_linear)
{
  GPtrArray *tranches;
  struct DmaBufFeedbackTranche *tranche;
  guint i, j;
  guint64 mod;

  if (!self->feedback)
    return FALSE;

  tranches = self->feedback->tranches;

  if (is_modifier)
    *is_modifier = FALSE;
  if (is_implicit)
    *is_implicit = FALSE;
  if (is_linear)
    *is_linear = FALSE;

  for (i = 0; i < tranches->len; i++) {
    tranche = g_ptr_array_index (tranches, i);

    GArray *modifiers = g_hash_table_lookup (tranche->formats,
        GUINT_TO_POINTER (fourcc));

    if (!modifiers)
      continue;

    for (j = 0; j < modifiers->len; j++) {
      mod = g_array_index (modifiers, guint64, j);
      if (is_modifier && mod == modifier)
        *is_modifier = TRUE;
      if (is_implicit && mod == DRM_FORMAT_MOD_INVALID)
        *is_implicit = TRUE;
      else if (is_linear && mod == DRM_FORMAT_MOD_LINEAR)
        *is_linear = TRUE;

      /* finish if all settable values are set */
      if ((!is_modifier || *is_modifier) && (!is_implicit || *is_implicit) &&
          (!is_linear || *is_linear)) {
        return TRUE;
      }
    }
  }

  return TRUE;
}

static guint
_fill_drm_format_list (GHashTable *formats, GValue *format_list)
{
  guint num_formats = 0;
  GHashTableIter format_iter;
  gpointer key, value;
  GArray *modifiers;
  guint32 fourcc;
  guint64 mod;
  GstVideoFormat format;
  guint i;
  GValue format_string = G_VALUE_INIT;
  gboolean support;

  g_hash_table_iter_init (&format_iter, formats);
  while (g_hash_table_iter_next (&format_iter, &key, &value)) {
    support = FALSE;
    fourcc = GPOINTER_TO_UINT (key);
    modifiers = (GArray *) value;
    format = gst_video_dma_drm_fourcc_to_format (fourcc);

    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    for (i = 0; i < modifiers->len; i++) {
      mod = g_array_index (modifiers, guint64, i);

      if (mod == DRM_FORMAT_MOD_INVALID)
        continue;

      g_value_init (&format_string, G_TYPE_STRING);
      g_value_set_static_string (&format_string,
          gst_video_dma_drm_fourcc_to_string (fourcc, mod));
      gst_value_list_append_and_take_value (format_list, &format_string);
      support = TRUE;
    }

    if (support)
      num_formats++;
  }

  return num_formats;
}

/* Fill list with fourcc:modifier pairs for "drm-format" field */
gboolean
gst_wl_dmabuf_feedback_fill_drm_format_list (GstWlDmaBufFeedback *self,
    GValue *format_list)
{
  GPtrArray *tranches = self->feedback->tranches;
  guint num_formats = 0;
  guint i;

  for (i = 0; i < tranches->len; i++) {
    struct DmaBufFeedbackTranche *tranche = g_ptr_array_index (tranches, i);
    num_formats += _fill_drm_format_list (tranche->formats, format_list);
  }

  return num_formats != 0;
}

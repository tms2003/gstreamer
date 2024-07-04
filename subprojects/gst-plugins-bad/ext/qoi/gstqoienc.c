/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 * SECTION:element-qoienc
 * @title: qoienc
 *
 * Encodes qoi images.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=1 ! video/x-raw, width='(int)'1280, height='(int)'720, format='(string)'RGB ! qoienc ! filesink location=result.qoi
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstqoienc.h"

#define QOI_IMPLEMENTATION

#include <qoi.h>

struct _GstQoiEnc
{
  GstVideoEncoder parent;

  GstVideoCodecState *input_state;

  qoi_desc *qoi_description;
};

GST_DEBUG_CATEGORY_STATIC (qoienc_debug);
#define GST_CAT_DEFAULT qoienc_debug

/* Src caps */
#define QOI_ENCODER_STATIC_CAPS_SRC     \
  "image/qoi, "                         \
  "width = (int) [ 1, 20000 ], "         \
  "height = (int) [ 1, 20000 ], "        \
  "framerate = " GST_VIDEO_FPS_RANGE

/* Sink caps */
#define QOI_ENCODER_STATIC_CAPS_SINK    \
  "video/x-raw, "                       \
  "format = (string) { RGB, RGBA }, "   \
  "width = (int) [ 1, 20000 ], "        \
  "height = (int) [ 1, 20000 ], "       \
  "framerate = " GST_VIDEO_FPS_RANGE

static GstStaticPadTemplate qoienc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QOI_ENCODER_STATIC_CAPS_SRC)
    );

static GstStaticPadTemplate qoienc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QOI_ENCODER_STATIC_CAPS_SINK)
    );

#define parent_class gst_qoienc_parent_class
G_DEFINE_TYPE (GstQoiEnc, gst_qoienc, GST_TYPE_VIDEO_ENCODER);
GST_ELEMENT_REGISTER_DEFINE (qoienc, "qoienc", GST_RANK_PRIMARY,
    GST_TYPE_QOIENC);

static GstFlowReturn gst_qoienc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_qoienc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_qoienc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_qoienc_finalize (GObject * object);

static void
gst_qoienc_class_init (GstQoiEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_add_static_pad_template
      (element_class, &qoienc_sink_template);
  gst_element_class_add_static_pad_template
      (element_class, &qoienc_src_template);

  gst_element_class_set_static_metadata (element_class,
      "Quiet-OK Image encoder", "Codec/Encoder/Image",
      "Encode a video frame to .qoi", "Abhay Chirania <a-chirania@ti.com>");

  venc_class->set_format = gst_qoienc_set_format;
  venc_class->handle_frame = gst_qoienc_handle_frame;
  venc_class->propose_allocation = gst_qoienc_propose_allocation;
  gobject_class->finalize = gst_qoienc_finalize;

  (void) qoi_decode;

  GST_DEBUG_CATEGORY_INIT (qoienc_debug, "qoienc", 0, "Quiet-OK image encoder");
}

static void
gst_qoienc_init (GstQoiEnc * qoienc)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (qoienc));

  qoienc->qoi_description = (qoi_desc *) g_malloc (sizeof (qoi_desc));
}

static void
gst_qoienc_finalize (GObject * object)
{
  GstQoiEnc *qoienc = GST_QOIENC (object);

  if (qoienc->input_state)
    gst_video_codec_state_unref (qoienc->input_state);

  if (qoienc->qoi_description)
    g_free (qoienc->qoi_description);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_qoienc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstQoiEnc *qoienc;
  gboolean ret = TRUE;
  GstVideoInfo *info;
  GstVideoCodecState *output_state;

  qoienc = GST_QOIENC (encoder);
  info = &state->info;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGB:
      qoienc->qoi_description->colorspace = QOI_SRGB;
      qoienc->qoi_description->channels = 3;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      qoienc->qoi_description->colorspace = QOI_SRGB;
      qoienc->qoi_description->channels = 4;
      break;
    default:
      ret = FALSE;
      goto done;
  }

  if (qoienc->input_state)
    gst_video_codec_state_unref (qoienc->input_state);
  qoienc->input_state = gst_video_codec_state_ref (state);

  output_state =
      gst_video_encoder_set_output_state (encoder,
      gst_caps_new_empty_simple ("image/qoi"), state);
  gst_video_codec_state_unref (output_state);

done:

  return ret;
}

static GstFlowReturn
gst_qoienc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstQoiEnc *qoienc;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoInfo *info;
  GstVideoFrame vframe;
  GstMemory *output_mem;
  GstMapInfo output_map;
  GstBuffer *output_buf;
  gsize max_size;
  gint encoded_size = 0;

  qoienc = GST_QOIENC (encoder);

  GST_DEBUG_OBJECT (qoienc, "handle_frame");

  info = &qoienc->input_state->info;

  qoienc->qoi_description->width = GST_VIDEO_INFO_WIDTH (info);
  qoienc->qoi_description->height = GST_VIDEO_INFO_HEIGHT (info);

  if (!gst_video_frame_map (&vframe, &qoienc->input_state->info,
          frame->input_buffer, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (qoienc, STREAM, FORMAT, (NULL),
        ("Failed to map video frame, caps problem?"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  max_size = qoienc->qoi_description->width * qoienc->qoi_description->height *
      (qoienc->qoi_description->channels + 1) + QOI_HEADER_SIZE +
      sizeof (qoi_padding);

  output_mem = gst_allocator_alloc (NULL, max_size, NULL);
  if (!output_mem) {
    GST_ERROR_OBJECT (qoienc, "Failed to allocate memory");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (!gst_memory_map (output_mem, &output_map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (qoienc, "Failed to map memory");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  qoi_encode (GST_VIDEO_FRAME_COMP_DATA (&vframe, 0), qoienc->qoi_description,
      &encoded_size, (void *) output_map.data);

  gst_video_frame_unmap (&vframe);
  gst_memory_unmap (output_mem, &output_map);

  gst_memory_resize (output_mem, 0, encoded_size);

  output_buf = gst_buffer_new ();
  gst_buffer_append_memory (output_buf, output_mem);

  output_mem = NULL;

  frame->output_buffer = output_buf;

  ret = gst_video_encoder_finish_frame (encoder, frame);

done:
  GST_DEBUG_OBJECT (qoienc, "END, ret:%d", ret);

  return ret;
}

static gboolean
gst_qoienc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

/* VMAF plugin
 * Copyright (C) 2021 Hudl
 *   @author: Casey Bateman <Casey.Bateman@hudl.com>
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
/**
 * SECTION:element-vmaf
 * @title: vmaf
 * @short_description: Provides Video Multi-Method Assessment Fusion quality metrics
 *
 * VMAF will perform perceptive video quality analysis on a set of input 
 * pads, the first pad is the reference video. 
 *
 * It will perform comparisons on video streams with the same geometry.
 *
 * The image output will be the be the reference video pad, the first pad.
 *
 * VMAF will post a message containing a structure named VMAF, at the end for
 * each supplied pad, or every reference frame if the property for 
 * frame-message=true.1
 *
 * It is possible to configure and run PSNR, SSIM, MS-SSIM together with VMAF
 * by setting the appropriate properties to true. 
 *
 * The message will contain a field for "type" this field will be one of two values:
 *  (int)0: score for the individual frame
 *  (int)1: pooled score for the entire stream
 *
 * The message will also contain a "stream" field, which is the index of the distorted pad.
 *
 * For example, if ms-ssim, ssim, psnr are set to true, and there are
 * two compared streams, the emitted structure will look like this:
 *
 * VMAF, score=(double)78.910751757633022, index=(int)26, type=(int)0, stream=(int)0, ms-ssim=(double)0.96676034472760064, ssim=(double)0.8706783652305603, psnr=(double)30.758853484390933;
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -m filesrc location=test1.yuv ! rawvideoparse ! video/x-raw,fromat=I420 ! v.sink_0 vmaf name=v frame-message=true log-filename=scores%05d.json psnr=true ssim=true ms-ssim=true   filesrc location=test2.yuv ! rawvideoparse ! video/x-raw,fromat=I420 ! v.sink_1  v.src ! videoconvert ! autovideosink 
 * ]| This pipeline will output messages to the console for each set of compared frames.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <libvmaf.h>
#include "gstvmafelements.h"
#include "gstvmafconvert.h"
#include "gstvmafenums.h"

GST_DEBUG_CATEGORY_STATIC (gst_vmaf_debug);
#define GST_CAT_DEFAULT gst_vmaf_debug
#define SINK_FORMATS " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define SRC_FORMAT " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define DEFAULT_MODEL_FILENAME     "vmaf_v0.6.1"
#define DEFAULT_DISABLE_CLIP       FALSE
#define DEFAULT_DISABLE_AVX        FALSE
#define DEFAULT_ENABLE_TRANSFORM   FALSE
#define DEFAULT_PHONE_MODEL        FALSE
#define DEFAULT_PSNR               FALSE
#define DEFAULT_SSIM               FALSE
#define DEFAULT_MS_SSIM            FALSE
#define DEFAULT_FRAME_MESSAGING    FALSE
#define DEFAULT_POOL_METHOD        POOL_METHOD_MEAN
#define DEFAULT_NUM_THREADS        0
#define DEFAULT_SUBSAMPLE          1
#define DEFAULT_CONF_INT           FALSE
#define DEFAULT_VMAF_LOG_LEVEL     VMAF_LOG_LEVEL_NONE
#define DEFAULT_VMAF_LOG_FORMAT    VMAF_OUTPUT_FORMAT_NONE
#define DEFAULT_VMAF_LOG_FILENAME  NULL
#define GST_TYPE_VMAF_POOL_METHOD  (gst_vmaf_pool_method_get_type ())
#define GST_TYPE_VMAF_LOG_FORMATS  (gst_vmaf_log_format_get_type ())

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT)));

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS)));


#define gst_vmaf_parent_class parent_class
G_DEFINE_TYPE (GstVmaf, gst_vmaf, GST_TYPE_VIDEO_AGGREGATOR);

static GType
gst_vmaf_pool_method_get_type (void)
{
  static const GEnumValue types[] = {
    {POOL_METHOD_MIN, "Minimum value", "min"},
    {POOL_METHOD_MAX, "Maximum value", "max"},
    {POOL_METHOD_MEAN, "Arithmetic mean", "mean"},
    {POOL_METHOD_HARMONIC_MEAN, "Harmonic mean", "harmonic_mean"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafPoolMethod", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

static void
gst_vmaf_set_pool_method (GstVmaf * self, gint pool_method)
{
  switch (pool_method) {
    case POOL_METHOD_MIN:
      self->vmaf_config_pool_method = POOL_METHOD_MIN;
      break;
    case POOL_METHOD_MAX:
      self->vmaf_config_pool_method = POOL_METHOD_MAX;
      break;
    case POOL_METHOD_MEAN:
      self->vmaf_config_pool_method = POOL_METHOD_MEAN;
      break;
    case POOL_METHOD_HARMONIC_MEAN:
      self->vmaf_config_pool_method = POOL_METHOD_HARMONIC_MEAN;
      break;
    default:
      g_assert_not_reached ();
  }
}

static GType
gst_vmaf_log_format_get_type (void)
{
  static const GEnumValue types[] = {
    {OUTPUT_FORMAT_CSV, "Comma Separated File (csv)", "csv"},
    {OUTPUT_FORMAT_JSON, "JSON", "json"},
    {OUTPUT_FORMAT_XML, "XML", "xml"},
    {OUTPUT_FORMAT_NONE, "None", "none"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafLogFormats", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

static void
gst_vmaf_set_log_format (GstVmaf * self, gint log_fmt)
{
  switch (log_fmt) {
    case OUTPUT_FORMAT_CSV:
      self->vmaf_config_log_format = OUTPUT_FORMAT_CSV;
      break;
    case OUTPUT_FORMAT_JSON:
      self->vmaf_config_log_format = OUTPUT_FORMAT_JSON;
      break;
    case OUTPUT_FORMAT_XML:
      self->vmaf_config_log_format = OUTPUT_FORMAT_XML;
      break;
    case OUTPUT_FORMAT_NONE:
      self->vmaf_config_log_format = OUTPUT_FORMAT_NONE;
      break;
    default:
      g_assert_not_reached ();
  }
}

static inline float
get_data_from_ptr (void *ptr, int i, int j, int frame_width, gint bpc)
{
  float result;
  if (bpc > 8)
    result = (float) ((guint16 *) ptr)[i * frame_width + j] / 4.0;
  else
    result = (float) ((guint8 *) ptr)[i * frame_width + j];
  return result;
}

/* libvmaf model setup */

static void
gst_vmaf_models_destroy (GstVmafThreadHelper * thread_data)
{
  GstVmaf *self = GST_VMAF (thread_data->gst_vmaf_p);
  vmaf_close (thread_data->vmaf_ctx);
  vmaf_model_destroy (thread_data->vmaf_model);
  if (self->vmaf_config_conf_int) {
    vmaf_model_collection_destroy (thread_data->vmaf_model_collection);
  }
}

static gint
gst_init_vmaf_model (GstVmaf * self, GstVmafThreadHelper * thread_data,
    VmafModelConfig * model_cfg)
{
  gint err = 0;

  //attempt to load from the built in models first
  err =
      vmaf_model_load (&thread_data->vmaf_model, model_cfg,
      self->vmaf_config_model_filename);
  if (err) {
    //if built in model will not load, attempt to load from file path
    err =
        vmaf_model_load_from_path (&thread_data->vmaf_model, model_cfg,
        self->vmaf_config_model_filename);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf model file from path: %s",
              self->vmaf_config_model_filename),
          ("Failed to load vmaf model file from path: %s",
              self->vmaf_config_model_filename));
      return EXIT_FAILURE;
    }
  }

  err =
      vmaf_use_features_from_model (thread_data->vmaf_ctx,
      thread_data->vmaf_model);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("Failed to load vmaf feature extractors from model file: %s",
            self->vmaf_config_model_filename),
        ("Failed to load vmaf feature extractors from model file: %s",
            self->vmaf_config_model_filename));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static gint
gst_init_vmaf_model_collection (GstVmaf * self,
    GstVmafThreadHelper * thread_data, VmafModelConfig * model_cfg)
{
  gint err = 0;
  //attempt to load from the built in models first
  err =
      vmaf_model_collection_load (&thread_data->vmaf_model,
      &thread_data->vmaf_model_collection, model_cfg,
      self->vmaf_config_model_filename);
  if (err) {
    //if built in model will not load, attempt to load from file path
    err =
        vmaf_model_collection_load_from_path (&thread_data->vmaf_model,
        &thread_data->vmaf_model_collection, model_cfg,
        self->vmaf_config_model_filename);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf model file from path: %s",
              self->vmaf_config_model_filename),
          ("Failed to load vmaf model file from path: %s",
              self->vmaf_config_model_filename));
      return EXIT_FAILURE;
    }
  }

  err =
      vmaf_use_features_from_model_collection (thread_data->vmaf_ctx,
      thread_data->vmaf_model_collection);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("Failed to load vmaf feature extractors from model file: %s",
            self->vmaf_config_model_filename),
        ("Failed to load vmaf feature extractors from model file: %s",
            self->vmaf_config_model_filename));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static gint
gst_vmaf_models_create (GstVmaf * self, GstVmafThreadHelper * thread_data)
{
  gint err = 0;
  gboolean result = TRUE;
  VmafFeatureDictionary *d = NULL;
  enum VmafModelFlags flags = VMAF_MODEL_FLAGS_DEFAULT;
  VmafModelConfig model_cfg = { 0 };
  VmafConfiguration cfg = {
    .log_level = DEFAULT_VMAF_LOG_LEVEL,
    .n_threads = self->vmaf_config_num_threads,
    .n_subsample = self->vmaf_config_subsample,
    .cpumask = self->vmaf_config_disable_avx ? -1 : 0,
  };

  err = vmaf_init (&thread_data->vmaf_ctx, cfg);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("Failed to initialize vmaf context."),
        ("Failed to initialize vmaf context."));
    result = FALSE;
    goto free_data;
  }

  if (self->vmaf_config_disable_clip)
    flags |= VMAF_MODEL_FLAG_DISABLE_CLIP;
  if (self->vmaf_config_enable_transform || self->vmaf_config_phone_model)
    flags |= VMAF_MODEL_FLAG_ENABLE_TRANSFORM;

  model_cfg.name = "vmaf";
  model_cfg.flags = flags;

  if (self->vmaf_config_conf_int) {
    err = gst_init_vmaf_model_collection (self, thread_data, &model_cfg);
    if (err) {
      result = FALSE;
      goto free_data;
    }
  } else {
    err = gst_init_vmaf_model (self, thread_data, &model_cfg);
    if (err) {
      result = FALSE;
      goto free_data;
    }
  }

  if (self->vmaf_config_psnr) {
    vmaf_feature_dictionary_set (&d, "enable_chroma", "false");

    err = vmaf_use_feature (thread_data->vmaf_ctx, "psnr", d);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Problem loading feature extractor: psnr"),
          ("Problem loading feature extractor: psnr"));
      result = FALSE;
      goto free_data;
    }
  }
  if (self->vmaf_config_ssim) {
    err = vmaf_use_feature (thread_data->vmaf_ctx, "float_ssim", NULL);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Problem loading feature extractor: ssim"),
          ("Problem loading feature extractor: ssim"));
      result = FALSE;
      goto free_data;
    }
  }
  if (self->vmaf_config_ms_ssim) {
    err = vmaf_use_feature (thread_data->vmaf_ctx, "float_ms_ssim", NULL);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Problem loading feature extractor: float_ms_ssim"),
          ("Problem loading feature extractor: float_ms_ssim"));
      result = FALSE;
      goto free_data;
    }
  }

  goto end;

free_data:
  gst_vmaf_models_destroy (thread_data);
end:
  return result;
}

static gint
gst_vmaf_post_pooled_score (GstVmafThreadHelper * thread_data)
{
  gint err = 0;
  gint res = EXIT_SUCCESS;
  gdouble vmaf_score = 0, ms_ssim_score = 0, ssim_score = 0, psnr_score = 0;
  gboolean successfulPost = TRUE;
  gchar *location;
  GstVmaf *self = thread_data->gst_vmaf_p;
  VmafModelCollectionScore model_collection_score;
  enum VmafOutputFormat vmaf_output_format =
      vmaf_map_log_fmt (self->vmaf_config_log_format);
  enum VmafPoolingMethod vmaf_pooling_method =
      vmaf_map_pooling_method (self->vmaf_config_pool_method);
  GstStructure *vmaf_message_structure = gst_structure_new_empty ("VMAF");
  GstMessage *vmaf_message = gst_message_new_element (GST_OBJECT (self),
      vmaf_message_structure);

  if (self->vmaf_config_conf_int) {
    err = vmaf_score_pooled_model_collection (thread_data->vmaf_ctx,
        thread_data->vmaf_model_collection,
        vmaf_pooling_method, &model_collection_score, 0,
        thread_data->last_frame_processed);
    if (err) {
      GST_DEBUG_OBJECT (self,
          "could not calculate pooled vmaf score on range 0 to %d, for model collection",
          thread_data->last_frame_processed);
      res = EXIT_FAILURE;
    }
  }

  err = vmaf_score_pooled (thread_data->vmaf_ctx,
      thread_data->vmaf_model,
      vmaf_pooling_method, &vmaf_score, 0, thread_data->last_frame_processed);
  if (err) {
    GST_WARNING_OBJECT (self,
        "could not calculate pooled vmaf score on range 0 to %d",
        thread_data->last_frame_processed);
    res = EXIT_FAILURE;
  } else {
    GST_DEBUG_OBJECT (self,
        "posting pooled vmaf score on stream:%d range:0-%d score:%f",
        thread_data->stream_index, thread_data->last_frame_processed,
        vmaf_score);

    gst_structure_set (vmaf_message_structure,
        "score", G_TYPE_DOUBLE, vmaf_score,
        "type", G_TYPE_INT, MESSAGE_TYPE_POOLED,
        "stream", G_TYPE_INT, thread_data->stream_index, NULL);
    if (self->vmaf_config_ms_ssim) {
      err =
          vmaf_feature_score_pooled (thread_data->vmaf_ctx, "float_ms_ssim",
          vmaf_pooling_method, &ms_ssim_score, 0,
          thread_data->last_frame_processed);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate ms-ssim score on stream:%d range:0-%d err:%d",
            thread_data->stream_index, thread_data->last_frame_processed, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "ms-ssim", G_TYPE_DOUBLE, ms_ssim_score, NULL);
      }
    }
    if (self->vmaf_config_ssim) {
      err =
          vmaf_feature_score_pooled (thread_data->vmaf_ctx, "float_ssim",
          vmaf_pooling_method, &ssim_score, 0,
          thread_data->last_frame_processed);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate ssim score on stream:%d range:0-%d err:%d",
            thread_data->stream_index, thread_data->last_frame_processed, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "ssim", G_TYPE_DOUBLE, ssim_score, NULL);
      }
    }
    if (self->vmaf_config_psnr) {
      err =
          vmaf_feature_score_pooled (thread_data->vmaf_ctx, "psnr_y",
          vmaf_pooling_method, &psnr_score, 0,
          thread_data->last_frame_processed);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate psnr score on stream:%d range:0-%d err:%d",
            thread_data->stream_index, thread_data->last_frame_processed, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "psnr", G_TYPE_DOUBLE, psnr_score, NULL);
      }
    }

    successfulPost =
        gst_element_post_message (GST_ELEMENT (self), vmaf_message);
    if (!successfulPost) {
      GST_WARNING_OBJECT (self,
          "could not post pooled VMAF on message bus. score:%f stream:%d",
          vmaf_score, thread_data->stream_index);
    }
  }

  if (vmaf_output_format == VMAF_OUTPUT_FORMAT_NONE
      && self->vmaf_config_log_filename) {
    vmaf_output_format = VMAF_OUTPUT_FORMAT_JSON;
    GST_DEBUG_OBJECT (self, "using default JSON style logging.");
  }

  if (vmaf_output_format) {
    location = g_strdup_printf (self->vmaf_config_log_filename,
        thread_data->stream_index);
    GST_DEBUG_OBJECT (self,
        "writing VMAF score data to location:%s.", location);

    err =
        vmaf_write_output (thread_data->vmaf_ctx, location, vmaf_output_format);
    if (err) {
      GST_WARNING_OBJECT (self, "could not write VMAF output:%s.", location);
      res = EXIT_FAILURE;
    }
  }

  return res;
}

static gint
gst_vmaf_post_frame_score (GstVmafThreadHelper * thread_data, gint frame_index)
{
  gint err = 0;
  gboolean res = TRUE;
  gdouble vmaf_score = 0, ms_ssim_score = 0, ssim_score = 0, psnr_score = 0;
  GstVmaf *self = thread_data->gst_vmaf_p;
  gint scored_frame = frame_index - self->vmaf_config_subsample;
  gint mod_frame = !(scored_frame % self->vmaf_config_subsample);
  GstStructure *vmaf_message_structure = gst_structure_new_empty ("VMAF");
  GstMessage *vmaf_message = gst_message_new_element (GST_OBJECT (self),
      vmaf_message_structure);

  if (self->vmaf_config_frame_messaging && frame_index > 0 && mod_frame) {
    err =
        vmaf_score_at_index (thread_data->vmaf_ctx, thread_data->vmaf_model,
        &vmaf_score, scored_frame);
    if (err) {
      GST_WARNING_OBJECT (self,
          "could not calculate vmaf score on stream:%d frame:%d err:%d",
          thread_data->stream_index, scored_frame, err);
      return EXIT_FAILURE;
    }

    GST_DEBUG_OBJECT (self,
        "posting frame vmaf score. score:%f stream:%d frame:%d",
        vmaf_score, thread_data->stream_index, scored_frame);

    gst_structure_set (vmaf_message_structure,
        "score", G_TYPE_DOUBLE, vmaf_score,
        "index", G_TYPE_INT, scored_frame,
        "type", G_TYPE_INT, MESSAGE_TYPE_FRAME,
        "stream", G_TYPE_INT, thread_data->stream_index, NULL);

    if (self->vmaf_config_ms_ssim) {
      err =
          vmaf_feature_score_at_index (thread_data->vmaf_ctx, "float_ms_ssim",
          &ms_ssim_score, scored_frame);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate ms-ssim score on stream:%d frame:%d err:%d",
            thread_data->stream_index, scored_frame, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "ms-ssim", G_TYPE_DOUBLE, ms_ssim_score, NULL);
      }
    }
    if (self->vmaf_config_ssim) {
      err =
          vmaf_feature_score_at_index (thread_data->vmaf_ctx, "float_ssim",
          &ssim_score, scored_frame);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate ssim score on stream:%d frame:%d err:%d",
            thread_data->stream_index, scored_frame, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "ssim", G_TYPE_DOUBLE, ssim_score, NULL);
      }
    }
    if (self->vmaf_config_psnr) {
      err =
          vmaf_feature_score_at_index (thread_data->vmaf_ctx, "psnr_y",
          &psnr_score, scored_frame);
      if (err) {
        GST_WARNING_OBJECT (self,
            "could not calculate psnr score on stream:%d frame:%d err:%d",
            thread_data->stream_index, scored_frame, err);
      } else {
        gst_structure_set (vmaf_message_structure,
            "psnr", G_TYPE_DOUBLE, psnr_score, NULL);
      }
    }
    res = gst_element_post_message (GST_ELEMENT (self), vmaf_message);
    if (!res) {
      GST_WARNING_OBJECT (self,
          "could not post frame VMAF on message bus. score:%f stream:%d frame:%d",
          vmaf_score, thread_data->stream_index, frame_index);
    }
  }
  return EXIT_SUCCESS;
}

static gint
gst_vmaf_read_frame_from_queue (float *ref_data, float *dist_data,
    float *temp_data, int stride, int *frame_index, void *h)
{
  int ret;
  GstVmafThreadHelper *helper = (GstVmafThreadHelper *) h;
  GstVmaf *self = helper->gst_vmaf_p;
  GstVmafQueueElem *frames_data;
  frames_data = g_async_queue_pop (helper->frame_queue);
  if (frames_data->ref_ptr && frames_data->dist_ptr) {
    int i, j;
    float *ref_ptr = ref_data;
    float *dist_ptr = dist_data;
    for (i = 0; i < helper->frame_height; i++) {
      for (j = 0; j < helper->frame_width; j++) {
        ref_ptr[j] =
            get_data_from_ptr (frames_data->ref_ptr, i, j,
            helper->frame_width, helper->bpc);
        dist_ptr[j] =
            get_data_from_ptr (frames_data->dist_ptr, i, j,
            helper->frame_width, helper->bpc);
      }
      ref_ptr += stride / sizeof (*ref_data);
      dist_ptr += stride / sizeof (*ref_data);
    }
    *frame_index = frames_data->frame_index;
    ret = READING_SUCCESSFUL;
  } else if (frames_data->ref_ptr || frames_data->dist_ptr) {
    GST_DEBUG_OBJECT (self, "missing frame from queue ref:%p dist:%p",
        frames_data->ref_ptr, frames_data->dist_ptr);
    ret = READING_ERROR;
  } else {
    GST_DEBUG_OBJECT (self, "null frame sent, signaling EOS");
    ret = READING_EOS;
  }

  if (frames_data) {
    if (frames_data->ref_ptr)
      g_free (frames_data->ref_ptr);
    if (frames_data->dist_ptr)
      g_free (frames_data->dist_ptr);
    g_free (frames_data);
  }

  return ret;
}

static gint
gst_vmaf_close_stream (GstVmafThreadHelper * thread_data)
{
  gint err = 0;
  GstVmaf *self = thread_data->gst_vmaf_p;

  if (thread_data->thread_eos)
    return TRUE;

  thread_data->thread_eos = TRUE;

  GST_DEBUG_OBJECT (self,
      "EOS reached, flushing buffers and calculating pooled score.");

  err = vmaf_read_pictures (thread_data->vmaf_ctx, NULL, NULL, 0);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to flush VMAF context"), ("failed to flush VMAF context"));
    return EXIT_FAILURE;
  }

  err = gst_vmaf_post_pooled_score (thread_data);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to calculate pooled VMAF score"),
        ("failed to calculate pooled VMAF score"));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static gboolean
gst_vmaf_stream_thread_read_pictures (GstVmafThreadHelper * thread_data)
{
  gint stride, err = 0;
  gfloat *ref_data, *dist_data, *temp_data;
  int frame_index = 0;
  gboolean result = TRUE;
  VmafPicture pic_ref, pic_dist;
  GstVmaf *self = thread_data->gst_vmaf_p;

  //allocate local picture buffer memory
  stride = thread_data->frame_width * sizeof (float);
  ref_data = g_malloc (thread_data->frame_height * stride);
  dist_data = g_malloc (thread_data->frame_height * stride);
  temp_data = g_malloc (thread_data->frame_height * stride);
  if (!ref_data | !dist_data | !temp_data) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("problem allocating picture buffer memory"),
        ("problem allocating picture buffer memory"));
    result = FALSE;
    goto end;
  }
  //read frame of data from frame queue
  err =
      gst_vmaf_read_frame_from_queue (ref_data, dist_data, temp_data, stride,
      &frame_index, thread_data);
  if (err == READING_ERROR) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to read frame"), ("failed to read frame"));
    result = FALSE;
    goto end;
  } else if (err == READING_EOS) {
    err = gst_vmaf_close_stream (thread_data);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("failed to flush VMAF context"), ("failed to flush VMAF context"));
      result = FALSE;
    }
    goto end;
  }
  //allocate vmaf pictures
  err =
      vmaf_picture_alloc (&pic_ref, thread_data->vmaf_pix_fmt, thread_data->bpc,
      thread_data->frame_width, thread_data->frame_height);
  err |=
      vmaf_picture_alloc (&pic_dist, thread_data->vmaf_pix_fmt,
      thread_data->bpc, thread_data->frame_width, thread_data->frame_height);
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to allocate VMAF picture memory"),
        ("failed to allocate VMAF picture memory"));
    result = FALSE;
    goto end;
  }
  //fill vmaf picture buffers
  if (thread_data->bpc > 8) {
    fill_vmaf_picture_buffer_hbd (ref_data, &pic_ref, thread_data->frame_width,
        thread_data->frame_height, stride, thread_data->bpc);
    fill_vmaf_picture_buffer_hbd (dist_data, &pic_dist,
        thread_data->frame_width, thread_data->frame_height, stride,
        thread_data->bpc);
  } else {
    fill_vmaf_picture_buffer (ref_data, &pic_ref, thread_data->frame_width,
        thread_data->frame_height, stride);
    fill_vmaf_picture_buffer (dist_data, &pic_dist, thread_data->frame_width,
        thread_data->frame_height, stride);
  }

  //read pictures, run calculation
  GST_DEBUG_OBJECT (self,
      "reading images into vmaf context. ref:%p dist:%p frame:%d",
      &pic_ref, &pic_dist, frame_index);

  err =
      vmaf_read_pictures (thread_data->vmaf_ctx, &pic_ref, &pic_dist,
      frame_index);
  thread_data->last_frame_processed = frame_index;
  if (err) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to read VMAF pictures into context"),
        ("failed to read VMAF pictures into context"));
    result = FALSE;
    goto end;
  }

  err = gst_vmaf_post_frame_score (thread_data, frame_index);
  if (err) {
    goto end;
  }

end:
  if (ref_data)
    free (ref_data);
  if (dist_data)
    free (dist_data);
  if (temp_data)
    free (temp_data);
  return result;
}

static void
gst_vmaf_stream_thread_call (void *vs)
{
  GstVmafThreadHelper *helper;
  gboolean thread_is_stopped = FALSE;
  gboolean ret_status;
  if (vs == NULL)
    return;

  helper = (GstVmafThreadHelper *) vs;
  g_mutex_lock (&helper->check_thread_failure);
  thread_is_stopped = helper->thread_failure;
  g_mutex_unlock (&helper->check_thread_failure);
  if (thread_is_stopped)
    return;

  ret_status = gst_vmaf_stream_thread_read_pictures (helper);

  g_mutex_lock (&helper->check_thread_failure);
  helper->thread_failure = !ret_status;
  if (helper->thread_failure)
    GST_DEBUG_OBJECT (helper->gst_vmaf_p, "thread failure for for sink:%d",
        helper->stream_index);
  g_mutex_unlock (&helper->check_thread_failure);
}

static gboolean
vmaf_stream_thread_stop (GstTask * thread)
{
  GstTaskState task_state;
  gboolean result;
  task_state = gst_task_get_state (thread);
  if (task_state == GST_TASK_STARTED) {
    result = gst_task_stop (thread);
  } else {
    result = TRUE;
  }
  return result;
}

static gboolean
gst_vmaf_read_and_queue_frames (GstVmaf * self, GstVideoFrame * ref,
    GstVideoFrame * dist, GstBuffer * outbuf, GstVmafThreadHelper * thread_data)
{
  gboolean result;
  GstMapInfo ref_info;
  GstMapInfo dist_info;
  GstMapInfo out_info;
  gint frames_size;
  GstVmafQueueElem *frames_data;
  // Check that thread is waiting
  g_mutex_lock (&thread_data->check_thread_failure);
  if (thread_data->thread_failure) {
    vmaf_stream_thread_stop (thread_data->vmaf_thread);
    return FALSE;
  }
  g_mutex_unlock (&thread_data->check_thread_failure);
  // Run reading
  gst_buffer_map (ref->buffer, &ref_info, GST_MAP_READ);
  gst_buffer_map (dist->buffer, &dist_info, GST_MAP_READ);
  gst_buffer_map (outbuf, &out_info, GST_MAP_WRITE);

  frames_size = thread_data->frame_height * thread_data->frame_width;
  if (thread_data->bpc > 8)
    frames_size *= 2;

  frames_data = g_malloc (sizeof (GstVmafQueueElem));
  frames_data->frame_index = thread_data->frame_index;
  frames_data->ref_ptr = g_memdup (ref_info.data, frames_size);
  frames_data->dist_ptr = g_memdup (dist_info.data, frames_size);

  g_async_queue_push (thread_data->frame_queue, frames_data);

  g_mutex_lock (&thread_data->check_thread_failure);
  if (!thread_data->thread_failure) {
    gst_buffer_copy_into (outbuf, ref->buffer, GST_BUFFER_COPY_ALL, 0, -1);
    result = TRUE;
  } else {
    vmaf_stream_thread_stop (thread_data->vmaf_thread);
    result = FALSE;
  }
  g_mutex_unlock (&thread_data->check_thread_failure);
  gst_buffer_unmap (ref->buffer, &ref_info);
  gst_buffer_unmap (dist->buffer, &dist_info);
  gst_buffer_unmap (outbuf, &out_info);
  return result;
}

static GstFlowReturn
gst_vmaf_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  gboolean successful = TRUE;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFrame *cmp_frame = NULL, *ref_frame = NULL;
  GstVmaf *self = GST_VMAF (vagg);
  guint sink_index = 0, dist_stream_index = 0;
  GstVmafThreadHelper *thread_data =
      &self->helper_struct_pointer[dist_stream_index];

  GST_OBJECT_LOCK (vagg);

  g_mutex_lock (&self->finish_mutex);
  if (self->finish_threads) {
    GST_DEBUG_OBJECT (self, "plugin has been stopped, returning GST_FLOW_EOS");
    ret = GST_FLOW_EOS;
    goto done;
  }

  GST_DEBUG_OBJECT (self, "frames are prepared and ready for processing");

  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);
    thread_data = &self->helper_struct_pointer[dist_stream_index];

    if (prepared_frame != NULL) {
      //reading frame for processing 
      if (!ref_frame) {
        ref_frame = prepared_frame;
      } else {
        GST_DEBUG_OBJECT (self,
            "posting distorted frame on queue frame:%d for sink:%d ",
            thread_data->frame_index, sink_index);
        cmp_frame = prepared_frame;

        successful &=
            gst_vmaf_read_and_queue_frames (self, ref_frame, cmp_frame,
            outbuf, thread_data);
      }
    }

    if (sink_index >= 1)
      ++dist_stream_index;
    if (sink_index > 0)
      ++thread_data->frame_index;
    ++sink_index;
  }


  if (!successful)
    ret = GST_FLOW_ERROR;

done:
  g_mutex_unlock (&self->finish_mutex);

  GST_OBJECT_UNLOCK (vagg);

  return ret;
}

static void
_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_FILENAME:
      g_free (self->vmaf_config_model_filename);
      self->vmaf_config_model_filename = g_value_dup_string (value);
      break;
    case PROP_DISABLE_CLIP:
      self->vmaf_config_disable_clip = g_value_get_boolean (value);
      break;
    case PROP_DISABLE_AVX:
      self->vmaf_config_disable_avx = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_TRANSFORM:
      self->vmaf_config_enable_transform = g_value_get_boolean (value);
      break;
    case PROP_PHONE_MODEL:
      self->vmaf_config_phone_model = g_value_get_boolean (value);
      break;
    case PROP_PSNR:
      self->vmaf_config_psnr = g_value_get_boolean (value);
      break;
    case PROP_SSIM:
      self->vmaf_config_ssim = g_value_get_boolean (value);
      break;
    case PROP_MS_SSIM:
      self->vmaf_config_ms_ssim = g_value_get_boolean (value);
      break;
    case PROP_POOL_METHOD:
      gst_vmaf_set_pool_method (self, g_value_get_enum (value));
      break;
    case PROP_NUM_THREADS:
      self->vmaf_config_num_threads = g_value_get_uint (value);
      break;
    case PROP_SUBSAMPLE:
      self->vmaf_config_subsample = g_value_get_uint (value);
      break;
    case PROP_CONF_INT:
      self->vmaf_config_conf_int = g_value_get_boolean (value);
      break;
    case PROP_FRAME_MESSAGING:
      self->vmaf_config_frame_messaging = g_value_get_boolean (value);
      break;
    case PROP_VMAF_LOG_FORMAT:
      gst_vmaf_set_log_format (self, g_value_get_enum (value));
      break;
    case PROP_VMAF_LOG_FILENAME:
      g_free (self->vmaf_config_log_filename);
      self->vmaf_config_log_filename = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_FILENAME:
      g_value_set_string (value, self->vmaf_config_model_filename);
      break;
    case PROP_DISABLE_CLIP:
      g_value_set_boolean (value, self->vmaf_config_disable_clip);
      break;
    case PROP_DISABLE_AVX:
      g_value_set_boolean (value, self->vmaf_config_disable_avx);
      break;
    case PROP_ENABLE_TRANSFORM:
      g_value_set_boolean (value, self->vmaf_config_enable_transform);
      break;
    case PROP_PHONE_MODEL:
      g_value_set_boolean (value, self->vmaf_config_phone_model);
      break;
    case PROP_PSNR:
      g_value_set_boolean (value, self->vmaf_config_psnr);
      break;
    case PROP_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ssim);
      break;
    case PROP_MS_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ms_ssim);
      break;
    case PROP_POOL_METHOD:
      g_value_set_enum (value, self->vmaf_config_pool_method);
      break;
    case PROP_NUM_THREADS:
      g_value_set_uint (value, self->vmaf_config_num_threads);
      break;
    case PROP_SUBSAMPLE:
      g_value_set_uint (value, self->vmaf_config_subsample);
      break;
    case PROP_CONF_INT:
      g_value_set_boolean (value, self->vmaf_config_conf_int);
      break;
    case PROP_FRAME_MESSAGING:
      g_value_set_boolean (value, self->vmaf_config_frame_messaging);
      break;
    case PROP_VMAF_LOG_FORMAT:
      g_value_set_enum (value, self->vmaf_config_log_format);
      break;
    case PROP_VMAF_LOG_FILENAME:
      g_value_set_string (value, self->vmaf_config_log_filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_vmaf_init (GstVmaf * self)
{
  GValue tmp = G_VALUE_INIT;
  g_value_init (&tmp, G_TYPE_STRING);
  g_value_set_static_string (&tmp, DEFAULT_MODEL_FILENAME);
  self->vmaf_config_model_filename = g_value_dup_string (&tmp);
  self->vmaf_config_disable_clip = DEFAULT_DISABLE_CLIP;
  self->vmaf_config_disable_avx = DEFAULT_DISABLE_AVX;
  self->vmaf_config_enable_transform = DEFAULT_ENABLE_TRANSFORM;
  self->vmaf_config_phone_model = DEFAULT_PHONE_MODEL;
  self->vmaf_config_psnr = DEFAULT_PSNR;
  self->vmaf_config_ssim = DEFAULT_SSIM;
  self->vmaf_config_ms_ssim = DEFAULT_MS_SSIM;
  self->vmaf_config_num_threads = DEFAULT_NUM_THREADS;
  self->vmaf_config_subsample = DEFAULT_SUBSAMPLE;
  self->vmaf_config_conf_int = DEFAULT_CONF_INT;
  self->vmaf_config_pool_method = DEFAULT_POOL_METHOD;
  self->vmaf_config_frame_messaging = DEFAULT_FRAME_MESSAGING;
  self->vmaf_config_log_filename = DEFAULT_VMAF_LOG_FILENAME;
  self->vmaf_config_log_format = DEFAULT_VMAF_LOG_FORMAT;

  g_mutex_init (&self->finish_mutex);
}

static void
gst_vmaf_finalize (GObject * object)
{
  GstVmaf *self = GST_VMAF (object);
  GST_DEBUG_OBJECT (self, "finalize plugin called, freeing memory");
  g_mutex_clear (&self->finish_mutex);
  g_free (self->vmaf_config_model_filename);
  self->vmaf_config_model_filename = NULL;
  g_free (self->vmaf_config_log_filename);
  self->vmaf_config_log_filename = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vmaf_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstVmaf *self = GST_VMAF (agg);
  gint width, height, vmaf_pix_fmt, bpc;
  const gchar *format;
  GList *sinkpads_list = GST_ELEMENT (agg)->sinkpads;
  GstStructure *caps_structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (caps_structure, "height", &height);
  gst_structure_get_int (caps_structure, "width", &width);
  format = gst_structure_get_string (caps_structure, "format");
  bpc = vmaf_map_bit_depth (format);
  vmaf_pix_fmt = vmaf_map_pix_fmt (format);

  self->number_of_input_streams = g_list_length (GST_ELEMENT (agg)->sinkpads);
  --self->number_of_input_streams;      // Without reference sink
  sinkpads_list = sinkpads_list->next;  // Skip reference sink
  self->finish_threads = FALSE;
  self->helper_struct_pointer =
      g_malloc (sizeof (GstVmafThreadHelper) * self->number_of_input_streams);
  GST_DEBUG_OBJECT (self, "setting up vmaf for comparison %d streams",
      self->number_of_input_streams);

  for (guint i = 0; i < self->number_of_input_streams; ++i) {
    //set stream information
    self->helper_struct_pointer[i].gst_vmaf_p = self;
    self->helper_struct_pointer[i].stream_index = i;
    self->helper_struct_pointer[i].thread_failure = FALSE;
    self->helper_struct_pointer[i].thread_eos = FALSE;
    self->helper_struct_pointer[i].frame_height = height;
    self->helper_struct_pointer[i].frame_width = width;
    self->helper_struct_pointer[i].vmaf_pix_fmt = vmaf_pix_fmt;
    self->helper_struct_pointer[i].bpc = bpc;
    self->helper_struct_pointer[i].frame_index = 0;
    self->helper_struct_pointer[i].last_frame_processed = 0;
    self->helper_struct_pointer[i].padname =
        gst_pad_get_name (sinkpads_list->data);
    sinkpads_list = sinkpads_list->next;

    //initialize a vmaf context for each input stream 
    gst_vmaf_models_create (self, &self->helper_struct_pointer[i]);

    //initialize frame queue and vmaf thread
    self->helper_struct_pointer[i].frame_queue = g_async_queue_new ();

    g_mutex_init (&self->helper_struct_pointer[i].check_thread_failure);
    g_rec_mutex_init (&self->helper_struct_pointer[i].vmaf_thread_mutex);

    self->helper_struct_pointer[i].vmaf_thread =
        gst_task_new (gst_vmaf_stream_thread_call,
        (void *) &self->helper_struct_pointer[i], NULL);

    gst_task_set_lock (self->helper_struct_pointer[i].vmaf_thread,
        &self->helper_struct_pointer[i].vmaf_thread_mutex);
    gst_task_start (self->helper_struct_pointer[i].vmaf_thread);
  }

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static void
gst_vmaf_stop_plugin (GstAggregator * aggregator)
{
  gint q_length = 0;
  GstVmafThreadHelper *helper = NULL;
  GstVmafQueueElem *frames_data;
  GstVmaf *self = GST_VMAF (aggregator);
  GST_DEBUG_OBJECT (self, "stopping vmaf plugin and flushing queues");

  g_mutex_lock (&self->finish_mutex);
  if (!self->finish_threads) {
    for (int i = 0; i < self->number_of_input_streams; ++i) {
      helper = &self->helper_struct_pointer[i];

      q_length = g_async_queue_length (helper->frame_queue);
      GST_DEBUG_OBJECT (self,
          "flushing frame queue of length:%d frames processed:%d", q_length,
          helper->last_frame_processed);
      for (gint i = 0; i < q_length; ++i) {
        frames_data = g_async_queue_try_pop (helper->frame_queue);
        if (frames_data) {
          g_free (frames_data->ref_ptr);
          g_free (frames_data->dist_ptr);
          g_free (frames_data);
        }
      }

      GST_DEBUG_OBJECT (self,
          "posting null frame on queue, to signal task of stop");
      frames_data = g_malloc (sizeof (GstVmafQueueElem));
      frames_data->ref_ptr = NULL;
      frames_data->dist_ptr = NULL;
      g_async_queue_push (helper->frame_queue, frames_data);

      GST_DEBUG_OBJECT (self,
          "cleaning up vmaf objects, attempting to join thread %p",
          helper->vmaf_thread);
      gst_task_join (helper->vmaf_thread);
      gst_vmaf_close_stream (helper);
      gst_vmaf_models_destroy (helper);
      g_free (helper->padname);
      gst_object_unref (helper->vmaf_thread);
      g_rec_mutex_clear (&helper->vmaf_thread_mutex);
      g_mutex_clear (&helper->check_thread_failure);
      g_async_queue_unref (helper->frame_queue);
    }
    g_free (self->helper_struct_pointer);
    self->finish_threads = TRUE;
  }
  g_mutex_unlock (&self->finish_mutex);
}

static gboolean
gst_vmaf_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * aggregator_pad, GstEvent * event)
{
  GstVmaf *self = GST_VMAF (aggregator);
  GST_DEBUG_OBJECT (self, "sink event fired %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    gst_vmaf_stop_plugin (aggregator);
    GST_DEBUG_OBJECT (self, "plugin stopped through EOS event");
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (aggregator,
      aggregator_pad, event);
}

static gboolean
gst_vmaf_stop (GstAggregator * aggregator)
{
  gst_vmaf_stop_plugin (aggregator);
  return TRUE;
}

static void
gst_vmaf_class_init (GstVmafClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *aggregator_class = (GstAggregatorClass *) klass;

  videoaggregator_class->aggregate_frames = gst_vmaf_aggregate_frames;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sink_factory, GST_TYPE_VIDEO_AGGREGATOR_CONVERT_PAD);

  aggregator_class->sink_event = gst_vmaf_sink_event;
  aggregator_class->negotiated_src_caps = gst_vmaf_negotiated_src_caps;
  aggregator_class->stop = gst_vmaf_stop;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vmaf_finalize);

  g_object_class_install_property (gobject_class, PROP_MODEL_FILENAME,
      g_param_spec_string ("model-filename",
          "model-filename",
          "Model *.pkl abs filename, or file version for built in models",
          DEFAULT_MODEL_FILENAME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISABLE_CLIP,
      g_param_spec_boolean ("disable-clip",
          "disable-clip",
          "Disable clipping VMAF values",
          DEFAULT_DISABLE_CLIP, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISABLE_AVX,
      g_param_spec_boolean ("disable-avx",
          "disable-avx",
          "Disable AVX intrinsics using",
          DEFAULT_DISABLE_AVX, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENABLE_TRANSFORM,
      g_param_spec_boolean ("enable-transform",
          "enable-transform",
          "Enable transform VMAF scores",
          DEFAULT_ENABLE_TRANSFORM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PHONE_MODEL,
      g_param_spec_boolean ("phone-model",
          "phone-model",
          "Use VMAF phone model", DEFAULT_PHONE_MODEL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PSNR,
      g_param_spec_boolean ("psnr", "psnr",
          "Estimate PSNR", DEFAULT_PSNR, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SSIM,
      g_param_spec_boolean ("ssim", "ssim",
          "Estimate SSIM", DEFAULT_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MS_SSIM,
      g_param_spec_boolean ("ms-ssim", "ms-ssim",
          "Estimate MS-SSIM", DEFAULT_MS_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_POOL_METHOD,
      g_param_spec_enum ("pool-method", "pool-method",
          "Pool method for mean", GST_TYPE_VMAF_POOL_METHOD,
          DEFAULT_POOL_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NUM_THREADS,
      g_param_spec_uint ("threads", "threads",
          "The number of threads",
          0, 32, DEFAULT_NUM_THREADS, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SUBSAMPLE,
      g_param_spec_uint ("subsample",
          "subsample",
          "Computing on one of every N frames",
          1, 128, DEFAULT_SUBSAMPLE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CONF_INT,
      g_param_spec_boolean ("conf-interval",
          "conf-interval",
          "Enable confidence intervals", DEFAULT_CONF_INT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FRAME_MESSAGING,
      g_param_spec_boolean ("frame-message",
          "frame-message",
          "Enable frame level score messaging", DEFAULT_FRAME_MESSAGING,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VMAF_LOG_FILENAME,
      g_param_spec_string ("log-filename",
          "log-filename",
          "VMAF log filename for scores",
          DEFAULT_VMAF_LOG_FILENAME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VMAF_LOG_FORMAT,
      g_param_spec_enum ("log-format", "log-format",
          "VMAF log file format used for scores (csv, xml, json)",
          GST_TYPE_VMAF_LOG_FORMATS, DEFAULT_VMAF_LOG_FORMAT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class, "vmaf",
      "Filter/Analyzer/Video",
      "Provides Video Multi-Method Assessment Fusion metric",
      "Casey Bateman <casey.bateman@hudl.com>");
  GST_DEBUG_CATEGORY_INIT (gst_vmaf_debug, "vmaf", 0, "vmaf");
}

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "vmaf.h"
#include "vmafmap.h"

GST_DEBUG_CATEGORY_STATIC (gst_vmaf_debug);
#define GST_CAT_DEFAULT gst_vmaf_debug
//TODO: we should expand this to support NV12 as well 
#define SINK_FORMATS " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define SRC_FORMAT " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define DEFAULT_MODEL_FILENAME   "vmaf_v0.6.1.pkl"
#define DEFAULT_DISABLE_CLIP     FALSE
#define DEFAULT_DISABLE_AVX      FALSE
#define DEFAULT_ENABLE_TRANSFORM FALSE
#define DEFAULT_PHONE_MODEL      FALSE
#define DEFAULT_PSNR             FALSE
#define DEFAULT_SSIM             FALSE
#define DEFAULT_MS_SSIM          FALSE
#define DEFAULT_NUM_THREADS      0
#define DEFAULT_SUBSAMPLE        1
#define DEFAULT_CONF_INT         FALSE
#define DEFAULT_VMAF_LOG_LEVEL   VMAF_LOG_LEVEL_NONE
#define GST_TYPE_VMAF_POOL_METHOD (gst_vmaf_pool_method_get_type ())

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT)));

enum
{
  PROP_0,
  PROP_MODEL_FILENAME,
  PROP_DISABLE_CLIP,
  PROP_DISABLE_AVX,
  PROP_ENABLE_TRANSFORM,
  PROP_PHONE_MODEL,
  PROP_PSNR,
  PROP_SSIM,
  PROP_MS_SSIM,
  PROP_NUM_THREADS,
  PROP_SUBSAMPLE,
  PROP_CONF_INT,
  PROP_LAST,
};

enum VMAFReadFuncRetCodes
{
  VMAF_SUCCESSFUL_READING = 0,
  VMAF_READING_FAILED = 2
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS)));

#define gst_vmaf_parent_class parent_class
G_DEFINE_TYPE (GstVmaf, gst_vmaf, GST_TYPE_VIDEO_AGGREGATOR);

static void
copy_data (float *src, VmafPicture * dst, unsigned width, unsigned height,
    int src_stride)
{
  float *a = src;
  uint8_t *b = dst->data[0];
  for (unsigned i = 0; i < height; i++) {
    for (unsigned j = 0; j < width; j++) {
      b[j] = a[j];
    }
    a += src_stride / sizeof (float);
    b += dst->stride[0];
  }
}

static void
copy_data_hbd (float *src, VmafPicture * dst, unsigned width, unsigned height,
    int src_stride, unsigned bpc)
{
  float *a = src;
  uint16_t *b = dst->data[0];
  for (unsigned i = 0; i < height; i++) {
    for (unsigned j = 0; j < width; j++) {
      b[j] = a[j] * (1 << (bpc - 8));
    }
    a += src_stride / sizeof (float);
    b += dst->stride[0] / sizeof (uint16_t);
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
static int
read_frame (float *ref_data, float *dist_data, float *temp_data, int stride,
    void *h)
{
  int ret;
  GstVmafThreadHelper *helper = (GstVmafThreadHelper *) h;
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
    ret = VMAF_SUCCESSFUL_READING;
    ++helper->frame_index;
  } else {
    ret = VMAF_READING_FAILED;
  }
  if (frames_data) {
    g_free (frames_data->ref_ptr);
    g_free (frames_data->dist_ptr);
  }
  g_free (frames_data);
  return ret;
}

static void
gst_vmaf_models_destroy (GstVmafThreadHelper * thread_data)
{
  vmaf_close (thread_data->vmaf_ctx);
  vmaf_model_destroy (thread_data->vmaf_model);
  vmaf_model_collection_destroy (thread_data->vmaf_model_collection);
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
    .n_threads = self->num_threads,
    .n_subsample = self->subsample,
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
    err =
        vmaf_model_collection_load_from_path (&thread_data->vmaf_model,
        &thread_data->vmaf_model_collection, &model_cfg, self->model_filename);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf model file from path: %s",
              self->model_filename),
          ("Failed to load vmaf model file from path: %s",
              self->model_filename));
      result = FALSE;
      goto free_data;
    }

    err =
        vmaf_use_features_from_model_collection (thread_data->vmaf_ctx,
        thread_data->vmaf_model_collection);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf feature extractors from model file: %s",
              self->model_filename),
          ("Failed to load vmaf feature extractors from model file: %s",
              self->model_filename));
      result = FALSE;
      goto free_data;
    }
  } else {
    err =
        vmaf_model_load_from_path (&thread_data->vmaf_model, &model_cfg,
        self->model_filename);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf model file from path: %s",
              self->model_filename),
          ("Failed to load vmaf model file from path: %s",
              self->model_filename));
      result = FALSE;
      goto free_data;
    }

    err =
        vmaf_use_features_from_model (thread_data->vmaf_ctx,
        thread_data->vmaf_model);
    if (err) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Failed to load vmaf feature extractors from model file: %s",
              self->model_filename),
          ("Failed to load vmaf feature extractors from model file: %s",
              self->model_filename));
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


static gboolean
vmaf_stream_thread_read_pictures (GstVmafThreadHelper * thread_data)
{
  gint err = 0;
  gboolean result = TRUE;
  VmafPicture pic_ref, pic_dist;

  //allocate local picture buffer memory
  gint stride = thread_data->frame_width * sizeof (float);
  gfloat *ref_data = g_malloc (thread_data->frame_height * stride);
  gfloat *dist_data = g_malloc (thread_data->frame_height * stride);
  gfloat *temp_data = g_malloc (thread_data->frame_height * stride);
  if (!ref_data | !dist_data | !temp_data) {
    GST_ELEMENT_ERROR (thread_data->gst_vmaf_p, RESOURCE, FAILED,
        ("Problem allocating picture buffer memory"),
        ("Problem allocating picture buffer memory"));
    result = FALSE;
    goto end;
  }
  //read frame of data from frame queue
  err = read_frame (ref_data, dist_data, temp_data, stride, thread_data);
  if (err == 1) {
    GST_ELEMENT_ERROR (thread_data->gst_vmaf_p, RESOURCE, FAILED,
        ("Failed to read frame"), ("Failed to read frame"));
    result = FALSE;
    goto end;
  } else if (err == 2) {
    //this signals an end of file
    err = vmaf_read_pictures (thread_data->vmaf_ctx, NULL, NULL, 0);
    if (err) {
      GST_ELEMENT_ERROR (thread_data->gst_vmaf_p, RESOURCE, FAILED,
          ("Failed to flush VMAF context"), ("Failed to flush VMAF context"));
      result = FALSE;
      goto end;
    }
  }
  //allocate vmaf pictures
  err =
      vmaf_picture_alloc (&pic_ref, thread_data->vmaf_pix_fmt, thread_data->bpc,
      thread_data->frame_width, thread_data->frame_height);
  err |=
      vmaf_picture_alloc (&pic_dist, thread_data->vmaf_pix_fmt,
      thread_data->bpc, thread_data->frame_width, thread_data->frame_height);
  if (err) {
    GST_ELEMENT_ERROR (thread_data->gst_vmaf_p, RESOURCE, FAILED,
        ("Failed to allocate VMAF picture memory"),
        ("Failed to allocate VMAF picture memory"));
    result = FALSE;
    goto end;
  }
  //fill vmaf picture buffers
  if (thread_data->bpc > 8) {
    copy_data_hbd (ref_data, &pic_ref, thread_data->frame_width,
        thread_data->frame_height, stride, thread_data->bpc);
    copy_data_hbd (dist_data, &pic_dist, thread_data->frame_width,
        thread_data->frame_height, stride, thread_data->bpc);
  } else {
    copy_data (ref_data, &pic_ref, thread_data->frame_width,
        thread_data->frame_height, stride);
    copy_data (dist_data, &pic_dist, thread_data->frame_width,
        thread_data->frame_height, stride);
  }

  //read pictures, run calculation
  err =
      vmaf_read_pictures (thread_data->vmaf_ctx, &pic_ref, &pic_dist,
      thread_data->frame_index);
  if (err) {
    GST_ELEMENT_ERROR (thread_data->gst_vmaf_p, RESOURCE, FAILED,
        ("Failed to allocate VMAF picture memory"),
        ("Failed to allocate VMAF picture memory"));
    result = FALSE;
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
vmaf_stream_thread_call (void *vs)
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

  ret_status = vmaf_stream_thread_read_pictures (helper);

  g_mutex_lock (&helper->check_thread_failure);
  helper->thread_failure = !ret_status;
  g_mutex_unlock (&helper->check_thread_failure);
  if (helper->error_msg)
    GST_ELEMENT_ERROR (helper->gst_vmaf_p, RESOURCE, FAILED,
        ("Launhing LibVMAF error: %s", helper->error_msg),
        ("Launhing LibVMAF error: %s", helper->error_msg));
}

static gboolean
try_thread_stop (GstTask * thread)
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
    try_thread_stop (thread_data->vmaf_thread);
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
    gint i;
    result = TRUE;
    for (i = 0; i < ref_info.size; i++) {
      out_info.data[i] = ref_info.data[i];
    }
  } else {
    try_thread_stop (thread_data->vmaf_thread);
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
  GstVideoFrame *ref_frame = NULL;
  GstVmaf *self = GST_VMAF (vagg);
  gboolean res = TRUE;
  guint stream_index = 0;


  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);

    if (prepared_frame != NULL) {
      if (!ref_frame) {
        //this means this is the source frame, in other words, the first sink for the vmaf filter.
        ref_frame = prepared_frame;
      } else {
        GstVmafThreadHelper *thread_data =
            &self->helper_struct_pointer[stream_index];
        GstVideoFrame *cmp_frame = prepared_frame;

        res &=
            gst_vmaf_read_and_queue_frames (self, ref_frame, cmp_frame,
            outbuf, thread_data);

        ++thread_data->frame_index;
        ++stream_index;
      }
    }
  }
  if (!res)
    goto failed;
  GST_OBJECT_UNLOCK (vagg);

  return GST_FLOW_OK;

failed:
  GST_OBJECT_UNLOCK (vagg);

  return GST_FLOW_ERROR;
}


static void
_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_FILENAME:
      g_free (self->model_filename);
      self->model_filename = g_value_dup_string (value);
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
    case PROP_NUM_THREADS:
      self->num_threads = g_value_get_uint (value);
      break;
    case PROP_SUBSAMPLE:
      self->subsample = g_value_get_uint (value);
      break;
    case PROP_CONF_INT:
      self->vmaf_config_conf_int = g_value_get_boolean (value);
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
      g_value_set_string (value, self->model_filename);
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
    case PROP_NUM_THREADS:
      g_value_set_uint (value, self->num_threads);
      break;
    case PROP_SUBSAMPLE:
      g_value_set_uint (value, self->subsample);
      break;
    case PROP_CONF_INT:
      g_value_set_boolean (value, self->vmaf_config_conf_int);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

/* GObject boilerplate */

static void
gst_vmaf_init (GstVmaf * self)
{
  GValue tmp = G_VALUE_INIT;
  g_value_init (&tmp, G_TYPE_STRING);
  g_value_set_static_string (&tmp, DEFAULT_MODEL_FILENAME);
  self->model_filename = g_value_dup_string (&tmp);
  self->vmaf_config_disable_clip = DEFAULT_DISABLE_CLIP;
  self->vmaf_config_disable_avx = DEFAULT_DISABLE_AVX;
  self->vmaf_config_enable_transform = DEFAULT_ENABLE_TRANSFORM;
  self->vmaf_config_phone_model = DEFAULT_PHONE_MODEL;
  self->vmaf_config_psnr = DEFAULT_PSNR;
  self->vmaf_config_ssim = DEFAULT_SSIM;
  self->vmaf_config_ms_ssim = DEFAULT_MS_SSIM;
  self->num_threads = DEFAULT_NUM_THREADS;
  self->subsample = DEFAULT_SUBSAMPLE;
  self->vmaf_config_conf_int = DEFAULT_CONF_INT;

  g_mutex_init (&self->finish_mutex);
}

static void
gst_vmaf_finalize (GObject * object)
{
  GstVmaf *self = GST_VMAF (object);

  for (guint i = 0; i < self->number_of_input_streams; ++i) {
    gst_vmaf_models_destroy (&self->helper_struct_pointer[i]);
  }

  g_mutex_clear (&self->finish_mutex);

  g_free (self->model_filename);
  self->model_filename = NULL;

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
  --self->number_of_input_streams;      // Without reference
  sinkpads_list = sinkpads_list->next;  // Skip reference
  self->finish_threads = FALSE;
  self->helper_struct_pointer =
      g_malloc (sizeof (GstVmafThreadHelper) * self->number_of_input_streams);
  for (guint i = 0; i < self->number_of_input_streams; ++i) {
    //set stream information
    self->helper_struct_pointer[i].gst_vmaf_p = self;
    self->helper_struct_pointer[i].thread_failure = FALSE;
    self->helper_struct_pointer[i].frame_height = height;
    self->helper_struct_pointer[i].frame_width = width;
    self->helper_struct_pointer[i].vmaf_pix_fmt = vmaf_pix_fmt;
    self->helper_struct_pointer[i].bpc = bpc;
    self->helper_struct_pointer[i].error_msg = NULL;
    self->helper_struct_pointer[i].frame_index = 0;
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
        gst_task_new (vmaf_stream_thread_call,
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
  GstVmafThreadHelper *helper = NULL;
  GstVmaf *self = GST_VMAF (aggregator);
  g_mutex_lock (&self->finish_mutex);
  if (!self->finish_threads) {
    for (int i = 0; i < self->number_of_input_streams; ++i) {
      gboolean thread_failure;
      helper = &self->helper_struct_pointer[i];
      g_mutex_lock (&helper->check_thread_failure);
      thread_failure = helper->thread_failure;
      g_mutex_unlock (&helper->check_thread_failure);
      if (thread_failure) {
        gint q_length = g_async_queue_length (helper->frame_queue);
        for (gint i = 0; i < q_length; ++i) {
          GstVmafQueueElem *frames_data;
          frames_data = g_async_queue_pop (helper->frame_queue);
          if (frames_data) {
            g_free (frames_data->ref_ptr);
            g_free (frames_data->dist_ptr);
            g_free (frames_data);
          }
        }
      } else {
        GstVmafQueueElem *frames_data;
        frames_data = g_malloc (sizeof (GstVmafQueueElem));
        frames_data->ref_ptr = NULL;
        frames_data->dist_ptr = NULL;
        g_async_queue_push (helper->frame_queue, frames_data);
      }
    }
    for (int i = 0; i < self->number_of_input_streams; ++i) {
      helper = &self->helper_struct_pointer[i];
      gst_task_join (helper->vmaf_thread);
      g_free (helper->error_msg);
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
  //destroy the vmaf models and context
  if (helper)
    gst_vmaf_models_destroy (helper);
}

static gboolean
gst_vmaf_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * aggregator_pad, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    gst_vmaf_stop_plugin (aggregator);
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
          "Model *.pkl abs filename",
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

  gst_element_class_set_static_metadata (gstelement_class, "vmaf",
      "Filter/Analyzer/Video",
      "Provides Video Multi-Method Assessment Fusion metric",
      "Casey Bateman <casey.bateman@hudl.com>");
  GST_DEBUG_CATEGORY_INIT (gst_vmaf_debug, "vmaf", 0, "vmaf");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean result =
      gst_element_register (plugin, "vmaf", GST_RANK_PRIMARY, GST_TYPE_VMAF);
  return result;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vmaf,
    "Netflix VMAF quality metric plugin",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

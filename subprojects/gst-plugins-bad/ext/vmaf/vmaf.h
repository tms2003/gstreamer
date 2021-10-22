/* VMAF quality assessment plugin
 */

#ifndef __GST_VMAF_H__
#define __GST_VMAF_H__

#include <libvmaf.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

G_BEGIN_DECLS
#define GST_TYPE_VMAF (gst_vmaf_get_type())
#define GST_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMAF, GstVmaf))
#define GST_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VMAF, GstVmafClass))
#define GST_IS_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VMAF))
#define GST_IS_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VMAF))
typedef struct _GstVmaf GstVmaf;
typedef struct _GstVmafClass GstVmafClass;

typedef enum _GstVmafLogFmtEnum
{
  JSON_LOG_FMT = 0,
  //XML_LOG_FMT = 1,
} GstVmafLogFmtEnum;

typedef enum _GstVmafPoolMethodEnum
{
  MIN_POOL_METHOD = 0,
  MEAN_POOL_METHOD = 1,
  HARMONIC_MEAN_POOL_METHOD = 2
} GstVmafPoolMethodEnum;

typedef struct
{
  void *ref_ptr;
  void *dist_ptr;
  gint frame_index;
} GstVmafQueueElem;

typedef struct
{
  GstVmaf *gst_vmaf_p;
  GstTask *vmaf_thread;
  GRecMutex vmaf_thread_mutex;
  GAsyncQueue *frame_queue;
  GMutex check_thread_failure;
  gboolean thread_failure;
  gchar *error_msg;
  gint vmaf_pix_fmt;
  gint frame_height;
  gint frame_width;
  gint frame_index;
  gint bpc;
  gchar *padname;
  VmafContext *vmaf_ctx;
  VmafModel *vmaf_model;
  VmafModelCollection *vmaf_model_collection;
} GstVmafThreadHelper;

/**
 * GstIqaVmaf:
 *
 * The opaque #GstIqaVmaf structure.
 */
struct _GstVmaf
{
  GstVideoAggregator videoaggregator;
  // VMAF settings from cmd
  gchar *model_filename;
  gboolean vmaf_config_disable_clip;
  gboolean vmaf_config_disable_avx;
  gboolean vmaf_config_enable_transform;
  gboolean vmaf_config_phone_model;
  gboolean vmaf_config_psnr;
  gboolean vmaf_config_ssim;
  gboolean vmaf_config_ms_ssim;
  GstVmafPoolMethodEnum pool_method;
  guint num_threads;
  guint subsample;
  gboolean vmaf_config_conf_int;
  // Thread helpers
  GstVmafThreadHelper *helper_struct_pointer;
  gint number_of_input_streams;

  gboolean finish_threads;
  GMutex finish_mutex;
};

struct _GstVmafClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_vmaf_get_type (void);

G_END_DECLS
#endif /* __GST_VMAF_H__ */

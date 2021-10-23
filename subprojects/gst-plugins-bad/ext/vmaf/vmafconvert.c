#include "vmafconvert.h"
#include "vmafenums.h"

gint
vmaf_map_pix_fmt (gchar * fmt)
{
  if (fmt) {
    if (!strcmp (fmt, "I420"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "NV12"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "YV12"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "Y42B"))
      return VMAF_PIX_FMT_YUV422P;
    if (!strcmp (fmt, "Y444"))
      return VMAF_PIX_FMT_YUV444P;
    if (!strcmp (fmt, "I420_10LE"))
      return VMAF_PIX_FMT_YUV420P;
    if (!strcmp (fmt, "I422_10LE"))
      return VMAF_PIX_FMT_YUV422P;
    if (!strcmp (fmt, "Y444_10LE"))
      return VMAF_PIX_FMT_YUV444P;
  }
  return VMAF_PIX_FMT_UNKNOWN;
}

gint
vmaf_map_bit_depth (gchar * fmt)
{
  if (fmt) {
    if (!strcmp (fmt, "I420_10LE"))
      return 10;
    if (!strcmp (fmt, "I422_10LE"))
      return 10;
    if (!strcmp (fmt, "Y444_10LE"))
      return 10;
  }
  return 8;
}

enum VmafPoolingMethod
vmaf_map_pooling_method (GstVmafPoolMethodEnum pool_method)
{
  if (pool_method == POOL_METHOD_MAX)
    return VMAF_POOL_METHOD_MAX;
  if (pool_method == POOL_METHOD_MIN)
    return VMAF_POOL_METHOD_MIN;
  if (pool_method == POOL_METHOD_MEAN)
    return VMAF_POOL_METHOD_MEAN;
  if (pool_method == POOL_METHOD_HARMONIC_MEAN)
    return VMAF_POOL_METHOD_HARMONIC_MEAN;
  return VMAF_POOL_METHOD_UNKNOWN;
}

void
fill_vmaf_picture_buffer (float *src, VmafPicture * dst, unsigned width,
    unsigned height, int src_stride)
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

void
fill_vmaf_picture_buffer_hbd (float *src, VmafPicture * dst, unsigned width,
    unsigned height, int src_stride, unsigned bpc)
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

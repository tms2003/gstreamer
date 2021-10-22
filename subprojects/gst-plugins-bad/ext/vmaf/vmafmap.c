#include "vmafmap.h"

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

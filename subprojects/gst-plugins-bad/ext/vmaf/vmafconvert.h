#ifndef __GST_VMAFMAP_H__
#define __GST_VMAFMAP_H__

#include <libvmaf.h>
#include <gst/gst.h>
#include "vmafenums.h"

G_BEGIN_DECLS 
enum VmafOutputFormat vmaf_map_log_fmt (GstVmafLogFormats log_fmt);
gint vmaf_map_pix_fmt (char *fmt);
gint vmaf_map_bit_depth (char *fmt);
enum VmafPoolingMethod vmaf_map_pooling_method(GstVmafPoolMethodEnum pool_method);
void fill_vmaf_picture_buffer (float *src, VmafPicture * dst, unsigned width, unsigned height, int src_stride);
void fill_vmaf_picture_buffer_hbd (float *src, VmafPicture * dst, unsigned width, unsigned height, int src_stride, unsigned bpc);

G_END_DECLS
#endif /* __GST_VMAFMAP_H__ */

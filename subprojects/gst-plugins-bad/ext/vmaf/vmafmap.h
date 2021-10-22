#ifndef __GST_VMAFMAP_H__
#define __GST_VMAFMAP_H__

#include <libvmaf.h>
#include <gst/gst.h>

G_BEGIN_DECLS gint vmaf_map_pix_fmt (char *fmt);
gint vmaf_map_bit_depth (char *fmt);

G_END_DECLS
#endif /* __GST_VMAFMAP_H__ */

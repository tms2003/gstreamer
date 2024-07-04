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

#ifndef __GST_QOIENC_H__
#define __GST_QOIENC_H__

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_QOIENC (gst_qoienc_get_type())
G_DECLARE_FINAL_TYPE (GstQoiEnc, gst_qoienc, GST, QOIENC, GstVideoEncoder)

GST_ELEMENT_REGISTER_DECLARE (qoienc);

G_END_DECLS

#endif /* __GST_QOIENC_H__ */
/* GStreamer
 * Copyright (C) 2022 GStreamer developers
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

#ifndef __GST_MEM_INDEX_H__
#define __GST_MEM_INDEX_H__

#include <gst/gstconfig.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_MEM_INDEX                (gst_mem_index_get_type ())
#define GST_IS_MEM_INDEX(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEM_INDEX))
#define GST_IS_MEM_INDEX_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEM_INDEX))
#define GST_MEM_INDEX_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MEM_INDEX, GstMemIndexClass))
#define GST_MEM_INDEX(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEM_INDEX, GstMemIndex))
#define GST_MEM_INDEX_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEM_INDEX, GstMemIndexClass))

typedef struct _GstMemIndex GstMemIndex;
typedef struct _GstMemIndexClass GstMemIndexClass;
typedef struct _GstMemIndexPrivate GstMemIndexPrivate;

GST_API
GType		gst_mem_index_get_type		(void);

GST_API
GstMemIndex * gst_mem_index_new (void);

GST_API
GVariant * gst_mem_index_to_variant (GstMemIndex *index);

GST_API
GstMemIndex * gst_mem_index_new_from_variant (GVariant *variant);

G_END_DECLS

#endif /* __GST_MEM_INDEX_H__ */

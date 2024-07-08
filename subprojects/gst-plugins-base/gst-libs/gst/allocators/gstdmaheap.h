/* GStreamer dmaheap allocator
 *
 * Copyright (C) 2024 Mediatek
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for mordetails.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DMAHEAP_H__
#define __GST_DMAHEAP_H__

#include <gst/gst.h>
#include <gst/allocators/gstfdmemory.h>

G_BEGIN_DECLS

#define GST_ALLOCATOR_DMAHEAP "dmaheap"

#define GST_TYPE_DMAHEAP_ALLOCATOR              (gst_dmaheap_allocator_get_type())
#define GST_IS_DMAHEAP_ALLOCATOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DMAHEAP_ALLOCATOR))
#define GST_IS_DMAHEAP_ALLOCATOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DMAHEAP_ALLOCATOR))
#define GST_DMAHEAP_ALLOCATOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DMAHEAP_ALLOCATOR, GstDmaHeapAllocatorClass))
#define GST_DMAHEAP_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DMAHEAP_ALLOCATOR, GstDmaHeapAllocator))
#define GST_DMAHEAP_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DMAHEAP_ALLOCATOR, GstDmaHeapAllocatorClass))
#define GST_DMAHEAP_ALLOCATOR_CAST(obj)         ((GstDmaHeapAllocator *)(obj))

typedef struct _GstDmaHeapAllocator GstDmaHeapAllocator;
typedef struct _GstDmaHeapAllocatorClass GstDmaHeapAllocatorClass;

/**
 * GstDmaHeapAllocator:
 *
 * Base class for allocators with dmaheap-backed memory
 *
 * Since: 1.12
 */
struct _GstDmaHeapAllocator
{
  GstDmaBufAllocator parent;

  int device_fd;
  guint32 fd_flags;
  guint64 heap_flags;

  gboolean contiguous_memory;

  GstMemoryMapFullFunction   parent_mem_map_full;
  GstMemoryUnmapFullFunction parent_mem_unmap_full;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstDmaHeapAllocatorClass
{
  GstDmaBufAllocatorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};


GST_ALLOCATORS_API
GType          gst_dmaheap_allocator_get_type (void);

GST_ALLOCATORS_API
GstAllocator * gst_dmaheap_allocator_new (char* device,
    guint32 fd_flags, guint64 heap_flags);

GST_ALLOCATORS_API
gboolean       gst_is_dmaheap_memory (GstMemory * mem);


G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDmaHeapAllocator, gst_object_unref)

G_END_DECLS
#endif /* __GST_DMAHEAP_H__ */

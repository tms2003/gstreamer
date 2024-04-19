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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdmaheap.h"

/**
 * SECTION:gstdmaheap
 * @title: GstDmaHeapAllocator
 * @short_description: Memory wrapper for Linux dmaheap memory
 * @see_also: #GstMemory
 *
 */
#ifdef HAVE_LINUX_DMA_HEAP_H
#include <sys/ioctl.h>
#include <linux/dma-heap.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <inttypes.h>
#endif

GST_DEBUG_CATEGORY_STATIC (dmaheap_debug);
#define GST_CAT_DEFAULT dmaheap_debug

#define _do_init                                        \
    GST_DEBUG_CATEGORY_INIT (dmaheap_debug,              \
    "dmaheap", 0, "dmaheap memory");

#define parent_class gst_dmaheap_allocator_parent_class

G_DEFINE_TYPE_WITH_CODE (GstDmaHeapAllocator, gst_dmaheap_allocator,
    GST_TYPE_DMABUF_ALLOCATOR, _do_init);

/**
 * gst_dmaheap_allocator_new:
 *
 * Return a new dmaheap allocator.
 *
 * @device: device path to open
 * @fd_flags: file descriptor flags when alloc buffer, such as O_RDWR | O_CLOEXEC
 * @heap_flags: heap flags when alloc buffer, such as DMA_HEAP_VALID_HEAP_FLAGS
 *
 * Returns: (transfer full): a new dmaheap allocator, or NULL if the allocator
 *    isn't available. Use gst_object_unref() to release the allocator after
 *    usage
 */
GstAllocator *
gst_dmaheap_allocator_new (char *device, guint32 fd_flags, guint64 heap_flags)
{
  int device_fd = -1;
  GstAllocator *alloc = NULL;
  GstDmaHeapAllocator *dmaheap_alloc = NULL;

#ifdef HAVE_LINUX_DMA_HEAP_H
  g_return_val_if_fail (device != NULL, NULL);
  g_return_val_if_fail (g_file_test (device, G_FILE_TEST_EXISTS), NULL);

  /* try to open device */
  device_fd = open (device, O_RDONLY | O_CLOEXEC);
  if (device_fd < 0) {
    GST_ERROR ("Failed to open dma heap device %s", device);
    return NULL;
  }

  /* new allocator instance */
  dmaheap_alloc = g_object_new (GST_TYPE_DMAHEAP_ALLOCATOR, NULL);
  gst_object_ref_sink (dmaheap_alloc);

  dmaheap_alloc->device_fd = device_fd;
  dmaheap_alloc->fd_flags = fd_flags;
  dmaheap_alloc->heap_flags = heap_flags;

  /* some memory are required to be contiguous */
  dmaheap_alloc->contiguous_memory =
      !g_strcmp0 (device, "/dev/dma_heap/linux,cma");

  GST_LOG_OBJECT (dmaheap_alloc,
      "Creating dma heap allocator %p, dmaheap fd: %d device: %s",
      dmaheap_alloc, dmaheap_alloc->device_fd, device);

  return GST_ALLOCATOR (dmaheap_alloc);
#else
  return NULL;
#endif
}

static void
gst_dmaheap_allocator_finalize (GObject * object)
{
  GstDmaHeapAllocator *dmaheap_alloc = GST_DMAHEAP_ALLOCATOR (object);

  if (dmaheap_alloc->device_fd >= 0) {
    GST_LOG_OBJECT (dmaheap_alloc,
        "Close dmaheap fd %d", dmaheap_alloc->device_fd);

    close (dmaheap_alloc->device_fd);
    dmaheap_alloc->device_fd = -1;
  }

  GST_LOG_OBJECT (dmaheap_alloc,
      "Finalizing DMA heap allocator %p", dmaheap_alloc);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_dmaheap_allocator_alloc:
 * @allocator: allocator to be used for this memory
 * @size: memory size
 * @params: allocator params
 *
 * Return a %GstMemory that wraps a dmaheap file descriptor.
 *
 * Returns: (transfer full): a GstMemory based on @allocator.
 * When the buffer will be released dmaheap allocator will close the @fd.
 * The memory is only mmapped on gst_buffer_map() request.
 */
static GstMemory *
gst_dmaheap_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstDmaHeapAllocator *dmaheap_alloc = GST_DMAHEAP_ALLOCATOR (allocator);
  gsize align = 0;
  gsize maxsize = 0;
  gsize prefix = 0;
  gsize padding = 0;
  GstMemory *new_mem = NULL;

#ifdef HAVE_LINUX_DMA_HEAP_H
  g_return_val_if_fail (GST_IS_DMAHEAP_ALLOCATOR (allocator), NULL);

  if (params) {
    g_return_val_if_fail (((params->align + 1) & params->align) == 0, NULL);

    align = params->align | gst_memory_alignment;
    prefix = params->prefix;
    padding = params->padding;
  }

  maxsize = size + prefix + padding;

  struct dma_heap_allocation_data alloc_data = {
    .len = maxsize,
    .fd_flags = dmaheap_alloc->fd_flags,
    .heap_flags = dmaheap_alloc->heap_flags,
  };

  if (ioctl (dmaheap_alloc->device_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
    GST_ERROR_OBJECT (allocator, "Failed to allocate DMA buffer");
    goto alloc_dma_buffer_failed;
  } else if (alloc_data.fd < 0) {
    GST_ERROR_OBJECT (allocator,
        "Failed to get file descriptor from DMA buffer");
    goto alloc_dma_buffer_failed;
  } else if (alloc_data.len < maxsize) {
    GST_ERROR_OBJECT (allocator,
        "Failed to allocate enough space on heap, request %" G_GSIZE_FORMAT
        " bytes, got %llu bytes", maxsize, alloc_data.len);
    close (alloc_data.fd);
    goto alloc_dma_buffer_failed;
  }

  new_mem = gst_fd_allocator_alloc ((GstAllocator *) dmaheap_alloc,
      alloc_data.fd, alloc_data.len, GST_FD_MEMORY_FLAG_NONE);

  new_mem->align = align;
  new_mem->offset = prefix;
  new_mem->size = size;
  new_mem->maxsize = alloc_data.len;

  /* setup the memory flags */
  GST_MEMORY_FLAGS (new_mem)
      |= dmaheap_alloc->contiguous_memory ?
      (params->flags | GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS) : params->flags;

  GST_DEBUG_OBJECT (dmaheap_alloc,
      "Allocated dma mem %p with maxsize %" G_GSIZE_FORMAT " size %"
      G_GSIZE_FORMAT " fd %d", new_mem, new_mem->maxsize, new_mem->size,
      alloc_data.fd);

  return new_mem;

alloc_dma_buffer_failed:
  return NULL;

#else
  return NULL;
#endif
}

/**
 * gst_is_dmaheap_memory:
 * @mem: the memory to be check
 *
 * Check if @mem is dmaheap memory.
 *
 * Returns: %TRUE if @mem is dmaheap memory, otherwise %FALSE
 */
gboolean
gst_is_dmaheap_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return GST_IS_DMAHEAP_ALLOCATOR (mem->allocator);
}

static gpointer
gst_dmabuf_mem_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  gpointer data = NULL;
  gsize padding;
  GstDmaHeapAllocator *dmaheap_alloc = NULL;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);

  dmaheap_alloc = GST_DMAHEAP_ALLOCATOR (mem->allocator);

  if (GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_NOT_MAPPABLE)) {
    GST_ERROR ("memory %p not mappable", mem);
    return NULL;
  }

  if (GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_READONLY)
      && (info->flags & GST_MAP_WRITE)) {
    GST_ERROR ("memory: %p read only, should not map with write access", mem);
    return NULL;
  }

  data = dmaheap_alloc->parent_mem_map_full (mem, info, maxsize);

  /* check if data is align with mem->align */
  if (((guintptr) data & mem->align) != 0) {
    GST_WARNING ("memory: %p data: %p is not aligned with alignment: (%u + 1)",
        mem, data, mem->align);
  }

  if (data && GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (data, 0, mem->offset);

  padding = mem->maxsize - (mem->offset + mem->size);
  if (padding && GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (data + mem->offset + mem->size, 0, padding);

  GST_DEBUG ("memory: %p map data: %p", mem, data);
  return data;
}

static void
gst_dmaheap_mem_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstDmaHeapAllocator *dmaheap_alloc = NULL;

  g_return_if_fail (mem != NULL);
  g_return_if_fail (info != NULL);

  dmaheap_alloc = GST_DMAHEAP_ALLOCATOR (mem->allocator);

  GST_DEBUG ("memory: %p unmap", mem);

  dmaheap_alloc->parent_mem_unmap_full (mem, info);
}

static void
gst_dmaheap_allocator_class_init (GstDmaHeapAllocatorClass * klass)
{
  GstAllocatorClass *allocator_klass = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *gobject_klass = G_OBJECT_CLASS (klass);

  allocator_klass->alloc = gst_dmaheap_allocator_alloc;
  gobject_klass->finalize = gst_dmaheap_allocator_finalize;
}

static void
gst_dmaheap_allocator_init (GstDmaHeapAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);
  GstDmaHeapAllocator *dmaheap_alloc = GST_DMAHEAP_ALLOCATOR (alloc);

  alloc->mem_type = GST_ALLOCATOR_DMAHEAP;

  dmaheap_alloc->parent_mem_map_full = alloc->mem_map_full;
  dmaheap_alloc->parent_mem_unmap_full = alloc->mem_unmap_full;

  alloc->mem_map_full = gst_dmabuf_mem_map_full;
  alloc->mem_unmap_full = gst_dmaheap_mem_unmap_full;
}

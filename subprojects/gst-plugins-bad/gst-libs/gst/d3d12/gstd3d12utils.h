/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

GST_D3D12_API
gboolean  gst_d3d12_handle_set_context (GstElement * element,
                                        GstContext * context,
                                        gint adapter_index,
                                        GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_handle_set_context_for_adapter_luid (GstElement * element,
                                                         GstContext * context,
                                                         gint64 adapter_luid,
                                                         GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_handle_context_query (GstElement * element,
                                          GstQuery * query,
                                          GstD3D12Device * device);

GST_D3D12_API
gboolean  gst_d3d12_ensure_element_data  (GstElement * element,
                                          gint adapter_index,
                                          GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_ensure_element_data_for_adapter_luid (GstElement * element,
                                                          gint64 adapter_luid,
                                                          GstD3D12Device ** device);

GST_D3D12_API
gint64    gst_d3d12_luid_to_int64 (const LUID * luid);

GST_D3D12_API
GstContext * gst_d3d12_context_new (GstD3D12Device * device);

GST_D3D12_API
gint64    gst_d3d12_create_user_token (void);

GST_D3D12_API
gboolean  gst_d3d12_buffer_copy_into (GstBuffer * dest,
                                      GstBuffer * src,
                                      const GstVideoInfo * info);

GST_D3D12_API
gboolean _gst_d3d12_result (HRESULT hr,
                            GstD3D12Device * device,
                            GstDebugCategory * cat,
                            const gchar * file,
                            const gchar * function,
                            gint line,
                            GstDebugLevel level);

GST_D3D12_API
gboolean _gst_d3d12_result_full (HRESULT hr,
                                 GstElement * element,
                                 GstD3D12Device * device,
                                 GstDebugCategory * cat,
                                 const gchar * file,
                                 const gchar * function,
                                 gint line,
                                 GstDebugLevel level);

GST_D3D12_API
gboolean _gst_d3d12_post_error_if_device_removed (GstElement * element,
                                                  GstD3D12Device * device,
                                                  const gchar * file,
                                                  const gchar * function,
                                                  gint line);

/**
 * gst_d3d12_result:
 * @result: HRESULT D3D12 API return code
 * @device: (nullable): Associated #GstD3D12Device
 *
 * Returns: %TRUE if D3D12 API call result is SUCCESS
 *
 * Since: 1.26
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#else
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, NULL, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#endif

/**
 * gst_d3d12_result_full:
 * @result: HRESULT D3D12 API return code
 * @element: (nullable): a #GstElement
 * @device: (nullable): Associated #GstD3D12Device
 *
 * Checks HRESULT code and posts GST_RESOURCE_ERROR_DEVICE_LOST message
 * if device removed state is detected
 *
 * Returns: %TRUE if D3D12 API call result is SUCCESS
 *
 * Since: 1.26
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d12_result_full(result,elem,device) \
    _gst_d3d12_result_full (result, (GstElement *) elem, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#else
#define gst_d3d12_result_full(result,elem,device) \
    _gst_d3d12_result_full (result, (GstElement *) elem, device, NULL, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#endif

/**
 * gst_d3d12_post_error_if_device_removed:
 * @element: a #GstElement
 * @device: a #GstD3D12Device
 *
 * Posts device lost error if device removed status is detected
 *
 * Returns: %TRUE if device lost message was posted
 *
 * Since: 1.26
 */
#define gst_d3d12_post_error_if_device_removed(elem,device) \
    _gst_d3d12_post_error_if_device_removed ((GstElement *) elem, device, __FILE__, GST_FUNCTION, __LINE__)

G_END_DECLS


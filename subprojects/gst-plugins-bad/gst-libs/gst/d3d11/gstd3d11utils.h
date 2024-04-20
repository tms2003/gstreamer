/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d11/gstd3d11_fwd.h>

G_BEGIN_DECLS

GST_D3D11_API
gboolean        gst_d3d11_handle_set_context        (GstElement * element,
                                                     GstContext * context,
                                                     gint adapter_index,
                                                     GstD3D11Device ** device);

GST_D3D11_API
gboolean        gst_d3d11_handle_set_context_for_adapter_luid (GstElement * element,
                                                               GstContext * context,
                                                               gint64 adapter_luid,
                                                               GstD3D11Device ** device);

GST_D3D11_API
gboolean        gst_d3d11_handle_context_query      (GstElement * element,
                                                     GstQuery * query,
                                                     GstD3D11Device * device);

GST_D3D11_API
gboolean        gst_d3d11_ensure_element_data       (GstElement * element,
                                                     gint adapter_index,
                                                     GstD3D11Device ** device);

GST_D3D11_API
gboolean        gst_d3d11_ensure_element_data_for_adapter_luid (GstElement * element,
                                                                gint64 adapter_luid,
                                                                GstD3D11Device ** device);

GST_D3D11_API
GstContext *    gst_d3d11_context_new               (GstD3D11Device * device);

GST_D3D11_API
gint64          gst_d3d11_luid_to_int64             (const LUID * luid);

GST_D3D11_API
gint64          gst_d3d11_create_user_token         (void);

GST_D3D11_API
gboolean       _gst_d3d11_result                    (HRESULT hr,
                                                     GstD3D11Device * device,
                                                     GstDebugCategory * cat,
                                                     const gchar * file,
                                                     const gchar * function,
                                                     gint line);

GST_D3D11_API
gboolean       _gst_d3d11_result_full               (HRESULT hr,
                                                     GstElement * element,
                                                     GstD3D11Device * device,
                                                     GstDebugCategory * cat,
                                                     const gchar * file,
                                                     const gchar * function,
                                                     gint line);

GST_D3D11_API
gboolean _gst_d3d11_post_error_if_device_removed (GstElement * element,
                                                  GstD3D11Device * device,
                                                  const gchar * file,
                                                  const gchar * function,
                                                  gint line);

/**
 * gst_d3d11_result:
 * @result: HRESULT D3D11 API return code
 * @device: (nullable): Associated #GstD3D11Device
 *
 * Returns: %TRUE if D3D11 API call result is SUCCESS
 *
 * Since: 1.22
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d11_result(result,device) \
    _gst_d3d11_result (result, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else
#define gst_d3d11_result(result,device) \
    _gst_d3d11_result (result, device, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif /* GST_DISABLE_GST_DEBUG */

/**
 * gst_d3d11_result_full:
 * @result: HRESULT D3D11 API return code
 * @element: (nullable): a #GstElement
 * @device: (nullable): Associated #GstD3D11Device
 *
 * Checks HRESULT code and posts GST_RESOURCE_ERROR_DEVICE_LOST message
 * if device removed state is detected
 *
 * Returns: %TRUE if D3D11 API call result is SUCCESS
 *
 * Since: 1.26
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d11_result_full(result,elem,device) \
    _gst_d3d11_result_full (result, (GstElement *) elem, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else
#define gst_d3d11_result_full(result,elem,device) \
    _gst_d3d11_result_full (result, (GstElement *) elem, device, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif

/**
 * gst_d3d11_post_error_if_device_removed:
 * @element: a #GstElement
 * @device: a #GstD3D11Device
 *
 * Posts device lost error if device removed status is detected
 *
 * Returns: %TRUE if device lost message was posted
 *
 * Since: 1.26
 */
#define gst_d3d11_post_error_if_device_removed(elem,device) \
    _gst_d3d11_post_error_if_device_removed ((GstElement *) elem, device, __FILE__, GST_FUNCTION, __LINE__)

G_END_DECLS


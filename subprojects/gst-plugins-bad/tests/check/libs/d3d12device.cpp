/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/d3d12/gstd3d12.h>
#include <wrl.h>
#include <mutex>
#include <condition_variable>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_START_TEST (test_device_equal)
{
  auto device = gst_d3d12_device_new (0);
  fail_unless (GST_IS_D3D12_DEVICE (device));

  auto other_device = gst_d3d12_device_new (0);
  fail_unless (GST_IS_D3D12_DEVICE (other_device));
  fail_unless (gst_d3d12_device_is_equal (device, other_device));

  auto handle = gst_d3d12_device_get_device_handle (device);
  auto other_handle = gst_d3d12_device_get_device_handle (other_device);
  fail_unless_equals_pointer (handle, other_handle);

  gst_object_unref (device);
  gst_object_unref (other_device);
}

GST_END_TEST;

struct DeviceRemovedData
{
  std::mutex lock;
  std::condition_variable cond;
  guint removed_count = 0;
};

static void
on_device_removed (GstD3D12Device * device, GParamSpec * pspec,
    DeviceRemovedData * data)
{
  HRESULT hr = S_OK;
  g_object_get (device, "device-removed-reason", &hr, nullptr);
  fail_unless (FAILED (hr));

  std::lock_guard <std::mutex> lk (data->lock);
  data->removed_count++;
  data->cond.notify_all ();
}

GST_START_TEST (test_device_removed)
{
  auto device = gst_d3d12_device_new (0);
  fail_unless (GST_IS_D3D12_DEVICE (device));

  ComPtr<ID3D12Device5> device5;
  auto handle = gst_d3d12_device_get_device_handle (device);
  fail_unless (handle != nullptr);

  handle->QueryInterface (IID_PPV_ARGS (&device5));
  if (!device5) {
    gst_object_unref (device);
    return;
  }

  auto other_device = gst_d3d12_device_new (0);

  DeviceRemovedData data;

  g_signal_connect (device, "notify::device-removed-reason",
      G_CALLBACK (on_device_removed), &data);
  g_signal_connect (other_device, "notify::device-removed-reason",
      G_CALLBACK (on_device_removed), &data);

  /* Emulate device removed case */
  device5->RemoveDevice ();
  device5 = nullptr;

  /* Callback will be called from other thread */
  {
    std::unique_lock <std::mutex> lk (data.lock);
    while (data.removed_count != 2)
      data.cond.wait (lk);
  }

  /* This will fail since we are holding removed device */
  auto null_device = gst_d3d12_device_new (0);
  fail_if (null_device);

  gst_object_unref (device);
  gst_object_unref (other_device);

  /* After releasing all devices, create device should be successful */
  device = gst_d3d12_device_new (0);
  fail_unless (GST_IS_D3D12_DEVICE (device));

  gst_object_unref (device);
}

GST_END_TEST;

struct DeviceLostData
{
  std::mutex lock;
  std::condition_variable cond;
  bool got_device_lost = false;
  bool got_eos = false;
};

static GstBusSyncReply
device_lost_bus_sync_handler (GstBus * bus, GstMessage * msg,
    DeviceLostData * data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gst_message_parse_error (msg, &err, nullptr);
      if (err->domain == GST_RESOURCE_ERROR &&
          err->code == GST_RESOURCE_ERROR_DEVICE_LOST) {
        std::lock_guard <std::mutex> lk (data->lock);
        data->got_device_lost = true;
        data->cond.notify_all ();
      }
      break;
    }
    case GST_MESSAGE_EOS:
    {
      std::lock_guard <std::mutex> lk (data->lock);
      data->got_eos = true;
      data->cond.notify_all ();
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

GST_START_TEST (test_device_lost)
{
  auto device = gst_d3d12_device_new (0);
  fail_unless (GST_IS_D3D12_DEVICE (device));

  ComPtr<ID3D12Device5> device5;
  auto handle = gst_d3d12_device_get_device_handle (device);
  fail_unless (handle != nullptr);

  handle->QueryInterface (IID_PPV_ARGS (&device5));
  if (!device5) {
    gst_object_unref (device);
    return;
  }

  DeviceLostData data;

  /* Since GstD3D12Device's internal struct is signeton per adapter,
   * don't need to handle need-context message. Once we make device removed,
   * pipeline's device will be broken too */
  auto pipeline = gst_parse_launch (
      "d3d12testsrc num-buffers=10 ! d3d12videosink sync=false", nullptr);
  fail_unless (pipeline);

  auto bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus);
  gst_bus_set_sync_handler (bus,
      (GstBusSyncHandler) device_lost_bus_sync_handler, &data, nullptr);
  gst_object_unref (bus);

  auto ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (ret != GST_STATE_CHANGE_FAILURE);

  device5->RemoveDevice ();
  device5 = nullptr;

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  {
    std::unique_lock <std::mutex> lk (data.lock);
    while (!data.got_device_lost && !data.got_eos)
      data.cond.wait (lk);
  }

  fail_unless (data.got_device_lost);

  HRESULT reason = S_OK;
  g_object_get (device, "device-removed-reason", &reason, nullptr);
  fail_unless (FAILED (reason));

  /* Release all */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (device);

  data.got_device_lost = false;
  data.got_eos = false;

  /* Create pipeline again, and run nomally */
  device = gst_d3d12_device_new (0);
  pipeline = gst_parse_launch (
      "d3d12testsrc num-buffers=10 ! d3d12videosink sync=false", nullptr);
  fail_unless (pipeline);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus);
  gst_bus_set_sync_handler (bus,
      (GstBusSyncHandler) device_lost_bus_sync_handler, &data, nullptr);
  gst_object_unref (bus);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret != GST_STATE_CHANGE_FAILURE);

  {
    std::unique_lock <std::mutex> lk (data.lock);
    while (!data.got_device_lost && !data.got_eos)
      data.cond.wait (lk);
  }

  fail_if (data.got_device_lost);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (device);
}

GST_END_TEST;

static gboolean
check_d3d12_available (void)
{
  auto device = gst_d3d12_device_new (0);
  if (!device)
    return FALSE;

  gst_object_unref (device);

  return TRUE;
}

/* ID3D12Device5::RemoveDevice requires Windows10 build 20348 or newer */
static gboolean
check_remove_device_supported (void)
{
  OSVERSIONINFOEXW osverinfo = { };
  typedef NTSTATUS (WINAPI fRtlGetVersion) (PRTL_OSVERSIONINFOEXW);
  gboolean ret = FALSE;

  memset (&osverinfo, 0, sizeof (OSVERSIONINFOEXW));
  osverinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

  auto hmodule = LoadLibraryW (L"ntdll.dll");
  if (!hmodule)
    return FALSE;

  auto RtlGetVersion = (fRtlGetVersion *) GetProcAddress (hmodule, "RtlGetVersion");
  if (RtlGetVersion) {
    RtlGetVersion (&osverinfo);

    if (osverinfo.dwMajorVersion > 10 ||
        (osverinfo.dwMajorVersion == 10 && osverinfo.dwBuildNumber >= 20348)) {
      ret = TRUE;
    }
  }

  FreeLibrary (hmodule);

  return ret;
}

static Suite *
d3d12device_suite (void)
{
  Suite *s = suite_create ("d3d12device");
  TCase *tc_basic = tcase_create ("general");

  suite_add_tcase (s, tc_basic);

  if (!check_d3d12_available ())
    return s;

  tcase_add_test (tc_basic, test_device_equal);
  if (check_remove_device_supported ()) {
    tcase_add_test (tc_basic, test_device_removed);
    tcase_add_test (tc_basic, test_device_lost);
  }

  return s;
}

GST_CHECK_MAIN (d3d12device);

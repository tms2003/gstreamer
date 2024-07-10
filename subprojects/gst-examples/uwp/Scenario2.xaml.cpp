// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Scenario2.xaml.h"
#include <gst/video/video.h>

using namespace gst_uwp_example;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

Scenario2::Scenario2()
{
  InitializeComponent();
}

void Scenario2::OnNavigatedTo(NavigationEventArgs ^ e)
{
  rootPage = MainPage::Current;
}

void Scenario2::OnNavigatedFrom(NavigationEventArgs ^ e)
{
  stopPipeline();
  rootPage->UpdateStatusMessage("");
}

void Scenario2::onPageLoaded(Platform::Object ^ sender,
                             Windows::UI::Xaml::RoutedEventArgs ^ e)
{
}

void Scenario2::btnStart_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  startPipeline();
  updateUIElements();
}


void Scenario2::btnStop_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  stopPipeline();
  updateUIElements();
}

bool Scenario2::startPipeline()
{
  GstDeviceMonitor* monitor;
  GList* devices;
  GstElement* vsrc = nullptr;
  GstElement* asrc = nullptr;
  GstElement *vqueue, *aqueue;
  GstElement *aconv, *resample;
  GstElement *vconv, *vscale = nullptr;
  GstElement* cf = nullptr;
  GstElement *asink, *vsink;
  GstElement *venc, *vdec;
  GstElement* vparse = nullptr;
  guint filter_id;
  bool hasHardware = false;

  monitor = gst_device_monitor_new();

  // Find video capture element first
  filter_id = gst_device_monitor_add_filter(monitor, "Source/Video", nullptr);
  devices = gst_device_monitor_get_devices(monitor);

  if (!devices) {
    // If we don't have video capture device, use videotestsrc
    vsrc = gst_element_factory_make("videotestsrc", nullptr);
  } else {
    // Otherwise, select just the first one
    GstDevice* dev = GST_DEVICE(devices->data);
    vsrc = gst_device_create_element(dev, nullptr);

    // Device should be activated from UI thread.
    // For that purpose, ICoreDispatcher needs to be passed to
    // wasapi2{src,sink} and mfvideosrc.
    //
    // NOTE1: if upwards state change happens from UI thread,
    // wasapi2{src,sink} and mfvideosrc are able to access to
    // a ICoreDispatcher object by themsevles.
    // but passing ICoreDispatcher to the elements would be the most
    // robust way.
    //
    // NOTE2: wasapi2{src,sink} and mfvideosrc will manage the referece count
    // of given ICoreDispatcher object. So application doesn't need to call
    // AddRef()/Release() in this case.
    g_object_set(
      vsrc, "dispatcher", reinterpret_cast<gpointer>(Dispatcher), nullptr);
    g_list_free_full(devices, (GDestroyNotify)gst_object_unref);
  }

  // Clear filters
  gst_device_monitor_remove_filter(monitor, filter_id);

  filter_id = gst_device_monitor_add_filter(monitor, "Source/Audio", nullptr);
  devices = gst_device_monitor_get_devices(monitor);

  if (!devices) {
    // If we don't have audio capture device, use audiotestsrc
    asrc = gst_element_factory_make("audiotestsrc", nullptr);
  } else {
    // Otherwise, select just the first one
    GstDevice* dev = GST_DEVICE(devices->data);
    asrc = gst_device_create_element(dev, nullptr);

    // HACK: clock from wasapi2src is known to be incorrect,
    // use system clock instead
    g_object_set(asrc, "provide-clock", FALSE, "low-latency", TRUE, nullptr);

    // Pass our dispacher so that audiosrc can activate device from UI thread.
    g_object_set(
      asrc, "dispatcher", reinterpret_cast<gpointer>(Dispatcher), nullptr);
    g_list_free_full(devices, (GDestroyNotify)gst_object_unref);
  }

  gst_object_unref(monitor);

  pipeline_ = gst_pipeline_new(nullptr);

  // Check hardware encoder and decoder, and use them if available
  auto helper = GstWrapper::Instance::get();

  vdec = helper->GetHardwareVideoDecoder("video/x-h264");
  venc = helper->GetHardwareVideoEncoder("video/x-h264");
  vparse = gst_element_factory_make("h264parse", nullptr);

  if (vdec && venc && vparse) {
    // TODO: Update UI to inform that we have usable hardware en/decoder
    GstCaps* caps;
    GObjectClass* encKlass = G_OBJECT_GET_CLASS(venc);
    GParamSpec* pspec;

    // Check whether Media Foundation encoder supports "low-latency"
    // property. Note that Media Foundation encoder is the only available
    // upstream hardware encoder element on UWP.
    pspec = g_object_class_find_property(encKlass, "low-latency");

    // If low-latency property is available, perfer to use it
    // for live streaming. Otherwise encoder will be running on frame encoding
    // mode which will introduce initial latency.
    if (pspec)
      g_object_set(venc, "low-latency", TRUE, nullptr);

    // Restrict the video resolution since we don't know about hardware
    // encoder's capability here
    vscale = gst_element_factory_make("videoscale", nullptr);
    cf = gst_element_factory_make("capsfilter", nullptr);
    caps = gst_caps_new_simple("video/x-raw",
                                "width",
                                G_TYPE_INT,
                                640,
                                "height",
                                G_TYPE_INT,
                                480,
                                nullptr);
    g_object_set(cf, "caps", caps, nullptr);
    gst_caps_unref(caps);

    hasHardware = true;
  } else {
    gst_clear_object(&vdec);
    gst_clear_object(&venc);
    gst_clear_object(&vparse);
  }

  // Configure video branch
  vqueue = gst_element_factory_make("queue", nullptr);
  vconv = gst_element_factory_make("videoconvert", nullptr);
  vsink = gst_element_factory_make("d3d11videosink", nullptr);

  // Pass our swap chain panel to d3d11 videosink
  gst_video_overlay_set_window_handle(
    GST_VIDEO_OVERLAY(vsink),
    (guintptr) reinterpret_cast<IUnknown*>(Scenario2_videoPanel));

  // Configure audio branch
  aqueue = gst_element_factory_make("queue", nullptr);
  aconv = gst_element_factory_make("audioconvert", nullptr);
  resample = gst_element_factory_make("audioresample", nullptr);
  asink = gst_element_factory_make("wasapi2sink", nullptr);
  g_object_set(asink, "low-latency", TRUE, nullptr);

  // Pass our dispacher so that audiosink can activate device from UI thread.
  g_object_set(
    asink, "dispatcher", reinterpret_cast<gpointer>(Dispatcher), nullptr);

  gst_bin_add_many(GST_BIN(pipeline_),
                    vsrc,
                    vqueue,
                    vconv,
                    vsink,
                    asrc,
                    aqueue,
                    aconv,
                    resample,
                    asink,
                    nullptr);

  if (hasHardware) {
    gst_bin_add_many(
      GST_BIN(pipeline_), vscale, cf, venc, vparse, vdec, nullptr);
    gst_element_link_many(
      vsrc, vqueue, vconv, vscale, cf, venc, vparse, vdec, vsink, nullptr);
  } else {
    gst_element_link_many(vsrc, vqueue, vconv, vsink, nullptr);
  }

  gst_element_link_many(asrc, aqueue, aconv, resample, asink, nullptr);

  GstStateChangeReturn ret;
  ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    rootPage->UpdateStatusMessage("Failed to start play");

    stopPipeline();
    return false;
  }

  Scenario2_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;

  rootPage->UpdateStatusMessage("Playing");
  isPlaying_ = true;

  return true;
}

bool Scenario2::stopPipeline()
{
  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_clear_object(&pipeline_);
  }

  // Otherwise the last render image will not be cleared
  Scenario2_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

  rootPage->UpdateStatusMessage("Ready To Play");
  isPlaying_ = false;

  return true;
}

void Scenario2::updateUIElements()
{
  btnStart->IsEnabled = !isPlaying_;
  btnStop->IsEnabled = isPlaying_;
}
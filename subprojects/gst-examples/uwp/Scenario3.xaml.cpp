// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Scenario3.xaml.h"
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

Scenario3::Scenario3()
{
  InitializeComponent();
}

void Scenario3::OnNavigatedTo(NavigationEventArgs ^ e)
{
  rootPage = MainPage::Current;
}

void Scenario3::OnNavigatedFrom(NavigationEventArgs ^ e)
{
  stopPipeline();
  rootPage->UpdateStatusMessage("");
}

void Scenario3::onPageLoaded(Platform::Object ^ sender,
                             Windows::UI::Xaml::RoutedEventArgs ^ e)
{
}

void Scenario3::btnStart_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  startPipeline();
  updateUIElements();
}

void Scenario3::btnStop_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  stopPipeline();
  updateUIElements();
}

bool Scenario3::startPipeline()
{
  stopPipeline();

  auto wrapper = GstWrapper::Instance::get();

  GstElement* pipeline;
  pipeline = gst_parse_launch("videotestsrc ! queue ! glimagesink name=overlay",
    nullptr);
  GstElement* overlay = gst_bin_get_by_name(GST_BIN(pipeline), "overlay");

  if (overlay) {
    gst_video_overlay_set_window_handle(
      GST_VIDEO_OVERLAY(overlay),
      (guintptr) reinterpret_cast<IUnknown*>(Scenario3_videoPanel));
    gst_object_unref(overlay);
  }

  // FIXME: this will cause flickering while staring pipelne.
  // For instance, if Scenario3_videoPanel has previously rendered image
  // before this pipeline, then this visibility change make the previous image
  // visible. Then while pipeline is starting rendering, some flickering might
  // happends. To make this more correct, visibility needs to be updated once
  // pipeline is about to start rendering actually
  // (e.g., async-done, state-changed or so).
  Scenario3_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;

  // glimagesink cannot be closed from UI thread
  // Pass pipeline to helper so that it can be running on another thread
  pipelineId_ = wrapper->LaunchPipeline(pipeline);

  if (!pipelineId_) {
    rootPage->UpdateStatusMessage("Failed to start play");

    stopPipeline();
    return false;
  }

  rootPage->UpdateStatusMessage("Playing");

  return true;
}

bool Scenario3::stopPipeline()
{
  if (pipelineId_) {
    auto wrapper = GstWrapper::Instance::get();

    // helper will take care of our pipeline object
    wrapper->DestroyPipeline(pipelineId_);
    pipelineId_ = 0;
  }

  // Otherwise the last render image will not be cleared
  Scenario3_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
  rootPage->UpdateStatusMessage("Ready To Play");

  return true;
}

void Scenario3::updateUIElements()
{
  btnStart->IsEnabled = pipelineId_ == 0;
  btnStop->IsEnabled = pipelineId_ != 0;
}
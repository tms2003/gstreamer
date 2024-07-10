// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Scenario1.xaml.h"
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

Scenario1::Scenario1()
{
  InitializeComponent();
}

void Scenario1::OnNavigatedTo(NavigationEventArgs ^ e)
{
  rootPage = MainPage::Current;
}

void Scenario1::OnNavigatedFrom(NavigationEventArgs ^ e)
{
  stopPipeline();
  rootPage->UpdateStatusMessage("");
}

void Scenario1::onPageLoaded(Platform::Object ^ sender,
                             Windows::UI::Xaml::RoutedEventArgs ^ e)
{
}

void Scenario1::btnStart_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  startPipeline();
  updateUIElements();
}

void Scenario1::btnStop_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  stopPipeline();
  updateUIElements();
}

bool Scenario1::startPipeline()
{
  stopPipeline();

  pipeline_ = gst_parse_launch(
      "videotestsrc ! queue ! d3d11videosink name=overlay", NULL);
  GstElement* overlay = gst_bin_get_by_name(GST_BIN(pipeline_), "overlay");

  if (overlay) {
    gst_video_overlay_set_window_handle(
      GST_VIDEO_OVERLAY(overlay),
      (guintptr) reinterpret_cast<IUnknown*>(Scenario1_videoPanel));
    gst_object_unref(overlay);
  }

  gst_element_set_state(pipeline_, GST_STATE_PLAYING);

  GstStateChangeReturn ret;
  ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    rootPage->UpdateStatusMessage("Failed to start play");

    stopPipeline();
    return false;
  }

  Scenario1_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;

  rootPage->UpdateStatusMessage("Playing");
  isPlaying_ = true;

  return true;
}

bool Scenario1::stopPipeline()
{
  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_clear_object(&pipeline_);
  }

  // Otherwise the last render image will not be cleared
  Scenario1_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

  rootPage->UpdateStatusMessage("Ready To Play");
  isPlaying_ = false;

  return true;
}

void Scenario1::updateUIElements()
{
  btnStart->IsEnabled = !isPlaying_;
  btnStop->IsEnabled = isPlaying_;
}
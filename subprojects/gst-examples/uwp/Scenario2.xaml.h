// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "pch.h"
#include "GstWrapper.h"
#include "MainPage.xaml.h"
#include "Scenario2.g.h"

namespace gst_uwp_example {
public
ref class Scenario2 sealed
{
public:
  Scenario2();

protected:
  virtual void OnNavigatedTo(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;
  virtual void OnNavigatedFrom(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;

private:
  void onPageLoaded(Platform::Object ^ sender,
                    Windows::UI::Xaml::RoutedEventArgs ^ e);
  void btnStart_Click(Platform::Object^ sender,
                      Windows::UI::Xaml::RoutedEventArgs^ e);
  void btnStop_Click(Platform::Object^ sender,
                     Windows::UI::Xaml::RoutedEventArgs^ e);

  bool startPipeline();
  bool stopPipeline();
  void updateUIElements();

private:
  GstElement* pipeline_ = nullptr;
  bool isPlaying_ = false;
  MainPage ^ rootPage;
};
}

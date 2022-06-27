// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "MainPage.g.h"
#include "SampleConfiguration.h"
#include <gst/gst.h>

namespace gst_uwp_example {
/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
public
ref class MainPage sealed
{
public:
  MainPage();
  void AddLogMessage(Platform::String ^ message);
  void UpdateStatusMessage(Platform::String ^ message);

protected:
  virtual void OnNavigatedTo(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;

private:
  void Button_Click(Platform::Object ^ sender,
                    Windows::UI::Xaml::RoutedEventArgs ^ e);
  void ScenarioControl_SelectionChanged(
    Platform::Object ^ sender,
    Windows::UI::Xaml::Controls::SelectionChangedEventArgs ^ e);
  void AddLogMessageInternal(Platform::String ^ message);
  concurrency::task<void> AddLogMessageAsync(Platform::String ^ message);
  void UpdateStatusMessageInternal(Platform::String ^ message);
  concurrency::task<void> UpdateStatusMessageAsync(Platform::String ^ message);
  void btnOpenAppDir_Click(Platform::Object ^ sender,
                           Windows::UI::Xaml::RoutedEventArgs ^ e);
  void comboDebugLevel_SelectionChanged(
    Platform::Object^ sender,
    Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);

  void SetupLogger(void);
  static void GstDebugCb(GstDebugCategory* category,
                         GstDebugLevel level,
                         const gchar* file,
                         const gchar* function,
                         gint line,
                         GObject* obj,
                         GstDebugMessage* message,
                         gpointer user_data);

  internal : static MainPage ^ Current;
};
}

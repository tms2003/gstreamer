// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif

#include "pch.h"
#include "GstWrapper.h"
#include "MainPage.xaml.h"
#include "Scenario4.g.h"
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>

namespace gst_uwp_example {
public
ref class Scenario4 sealed
{
public:
  Scenario4();

protected:
  virtual void OnNavigatedTo(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;
  virtual void OnNavigatedFrom(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;

private:
  void onPageLoaded(Platform::Object ^ sender,
                    Windows::UI::Xaml::RoutedEventArgs ^ e);

  static void WebRtcLPadAdded(GstElement* webrtc,
                              GstPad* newPad,
                              gpointer userData);
  static void WebRtcRPadAdded(GstElement* webrtc,
                              GstPad* newPad,
                              gpointer userData);
  static void OnAnswerReceived(GstPromise* promise, gpointer userData);
  static void OnOfferReceived(GstPromise* promise, gpointer userData);
  static void OnNegotiationNeeded(GstElement* element, gpointer userData);
  static void OnIceCandidate(GstElement* webrtc,
                             guint mlineindex,
                             gchar* candidate,
                             GstElement* other);
  void btnStart_Click(Platform::Object^ sender,
                      Windows::UI::Xaml::RoutedEventArgs^ e);
  void btnStop_Click(Platform::Object^ sender,
                     Windows::UI::Xaml::RoutedEventArgs^ e);

  bool startPipeline();
  bool stopPipeline();
  void updateUIElements();

private:
  GstElement* pipeline_ = nullptr;
  GstElement* webrtcL_ = nullptr;
  GstElement* webrtcR_ = nullptr;
  bool isPlaying_ = false;
  MainPage ^ rootPage;
};
}

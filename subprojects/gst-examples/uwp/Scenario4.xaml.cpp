// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Scenario4.xaml.h"
#include "MainPage.xaml.h"
#include "Utils.h"
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

Scenario4::Scenario4()
{
  InitializeComponent();
}

void
Scenario4::WebRtcLPadAdded(GstElement* webrtc,
                           GstPad* newPad,
                           gpointer userData)
{
  Scenario4 ^ self = reinterpret_cast<Scenario4 ^>(userData);
  GstElement* out;
  GstPad* sink;

  if (GST_PAD_DIRECTION(newPad) != GST_PAD_SRC)
    return;

  out = gst_parse_bin_from_description(
    "rtpvp8depay ! vp8dec ! "
    "videoconvert ! queue ! d3d11videosink name=overlay-left",
    TRUE,
    nullptr);

  GstElement* overlay = gst_bin_get_by_name(GST_BIN(out), "overlay-left");
  if (overlay) {
    gst_video_overlay_set_window_handle(
      GST_VIDEO_OVERLAY(overlay),
      (guintptr) reinterpret_cast<IUnknown*>(self->Scenario4_videoPanel_Left));
    gst_object_unref(overlay);
  }

  g_assert(gst_bin_add(GST_BIN(self->pipeline_), out));
  sink = (GstPad*)out->sinkpads->data;
  g_assert(gst_pad_link(newPad, sink) == GST_PAD_LINK_OK);

  gst_element_sync_state_with_parent(out);

  self->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                             ref new DispatchedHandler([self]() {
                              self->Scenario4_videoPanel_Left->Visibility =
                                Windows::UI::Xaml::Visibility::Visible; }));
}

void
Scenario4::WebRtcRPadAdded(GstElement* webrtc,
                           GstPad* newPad,
                           gpointer userData)
{
  Scenario4 ^ self = reinterpret_cast<Scenario4 ^>(userData);
  GstElement* out;
  GstPad* sink;

  if (GST_PAD_DIRECTION(newPad) != GST_PAD_SRC)
    return;

  out = gst_parse_bin_from_description(
    "rtpvp8depay ! vp8dec ! "
    "videoconvert ! queue ! d3d11videosink name=overlay-right",
    TRUE,
    nullptr);

  GstElement* overlay = gst_bin_get_by_name(GST_BIN(out), "overlay-right");
  if (overlay) {
    gst_video_overlay_set_window_handle(
      GST_VIDEO_OVERLAY(overlay),
      (guintptr) reinterpret_cast<IUnknown*>(self->Scenario4_videoPanel_Right));
    gst_object_unref(overlay);
  }

  g_assert(gst_bin_add(GST_BIN(self->pipeline_), out));

  sink = (GstPad*)out->sinkpads->data;
  g_assert(gst_pad_link(newPad, sink) == GST_PAD_LINK_OK);

  gst_element_sync_state_with_parent(out);

  self->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                             ref new DispatchedHandler([self]() {
                              self->Scenario4_videoPanel_Right->Visibility =
                                Windows::UI::Xaml::Visibility::Visible; }));
}

void
Scenario4::OnAnswerReceived(GstPromise* promise, gpointer userData)
{
  Scenario4 ^ self = reinterpret_cast<Scenario4 ^>(userData);
  GstWebRTCSessionDescription* answer = nullptr;
  const GstStructure* reply;
  gchar* desc;

  g_assert(gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply(promise);
  gst_structure_get(
    reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
  gst_promise_unref(promise);
  desc = gst_sdp_message_as_text(answer->sdp);
  Platform::String ^ msg = "Created answer: " + ToPlatformString(desc);
  g_free(desc);

  MainPage::Current->AddLogMessage(msg);

  // this is one way to tell webrtcbin that we don't want to be notified when
  // this task is complete: set a NULL promise
  g_signal_emit_by_name(
    self->webrtcL_, "set-remote-description", answer, nullptr);

  // this is another way to tell webrtcbin that we don't want to be notified
  // when this task is complete: interrupt the promise
  promise = gst_promise_new();
  g_signal_emit_by_name(
    self->webrtcR_, "set-local-description", answer, promise);

  gst_promise_interrupt(promise);
  gst_promise_unref(promise);

  gst_webrtc_session_description_free(answer);
}

void
Scenario4::OnOfferReceived(GstPromise* promise, gpointer userData)
{
  Scenario4 ^ self = reinterpret_cast<Scenario4 ^>(userData);
  GstWebRTCSessionDescription* offer = nullptr;
  const GstStructure* reply;
  gchar* desc;

  g_assert(gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply(promise);
  gst_structure_get(
    reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
  gst_promise_unref(promise);
  desc = gst_sdp_message_as_text(offer->sdp);
  Platform::String ^ msg = "Created offer: " + ToPlatformString(desc);
  g_free(desc);

  MainPage::Current->AddLogMessage(msg);

  g_signal_emit_by_name(
    self->webrtcL_, "set-local-description", offer, nullptr);
  g_signal_emit_by_name(
    self->webrtcR_, "set-remote-description", offer, nullptr);

  promise = gst_promise_new_with_change_func(
    (GstPromiseChangeFunc)Scenario4::OnAnswerReceived, userData, nullptr);
  g_signal_emit_by_name(self->webrtcR_, "create-answer", nullptr, promise);

  gst_webrtc_session_description_free(offer);
}

void
Scenario4::OnNegotiationNeeded(GstElement* element, gpointer userData)
{
  Scenario4 ^ self = reinterpret_cast<Scenario4 ^>(userData);
  GstPromise* promise;

  promise = gst_promise_new_with_change_func(
    (GstPromiseChangeFunc)Scenario4::OnOfferReceived, userData, nullptr);
  g_signal_emit_by_name(self->webrtcL_, "create-offer", nullptr, promise);
}

void
Scenario4::OnIceCandidate(GstElement* webrtc,
                          guint mlineindex,
                          gchar* candidate,
                          GstElement* other)
{
  g_signal_emit_by_name(other, "add-ice-candidate", mlineindex, candidate);
}

void Scenario4::OnNavigatedTo(NavigationEventArgs ^ e)
{
  rootPage = MainPage::Current;
}

void Scenario4::OnNavigatedFrom(NavigationEventArgs ^ e)
{
  stopPipeline();
  rootPage->UpdateStatusMessage("");
}

void Scenario4::onPageLoaded(Platform::Object ^ sender,
                             Windows::UI::Xaml::RoutedEventArgs ^ e)
{
}

void Scenario4::btnStart_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  startPipeline();
  updateUIElements();
}


void Scenario4::btnStop_Click(Object ^ sender, RoutedEventArgs ^ e)
{
  stopPipeline();
  updateUIElements();
}

bool Scenario4::startPipeline()
{
  stopPipeline();

  pipeline_ = gst_parse_launch(
      "videotestsrc ! queue ! vp8enc ! rtpvp8pay ! queue ! "
      "application/x-rtp,media=video,payload=96,encoding-name=VP8 ! "
      "webrtcbin name=smpte videotestsrc pattern=ball ! queue ! vp8enc ! "
      "rtpvp8pay ! queue ! "
      "application/x-rtp,media=video,payload=96,encoding-name=VP8 ! webrtcbin "
      "name=ball",
      NULL);
  g_assert(pipeline_);

  webrtcL_ = gst_bin_get_by_name(GST_BIN(pipeline_), "smpte");
  g_signal_connect(webrtcL_,
                    "on-negotiation-needed",
                    G_CALLBACK(Scenario4::OnNegotiationNeeded),
                    (gpointer)this);
  g_signal_connect(webrtcL_,
                    "pad-added",
                    G_CALLBACK(Scenario4::WebRtcLPadAdded),
                    (gpointer)this);
  webrtcR_ = gst_bin_get_by_name(GST_BIN(pipeline_), "ball");
  g_signal_connect(webrtcR_,
                    "pad-added",
                    G_CALLBACK(Scenario4::WebRtcRPadAdded),
                    (gpointer)this);
  g_signal_connect(webrtcL_,
                    "on-ice-candidate",
                    G_CALLBACK(Scenario4::OnIceCandidate),
                    webrtcR_);
  g_signal_connect(webrtcR_,
                    "on-ice-candidate",
                    G_CALLBACK(Scenario4::OnIceCandidate),
                    webrtcL_);

  GstStateChangeReturn ret;
  ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    rootPage->UpdateStatusMessage("Failed to start play");

    stopPipeline();
    return false;
  }

  rootPage->UpdateStatusMessage("Playing");
  isPlaying_ = true;

  return true;
}

bool Scenario4::stopPipeline()
{
  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_clear_object(&webrtcL_);
    gst_clear_object(&webrtcR_);
    gst_clear_object(&pipeline_);
  }

  // Otherwise the last render image will not be cleared
  Scenario4_videoPanel_Left->Visibility =
      Windows::UI::Xaml::Visibility::Collapsed;
  Scenario4_videoPanel_Right->Visibility =
      Windows::UI::Xaml::Visibility::Collapsed;

  rootPage->UpdateStatusMessage("Ready To Play");
  isPlaying_ = false;

  return true;
}

void Scenario4::updateUIElements()
{
  btnStart->IsEnabled = !isPlaying_;
  btnStop->IsEnabled = isPlaying_;
}
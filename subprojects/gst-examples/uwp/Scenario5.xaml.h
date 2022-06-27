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
#include "Scenario5.g.h"
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>

namespace gst_uwp_example {
public
ref class Scenario5 sealed
{
public:
  Scenario5();

protected:
  virtual void OnNavigatedTo(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;
  virtual void OnNavigatedFrom(
    Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) override;

private:
  void onPageLoaded(Platform::Object ^ sender,
                    Windows::UI::Xaml::RoutedEventArgs ^ e);

private:
  // UI elements event handlers
  void btnStart_Click(Platform::Object ^ sender,
                      Windows::UI::Xaml::RoutedEventArgs ^ e);
  void btnStop_Click(Platform::Object ^ sender,
                     Windows::UI::Xaml::RoutedEventArgs ^ e);
  void btnOpenBrower_Click(Platform::Object ^ sender,
                           Windows::UI::Xaml::RoutedEventArgs ^ e);
  void btnPeerIdEnter_Click(Platform::Object ^ sender,
                            Windows::UI::Xaml::RoutedEventArgs ^ e);

  // Socket control
  Concurrency::task<void> connectAsync();
  Concurrency::task<void> sendMsgAsync(Platform::String ^ msg);
  void onMsgReceived(
    Windows::Networking::Sockets::MessageWebSocket ^ sender,
    Windows::Networking::Sockets::MessageWebSocketMessageReceivedEventArgs ^
      args);
  void onClosed(Windows::Networking::Sockets::IWebSocket ^ sender,
                Windows::Networking::Sockets::WebSocketClosedEventArgs ^ args);
  void closeSocket(void);
  void closeSocketAsyncWithMsg(Platform::String ^ msg);
  Platform::String ^ webSocketErrorToString(Platform::Exception ^ ex);

  // Managing GStreamer pipeline and callbacks,
  // WebRTC Signalling bits
  enum class AppState
  {
    INIT = 0,
    UNKNOWN_ERROR = 1, // generic error
    SERVER_CONNECTING = 1000,
    SERVER_CONNECTION_ERROR,
    SERVER_CONNECTED, // Ready to register
    SERVER_REGISTERING = 2000,
    SERVER_REGISTRATION_ERROR,
    SERVER_REGISTERED, // Ready to call a peer
    SERVER_CLOSED,     // server connection closed by us or the server
    PEER_CONNECTING = 3000,
    PEER_CONNECTION_ERROR,
    PEER_CONNECTED,
    PEER_CALL_NEGOTIATING = 4000,
    PEER_CALL_STARTED,
    PEER_CALL_STOPPING,
    PEER_CALL_STOPPED,
    PEER_CALL_ERROR,
  };

  Platform::String ^ appStateToString(AppState state);

  void handleMediaStream(GstPad* pad, bool isAudio);
  static void onDecodebinPadAdded(GstElement* dbin,
                                  GstPad* pad,
                                  gpointer userData);
  static void onIncomingStream(GstElement* webrtc,
                               GstPad* pad,
                               gpointer userData);
  static void onIceCandidate(GstElement* webrtc,
                             guint mlineindex,
                             gchar* candidate,
                             gpointer userData);
  void sendSdpToPeer(GstWebRTCSessionDescription* desc);
  static void onOfferCreated(GstPromise* promise, gpointer userData);
  static void onNegotiationNeeded(GstElement* webrtc, gpointer userData);
  static void dataChannelOnError(GObject* ch, gpointer userData);
  static void dataChannelOpen(GObject* ch, gpointer userData);
  static void dataChannelOnClose(GObject* ch, gpointer userData);
  static void dataChannelOnMsgString(GObject* ch,
                                     gchar* str,
                                     gpointer userData);
  void connectDataChannelSignals(GObject* ch);
  static void onDataChannel(GstElement* webrtc, GObject* ch, gpointer userData);
  static void onIceGatheringStateNotify(GstElement* webrtc,
                                        GParamSpec* pspec,
                                        gpointer userData);
  bool startPipeline();
  bool stopPipeline();
  void registerWithServer();
  void setupCall();
  static void onAnswerCreated(GstPromise* promise, gpointer userData);
  static void onOfferSet(GstPromise* promise, gpointer userData);
  void onOfferReceived(GstSDPMessage* sdp);
  bool handleWsMsgHello();
  bool handleWsMsgSessionOk();
  bool handleWsMsgJson(Windows::Data::Json::JsonObject ^ jsonData);

  // Misc. internal
  void updateUIElements();
  void addSignallingLog(Platform::String ^ msg);
  void addSignallingLogAsync(Platform::String ^ msg);

private:
  GstElement* pipeline_ = nullptr;
  GstElement* webrtc_ = nullptr;
  bool isPlaying_ = false;
  MainPage ^ rootPage;
  Platform::String ^ peerId_;
  AppState state_;

  Windows::Networking::Sockets::MessageWebSocket ^ socket_;
  Windows::Storage::Streams::DataWriter ^ writer_;
};
}

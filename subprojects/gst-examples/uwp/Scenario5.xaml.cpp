// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "Scenario5.xaml.h"
#include "Utils.h"
#include <cstring>
#include <gst/video/video.h>

using namespace gst_uwp_example;

using namespace concurrency;

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
using namespace Windows::System;
using namespace Windows::Networking::Sockets;
using namespace Windows::Web;
using namespace Windows::Storage::Streams;
using namespace Windows::Data::Json;

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS                                                          \
  "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

Scenario5::Scenario5()
{
  InitializeComponent();
}

void Scenario5::OnNavigatedTo(NavigationEventArgs ^ e)
{
  rootPage = MainPage::Current;
}

void Scenario5::OnNavigatedFrom(NavigationEventArgs ^ e)
{
  stopPipeline();
  rootPage->UpdateStatusMessage("");
}

void Scenario5::onPageLoaded(Platform::Object ^ sender,
                             Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  updateUIElements();
}

void Scenario5::btnStart_Click(Platform::Object ^ sender,
                               Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  state_ = AppState::INIT;

  if (peerId_->IsEmpty()) {
    rootPage->UpdateStatusMessage("Must set peer id before starting pipeline");
    return;
  }

  try {
    std::stoi(ToStdString(peerId_));
  } catch (...) {
    rootPage->UpdateStatusMessage("Invalid peer id " + peerId_);
    return;
  }

  state_ = AppState::SERVER_CONNECTING;
  connectAsync().then([this]() { registerWithServer(); });
}

void Scenario5::btnStop_Click(Platform::Object ^ sender,
                              Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  closeSocket();
}

void Scenario5::btnOpenBrower_Click(Platform::Object ^ sender,
                                    Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  auto uri = ref new Uri("https://webrtc.nirbheek.in");
  auto launchOptions = ref new LauncherOptions();

  // For dialog
  launchOptions->TreatAsUntrusted = true;

  concurrency::task<bool> launchUriOperation(
    Launcher::LaunchUriAsync(uri, launchOptions));
  launchUriOperation.then([this](bool success) {
    if (success) {
      // Nothing to do
    } else {
      // Likely user cancelled
      rootPage->UpdateStatusMessage("Couldn't open browser");
    }
  });
}

void Scenario5::btnPeerIdEnter_Click(Platform::Object ^ sender,
                                     Windows::UI::Xaml::RoutedEventArgs ^ e)
{
  peerId_ = textBoxPeerId->Text;
}

task<void>
Scenario5::connectAsync()
{
  auto uri = ref new Uri("wss://webrtc.nirbheek.in:8443");
  if (!uri) {
    rootPage->UpdateStatusMessage("Invalid Uri");
    return task_from_result();
  }

  socket_ = ref new MessageWebSocket();
  socket_->Control->MessageType = SocketMessageType::Utf8;
  socket_->MessageReceived +=
    ref new TypedEventHandler<MessageWebSocket ^,
                              MessageWebSocketMessageReceivedEventArgs ^>(
      this, &Scenario5::onMsgReceived);
  socket_->Closed +=
    ref new TypedEventHandler<IWebSocket ^, WebSocketClosedEventArgs ^>(
      this, &Scenario5::onClosed);

  addSignallingLog("Connecting to wss://webrtc.nirbheek.in:8443");

  return create_task(socket_->ConnectAsync(uri))
    .then([this](task<void> previousTask) {
      try {
        previousTask.get();
      } catch (Exception ^ ex) {
        delete socket_;
        socket_ = nullptr;

        auto err = webSocketErrorToString(ex);
        addSignallingLog(err);

        state_ = AppState::SERVER_CONNECTION_ERROR;

        return;
      }

      writer_ = ref new DataWriter(socket_->OutputStream);
      addSignallingLog("Connection Established");

      state_ = AppState::SERVER_CONNECTED;
    });
}

String ^ Scenario5::appStateToString(AppState state)
{
  switch (state) {
    case AppState::INIT:
      return "INIT";
    case AppState::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
    case AppState::SERVER_CONNECTING:
      return "SERVER_CONNECTING";
    case AppState::SERVER_CONNECTION_ERROR:
      return "SERVER_CONNECTION_ERROR";
    case AppState::SERVER_REGISTERING:
      return "SERVER_REGISTERING";
    case AppState::SERVER_REGISTRATION_ERROR:
      return "SERVER_REGISTRATION_ERROR";
    case AppState::SERVER_REGISTERED:
      return "SERVER_REGISTERED";
    case AppState::SERVER_CLOSED:
      return "SERVER_CLOSED";
    case AppState::PEER_CONNECTING:
      return "PEER_CONNECTING";
    case AppState::PEER_CONNECTION_ERROR:
      return "PEER_CONNECTION_ERROR";
    case AppState::PEER_CONNECTED:
      return "PEER_CONNECTED";
    case AppState::PEER_CALL_NEGOTIATING:
      return "PEER_CALL_NEGOTIATING";
    case AppState::PEER_CALL_STARTED:
      return "PEER_CALL_STARTED";
    case AppState::PEER_CALL_STOPPING:
      return "PEER_CALL_STOPPING";
    case AppState::PEER_CALL_STOPPED:
      return "PEER_CALL_STOPPED";
    case AppState::PEER_CALL_ERROR:
      return "PEER_CALL_ERROR";
    default:
      break;
  }

  return "Unknown";
}

task<void> Scenario5::sendMsgAsync(String ^ msg)
{
  if (msg == "") {
    addSignallingLogAsync("Empty message");
    return task_from_result();
  }

  addSignallingLogAsync("Sending Message: " + msg);

  // Buffer any data we want to send.
  writer_->WriteString(msg);

  // Send the data as one complete message.
  return create_task(writer_->StoreAsync())
    .then([this](task<unsigned int> previousTask) {
      try {
        // Reraise any exception that occurred in the task.
        previousTask.get();
      } catch (Exception ^ ex) {
        addSignallingLogAsync(webSocketErrorToString(ex));
        addSignallingLogAsync(ex->Message);
        closeSocket();
        return;
      }

      addSignallingLogAsync("Send Complete");
    });
}

bool
Scenario5::handleWsMsgHello()
{
  if (state_ != AppState::SERVER_REGISTERING) {
    addSignallingLog("HELLO is not expected in " + appStateToString(state_) +
                     " state");
    return false;
  }

  state_ = AppState::SERVER_REGISTERED;

  addSignallingLog("Registered with server");
  // Ask signalling server to connect us with a specific peer
  setupCall();

  return true;
}

bool
Scenario5::handleWsMsgSessionOk()
{
  if (state_ != AppState::PEER_CONNECTING) {
    addSignallingLog("HELLO is not expected in " + appStateToString(state_) +
                     " state");
    return false;
  }

  state_ = AppState::PEER_CONNECTED;

  return startPipeline();
}

bool Scenario5::handleWsMsgJson(JsonObject ^ jsonData)
{
  if (jsonData->HasKey("sdp")) {
    JsonObject ^ sdpObj;
    String ^ sdpType;
    String ^ sdpString;
    GstSDPMessage* sdpMsg;
    std::string sdpText;
    GstWebRTCSessionDescription* answer;
    GstSDPResult ret;

    try {
      sdpObj = jsonData->GetNamedObject("sdp");
    } catch (...) {
      addSignallingLog("Unknown json message, ignoring");
      return true;
    }

    if (state_ != AppState::PEER_CALL_NEGOTIATING) {
      addSignallingLog("SDP message is not expected in " +
                       appStateToString(state_));
      return false;
    }

    try {
      sdpType = sdpObj->GetNamedString("type");
    } catch (Exception ^ ex) {
      addSignallingLog("Couldn't get type object, exception: " + ex->Message);
      return false;
    }

    try {
      sdpString = sdpObj->GetNamedString("sdp");
    } catch (Exception ^ ex) {
      addSignallingLog("Couldn't get sdp object, exception: " + ex->Message);
      return false;
    }

    // In this example, we create the offer and receive one answer by default,
    // but it's possible to comment out the offer creation and wait for an offer
    // instead, so we handle either here.
    //
    // See tests/examples/webrtcbidirectional.c in gst-plugins-bad for another
    // example how to handle offers from peers and reply with answers using
    // webrtcbin.
    // text = json_object_get_string_member(child, "sdp");
    sdpText = ToStdString(sdpString);
    ret = gst_sdp_message_new(&sdpMsg);
    g_assert_cmphex(ret, ==, GST_SDP_OK);
    ret = gst_sdp_message_parse_buffer(
      (const guint8*)sdpText.c_str(), sdpText.size(), sdpMsg);
    g_assert_cmphex(ret, ==, GST_SDP_OK);

    if (sdpType == "answer") {
      addSignallingLog("Received answer:\n" + sdpString);
      answer =
        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdpMsg);
      g_assert_nonnull(answer);

      /* Set remote description on our pipeline */
      GstPromise* promise = gst_promise_new();
      g_signal_emit_by_name(webrtc_, "set-remote-description", answer, promise);
      gst_promise_interrupt(promise);
      gst_promise_unref(promise);

      state_ = AppState::PEER_CALL_STARTED;
    } else {
      addSignallingLog("Received offer:\n" + sdpString);
      onOfferReceived(sdpMsg);
    }
  } else if (jsonData->HasKey("ice")) {
    JsonObject ^ iceObj;
    String ^ candidateStr;
    double sdpmlineindex;

    try {
      iceObj = jsonData->GetNamedObject("ice");
    } catch (...) {
      addSignallingLog("Unknown json message, ignoring");
      return true;
    }

    try {
      candidateStr = iceObj->GetNamedString("candidate");
    } catch (Exception ^ ex) {
      addSignallingLog("Couldn't get candidate object, exception: " +
                       ex->Message);
      return false;
    }

    try {
      sdpmlineindex = iceObj->GetNamedNumber("sdpMLineIndex");
    } catch (Exception ^ ex) {
      addSignallingLog("Couldn't get sdpMLineIndex object, exception: " +
                       ex->Message);
      return false;
    }

    std::string candidate = ToStdString(candidateStr);

    /* Add ice candidate sent by remote peer */
    g_signal_emit_by_name(
      webrtc_, "add-ice-candidate", (guint)sdpmlineindex, candidate.c_str());
  } else {
    addSignallingLog("Ignoring unknown JSON message:\n" +
                     jsonData->Stringify());
  }

  return true;
}

void Scenario5::onMsgReceived(
  Windows::Networking::Sockets::MessageWebSocket ^ sender,
  Windows::Networking::Sockets::MessageWebSocketMessageReceivedEventArgs ^ args)
{
  // Dispatch the event to the UI thread so we can update UI
  Dispatcher->RunAsync(
    CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, args]() {
      addSignallingLog("Message Received, Type: " +
                       args->MessageType.ToString());
      DataReader ^ reader = args->GetDataReader();
      reader->UnicodeEncoding = UnicodeEncoding::Utf8;
      String ^ readData;
      bool result = true;

      try {
        readData = reader->ReadString(reader->UnconsumedBufferLength);
        addSignallingLog(readData);
      } catch (Exception ^ ex) {
        addSignallingLog(webSocketErrorToString(ex));
        addSignallingLog(ex->Message);
      }
      delete reader;

      if (!readData)
        return;

      std::string data = ToStdString(readData);

      if (readData == "HELLO") {
        result = handleWsMsgHello();
      } else if (readData == "SESSION_OK") {
        result = handleWsMsgSessionOk();
      } else if (g_str_has_prefix(data.c_str(), "ERROR")) {
        // Handle errors
        switch (state_) {
          case AppState::SERVER_CONNECTING:
            state_ = AppState::SERVER_CONNECTION_ERROR;
            break;
          case AppState::SERVER_REGISTERING:
            state_ = AppState::SERVER_REGISTRATION_ERROR;
            break;
          case AppState::PEER_CONNECTING:
            state_ = AppState::PEER_CONNECTION_ERROR;
            break;
          case AppState::PEER_CONNECTED:
          case AppState::PEER_CALL_NEGOTIATING:
            state_ = AppState::PEER_CALL_ERROR;
            break;
          default:
            state_ = AppState::UNKNOWN_ERROR;
        }

        addSignallingLog("Received error message " + readData);
        result = false;
      } else {
        JsonObject ^ jsonData;

        if (JsonObject::TryParse(readData, &jsonData)) {
          result = handleWsMsgJson(jsonData);
        } else {
          addSignallingLog("Unknown message, ignoring");
        }
      }

      if (!result) {
        addSignallingLog("Failed to handle message:\n" + readData);
        closeSocket();
      }
    }));
}

void Scenario5::onClosed(
  Windows::Networking::Sockets::IWebSocket ^ sender,
  Windows::Networking::Sockets::WebSocketClosedEventArgs ^ args)
{
  Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                       ref new DispatchedHandler([this, sender, args]() {
                         addSignallingLog(
                           "Closed, Code: " + args->Code.ToString() +
                           ", Reason: " + args->Reason);

                         if (socket_ == sender)
                           closeSocket();
                       }));
}

void
Scenario5::closeSocket(void)
{
  if (socket_ != nullptr) {
    addSignallingLog("Closing socket on state " + appStateToString(state_));

    try {
      socket_->Close(1000, "Closed due to user request.");
    } catch (Exception ^ ex) {
      addSignallingLog(webSocketErrorToString(ex));
      addSignallingLog(ex->Message);
    }

    socket_ = nullptr;
  }

  state_ = AppState::INIT;

  stopPipeline();
  updateUIElements();
}

void Scenario5::closeSocketAsyncWithMsg(String ^ msg)
{
  Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                       ref new DispatchedHandler([this, msg]() {
                         if (!msg->IsEmpty())
                           addSignallingLog(msg);
                         closeSocket();
                       }));
}

String ^ Scenario5::webSocketErrorToString(Exception ^ ex)
{
  WebErrorStatus status = WebSocketError::GetStatus(ex->HResult);

  switch (status) {
    case WebErrorStatus::CannotConnect:
    case WebErrorStatus::NotFound:
    case WebErrorStatus::RequestTimeout:
      return "Cannot connect to the server";
    case WebErrorStatus::Unknown:
      return "COM error: " + ex->HResult.ToString();
    default:
      break;
  }

  return "Error: " + status.ToString();
}

void
Scenario5::handleMediaStream(GstPad* pad, bool isAudio)
{
  GstPad* sinkpad;
  GstPadLinkReturn ret;
  GstElement* bin;
  GError* err = nullptr;

  if (isAudio) {
    bin = gst_parse_bin_from_description_full(
      "queue ! audioconvert ! audioresample ! wasapi2sink name=asink",
      TRUE,
      NULL,
      GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS,
      &err);
  } else {
    bin = gst_parse_bin_from_description_full(
      "queue ! videoconvert ! d3d11videosink name=vsink",
      TRUE,
      NULL,
      GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS,
      &err);
  }

  if (!bin) {
    String ^ msg =
      "Couldn't configure " + isAudio
        ? "audio"
        : "video" + " render branch, error: " + ToPlatformString(err->message);

    g_clear_error(&err);
    closeSocketAsyncWithMsg(msg);
    return;
  }

  if (isAudio) {
    GstElement* asink = gst_bin_get_by_name(GST_BIN(bin), "asink");

    g_assert_nonnull(asink);
    // Pass our dispacher so that audiosink can activate device from UI thread
    g_object_set(
      asink, "dispatcher", reinterpret_cast<gpointer>(Dispatcher), nullptr);
    gst_object_unref(asink);
  } else {
    GstElement* vsink = gst_bin_get_by_name(GST_BIN(bin), "vsink");

    // Set our swapchain panel handle
    gst_video_overlay_set_window_handle(
      GST_VIDEO_OVERLAY(vsink),
      (guintptr) reinterpret_cast<IUnknown*>(Scenario5_videoPanel));
    gst_object_unref(vsink);
  }

  g_assert(gst_bin_add(GST_BIN(pipeline_), bin));
  gst_element_sync_state_with_parent(bin);

  sinkpad = gst_element_get_static_pad(bin, "sink");
  g_assert_nonnull(sinkpad);

  GstPadLinkReturn linkRet;
  linkRet = gst_pad_link(pad, sinkpad);
  g_assert(linkRet == GST_PAD_LINK_OK);
  gst_object_unref(sinkpad);

  return;
}

void
Scenario5::onDecodebinPadAdded(GstElement* dbin, GstPad* pad, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);
  GstCaps* caps;
  const gchar* name;

  if (!gst_pad_has_current_caps(pad)) {
    self->addSignallingLogAsync("Pad '" + ToPlatformString(GST_PAD_NAME(pad)) +
                                "' has no caps, can't do anything, ignoring");
    return;
  }

  caps = gst_pad_get_current_caps(pad);
  name = gst_structure_get_name(gst_caps_get_structure(caps, 0));

  bool isAudio = true;
  if (g_str_has_prefix(name, "video")) {
    isAudio = false;
  } else if (!g_str_has_prefix(name, "audio")) {
    self->addSignallingLogAsync(
      "Unknown '" + ToPlatformString(GST_PAD_NAME(pad)) + "' ignoring");
    return;
  }

  self->handleMediaStream(pad, isAudio);
}

void
Scenario5::onIncomingStream(GstElement* webrtc, GstPad* pad, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);
  GstElement* dbin;
  GstPad* sinkpad;

  if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
    return;

  dbin = gst_element_factory_make("decodebin", nullptr);
  g_assert_nonnull(dbin);
  g_signal_connect(dbin,
                   "pad-added",
                   G_CALLBACK(Scenario5::onDecodebinPadAdded),
                   reinterpret_cast<gpointer>(self));
  gst_bin_add(GST_BIN(self->pipeline_), dbin);
  sinkpad = gst_element_get_static_pad(dbin, "sink");
  gst_pad_link(pad, sinkpad);
  gst_object_unref(sinkpad);

  gst_element_sync_state_with_parent(dbin);
}

void
Scenario5::onIceCandidate(GstElement* webrtc,
                          guint mlineindex,
                          gchar* candidate,
                          gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  if ((int)self->state_ < (int)AppState::PEER_CALL_NEGOTIATING) {
    self->closeSocketAsyncWithMsg("Can't send ICE, not in call");
    return;
  }

  auto ice = ref new JsonObject();
  ice->SetNamedValue("candidate",
                     JsonValue::CreateStringValue(ToPlatformString(candidate)));
  ice->SetNamedValue("sdpMLineIndex", JsonValue::CreateNumberValue(mlineindex));

  auto msg = ref new JsonObject();
  msg->SetNamedValue("ice", ice);

  String ^ text = msg->Stringify();

  self->addSignallingLogAsync("Sending ICE candidate message: " + text);
  self->sendMsgAsync(text);
}

void
Scenario5::sendSdpToPeer(GstWebRTCSessionDescription* desc)
{
  gchar* text;
  String ^ str;

  if ((int)state_ < (int)AppState::PEER_CALL_NEGOTIATING) {
    closeSocketAsyncWithMsg("Can't send SDP to peer, not in call");
    return;
  }

  text = gst_sdp_message_as_text(desc->sdp);
  str = ToPlatformString(text);
  g_free(text);

  auto sdp = ref new JsonObject();

  if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
    addSignallingLogAsync("Sending offer: \n" + str);
    sdp->SetNamedValue("type", JsonValue::CreateStringValue("offer"));
  } else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    addSignallingLogAsync("Sending answer: \n" + str);
    sdp->SetNamedValue("type", JsonValue::CreateStringValue("answer"));
  } else {
    g_assert_not_reached();
  }

  sdp->SetNamedValue("sdp", JsonValue::CreateStringValue(str));

  auto msg = ref new JsonObject();
  msg->SetNamedValue("sdp", sdp);

  sendMsgAsync(msg->Stringify());
}

// Offer created by our pipeline, to be sent to the peer
void
Scenario5::onOfferCreated(GstPromise* promise, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);
  GstWebRTCSessionDescription* offer = NULL;
  const GstStructure* reply;

  if (self->state_ != AppState::PEER_CALL_NEGOTIATING) {
    self->closeSocketAsyncWithMsg("Offer is created at unexpected state " +
                                  self->appStateToString(self->state_));
    return;
  }

  g_assert_cmphex(gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);

  reply = gst_promise_get_reply(promise);
  gst_structure_get(
    reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
  gst_promise_unref(promise);

  g_assert(self->webrtc_);

  promise = gst_promise_new();
  g_signal_emit_by_name(self->webrtc_, "set-local-description", offer, promise);
  gst_promise_interrupt(promise);
  gst_promise_unref(promise);

  /* Send offer to peer */
  self->sendSdpToPeer(offer);
  gst_webrtc_session_description_free(offer);
}

void
Scenario5::onNegotiationNeeded(GstElement* webrtc, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->state_ = AppState::PEER_CALL_NEGOTIATING;

  // TODO: Handle remote-is-offfer case
  GstPromise* promise;
  promise = gst_promise_new_with_change_func(
    (GstPromiseChangeFunc)Scenario5::onOfferCreated, userData, nullptr);
  g_signal_emit_by_name(self->webrtc_, "create-offer", nullptr, promise);
}

void
Scenario5::dataChannelOnError(GObject* ch, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->closeSocketAsyncWithMsg("Data channel error");
}

void
Scenario5::dataChannelOpen(GObject* ch, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->addSignallingLogAsync("Data channel opened");

  GBytes* bytes = g_bytes_new("data", strlen("data"));
  g_signal_emit_by_name(ch, "send-string", "Hi! from GStreamer");
  g_signal_emit_by_name(ch, "send-data", bytes);
  g_bytes_unref(bytes);
}

void
Scenario5::dataChannelOnClose(GObject* ch, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->closeSocketAsyncWithMsg("Data channel closed");
}

void
Scenario5::dataChannelOnMsgString(GObject* ch, gchar* str, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->addSignallingLogAsync("Received data channel message: " +
                              ToPlatformString(str));
}

void
Scenario5::connectDataChannelSignals(GObject* ch)
{
  gpointer userData = reinterpret_cast<gpointer>(this);

  g_signal_connect(
    ch, "on-error", G_CALLBACK(Scenario5::dataChannelOnError), userData);
  g_signal_connect(
    ch, "on-open", G_CALLBACK(Scenario5::dataChannelOpen), userData);
  g_signal_connect(
    ch, "on-close", G_CALLBACK(Scenario5::dataChannelOnClose), userData);
  g_signal_connect(ch,
                   "on-message-string",
                   G_CALLBACK(Scenario5::dataChannelOnMsgString),
                   userData);
}

void
Scenario5::onDataChannel(GstElement* webrtc, GObject* ch, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  self->addSignallingLogAsync("On data channel signal");
  self->connectDataChannelSignals(ch);
}

void
Scenario5::onIceGatheringStateNotify(GstElement* webrtc,
                                     GParamSpec* pspec,
                                     gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);
  GstWebRTCICEGatheringState state;
  String ^ newState = "unknown";

  g_object_get(webrtc, "ice-gathering-state", &state, NULL);
  switch (state) {
    case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
      newState = "new";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
      newState = "gathering";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
      newState = "complete";
      break;
    default:
      break;
  }

  self->addSignallingLogAsync("ICE gathering state changed to " + newState);
}

// This method will be called from UI thread
bool
Scenario5::startPipeline()
{
  GstStateChangeReturn ret;
  GError* err = nullptr;

  // Clear previous pipeline if any
  stopPipeline();

  pipeline_ = gst_parse_launch(
    "webrtcbin bundle-policy=max-bundle name=sendrecv " STUN_SERVER
    "videotestsrc is-live=true pattern=ball ! videoconvert ! queue ! vp8enc "
    "deadline=1 ! rtpvp8pay ! "
    "queue ! " RTP_CAPS_VP8 "96 ! sendrecv. "
    "audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! "
    "queue ! opusenc ! rtpopuspay ! "
    "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. ",
    &err);

  if (err) {
    addSignallingLog("Failed to parse launch: " +
                     ToPlatformString(err->message));
    g_clear_error(&err);
    return false;
  }

  webrtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sendrecv");
  g_assert_nonnull(webrtc_);

  // This is the gstwebrtc entry point where we create the offer and so on. It
  // will be called when the pipeline goes to PLAYING.
  g_signal_connect(webrtc_,
                   "on-negotiation-needed",
                   G_CALLBACK(Scenario5::onNegotiationNeeded),
                   reinterpret_cast<gpointer>(this));

  // We need to transmit this ICE candidate to the browser via the websockets
  // signalling server. Incoming ice candidates from the browser need to be
  // added by us too, see on_server_message()
  g_signal_connect(webrtc_,
                   "on-ice-candidate",
                   G_CALLBACK(Scenario5::onIceCandidate),
                   reinterpret_cast<gpointer>(this));
  g_signal_connect(webrtc_,
                   "notify::ice-gathering-state",
                   G_CALLBACK(Scenario5::onIceGatheringStateNotify),
                   reinterpret_cast<gpointer>(this));

  gst_element_set_state(pipeline_, GST_STATE_READY);

  GObject* sendChannel = nullptr;
  g_signal_emit_by_name(
    webrtc_, "create-data-channel", "channel", nullptr, &sendChannel);
  if (sendChannel) {
    addSignallingLog("Created data channel");
    connectDataChannelSignals(sendChannel);
    g_object_unref(sendChannel);
  } else {
    addSignallingLog("Could not create data channel, is usrsctp available?");
  }

  g_signal_connect(webrtc_,
                   "on-data-channel",
                   G_CALLBACK(Scenario5::onDataChannel),
                   reinterpret_cast<gpointer>(this));

  // Incoming streams will be exposed via this signal
  g_signal_connect(webrtc_,
                   "pad-added",
                   G_CALLBACK(Scenario5::onIncomingStream),
                   reinterpret_cast<gpointer>(this));

  rootPage->UpdateStatusMessage("Starting pipeline");
  ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    rootPage->UpdateStatusMessage("Failed to start play");

    stopPipeline();
    return false;
  }

  Scenario5_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;

  rootPage->UpdateStatusMessage("Playing");
  updateUIElements();

  return true;
}

bool
Scenario5::stopPipeline()
{
  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_clear_object(&pipeline_);
    gst_clear_object(&webrtc_);
  }

  // Otherwise the last render image will not be cleared
  Scenario5_videoPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

  rootPage->UpdateStatusMessage("Ready To Play");

  return true;
}

void
Scenario5::registerWithServer()
{
  String ^ hello;
  String ^ idString;
  int id;

  id = g_random_int_range(10, 10000);

  idString = ToPlatformString(std::to_string(id));

  String ^ msg = "Registering id " + idString + " with server";
  addSignallingLog(msg);

  // Register with the server with a random integer id. Reply will be received
  // by on_server_message()
  hello = "HELLO " + idString;

  state_ = AppState::SERVER_REGISTERING;
  sendMsgAsync(hello);
}

void
Scenario5::setupCall()
{
  addSignallingLog("Setting up signalling server call with " + peerId_);

  String ^ msg = "SESSION " + peerId_;

  state_ = AppState::PEER_CONNECTING;
  sendMsgAsync(msg);
}

void
Scenario5::onAnswerCreated(GstPromise* promise, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);
  GstWebRTCSessionDescription* answer = nullptr;
  const GstStructure* reply;

  if (self->state_ != AppState::PEER_CALL_NEGOTIATING) {
    self->closeSocketAsyncWithMsg("Answer is created at unexpected state " +
                                  self->appStateToString(self->state_));
    return;
  }

  g_assert_cmphex(gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply(promise);
  gst_structure_get(
    reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
  gst_promise_unref(promise);

  promise = gst_promise_new();
  g_signal_emit_by_name(
    self->webrtc_, "set-local-description", answer, promise);
  gst_promise_interrupt(promise);
  gst_promise_unref(promise);

  // Send answer to peer
  self->sendSdpToPeer(answer);
  gst_webrtc_session_description_free(answer);
}

void
Scenario5::onOfferSet(GstPromise* promise, gpointer userData)
{
  Scenario5 ^ self = reinterpret_cast<Scenario5 ^>(userData);

  gst_promise_unref(promise);
  promise = gst_promise_new_with_change_func(
    (GstPromiseChangeFunc)Scenario5::onAnswerCreated, userData, nullptr);
  g_signal_emit_by_name(self->webrtc_, "create-answer", nullptr, promise);
}

void
Scenario5::onOfferReceived(GstSDPMessage* sdp)
{
  GstWebRTCSessionDescription* offer = nullptr;
  GstPromise* promise;

  offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  g_assert_nonnull(offer);

  // Set remote description on our pipeline
  promise = gst_promise_new_with_change_func(
    (GstPromiseChangeFunc)Scenario5::onOfferSet,
    reinterpret_cast<gpointer>(this),
    nullptr);
  g_signal_emit_by_name(webrtc_, "set-remote-description", offer, promise);
  gst_webrtc_session_description_free(offer);
}

void
Scenario5::updateUIElements(void)
{
  btnStart->IsEnabled = state_ == AppState::INIT;
  btnStop->IsEnabled = state_ != AppState::INIT;
  ;
}

void Scenario5::addSignallingLog(String ^ msg)
{
  SignallingLoggingListBox->Items->InsertAt(0, msg);
}

void Scenario5::addSignallingLogAsync(Platform::String ^ msg)
{
  Dispatcher->RunAsync(
    CoreDispatcherPriority::Normal,
    ref new DispatchedHandler([this, msg]() { addSignallingLog(msg); }));
}

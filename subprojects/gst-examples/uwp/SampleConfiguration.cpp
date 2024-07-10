// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "SampleConfiguration.h"
#include "MainPage.xaml.h"

using namespace gst_uwp_example;

Platform::Array<Scenario> ^ MainPage::scenariosInner =
  ref new Platform::Array<Scenario>{
    { "Display on SwapChainPanel", "gst_uwp_example.Scenario1" },
    { "Audio/Video Capture", "gst_uwp_example.Scenario2" },
    { "OpenGL rendering", "gst_uwp_example.Scenario3" },
    { "WebRTC bidirectional", "gst_uwp_example.Scenario4" },
    { "WebRTC Send/Receive", "gst_uwp_example.Scenario5" },
  };
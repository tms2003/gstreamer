// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "pch.h"

namespace gst_uwp_example {
value struct Scenario;

partial ref class MainPage
{
  internal
    : static property Platform::String ^
      FEATURE_NAME {
        Platform::String ^ get() { return "GStreamer UWP Sample"; }
      }

      static property Platform::Array<Scenario> ^
      scenarios { Platform::Array<Scenario> ^ get() { return scenariosInner; } }

      private
    : static Platform::Array<Scenario> ^
      scenariosInner;
};

public
value struct Scenario
{
  Platform::String ^ Title;
  Platform::String ^ ClassName;
};
}
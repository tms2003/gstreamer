// Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "pch.h"
#include <string>

namespace gst_uwp_example {
static Platform::String ^
  ToPlatformString(const std::string& input) {
    std::wstring w_str = std::wstring(input.begin(), input.end());
    const wchar_t* w_chars = w_str.c_str();
    return (ref new Platform::String(w_chars));
  }

  static std::string ToStdString(Platform::String ^ input)
{
  std::wstring w_str(input->Begin());
  return std::string(w_str.begin(), w_str.end());
}
}

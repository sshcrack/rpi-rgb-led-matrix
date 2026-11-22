// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2023 Hendrik
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// RGBMatrix::Options and RuntimeOptions for builds without the hardware
// backend (native Windows), where options-initialize.cc is not compiled.

#include "led-matrix.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace rgb_matrix {

RGBMatrix::Options::Options()
    : hardware_mapping("regular"), rows(32), cols(32), chain_length(1), parallel(1),
      pwm_bits(11), pwm_lsb_nanoseconds(130), pwm_dither_bits(0), brightness(100),
      scan_mode(0), row_address_type(0), multiplexing(0), disable_hardware_pulsing(false),
      show_refresh_rate(false), inverse_colors(false), led_rgb_sequence("RGB"),
      pixel_mapper_config(nullptr), panel_type(nullptr), limit_refresh_rate_hz(0),
      disable_busy_waiting(false) {}

bool RGBMatrix::Options::Validate(std::string *err_in) const {
  std::string scratch;
  std::string *err = err_in ? err_in : &scratch;
  bool valid = true;
  const auto fail = [&](const char *message) {
    err->append(message);
    err->push_back('\n');
    valid = false;
  };

  if (rows < 8 || rows > 64 || rows % 2 != 0) fail("rows must be even and between 8 and 64");
  if (cols < 16) fail("cols must be at least 16");
  if (chain_length < 1) fail("chain_length must be positive");
  if (parallel < 1 || parallel > 3) fail("parallel must be between 1 and 3");
  if (brightness < 1 || brightness > 100) fail("brightness must be between 1 and 100");
  if (pwm_bits < 1 || pwm_bits > 11) fail("pwm_bits must be between 1 and 11");
  if (scan_mode < 0 || scan_mode > 1) fail("scan_mode must be 0 or 1");
  if (row_address_type < 0 || row_address_type > 5) fail("row_address_type must be between 0 and 5");
  if (multiplexing < 0) fail("multiplexing must not be negative");
  if (pwm_lsb_nanoseconds < 50 || pwm_lsb_nanoseconds > 3000) fail("pwm_lsb_nanoseconds must be between 50 and 3000");
  if (pwm_dither_bits < 0 || pwm_dither_bits > 2) fail("pwm_dither_bits must be between 0 and 2");
  if (!led_rgb_sequence || std::strlen(led_rgb_sequence) != 3 ||
      (!std::strchr(led_rgb_sequence, 'R') && !std::strchr(led_rgb_sequence, 'r')) ||
      (!std::strchr(led_rgb_sequence, 'G') && !std::strchr(led_rgb_sequence, 'g')) ||
      (!std::strchr(led_rgb_sequence, 'B') && !std::strchr(led_rgb_sequence, 'b'))) {
    fail("led_rgb_sequence must contain R, G and B");
  }
  if (!valid && !err_in) std::fprintf(stderr, "%s", err->c_str());
  return valid;
}

RuntimeOptions::RuntimeOptions()
    : gpio_slowdown(1), daemon(0), drop_privileges(0), do_gpio_init(false),
      drop_priv_user(nullptr), drop_priv_group(nullptr) {}

}  // namespace rgb_matrix

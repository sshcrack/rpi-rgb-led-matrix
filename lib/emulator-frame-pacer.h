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

// Frame pacing and redraw tracking shared by the SDL and headless emulator.
// The emulator has no refresh thread: SwapOnVSync() draws the frame on the
// caller's thread, and these helpers decide when it blocks and whether a
// later Render() has anything new to draw.

#ifndef RPI_RGBMATRIX_EMULATOR_FRAME_PACER_H
#define RPI_RGBMATRIX_EMULATOR_FRAME_PACER_H

#include <chrono>
#include <cstdint>
#include <thread>

namespace rgb_matrix {
namespace internal {

// Makes SwapOnVSync() block like a hardware vsync at refresh_hz, showing a
// frame only every framerate_fraction refreshes. Disabled, it never blocks.
class EmulatorFramePacer {
public:
  EmulatorFramePacer(bool enabled, int refresh_hz)
      : enabled_(enabled && refresh_hz > 0),
        refresh_interval_(enabled_ ? std::chrono::nanoseconds(1000000000LL / refresh_hz)
                                   : std::chrono::nanoseconds(0)) {}

  bool enabled() const { return enabled_; }

  // Blocks until the next frame may be shown.
  void WaitForVSync(unsigned framerate_fraction) {
    if (!enabled_) return;
    if (framerate_fraction < 1) framerate_fraction = 1;
    const auto interval = refresh_interval_ * framerate_fraction;
    const auto now = Clock::now();
    if (!started_ || next_frame_ + interval < now) {
      // First frame, or more than a frame late: show now and restart the
      // cadence instead of bursting to catch up.
      next_frame_ = now;
      started_ = true;
    } else {
      std::this_thread::sleep_until(next_frame_);
    }
    next_frame_ += interval;
  }

private:
  using Clock = std::chrono::steady_clock;
  const bool enabled_;
  const std::chrono::nanoseconds refresh_interval_;
  bool started_ = false;
  Clock::time_point next_frame_;
};

// Identifies what was last drawn, so Render() can skip a frame that
// SwapOnVSync() already showed. Content changes are detected with a
// generation counter that the emulator framebuffers bump on every write.
class EmulatorDrawnFrame {
public:
  bool Matches(const void *canvas, uint64_t generation, uint8_t brightness) const {
    return valid_ && canvas == canvas_ && generation == generation_ &&
           brightness == brightness_;
  }
  void Set(const void *canvas, uint64_t generation, uint8_t brightness) {
    valid_ = true;
    canvas_ = canvas;
    generation_ = generation;
    brightness_ = brightness;
  }

private:
  bool valid_ = false;
  const void *canvas_ = nullptr;
  uint64_t generation_ = 0;
  uint8_t brightness_ = 0;
};

}  // namespace internal
}  // namespace rgb_matrix

#endif  // RPI_RGBMATRIX_EMULATOR_FRAME_PACER_H

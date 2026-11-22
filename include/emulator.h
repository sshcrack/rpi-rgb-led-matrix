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

#ifndef RPI_RGBMATRIX_EMULATOR_H
#define RPI_RGBMATRIX_EMULATOR_H

#include "led-matrix.h"
#include <string>

// This header is the same in every build. In a library built without
// emulator support (ENABLE_EMULATOR), EmulatorMatrix::Create() returns
// nullptr, so a program can use this API unconditionally and check at runtime.
// ENABLE_EMULATOR is still defined for CMake consumers when the library
// supports the emulator; EmulatorMatrix::Supported() tells at runtime.

namespace rgb_matrix {

// EmulatorOptions contains configuration for the matrix emulator
struct EmulatorOptions {
  EmulatorOptions();
  
  // Window scaling factor (default: 10)
  int display_scale;
  
  // Window title (default: "RGB Matrix Emulator")
  std::string window_title;

  // Pace SwapOnVSync() like hardware even when headless (default: false).
  // With a window, swaps are always paced. Headless emulators (render
  // workers, tests) run at full speed unless this is set.
  bool emulate_hardware_timing;

  // Display refresh rate in Hz used for pacing (default: 60). As on hardware,
  // SwapOnVSync(canvas, n) shows a frame every n refreshes.
  int refresh_rate_hz;

  // Path to export each rendered frame as raw binary (width u32, height u32, RGB bytes).
  // If empty, no frame export is performed. (default: "")
  std::string frame_export_path;

  // Run without a visible window using SDL offscreen renderer. (default: false)
  bool headless;

};

// EmulatorMatrix is an implementation of RGBMatrix that shows the matrix content
// in a window rather than requiring physical GPIO hardware
class EmulatorMatrix : public RGBMatrixBase {
public:
  // True if this library was built with emulator support.
  static bool Supported();

  // Create a new EmulatorMatrix with the given options. Returns nullptr if
  // the options are invalid, the window can't be opened, or the library was
  // built without emulator support.
  static EmulatorMatrix *Create(const RGBMatrix::Options &options,
                                const EmulatorOptions &emulator_options);
  
  // Destructor
  virtual ~EmulatorMatrix();

  EmulatorMatrix *AsEmulator() override { return this; }
  
  // Canvas interface implementation
  virtual int width() const;
  virtual int height() const;
  virtual void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue);
  virtual bool GetPixel(int x, int y, uint8_t *red, uint8_t *green, uint8_t *blue) const;
  virtual void Clear();
  virtual void Fill(uint8_t red, uint8_t green, uint8_t blue);
  
  // Create a new off-screen canvas
  FrameCanvas *CreateFrameCanvas();
  
  // Make `other` the displayed canvas and show it: draw the window and/or
  // write the frame export, on the calling thread. When paced (see
  // EmulatorOptions::emulate_hardware_timing), first blocks until the next
  // refresh, like hardware. Returns the previously displayed canvas.
  FrameCanvas *SwapOnVSync(FrameCanvas *other, unsigned framerate_fraction = 1);
  
  // Apply pixel mapper to the matrix
  bool ApplyPixelMapper(const PixelMapper *mapper);
  
  // Set brightness
  void SetBrightness(uint8_t brightness);
  uint8_t brightness();
  
  // Set PWM bits
  bool SetPWMBits(uint8_t value);
  uint8_t pwmbits();
  
  // Luminance correction
  void set_luminance_correct(bool on);
  bool luminance_correct() const;
  
  // No-op that returns true: there is no refresh thread, frames are shown
  // by SwapOnVSync() and Render() on the caller's thread.
  bool StartRefresh();

  // Show the displayed canvas now. Only needed by programs that draw
  // straight onto the matrix (SetPixel(), Fill(), ...) without swapping.
  // Does nothing if this exact content was already shown, so calling it
  // after SwapOnVSync() does not draw a frame twice.
  void Render();

private:
  class Impl;
  EmulatorMatrix(Impl *impl);
  
  Impl *const impl_;
};

// Parse emulator options from command line
bool ParseEmulatorOptionsFromFlags(int *argc, char ***argv,
                                  EmulatorOptions *default_options,
                                  bool remove_consumed_flags = true);

// Print emulator-specific flags help
void PrintEmulatorFlags(FILE *out, const EmulatorOptions &defaults = EmulatorOptions());

}  // namespace rgb_matrix

#endif  // RPI_RGBMATRIX_EMULATOR_H

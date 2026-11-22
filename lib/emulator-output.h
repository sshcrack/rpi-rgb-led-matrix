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

// Ways of displaying emulator frames. The portable emulator core
// (emulator-core.cc) hands every shown frame to its outputs: the SDL window
// (emulator-sdl.cc, only in builds with ENABLE_EMULATOR_SDL), the frame export
// file, and so on.

#ifndef RPI_RGBMATRIX_EMULATOR_OUTPUT_H
#define RPI_RGBMATRIX_EMULATOR_OUTPUT_H

#include <cstdint>
#include <memory>
#include <string>

namespace rgb_matrix {
struct EmulatorOptions;

namespace internal {

class EmulatorOutput {
public:
  virtual ~EmulatorOutput() = default;

  // Shows one frame: packed row-major RGB888 with brightness applied. The
  // pointer is only valid during the call.
  virtual void ShowFrame(const uint8_t *rgb, int width, int height) = 0;

  // Called on every swap and Render(), even without a new frame, so a window
  // stays responsive.
  virtual void PumpEvents() {}
};

// Writes each frame to `path`: width and height as native-endian uint32,
// then the RGB bytes. Writes `path`.tmp and renames it, so readers never see
// a partial frame.
std::unique_ptr<EmulatorOutput> CreateFrameExportOutput(const std::string &path);

// True if this library has the SDL window output.
bool EmulatorHasSdlOutput();

// Opens the SDL window. Returns nullptr if it can't be opened, or if the
// library has no SDL output.
std::unique_ptr<EmulatorOutput> CreateSdlOutput(int width, int height,
                                                const EmulatorOptions &options);

}  // namespace internal
}  // namespace rgb_matrix

#endif  // RPI_RGBMATRIX_EMULATOR_OUTPUT_H

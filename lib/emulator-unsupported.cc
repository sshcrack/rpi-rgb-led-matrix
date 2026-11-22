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

// The emulator API for libraries built without emulator support. emulator.h
// is the same in every build, so programs using it link against any build;
// here EmulatorMatrix::Create() fails cleanly and no EmulatorMatrix exists.

#include "emulator.h"

#include <cstdio>

namespace rgb_matrix {

EmulatorOptions::EmulatorOptions()
    : display_scale(10), window_title("RGB Matrix Emulator"),
      emulate_hardware_timing(false), refresh_rate_hz(60), frame_export_path(""),
      headless(false), show_physical_layout(false) {}

bool EmulatorMatrix::Supported() { return false; }

EmulatorMatrix *EmulatorMatrix::Create(const RGBMatrix::Options &,
                                       const EmulatorOptions &) {
  fprintf(stderr, "Can't create an emulator: the rgbmatrix library was built "
                  "without emulator support (ENABLE_EMULATOR).\n");
  return nullptr;
}

// Create() never returns an instance, so the members below are never called.
// They exist so programs using the emulator API link.
EmulatorMatrix::EmulatorMatrix(Impl *impl) : impl_(impl) {}
EmulatorMatrix::~EmulatorMatrix() {}
int EmulatorMatrix::width() const { return 0; }
int EmulatorMatrix::height() const { return 0; }
void EmulatorMatrix::SetPixel(int, int, uint8_t, uint8_t, uint8_t) {}
bool EmulatorMatrix::GetPixel(int, int, uint8_t *, uint8_t *, uint8_t *) const { return false; }
void EmulatorMatrix::Clear() {}
void EmulatorMatrix::Fill(uint8_t, uint8_t, uint8_t) {}
FrameCanvas *EmulatorMatrix::CreateFrameCanvas() { return nullptr; }
FrameCanvas *EmulatorMatrix::SwapOnVSync(FrameCanvas *, unsigned) { return nullptr; }
bool EmulatorMatrix::ApplyPixelMapper(const PixelMapper *) { return false; }
void EmulatorMatrix::SetBrightness(uint8_t) {}
uint8_t EmulatorMatrix::brightness() { return 0; }
bool EmulatorMatrix::SetPWMBits(uint8_t) { return false; }
uint8_t EmulatorMatrix::pwmbits() { return 0; }
void EmulatorMatrix::set_luminance_correct(bool) {}
bool EmulatorMatrix::luminance_correct() const { return false; }
bool EmulatorMatrix::StartRefresh() { return false; }
void EmulatorMatrix::Render() {}
void EmulatorMatrix::SetFrameCallback(FrameCallback) {}

}  // namespace rgb_matrix

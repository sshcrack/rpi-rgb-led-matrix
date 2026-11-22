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

// The portable emulator core, built on every platform with ENABLE_EMULATOR.
// It owns the canvases, swapping, pacing and brightness/PWM settings, and
// hands each shown frame to its outputs (emulator-output.h). It needs no SDL;
// the SDL window is one optional output.

#include "emulator.h"

#include "emulator-frame-pacer.h"
#include "emulator-output.h"
#include "framebuffer-interface.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rgb_matrix {

EmulatorOptions::EmulatorOptions()
    : display_scale(10), window_title("RGB Matrix Emulator"),
      emulate_hardware_timing(false), refresh_rate_hz(60), frame_export_path(""),
      // Without the SDL output there is no window to show.
      headless(!internal::EmulatorHasSdlOutput()) {}

namespace internal {
namespace {

uint8_t ClampBrightness(int value) {
  return static_cast<uint8_t>(std::max(1, std::min(100, value)));
}

// Software framebuffer holding packed RGB888 pixels.
class EmulatorFramebuffer final : public FramebufferInterface {
public:
  EmulatorFramebuffer(int width, int height)
      : width_(width), height_(height),
        pixels_(static_cast<size_t>(width) * static_cast<size_t>(height) * 3U, 0) {}

  int width() const override { return width_; }
  int height() const override { return height_; }

  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    ++generation_;
    const size_t offset = (static_cast<size_t>(y) * width_ + x) * 3U;
    pixels_[offset] = r;
    pixels_[offset + 1] = g;
    pixels_[offset + 2] = b;
  }

  bool GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    const size_t offset = (static_cast<size_t>(y) * width_ + x) * 3U;
    if (r) *r = pixels_[offset];
    if (g) *g = pixels_[offset + 1];
    if (b) *b = pixels_[offset + 2];
    return true;
  }

  void Clear() override {
    ++generation_;
    std::fill(pixels_.begin(), pixels_.end(), uint8_t{0});
  }

  void Fill(uint8_t r, uint8_t g, uint8_t b) override {
    ++generation_;
    for (size_t i = 0; i < pixels_.size(); i += 3) {
      pixels_[i] = r;
      pixels_[i + 1] = g;
      pixels_[i + 2] = b;
    }
  }

  bool SetPWMBits(uint8_t value) override {
    if (value < 1 || value > 11) return false;
    pwm_bits_ = value;
    return true;
  }
  uint8_t pwmbits() override { return pwm_bits_; }
  void set_luminance_correct(bool on) override { luminance_correct_ = on; }
  bool luminance_correct() const override { return luminance_correct_; }
  void SetBrightness(uint8_t value) override { brightness_ = ClampBrightness(value); }
  uint8_t brightness() override { return brightness_; }

  void Serialize(const char **data, size_t *len) const override {
    *data = reinterpret_cast<const char *>(pixels_.data());
    *len = pixels_.size();
  }

  bool Deserialize(const char *data, size_t len) override {
    if (!data || len != pixels_.size()) return false;
    ++generation_;
    std::memcpy(pixels_.data(), data, len);
    return true;
  }

  void CopyFrom(const FramebufferInterface *other) override {
    if (!other || other == this || other->width() != width_ || other->height() != height_) return;
    const char *data = nullptr;
    size_t len = 0;
    other->Serialize(&data, &len);
    Deserialize(data, len);
  }

  const uint8_t *rgb() const { return pixels_.data(); }

  // Bumped on every pixel write, so Render() can tell whether the content
  // changed since it was last shown.
  uint64_t generation() const { return generation_; }

private:
  const int width_;
  const int height_;
  std::vector<uint8_t> pixels_;
  uint8_t brightness_ = 100;
  uint8_t pwm_bits_ = 8;
  bool luminance_correct_ = false;
  uint64_t generation_ = 0;
};

class FrameExportOutput final : public EmulatorOutput {
public:
  explicit FrameExportOutput(const std::string &path) : path_(path), tmp_path_(path + ".tmp") {}

  void ShowFrame(const uint8_t *rgb, int width, int height) override {
    FILE *file = std::fopen(tmp_path_.c_str(), "wb");
    if (!file) return;
    const uint32_t size[2] = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    const size_t bytes = static_cast<size_t>(width) * height * 3U;
    const bool ok = std::fwrite(size, sizeof(size), 1, file) == 1 &&
                    std::fwrite(rgb, 1, bytes, file) == bytes;
    if (std::fclose(file) != 0 || !ok) {
      std::remove(tmp_path_.c_str());
      return;
    }
#ifdef _WIN32
    std::remove(path_.c_str());  // rename() doesn't replace on Windows.
#endif
    std::rename(tmp_path_.c_str(), path_.c_str());
  }

private:
  const std::string path_;
  const std::string tmp_path_;
};

}  // namespace

std::unique_ptr<EmulatorOutput> CreateFrameExportOutput(const std::string &path) {
  return std::unique_ptr<EmulatorOutput>(new FrameExportOutput(path));
}

#ifndef ENABLE_EMULATOR_SDL
bool EmulatorHasSdlOutput() { return false; }
std::unique_ptr<EmulatorOutput> CreateSdlOutput(int, int, const EmulatorOptions &) {
  return nullptr;
}
#endif

}  // namespace internal

class EmulatorMatrix::Impl {
public:
  Impl(const RGBMatrix::Options &options, const EmulatorOptions &emulator_options,
       std::vector<std::unique_ptr<internal::EmulatorOutput>> outputs, bool has_window)
      : width(options.cols * options.chain_length),
        height(options.rows * options.parallel),
        brightness(internal::ClampBrightness(options.brightness)),
        pwm_bits(static_cast<uint8_t>(std::max(1, std::min(11, options.pwm_bits)))),
        outputs(std::move(outputs)),
        // A window is paced like a hardware display. Headless render workers
        // run at full speed unless they ask for hardware timing.
        pacer(emulator_options.emulate_hardware_timing || has_window,
              emulator_options.refresh_rate_hz) {}

  ~Impl() {
    for (auto *canvas : canvases) delete canvas;
  }

  FrameCanvas *CreateFrameCanvas() {
    auto *canvas = new FrameCanvas(new internal::EmulatorFramebuffer(width, height));
    canvas->SetBrightness(brightness);
    canvas->SetPWMBits(pwm_bits);
    canvas->set_luminance_correct(luminance_correct);
    canvases.push_back(canvas);
    if (!active) active = canvas;
    return canvas;
  }

  FrameCanvas *SwapOnVSync(FrameCanvas *other, unsigned framerate_fraction) {
    std::lock_guard<std::mutex> lock(mutex);
    pacer.WaitForVSync(framerate_fraction);
    FrameCanvas *previous = active;
    if (other) active = other;
    ShowLocked();
    return previous;
  }

  void Render() {
    std::lock_guard<std::mutex> lock(mutex);
    ShowLocked();
  }

  const int width;
  const int height;
  uint8_t brightness;
  uint8_t pwm_bits;
  bool luminance_correct = false;
  FrameCanvas *active = nullptr;
  std::vector<FrameCanvas *> canvases;

private:
  // Hands the active frame to the outputs unless that exact content was
  // already shown.
  void ShowLocked() {
    for (auto &output : outputs) output->PumpEvents();
    if (!active || outputs.empty()) return;
    auto *framebuffer = static_cast<internal::EmulatorFramebuffer *>(active->framebuffer());
    if (drawn.Matches(active, framebuffer->generation(), brightness)) return;
    const uint8_t *rgb = framebuffer->rgb();
    if (brightness < 100) {
      const size_t bytes = static_cast<size_t>(width) * height * 3U;
      scaled.resize(bytes);
      const float factor = brightness / 100.0f;
      for (size_t i = 0; i < bytes; ++i) scaled[i] = static_cast<uint8_t>(rgb[i] * factor);
      rgb = scaled.data();
    }
    for (auto &output : outputs) output->ShowFrame(rgb, width, height);
    drawn.Set(active, framebuffer->generation(), brightness);
  }

  std::mutex mutex;
  std::vector<std::unique_ptr<internal::EmulatorOutput>> outputs;
  internal::EmulatorFramePacer pacer;
  internal::EmulatorDrawnFrame drawn;
  std::vector<uint8_t> scaled;
};

bool EmulatorMatrix::Supported() { return true; }

EmulatorMatrix *EmulatorMatrix::Create(const RGBMatrix::Options &options,
                                       const EmulatorOptions &emulator_options) {
  std::string error;
  // Emulators also back virtual canvases (preview_gen/scene workers), whose
  // dimensions need not be legal physical panel rows/columns/chains.
  auto validation = options;
  validation.rows = validation.cols = 32;
  validation.chain_length = validation.parallel = 1;
  if (options.rows <= 0 || options.cols <= 0 || options.chain_length <= 0 || options.parallel <= 0
      || !validation.Validate(&error)) return nullptr;
  const int64_t width = static_cast<int64_t>(options.cols) * options.chain_length;
  const int64_t height = static_cast<int64_t>(options.rows) * options.parallel;
  if (width > INT_MAX || height > INT_MAX || width * height > INT_MAX / 3) return nullptr;

  std::vector<std::unique_ptr<internal::EmulatorOutput>> outputs;
  bool has_window = false;
  if (!emulator_options.headless) {
    if (internal::EmulatorHasSdlOutput()) {
      auto window = internal::CreateSdlOutput(static_cast<int>(width), static_cast<int>(height),
                                              emulator_options);
      if (!window) return nullptr;  // Asked for a window that can't open.
      outputs.push_back(std::move(window));
      has_window = true;
    } else {
      fprintf(stderr, "The rgbmatrix library was built without the SDL emulator "
                      "window (ENABLE_EMULATOR_SDL); running headless.\n");
    }
  }
  if (!emulator_options.frame_export_path.empty()) {
    outputs.push_back(internal::CreateFrameExportOutput(emulator_options.frame_export_path));
  }

  auto *matrix = new EmulatorMatrix(
      new Impl(options, emulator_options, std::move(outputs), has_window));
  matrix->CreateFrameCanvas();
  return matrix;
}

EmulatorMatrix::EmulatorMatrix(Impl *impl) : impl_(impl) {}
EmulatorMatrix::~EmulatorMatrix() { delete impl_; }

int EmulatorMatrix::width() const { return impl_->width; }
int EmulatorMatrix::height() const { return impl_->height; }
void EmulatorMatrix::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (impl_->active) impl_->active->SetPixel(x, y, r, g, b);
}
bool EmulatorMatrix::GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const {
  return impl_->active && impl_->active->GetPixel(x, y, r, g, b);
}
void EmulatorMatrix::Clear() { if (impl_->active) impl_->active->Clear(); }
void EmulatorMatrix::Fill(uint8_t r, uint8_t g, uint8_t b) {
  if (impl_->active) impl_->active->Fill(r, g, b);
}

FrameCanvas *EmulatorMatrix::CreateFrameCanvas() { return impl_->CreateFrameCanvas(); }

// Like hardware, blocks until the next (paced) refresh, then shows the new
// frame on every output.
FrameCanvas *EmulatorMatrix::SwapOnVSync(FrameCanvas *other, unsigned framerate_fraction) {
  return impl_->SwapOnVSync(other, framerate_fraction);
}

bool EmulatorMatrix::ApplyPixelMapper(const PixelMapper *mapper) { return mapper == nullptr; }

void EmulatorMatrix::SetBrightness(uint8_t value) {
  impl_->brightness = internal::ClampBrightness(value);
  for (auto *canvas : impl_->canvases) canvas->SetBrightness(impl_->brightness);
}
uint8_t EmulatorMatrix::brightness() { return impl_->brightness; }

bool EmulatorMatrix::SetPWMBits(uint8_t value) {
  if (value < 1 || value > 11) return false;
  impl_->pwm_bits = value;
  for (auto *canvas : impl_->canvases) canvas->SetPWMBits(value);
  return true;
}
uint8_t EmulatorMatrix::pwmbits() { return impl_->pwm_bits; }

void EmulatorMatrix::set_luminance_correct(bool on) {
  impl_->luminance_correct = on;
  for (auto *canvas : impl_->canvases) canvas->set_luminance_correct(on);
}
bool EmulatorMatrix::luminance_correct() const { return impl_->luminance_correct; }

// There is no refresh thread to start: frames are shown by SwapOnVSync()
// and Render() on the caller's thread.
bool EmulatorMatrix::StartRefresh() { return true; }
void EmulatorMatrix::Render() { impl_->Render(); }

}  // namespace rgb_matrix

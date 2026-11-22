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
// It owns the canvases, swapping, pacing, pixel mappers and brightness/PWM
// settings, and hands each shown frame to its outputs (emulator-output.h). It
// needs no SDL; the SDL window is one optional output.
//
// Like hardware, canvases store pixels in physical panel layout
// (cols * chain by rows * parallel). Pixel mappers only change the shared
// EmulatorGeometry that maps visible canvas coordinates to physical pixels, so
// width(), height() and coordinates match a hardware matrix with the same
// options, and content survives a later ApplyPixelMapper() as on hardware.

#include "emulator.h"

#include "emulator-frame-pacer.h"
#include "emulator-output.h"
#include "framebuffer-interface.h"
#include "pixel-mapper.h"

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
      headless(!internal::EmulatorHasSdlOutput()), show_physical_layout(false) {}

namespace internal {
namespace {

uint8_t ClampBrightness(int value) {
  return static_cast<uint8_t>(std::max(1, std::min(100, value)));
}

// Maps visible canvas coordinates to physical panel pixels. Shared by all
// canvases of a matrix, like the hardware PixelDesignatorMap.
class EmulatorGeometry {
public:
  EmulatorGeometry(int physical_width, int physical_height)
      : physical_width(physical_width), physical_height(physical_height),
        width(physical_width), height(physical_height) {}

  // Index into the physical RGB pixels, or -1 if (x, y) is outside the
  // canvas or not mapped to any physical pixel.
  int PhysicalIndex(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return -1;
    return map_.empty() ? y * width + x : map_[static_cast<size_t>(y) * width + x];
  }
  bool identity() const { return map_.empty(); }

  // Applies a mapper on top of the current mapping, like
  // RGBMatrix::ApplyPixelMapper(). Returns false if the mapper rejects the
  // current size.
  bool Apply(const PixelMapper &mapper) {
    int new_width = 0, new_height = 0;
    if (!mapper.GetSizeMapping(width, height, &new_width, &new_height)) return false;
    if (new_width <= 0 || new_height <= 0 ||
        static_cast<int64_t>(new_width) * new_height > INT_MAX / 3) {
      fprintf(stderr, "PixelMapper %s: invalid size %dx%d\n", mapper.GetName(),
              new_width, new_height);
      return false;
    }
    std::vector<int> next(static_cast<size_t>(new_width) * new_height, -1);
    bool reported = false;
    if (mapper.GetMappingType() == PixelMapper::VisibleToMatrix) {
      for (int y = 0; y < new_height; ++y) {
        for (int x = 0; x < new_width; ++x) {
          int matrix_x = -1, matrix_y = -1;
          mapper.MapVisibleToMatrix(width, height, x, y, &matrix_x, &matrix_y);
          const int index = PhysicalIndex(matrix_x, matrix_y);
          if (index < 0 && !reported && (matrix_x < 0 || matrix_y < 0 ||
                                         matrix_x >= width || matrix_y >= height)) {
            fprintf(stderr, "Error in PixelMapper %s: (%d, %d) -> (%d, %d) "
                    "[range: %dx%d]\n", mapper.GetName(), x, y, matrix_x, matrix_y,
                    width, height);
            reported = true;
          }
          next[static_cast<size_t>(y) * new_width + x] = index;
        }
      }
    } else {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          int visible_x = -1, visible_y = -1;
          if (!mapper.MapMatrixToVisible(width, height, x, y, &visible_x, &visible_y)) continue;
          if (visible_x < 0 || visible_y < 0 || visible_x >= new_width || visible_y >= new_height) {
            if (!reported) {
              fprintf(stderr, "Error in PixelMapper %s MapMatrixToVisible: (%d, %d) -> "
                      "(%d, %d) [range: %dx%d]\n", mapper.GetName(), x, y,
                      visible_x, visible_y, new_width, new_height);
              reported = true;
            }
            continue;
          }
          next[static_cast<size_t>(visible_y) * new_width + visible_x] = PhysicalIndex(x, y);
        }
      }
    }
    width = new_width;
    height = new_height;
    map_.swap(next);
    return true;
  }

  const int physical_width;
  const int physical_height;
  int width;   // Visible canvas size.
  int height;

private:
  std::vector<int> map_;  // Visible index -> physical index; empty = identity.
};

// Software framebuffer holding packed RGB888 pixels in physical layout.
class EmulatorFramebuffer final : public FramebufferInterface {
public:
  explicit EmulatorFramebuffer(const EmulatorGeometry *geometry)
      : geometry_(geometry),
        pixels_(static_cast<size_t>(geometry->physical_width) * geometry->physical_height * 3U, 0) {}

  int width() const override { return geometry_->width; }
  int height() const override { return geometry_->height; }

  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    const int index = geometry_->PhysicalIndex(x, y);
    if (index < 0) return;
    ++generation_;
    uint8_t *pixel = &pixels_[static_cast<size_t>(index) * 3U];
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
  }

  bool GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const override {
    if (x < 0 || y < 0 || x >= geometry_->width || y >= geometry_->height) return false;
    const int index = geometry_->PhysicalIndex(x, y);
    const uint8_t *pixel = index < 0 ? kBlack : &pixels_[static_cast<size_t>(index) * 3U];
    if (r) *r = pixel[0];
    if (g) *g = pixel[1];
    if (b) *b = pixel[2];
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
    if (!other || other == this || other->width() != width() || other->height() != height()) return;
    const char *data = nullptr;
    size_t len = 0;
    other->Serialize(&data, &len);
    Deserialize(data, len);
  }

  // Packed RGB in physical layout.
  const uint8_t *physical_rgb() const { return pixels_.data(); }

  // Bumped on every pixel write, so Render() can tell whether the content
  // changed since it was last shown.
  uint64_t generation() const { return generation_; }

private:
  static constexpr uint8_t kBlack[3] = {0, 0, 0};
  const EmulatorGeometry *const geometry_;
  std::vector<uint8_t> pixels_;
  uint8_t brightness_ = 100;
  uint8_t pwm_bits_ = 8;
  bool luminance_correct_ = false;
  uint64_t generation_ = 0;
};

constexpr uint8_t EmulatorFramebuffer::kBlack[3];

// Applies "Name[:param];Name[:param]..." like --led-pixel-mapper on hardware.
// Unknown names are reported by FindPixelMapper() and skipped, as on hardware.
void ApplyNamedPixelMappers(const char *config, int chain, int parallel,
                            EmulatorGeometry *geometry) {
  if (!config) return;
  const std::string all(config);
  size_t start = 0;
  while (start <= all.size()) {
    size_t end = all.find(';', start);
    if (end == std::string::npos) end = all.size();
    std::string name = all.substr(start, end - start);
    std::string parameter;
    const size_t colon = name.find(':');
    const bool has_parameter = colon != std::string::npos;
    if (has_parameter) {
      parameter = name.substr(colon + 1);
      name.resize(colon);
    }
    if (name.empty() && !parameter.empty()) {
      fprintf(stderr, "Stray parameter ':%s' without mapper name ?\n", parameter.c_str());
    }
    if (!name.empty()) {
      const PixelMapper *mapper = FindPixelMapper(name.c_str(), chain, parallel,
                                                  has_parameter ? parameter.c_str() : nullptr);
      if (mapper) geometry->Apply(*mapper);
    }
    start = end + 1;
  }
}

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

// Hands frames to EmulatorMatrix::SetFrameCallback().
class FrameCallbackOutput final : public EmulatorOutput {
public:
  explicit FrameCallbackOutput(EmulatorMatrix::FrameCallback callback)
      : callback_(std::move(callback)) {}
  void ShowFrame(const uint8_t *rgb, int width, int height) override {
    callback_(rgb, width, height);
  }

private:
  const EmulatorMatrix::FrameCallback callback_;
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
       internal::EmulatorGeometry geometry,
       std::vector<std::unique_ptr<internal::EmulatorOutput>> outputs, bool has_window)
      : geometry(std::move(geometry)),
        show_physical_layout(emulator_options.show_physical_layout),
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
    auto *canvas = new FrameCanvas(new internal::EmulatorFramebuffer(&geometry));
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

  bool ApplyPixelMapper(const PixelMapper *mapper) {
    if (!mapper) return true;
    std::lock_guard<std::mutex> lock(mutex);
    if (!geometry.Apply(*mapper)) return false;
    drawn = internal::EmulatorDrawnFrame();  // The visible image changed.
    return true;
  }

  void SetFrameCallback(EmulatorMatrix::FrameCallback callback) {
    std::lock_guard<std::mutex> lock(mutex);
    if (callback_output) {
      outputs.erase(std::find_if(outputs.begin(), outputs.end(),
                                 [this](const std::unique_ptr<internal::EmulatorOutput> &o) {
                                   return o.get() == callback_output;
                                 }));
      callback_output = nullptr;
    }
    if (!callback) return;
    outputs.emplace_back(new internal::FrameCallbackOutput(std::move(callback)));
    callback_output = outputs.back().get();
    drawn = internal::EmulatorDrawnFrame();  // Deliver the current frame next.
  }

  internal::EmulatorGeometry geometry;
  const bool show_physical_layout;
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
    const uint8_t *physical = framebuffer->physical_rgb();
    const uint8_t *rgb = physical;
    int width = geometry.physical_width, height = geometry.physical_height;
    if (!show_physical_layout && !geometry.identity()) {
      // Gather the visible image; unmapped visible pixels stay black.
      width = geometry.width;
      height = geometry.height;
      visible.assign(static_cast<size_t>(width) * height * 3U, 0);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const int index = geometry.PhysicalIndex(x, y);
          if (index < 0) continue;
          std::memcpy(&visible[(static_cast<size_t>(y) * width + x) * 3U],
                      physical + static_cast<size_t>(index) * 3U, 3);
        }
      }
      rgb = visible.data();
    }
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
  internal::EmulatorOutput *callback_output = nullptr;  // Owned by outputs.
  internal::EmulatorFramePacer pacer;
  internal::EmulatorDrawnFrame drawn;
  std::vector<uint8_t> visible;
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

  // Multiplex mappers only describe panel wiring; the visible panel keeps
  // its size, so only the --led-pixel-mapper chain changes the geometry.
  internal::EmulatorGeometry geometry(static_cast<int>(width), static_cast<int>(height));
  internal::ApplyNamedPixelMappers(options.pixel_mapper_config, options.chain_length,
                                   options.parallel, &geometry);

  std::vector<std::unique_ptr<internal::EmulatorOutput>> outputs;
  bool has_window = false;
  if (!emulator_options.headless) {
    if (internal::EmulatorHasSdlOutput()) {
      const bool physical = emulator_options.show_physical_layout;
      auto window = internal::CreateSdlOutput(physical ? geometry.physical_width : geometry.width,
                                              physical ? geometry.physical_height : geometry.height,
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
      new Impl(options, emulator_options, std::move(geometry), std::move(outputs), has_window));
  matrix->CreateFrameCanvas();
  return matrix;
}

EmulatorMatrix::EmulatorMatrix(Impl *impl) : impl_(impl) {}
EmulatorMatrix::~EmulatorMatrix() { delete impl_; }

int EmulatorMatrix::width() const { return impl_->geometry.width; }
int EmulatorMatrix::height() const { return impl_->geometry.height; }
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

bool EmulatorMatrix::ApplyPixelMapper(const PixelMapper *mapper) {
  return impl_->ApplyPixelMapper(mapper);
}

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

void EmulatorMatrix::SetFrameCallback(FrameCallback callback) {
  impl_->SetFrameCallback(std::move(callback));
}

}  // namespace rgb_matrix

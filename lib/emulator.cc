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

#include "emulator.h"
#ifdef ENABLE_EMULATOR
#include "emulator-frame-pacer.h"
#include "framebuffer-internal.h"  // Add this include for Framebuffer class
#include <SDL2/SDL.h>
#include <assert.h>
#include <algorithm>
#include <climits>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string.h>
#include <string>
#include <vector>
#include <unistd.h>

namespace rgb_matrix {

EmulatorOptions::EmulatorOptions()
  : display_scale(10),
    window_title("RGB Matrix Emulator"),
    emulate_hardware_timing(false),
    refresh_rate_hz(60),
    frame_export_path(""),
    headless(false) {
}

namespace {

// Structure to hold pixel data for the display
struct EmulatedPixel {
  uint8_t r, g, b;
};

static bool sdl_initialized = false;

// Initialize SDL if not already initialized
static bool InitSDL() {
  if (sdl_initialized) return true;
  
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
    return false;
  }
  
  atexit(SDL_Quit);
  sdl_initialized = true;
  return true;
}

// Class to handle SDL window and rendering
class SDLDisplay {
public:
  SDLDisplay(int width, int height, int scale, const std::string& title,
             const std::string& frame_export_path = "", bool headless = false)
    : width_(width), height_(height), scale_(scale),
      frame_export_path_(frame_export_path), headless_(headless) {

    pixels_ = new EmulatedPixel[width * height]();
    if (headless_) return;
    if (!InitSDL()) return;
    
    window_ = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      width * scale, height * scale,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
      
    if (!window_) {
      fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
      return;
    }
    
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
      fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
      return;
    }
    
    texture_ = SDL_CreateTexture(
      renderer_,
      SDL_PIXELFORMAT_RGB24,
      SDL_TEXTUREACCESS_STREAMING,
      width, height);
      
    if (!texture_) {
      fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
      return;
    }
    
  }
  
  ~SDLDisplay() {
    if (pixels_) delete[] pixels_;
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
  }

  bool IsReady() const {
    return pixels_ && (headless_ || (window_ && renderer_ && texture_));
  }
  
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!pixels_ || x < 0 || x >= width_ || y < 0 || y >= height_) return;
    
    const int pos = y * width_ + x;
    pixels_[pos].r = r;
    pixels_[pos].g = g;
    pixels_[pos].b = b;
  }
  
  void Clear() {
    memset(pixels_, 0, width_ * height_ * sizeof(EmulatedPixel));
  }
  
  void Fill(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < width_ * height_; ++i) {
      pixels_[i].r = r;
      pixels_[i].g = g;
      pixels_[i].b = b;
    }
  }
  
  void Render(uint8_t brightness) {
    if (!IsReady()) return;
    if (!headless_) RenderWindow(brightness);
    ExportFrame(brightness);
  }

  // Handles window events so the window stays responsive. Called on every
  // swap and Render(), even when there is no new frame to draw.
  void PumpEvents() {
    if (headless_ || !IsReady()) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        exit(0);  // Quit the application
      } else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          // Update scale based on new window size
          int new_width = event.window.data1;
          int new_height = event.window.data2;
          
          scale_ = std::min(new_width / width_, new_height / height_);
          if (scale_ < 1) scale_ = 1;
        }
      }
    }
  }

  void RenderWindow(uint8_t brightness) {
    if (!IsReady()) return;
    PumpEvents();

    // Copy pixels to texture with brightness adjustment
    uint8_t *tex_pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, NULL, (void**)&tex_pixels, &pitch) != 0) {
      fprintf(stderr, "Unable to lock texture: %s\n", SDL_GetError());
      return;
    }
    if (!tex_pixels || pitch < width_ * 3) {
      SDL_UnlockTexture(texture_);
      return;
    }
    
    const float brightness_factor = brightness / 100.0f;
    
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        const int src_pos = y * width_ + x;
        const int dst_pos = y * pitch + x * 3;
        
        tex_pixels[dst_pos]     = (uint8_t)(pixels_[src_pos].r * brightness_factor);
        tex_pixels[dst_pos + 1] = (uint8_t)(pixels_[src_pos].g * brightness_factor);
        tex_pixels[dst_pos + 2] = (uint8_t)(pixels_[src_pos].b * brightness_factor);
      }
    }
    
    SDL_UnlockTexture(texture_);
    
    // Render the texture to the window
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    
    // Calculate destination rectangle to center the texture
    SDL_Rect dest_rect;
    dest_rect.w = width_ * scale_;
    dest_rect.h = height_ * scale_;
    
    // Get window size for centering
    int win_width, win_height;
    SDL_GetWindowSize(window_, &win_width, &win_height);
    
    dest_rect.x = (win_width - dest_rect.w) / 2;
    dest_rect.y = (win_height - dest_rect.h) / 2;
    
    SDL_RenderCopy(renderer_, texture_, NULL, &dest_rect);
    SDL_RenderPresent(renderer_);

  }

  void ExportFrame(uint8_t brightness) {
    // Export frame to binary file for MCP server or other consumers.
    // Format: [width: u32 LE][height: u32 LE][R,G,B bytes ... width*height*3]
    if (!frame_export_path_.empty()) {
      std::string tmp_path = frame_export_path_ + ".tmp";
      FILE* f = fopen(tmp_path.c_str(), "wb");
      if (f) {
        uint32_t w = (uint32_t)width_, h = (uint32_t)height_;
        fwrite(&w, sizeof(w), 1, f);
        fwrite(&h, sizeof(h), 1, f);
        const float bf = brightness / 100.0f;
        std::vector<uint8_t> rgb(width_ * height_ * 3);
        for (int i = 0; i < width_ * height_; ++i) {
          rgb[i*3]   = (uint8_t)(pixels_[i].r * bf);
          rgb[i*3+1] = (uint8_t)(pixels_[i].g * bf);
          rgb[i*3+2] = (uint8_t)(pixels_[i].b * bf);
        }
        fwrite(rgb.data(), 1, rgb.size(), f);
        fclose(f);
        rename(tmp_path.c_str(), frame_export_path_.c_str());
      }
    }
  }
  
private:
  int width_;
  int height_;
  int scale_;
  std::string frame_export_path_;
  bool headless_;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;
  EmulatedPixel *pixels_ = nullptr;
};

}  // namespace

// Create internal framebuffer implementation for the emulator to match the RGBMatrix design
namespace internal {

// Static shared mapper for emulator framebuffers. The base Framebuffer constructor
// allocates a PixelDesignatorMap if *mapper is NULL. Using a static ensures the
// pointer is properly initialized before the base constructor runs (avoiding UB
// from passing an uninitialized member variable).
static rgb_matrix::internal::PixelDesignatorMap* s_emulator_shared_mapper = nullptr;

class EmulatorFramebuffer : public rgb_matrix::internal::Framebuffer {
public:
  EmulatorFramebuffer(int width, int height)
    : rgb_matrix::internal::Framebuffer(width, height, 1, 0, "RGB", false, &s_emulator_shared_mapper),
      width_(width), height_(height), brightness_(100), pwm_bits_(8),
      do_luminance_correct_(false) {
      
    pixels_ = new EmulatedPixel[width_ * height_];
    Clear();
    buffer_size_ = width_ * height_ * sizeof(EmulatedPixel);
  }
  
  ~EmulatorFramebuffer() override {
    delete[] pixels_;
  }
  
  // The base Framebuffer versions of these write GPIO bitplanes; route them
  // through SetPixel() so they update the emulated pixels.
  void SetRGB(int x, int y, int w, int h, const uint8_t *rgb) override {
    FramebufferInterface::SetRGB(x, y, w, h, rgb);
  }
  void SetPixels(int x, int y, int w, int h, Color *colors) override {
    FramebufferInterface::SetPixels(x, y, w, h, colors);
  }
  void SubFill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) override {
    FramebufferInterface::SubFill(x, y, w, h, r, g, b);
  }
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    ++generation_;
    const int pos = y * width_ + x;
    pixels_[pos].r = r;
    pixels_[pos].g = g;
    pixels_[pos].b = b;
  }
  
  void Clear() override {
    ++generation_;
    memset(pixels_, 0, width_ * height_ * sizeof(EmulatedPixel));
  }
  
  void Fill(uint8_t r, uint8_t g, uint8_t b) override {
    ++generation_;
    for (int i = 0; i < width_ * height_; ++i) {
      pixels_[i].r = r;
      pixels_[i].g = g;
      pixels_[i].b = b;
    }
  }

  // Bumped on every pixel write, so Render() can tell whether the content
  // changed since it was last drawn.
  uint64_t generation() const { return generation_; }
  
  // Override the width and height methods to use our local values
  // REVIEW This was a sigfault before overwriting, should probably review this
  int width() const override { return width_; }
  int height() const override { return height_; }
  
  bool SetPWMBits(uint8_t bits) override {
    if (bits < 1 || bits > 11) return false;
    pwm_bits_ = bits;
    return true;
  }
  
  uint8_t pwmbits() override {
    return pwm_bits_;
  }
  
  void set_luminance_correct(bool correct) override {
    do_luminance_correct_ = correct;
  }
  
  bool luminance_correct() const override {
    return do_luminance_correct_;
  }
  
  void SetBrightness(uint8_t brightness) override {
    brightness_ = brightness > 100 ? 100 : brightness;
  }
  
  uint8_t brightness() override {
    return brightness_;
  }
  
  void DumpToMatrix(GPIO* io, int bit_plane) override {
    // Not used in the emulator - this would normally send data to GPIO
  }
  
  void Serialize(const char** data, size_t* len) const override {
    *data = reinterpret_cast<const char*>(pixels_);
    *len = buffer_size_;
  }
  
  bool Deserialize(const char* data, size_t len) override {
    if (len != buffer_size_) return false;
    ++generation_;
    memcpy(pixels_, data, len);
    return true;
  }
  
  void CopyFrom(const FramebufferInterface *other) override {
    const EmulatorFramebuffer* o = dynamic_cast<const EmulatorFramebuffer*>(other);
    if (!o || o == this || o->width_ != width_ || o->height_ != height_) return;
    ++generation_;
    memcpy(pixels_, o->pixels_, width_ * height_ * sizeof(EmulatedPixel));
  }
  
  // Render this framebuffer to the display
  void Render(SDLDisplay* display) const {
    if (!display) return;
    
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        const int pos = y * width_ + x;
        display->SetPixel(x, y, pixels_[pos].r, pixels_[pos].g, pixels_[pos].b);
      }
    }
  }
  
  bool GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
      return false;
    }
    
    const int pos = y * width_ + x;
    *r = pixels_[pos].r;
    *g = pixels_[pos].g;
    *b = pixels_[pos].b;
    return true;
  }
  
private:
  const int width_;
  const int height_;
  uint8_t brightness_;
  uint8_t pwm_bits_;
  bool do_luminance_correct_;
  EmulatedPixel* pixels_;
  size_t buffer_size_;  // Added buffer size member to track serialized data size
  uint64_t generation_ = 0;
};
} // namespace internal

class EmulatorMatrix::Impl {
public:
  Impl(const RGBMatrix::Options& opts, const EmulatorOptions& emulator_opts)
    : matrix_options_(opts),
      emulator_options_(emulator_opts),
      display_(nullptr),
      brightness_(opts.brightness),
      pwm_bits_(opts.pwm_bits),
      luminance_correct_(false),
      // A window is paced like a hardware display. Headless render workers
      // run at full speed unless they ask for hardware timing.
      pacer_(emulator_opts.emulate_hardware_timing || !emulator_opts.headless,
             emulator_opts.refresh_rate_hz) {
    
    width_ = opts.cols * opts.chain_length;
    height_ = opts.rows * opts.parallel;
    
    // Initialize hardware mapping before creating framebuffer
    rgb_matrix::internal::Framebuffer::InitHardwareMapping(opts.hardware_mapping);
    
    display_ = new SDLDisplay(width_, height_,
                              emulator_opts.display_scale,
                              emulator_opts.window_title,
                              emulator_opts.frame_export_path,
                              emulator_opts.headless);

    if (!display_->IsReady()) return;
    
    // Create initial framebuffer and canvas, track for cleanup
    rgb_matrix::internal::Framebuffer* fb = new internal::EmulatorFramebuffer(width_, height_);
    active_buffer_ = new FrameCanvas(fb);
    active_buffer_->SetBrightness(brightness_);
    active_buffer_->SetPWMBits(pwm_bits_);
    active_buffer_->set_luminance_correct(luminance_correct_);
    created_frames_.push_back(active_buffer_);
  }

  bool IsReady() const { return display_ && display_->IsReady() && active_buffer_; }
  
  ~Impl() {
    if (display_) delete display_;
    
    // Delete any active framebuffers
    for (auto canvas : created_frames_) {
      delete canvas;
    }
  }

  
  // Like hardware, blocks until the next (paced) refresh, then shows the
  // new frame: draws the window and/or writes the frame export.
  FrameCanvas* SwapOnVSync(FrameCanvas* other, unsigned frame_fraction) {
    std::unique_lock<std::mutex> lock(mutex_);
    pacer_.WaitForVSync(frame_fraction);
    FrameCanvas* previous = active_buffer_;
    if (other) active_buffer_ = other;
    DrawLocked();
    return previous;
  }
  
  bool ApplyPixelMapper(const PixelMapper* mapper) {
    // Pixel mapping not implemented in emulator
    return false;
  }
  
  // Canvas interface
  int width() const { return width_; }
  int height() const { return height_; }
  
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!active_buffer_) return;
    active_buffer_->SetPixel(x, y, r, g, b);
  }
  
  void Clear() {
    if (!active_buffer_) return;
    active_buffer_->Clear();
  }
  
  void Fill(uint8_t r, uint8_t g, uint8_t b) {
    if (!active_buffer_) return;
    active_buffer_->Fill(r, g, b);
  }
  
  bool GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const {
    if (!active_buffer_) return false;
    return active_buffer_->GetPixel(x, y, r, g, b);
  }
  
  // Frame canvas management
  FrameCanvas* CreateFrameCanvas() {
    rgb_matrix::internal::Framebuffer* fb = new internal::EmulatorFramebuffer(width_, height_);
    FrameCanvas* canvas = new FrameCanvas(fb);
    
    canvas->SetBrightness(brightness_);
    canvas->SetPWMBits(pwm_bits_);
    canvas->set_luminance_correct(luminance_correct_);
    
    created_frames_.push_back(canvas);
    return canvas;
  }
  
  // Settings
  void SetBrightness(uint8_t brightness) {
    brightness_ = brightness > 100 ? 100 : brightness;
    for (auto *canvas : created_frames_) canvas->SetBrightness(brightness_);
  }
  
  uint8_t GetBrightness() const { return brightness_; }
  
  bool SetPWMBits(uint8_t bits) {
    if (bits < 1 || bits > 11) return false;
    pwm_bits_ = bits;
    if (active_buffer_) active_buffer_->SetPWMBits(pwm_bits_);
    return true;
  }
  
  uint8_t PWMBits() const { return pwm_bits_; }
  
  void SetLuminanceCorrect(bool correct) {
    luminance_correct_ = correct;
    if (active_buffer_) active_buffer_->set_luminance_correct(correct);
  }
  
  bool LuminanceCorrect() const { return luminance_correct_; }
  
  // Draws the active frame unless that exact content is already shown.
  void Render() {
    std::unique_lock<std::mutex> lock(mutex_);
    DrawLocked();
  }
private:
  void DrawLocked() {
    if (!display_ || !active_buffer_) return;
    auto* emulator_fb = static_cast<internal::EmulatorFramebuffer*>(active_buffer_->framebuffer());
    if (drawn_.Matches(active_buffer_, emulator_fb->generation(), brightness_)) {
      display_->PumpEvents();
      return;
    }
    emulator_fb->Render(display_);
    display_->Render(brightness_);
    drawn_.Set(active_buffer_, emulator_fb->generation(), brightness_);
  }

  // Matrix configuration
  const RGBMatrix::Options matrix_options_;
  const EmulatorOptions emulator_options_;
  
  // Display state
  int width_;
  int height_;
  SDLDisplay* display_;
  uint8_t brightness_;
  uint8_t pwm_bits_;
  bool luminance_correct_;
  
  std::mutex mutex_;
  internal::EmulatorFramePacer pacer_;
  internal::EmulatorDrawnFrame drawn_;

  // Frame buffer management
  FrameCanvas* active_buffer_ = nullptr;
  std::vector<FrameCanvas*> created_frames_;
};

// Constructor
EmulatorMatrix::EmulatorMatrix(Impl* impl) : impl_(impl) {}

// Destructor
EmulatorMatrix::~EmulatorMatrix() {
  delete impl_;
}

bool EmulatorMatrix::Supported() { return true; }

// Factory method to create a new emulator
EmulatorMatrix* EmulatorMatrix::Create(const RGBMatrix::Options& options,
                                       const EmulatorOptions& emulator_options) {
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

  Impl* impl = new Impl(options, emulator_options);
  if (!impl->IsReady()) {
    delete impl;
    return nullptr;
  }
  return new EmulatorMatrix(impl);
}

// Canvas interface
int EmulatorMatrix::width() const { return impl_->width(); }
int EmulatorMatrix::height() const { return impl_->height(); }

void EmulatorMatrix::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  impl_->SetPixel(x, y, r, g, b);
}

void EmulatorMatrix::Clear() { impl_->Clear(); }
void EmulatorMatrix::Fill(uint8_t r, uint8_t g, uint8_t b) { impl_->Fill(r, g, b); }

bool EmulatorMatrix::GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const {
  return impl_->GetPixel(x, y, r, g, b);
}

// Frame canvas management
FrameCanvas* EmulatorMatrix::CreateFrameCanvas() {
  return impl_->CreateFrameCanvas();
}

FrameCanvas* EmulatorMatrix::SwapOnVSync(FrameCanvas* other, unsigned framerate_fraction) {
  return impl_->SwapOnVSync(other, framerate_fraction);
}

// Settings
bool EmulatorMatrix::ApplyPixelMapper(const PixelMapper* mapper) {
  return impl_->ApplyPixelMapper(mapper);
}

void EmulatorMatrix::SetBrightness(uint8_t brightness) {
  impl_->SetBrightness(brightness);
}

uint8_t EmulatorMatrix::brightness() {
  return impl_->GetBrightness();
}

bool EmulatorMatrix::SetPWMBits(uint8_t bits) {
  return impl_->SetPWMBits(bits);
}

uint8_t EmulatorMatrix::pwmbits() {
  return impl_->PWMBits();
}

void EmulatorMatrix::set_luminance_correct(bool correct) {
  impl_->SetLuminanceCorrect(correct);
}

bool EmulatorMatrix::luminance_correct() const {
  return impl_->LuminanceCorrect();
}


// There is no refresh thread to start: frames are drawn by SwapOnVSync()
// and Render() on the caller's thread.
bool EmulatorMatrix::StartRefresh() {
  return true;
}

void EmulatorMatrix::Render() {
  impl_->Render();
}

}  // namespace rgb_matrix

#endif

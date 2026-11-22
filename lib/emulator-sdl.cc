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

// The SDL window output of the emulator. Only built with ENABLE_EMULATOR_SDL.
// SDL is initialized only when a window is opened, so headless emulators in
// the same library never touch SDL.

#include "emulator.h"
#include "emulator-output.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rgb_matrix {
namespace internal {
namespace {

bool InitSDL() {
  static bool initialized = false;
  if (initialized) return true;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
    return false;
  }
  atexit(SDL_Quit);
  initialized = true;
  return true;
}

class SdlOutput final : public EmulatorOutput {
public:
  SdlOutput(int width, int height, const EmulatorOptions &options)
      : width_(width), height_(height), scale_(std::max(1, options.display_scale)) {
    if (!InitSDL()) return;
    window_ = SDL_CreateWindow(options.window_title.c_str(),
                               SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               width * scale_, height * scale_,
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
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                 SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture_) {
      fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
    }
  }

  ~SdlOutput() override {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
  }

  bool IsReady() const { return window_ && renderer_ && texture_; }

  void PumpEvents() override {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        exit(0);  // Closing the window quits the program.
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_RESIZED) {
        scale_ = std::max(1, std::min(event.window.data1 / width_,
                                      event.window.data2 / height_));
      }
    }
  }

  void ShowFrame(const uint8_t *rgb, int width, int height) override {
    if ((width != width_ || height != height_) && !Resize(width, height)) return;
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, reinterpret_cast<void **>(&pixels), &pitch) != 0) {
      fprintf(stderr, "Unable to lock texture: %s\n", SDL_GetError());
      return;
    }
    const size_t row_bytes = static_cast<size_t>(width_) * 3U;
    if (pixels && pitch >= static_cast<int>(row_bytes)) {
      for (int y = 0; y < height_; ++y)
        std::memcpy(pixels + static_cast<size_t>(y) * pitch, rgb + y * row_bytes, row_bytes);
    }
    SDL_UnlockTexture(texture_);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    int window_width = 0, window_height = 0;
    SDL_GetWindowSize(window_, &window_width, &window_height);
    SDL_Rect dest;
    dest.w = width_ * scale_;
    dest.h = height_ * scale_;
    dest.x = (window_width - dest.w) / 2;  // Centered in the window.
    dest.y = (window_height - dest.h) / 2;
    SDL_RenderCopy(renderer_, texture_, nullptr, &dest);
    SDL_RenderPresent(renderer_);
  }

private:
  // A pixel mapper applied at runtime changed the frame size.
  bool Resize(int width, int height) {
    SDL_Texture *texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
      fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
      return false;
    }
    SDL_DestroyTexture(texture_);
    texture_ = texture;
    width_ = width;
    height_ = height;
    SDL_SetWindowSize(window_, width * scale_, height * scale_);
    return true;
  }

  int width_;
  int height_;
  int scale_;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;
};

}  // namespace

bool EmulatorHasSdlOutput() { return true; }

std::unique_ptr<EmulatorOutput> CreateSdlOutput(int width, int height,
                                                const EmulatorOptions &options) {
  std::unique_ptr<SdlOutput> output(new SdlOutput(width, height, options));
  if (!output->IsReady()) return nullptr;
  return std::unique_ptr<EmulatorOutput>(output.release());
}

}  // namespace internal
}  // namespace rgb_matrix

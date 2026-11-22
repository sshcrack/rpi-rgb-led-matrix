// FrameCanvas::GetRGB() must return exactly what a GetPixel() loop returns,
// on hardware framebuffers (created without opening GPIO) and the emulator.
// With "bench", it also times GetRGB() against the GetPixel() loop.

#include "emulator.h"
#include "led-matrix.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace rgb_matrix;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Failed line %d (%s): %s\n", __LINE__, name, #c); return 1; } } while (0)

struct Config {
  const char *name;
  int rows, cols, chain, parallel, pwm_bits, brightness;
  bool luminance, inverse;
  const char *sequence;
  const char *mappers;
};

static const Config kConfigs[] = {
  {"default", 32, 64, 1, 1, 11, 100, true, false, "RGB", nullptr},
  {"linear 50%", 32, 64, 2, 1, 11, 50, false, false, "RGB", nullptr},
  {"7 bits 23%", 16, 32, 1, 1, 7, 23, true, false, "BGR", nullptr},
  {"1 bit", 32, 32, 1, 1, 1, 100, false, false, "RGB", nullptr},
  {"inverse", 32, 32, 1, 1, 9, 80, false, true, "GRB", nullptr},
  {"parallel 3", 32, 64, 1, 3, 11, 100, true, false, "RGB", nullptr},
  {"mapper", 32, 32, 4, 2, 11, 70, true, false, "RGB", "U-mapper;Rotate:90"},
  {"64 rows", 64, 64, 2, 1, 11, 100, true, false, "RGB", nullptr},
};

static RGBMatrix::Options Options(const Config &c) {
  RGBMatrix::Options options;
  options.rows = c.rows;
  options.cols = c.cols;
  options.chain_length = c.chain;
  options.parallel = c.parallel;
  options.pwm_bits = c.pwm_bits;
  options.brightness = c.brightness;
  options.inverse_colors = c.inverse;
  options.led_rgb_sequence = c.sequence;
  options.pixel_mapper_config = c.mappers;
  return options;
}

static void Pattern(FrameCanvas *canvas, unsigned seed) {
  const int w = canvas->width(), h = canvas->height();
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  uint32_t state = seed * 2654435761u + 1;
  for (auto &byte : rgb) {
    state = state * 1664525u + 1013904223u;
    byte = static_cast<uint8_t>(state >> 24);
  }
  canvas->SetRGB(0, 0, w, h, rgb.data());
}

static std::vector<uint8_t> PixelLoop(const FrameCanvas *canvas, int x, int y, int w, int h) {
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3, 0);
  for (int row = 0; row < h; ++row)
    for (int col = 0; col < w; ++col) {
      uint8_t *p = &out[(static_cast<size_t>(row) * w + col) * 3];
      if (!canvas->GetPixel(x + col, y + row, &p[0], &p[1], &p[2])) p[0] = p[1] = p[2] = 0;
    }
  return out;
}

static std::vector<uint8_t> Bulk(const FrameCanvas *canvas, int x, int y, int w, int h) {
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3, 0xAB);
  canvas->GetRGB(x, y, w, h, out.data());
  return out;
}

// Full, partial and clipped reads must match the GetPixel() loop.
static int Compare(const char *name, FrameCanvas *canvas) {
  for (unsigned seed = 1; seed <= 3; ++seed) {
    Pattern(canvas, seed);
    const int w = canvas->width(), h = canvas->height();
    CHECK(Bulk(canvas, 0, 0, w, h) == PixelLoop(canvas, 0, 0, w, h));
    CHECK(Bulk(canvas, 3, 2, w / 2, h / 3) == PixelLoop(canvas, 3, 2, w / 2, h / 3));
    CHECK(Bulk(canvas, -4, -3, w + 8, h + 6) == PixelLoop(canvas, -4, -3, w + 8, h + 6));
    CHECK(Bulk(canvas, 0, 1, w, h - 1) == PixelLoop(canvas, 0, 1, w, h - 1));
  }
  return 0;
}

template <typename F>
static double MicrosPerCall(F &&f) {
  using Clock = std::chrono::steady_clock;
  int iterations = 0;
  const auto start = Clock::now();
  auto now = start;
  do {
    f();
    ++iterations;
    now = Clock::now();
  } while (now - start < std::chrono::milliseconds(400));
  return std::chrono::duration<double, std::micro>(now - start).count() / iterations;
}

static void Bench(const char *backend, FrameCanvas *canvas) {
  const int w = canvas->width(), h = canvas->height();
  Pattern(canvas, 7);
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3), in(out.size(), 77);
  volatile uint8_t sink = 0;
  const double loop = MicrosPerCall([&] {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        uint8_t r, g, b;
        canvas->GetPixel(x, y, &r, &g, &b);
        sink = sink + r;
      }
  });
  const double bulk = MicrosPerCall([&] { canvas->GetRGB(0, 0, w, h, out.data()); sink = sink + out[0]; });
  const double write = MicrosPerCall([&] { canvas->SetRGB(0, 0, w, h, in.data()); });
  printf("%-9s %dx%d: GetPixel loop %8.1f us  GetRGB %8.1f us  (%.1fx)  SetRGB %8.1f us\n",
         backend, w, h, loop, bulk, loop / bulk, write);
}

int main(int argc, char **argv) {
  const bool bench = argc == 2 && std::strcmp(argv[1], "bench") == 0;
  for (const Config &c : kConfigs) {
    const char *name = c.name;
    const RGBMatrix::Options options = Options(c);
#ifndef _WIN32
    RuntimeOptions runtime;
    runtime.do_gpio_init = false;
    runtime.drop_privileges = -1;
    std::unique_ptr<RGBMatrix> hardware(RGBMatrix::CreateFromOptions(options, runtime));
    CHECK(hardware);
    FrameCanvas *canvas = hardware->CreateFrameCanvas();
    canvas->set_luminance_correct(c.luminance);
    if (Compare(name, canvas)) return 1;
    if (bench) {
      printf("[%s] ", name);
      Bench("hardware", canvas);
    }
#endif
    if (EmulatorMatrix::Supported()) {
      EmulatorOptions emulator_options;
      emulator_options.headless = true;
      std::unique_ptr<EmulatorMatrix> emulator(EmulatorMatrix::Create(options, emulator_options));
      CHECK(emulator);
      FrameCanvas *emulated = emulator->CreateFrameCanvas();
      if (Compare(name, emulated)) return 1;
      if (bench) {
        printf("[%s] ", name);
        Bench("emulator", emulated);
      }
    }
  }
  return 0;
}

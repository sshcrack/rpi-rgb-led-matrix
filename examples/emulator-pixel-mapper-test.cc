// Pixel mappers in the emulator must give the same canvas geometry as a
// hardware matrix with the same options (created without opening GPIO).

#include "emulator.h"
#include "led-matrix.h"
#include "pixel-mapper.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace rgb_matrix;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Failed line %d (%s): %s\n", __LINE__, config, #c); return 1; } } while (0)

struct Config {
  int rows, cols, chain, parallel;
  const char *mappers;
};

static const Config kConfigs[] = {
  {32, 64, 1, 1, "Rotate:90"},
  {32, 32, 4, 1, "U-mapper"},
  {32, 32, 4, 1, "V-mapper"},
  {32, 32, 2, 1, "V-mapper:Z"},
  {16, 32, 1, 3, "StackToRow"},
  {16, 32, 2, 2, "Mirror:H;Rotate:270"},
  {32, 32, 2, 2, "U-mapper;Rotate:180"},
  {32, 32, 1, 1, "NoSuchMapper"},  // Ignored, as on hardware.
};

static RGBMatrix::Options Options(const Config &c) {
  RGBMatrix::Options options;
  options.rows = c.rows;
  options.cols = c.cols;
  options.chain_length = c.chain;
  options.parallel = c.parallel;
  options.pixel_mapper_config = c.mappers;
  return options;
}

#ifndef _WIN32
static std::unique_ptr<RGBMatrix> CreateHardware(const RGBMatrix::Options &options) {
  RuntimeOptions runtime;
  runtime.do_gpio_init = false;
  runtime.drop_privileges = -1;
  return std::unique_ptr<RGBMatrix>(RGBMatrix::CreateFromOptions(options, runtime));
}

// Draws one red pixel, rotates at runtime, and returns where the red pixel
// is visible now. Hardware and emulator must agree.
template <typename Matrix>
static bool FindAfterRuntimeRotate(Matrix *matrix, int *found_x, int *found_y) {
  matrix->Clear();
  matrix->SetPixel(1, 2, 255, 0, 0);
  if (!matrix->ApplyPixelMapper(FindPixelMapper("Rotate", 1, 1, "90"))) return false;
  int hits = 0;
  for (int y = 0; y < matrix->height(); ++y)
    for (int x = 0; x < matrix->width(); ++x) {
      uint8_t r = 0, g = 0, b = 0;
      if (matrix->GetPixel(x, y, &r, &g, &b) && r == 255) {
        *found_x = x;
        *found_y = y;
        ++hits;
      }
    }
  return hits == 1;
}
#endif

static std::vector<unsigned char> ReadAndRemove(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
  file.close();
  std::remove(path.c_str());
  return data;
}

int main() {
  EmulatorOptions emulator_options;
  emulator_options.headless = true;
  const std::string export_path = "emulator-mapper-frame-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) + ".rgb";

  for (const Config &c : kConfigs) {
    const char *config = c.mappers;
    const RGBMatrix::Options options = Options(c);
    std::unique_ptr<EmulatorMatrix> emulator(EmulatorMatrix::Create(options, emulator_options));
    CHECK(emulator);
    const int physical_width = c.cols * c.chain, physical_height = c.rows * c.parallel;
    CHECK(emulator->width() * emulator->height() == physical_width * physical_height);
#ifndef _WIN32
    std::unique_ptr<RGBMatrix> hardware = CreateHardware(options);
    CHECK(hardware);
    CHECK(emulator->width() == hardware->width() && emulator->height() == hardware->height());
    FrameCanvas *hardware_canvas = hardware->CreateFrameCanvas();
    FrameCanvas *emulator_canvas = emulator->CreateFrameCanvas();
    CHECK(emulator_canvas->width() == hardware_canvas->width() &&
          emulator_canvas->height() == hardware_canvas->height());
#endif
  }

  // With one mapper, every visible pixel must land where the mapper puts it
  // in the physical layout.
  {
    const char *config = "Rotate:90 physical layout";
    RGBMatrix::Options options;
    options.rows = 16;
    options.cols = 32;
    options.pixel_mapper_config = "Rotate:90";
    EmulatorOptions physical = emulator_options;
    physical.show_physical_layout = true;
    physical.frame_export_path = export_path;
    std::unique_ptr<EmulatorMatrix> emulator(EmulatorMatrix::Create(options, physical));
    CHECK(emulator && emulator->width() == 16 && emulator->height() == 32);
    FrameCanvas *canvas = emulator->CreateFrameCanvas();
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 16; ++x) canvas->SetPixel(x, y, x, y, 7);
    emulator->SwapOnVSync(canvas);
    std::vector<unsigned char> frame = ReadAndRemove(export_path);
    CHECK(frame.size() == 8 + 32 * 16 * 3 && frame[0] == 32 && frame[4] == 16);
    const PixelMapper *rotate = FindPixelMapper("Rotate", 1, 1, "90");
    CHECK(rotate);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 16; ++x) {
        int matrix_x = -1, matrix_y = -1;
        rotate->MapVisibleToMatrix(32, 16, x, y, &matrix_x, &matrix_y);
        const unsigned char *pixel = &frame[8 + (matrix_y * 32 + matrix_x) * 3];
        CHECK(pixel[0] == x && pixel[1] == y && pixel[2] == 7);
      }

    // The default export shows the visible image.
    EmulatorOptions visible = emulator_options;
    visible.frame_export_path = export_path;
    std::unique_ptr<EmulatorMatrix> visible_emulator(EmulatorMatrix::Create(options, visible));
    canvas = visible_emulator->CreateFrameCanvas();
    canvas->SetPixel(3, 20, 9, 8, 7);
    visible_emulator->SwapOnVSync(canvas);
    frame = ReadAndRemove(export_path);
    CHECK(frame.size() == 8 + 16 * 32 * 3 && frame[0] == 16 && frame[4] == 32);
    const unsigned char *pixel = &frame[8 + (20 * 16 + 3) * 3];
    CHECK(pixel[0] == 9 && pixel[1] == 8 && pixel[2] == 7);
  }

#ifndef _WIN32
  // Content stays in the physical layout when a mapper is applied later.
  {
    const char *config = "runtime Rotate:90";
    RGBMatrix::Options options;
    options.rows = 16;
    options.cols = 32;
    options.chain_length = 2;
    std::unique_ptr<RGBMatrix> hardware = CreateHardware(options);
    std::unique_ptr<EmulatorMatrix> emulator(EmulatorMatrix::Create(options, emulator_options));
    CHECK(hardware && emulator);
    int hardware_x = -1, hardware_y = -1, emulator_x = -2, emulator_y = -2;
    CHECK(FindAfterRuntimeRotate(hardware.get(), &hardware_x, &hardware_y));
    CHECK(FindAfterRuntimeRotate(emulator.get(), &emulator_x, &emulator_y));
    CHECK(emulator->width() == hardware->width() && emulator->height() == hardware->height());
    CHECK(emulator_x == hardware_x && emulator_y == hardware_y);
  }
#endif
  return 0;
}

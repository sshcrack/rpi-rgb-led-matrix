// Uses the emulator API from a program compiled WITHOUT ENABLE_EMULATOR
// (the build removes the define), against libraries built with and without
// emulator support. With support, options must arrive intact, which fails if
// the headers changed shape with the define. Without, everything must
// compile, link and fail cleanly at runtime.

#ifdef ENABLE_EMULATOR
#error "this test must be compiled without ENABLE_EMULATOR"
#endif

#include "emulator.h"
#include "matrix-factory.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace rgb_matrix;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)

// Parses argv like a program's main() would.
static bool Parse(std::vector<std::string> args, MatrixFactory::Options *options,
                  int *remaining = nullptr) {
  std::vector<char *> argv;
  for (auto &arg : args) argv.push_back(&arg[0]);
  argv.push_back(nullptr);
  int argc = static_cast<int>(args.size());
  char **argv_pointer = argv.data();
  const bool ok = MatrixFactory::ParseOptionsFromFlags(&argc, &argv_pointer, options);
  if (remaining) *remaining = argc;
  return ok;
}

static int TestSupported() {
  const std::string export_path = "emulator-api-frame-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) + ".rgb";
  MatrixFactory::Options options;
  options.led_options.rows = 16;
  options.led_options.cols = 32;
  options.led_options.brightness = 50;
  CHECK(Parse({"test", "--led-emulator", "--led-emulator-headless",
               "--led-emulator-frame-export=" + export_path}, &options));
  CHECK(options.use_emulator && options.emulator_options.headless);
  std::unique_ptr<RGBMatrixBase> matrix(MatrixFactory::CreateMatrix(options));
  CHECK(matrix);
  CHECK(matrix->AsEmulator() == static_cast<EmulatorMatrix *>(matrix.get()));
  CHECK(matrix->width() == 32 && matrix->height() == 16);

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();
  offscreen->Fill(200, 100, 0);
  matrix->SwapOnVSync(offscreen);
  std::ifstream file(export_path, std::ios::binary);
  std::vector<unsigned char> frame((std::istreambuf_iterator<char>(file)), {});
  file.close();
  std::remove(export_path.c_str());
  // Geometry, brightness and the export path all come through the options
  // structs, so a layout mismatch shows up here.
  CHECK(frame.size() == 8 + 32 * 16 * 3);
  CHECK(frame[0] == 32 && frame[4] == 16 && frame[8] == 100 && frame[9] == 50);
  return 0;
}

static int TestUnsupported() {
  RGBMatrix::Options led_options;
  EmulatorOptions emulator_options;
  emulator_options.headless = true;
  CHECK(EmulatorMatrix::Create(led_options, emulator_options) == nullptr);

  // Emulator flags are still accepted, so one command line works everywhere.
  MatrixFactory::Options options;
  int remaining = 0;
  CHECK(Parse({"test", "--led-emulator-scale=3", "keep"}, &options, &remaining));
  CHECK(remaining == 2 && options.emulator_options.display_scale == 3);
  // Asking for the emulator fails at parse time and at creation.
  MatrixFactory::Options emulated;
  CHECK(!Parse({"test", "--led-emulator"}, &emulated));
  emulated.use_emulator = true;
  CHECK(MatrixFactory::CreateMatrix(emulated) == nullptr);

#ifndef _WIN32
  RuntimeOptions runtime;
  runtime.do_gpio_init = false;
  runtime.drop_privileges = -1;
  std::unique_ptr<RGBMatrix> hardware(RGBMatrix::CreateFromOptions(led_options, runtime));
  CHECK(hardware && hardware->AsEmulator() == nullptr);
#endif
  return 0;
}

int main() {
  return EmulatorMatrix::Supported() ? TestSupported() : TestUnsupported();
}

#include "emulator.h"

#include <cstdio>
#include <climits>
#include <cstring>
#include <memory>

using namespace rgb_matrix;

static bool Check(bool condition, const char *message) {
  if (!condition) fprintf(stderr, "%s\n", message);
  return condition;
}

int main(int argc, char **argv) {
  RGBMatrix::Options options;
  options.rows = options.cols = 32;
  options.brightness = 25;
  options.pwm_bits = 5;
  EmulatorOptions emulator_options;
  if (argc == 2 && strcmp(argv[1], "display-failure") == 0) {
    // Run in a separate process with SDL_VIDEODRIVER=dummy: there is no
    // accelerated renderer, so creation must fail without exposing a broken matrix.
    emulator_options.headless = false;
    std::unique_ptr<EmulatorMatrix> matrix(EmulatorMatrix::Create(options, emulator_options));
    return Check(!matrix, "failed SDL display initialization must return null") ? 0 : 1;
  }

  char name[] = "test";
  char headless[] = "--led-emulator-headless";
  char keep[] = "keep";
  char *arguments[] = {name, headless, keep, nullptr};
  char **argument_pointer = arguments;
  int argument_count = 3;
  if (!Check(ParseEmulatorOptionsFromFlags(&argument_count, &argument_pointer,
                                           &emulator_options, true), "parse options") ||
      !Check(argument_count == 2 && arguments[1] == keep && arguments[2] == nullptr,
             "consumed flags must be removed from both argc and argv")) return 1;

  emulator_options.headless = true;
  std::unique_ptr<EmulatorMatrix> matrix(EmulatorMatrix::Create(options, emulator_options));
  if (!Check(bool(matrix), "create headless emulator")) return 1;
  FrameCanvas *initial = matrix->SwapOnVSync(nullptr);
  if (!Check(initial && matrix->SwapOnVSync(nullptr) == initial,
             "null swap must preserve the active frame") ||
      !Check(matrix->brightness() == 25 && initial->brightness() == 25,
             "initial brightness must match options") ||
      !Check(matrix->pwmbits() == 5 && initial->pwmbits() == 5,
             "initial PWM depth must match options") ||
      !Check(!matrix->luminance_correct() && !initial->luminance_correct(),
             "initial luminance setting must match the emulator")) return 1;
  matrix->Fill(11, 22, 33);
  uint8_t r = 0, g = 0, b = 0;
  if (!Check(matrix->GetPixel(0, 0, &r, &g, &b) && r == 11 && g == 22 && b == 33,
             "null swap must leave the active canvas writable")) return 1;

  FrameCanvas *next = matrix->CreateFrameCanvas();
  if (!Check(next->brightness() == 25 && next->pwmbits() == 5,
             "new canvases must inherit matrix settings")) return 1;
  matrix->SetBrightness(37);
  if (!Check(next->brightness() == 37 && initial->brightness() == 37,
             "brightness changes must reach all canvases")) return 1;
  next->CopyFrom(*initial);
  if (!Check(next->GetPixel(0, 0, &r, &g, &b) && r == 11 && g == 22 && b == 33,
             "same-sized frame copies must preserve pixels")) return 1;

  RGBMatrix::Options smaller_options = options;
  smaller_options.rows = smaller_options.cols = 16;
  std::unique_ptr<EmulatorMatrix> smaller(
      EmulatorMatrix::Create(smaller_options, emulator_options));
  if (!Check(bool(smaller), "create smaller emulator")) return 1;
  FrameCanvas *small = smaller->CreateFrameCanvas();
  small->Fill(255, 0, 0);
  next->CopyFrom(*small);
  if (!Check(next->GetPixel(0, 0, &r, &g, &b) && r == 11 && g == 22 && b == 33,
             "incompatible frame copies must leave the destination unchanged")) return 1;
  RGBMatrix::Options invalid = options; invalid.brightness = 0;
  if (!Check(!EmulatorMatrix::Create(invalid, emulator_options), "invalid options must be rejected")) return 1;
  invalid = options; invalid.cols = INT_MAX; invalid.chain_length = 2;
  if (!Check(!EmulatorMatrix::Create(invalid, emulator_options), "overflowing geometry must be rejected")) return 1;
  matrix->Render();
  return 0;
}

#include "content-streamer.h"
#include "led-matrix.h"
#ifdef ENABLE_EMULATOR
#include "emulator.h"
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace rgb_matrix;

static std::string Snapshot(const FrameCanvas &frame) {
  const char *data = NULL;
  size_t size = 0;
  frame.Serialize(&data, &size);
  return std::string(data, size);
}

static bool Check(bool read, const FrameCanvas &frame, uint32_t hold,
                  const std::string &expected, uint32_t expected_hold,
                  const char *step) {
  if (read && hold == expected_hold && Snapshot(frame) == expected) return true;
  fprintf(stderr, "%s: frame payload or hold time differs\n", step);
  return false;
}

int main(int argc, char **argv) {
  if (argc != 2) return 1;
  const bool repeat_peek = strcmp(argv[1], "repeat-peek") == 0;
  RGBMatrix::Options options;
  options.rows = options.cols = 64;
  options.chain_length = options.parallel = 2;
#ifdef ENABLE_EMULATOR
  EmulatorOptions emulator_options;
  emulator_options.headless = true;
  std::unique_ptr<EmulatorMatrix> matrix(EmulatorMatrix::Create(options, emulator_options));
#else
  RuntimeOptions runtime;
  runtime.do_gpio_init = false;
  runtime.drop_privileges = -1;
  std::unique_ptr<RGBMatrix> matrix(RGBMatrix::CreateFromOptions(options, runtime));
#endif
  if (!matrix) return 1;
  FrameCanvas *source = matrix->CreateFrameCanvas();
  FrameCanvas *target = matrix->CreateFrameCanvas();
  for (int y = 0; y < source->height(); ++y)
    for (int x = 0; x < source->width(); ++x)
      source->SetPixel(x, y, x * 2, y * 2, (x + y) & 255);
  const std::string first = Snapshot(*source);
  MemStreamIO stream;
  StreamWriter writer(&stream);
  if (!writer.Stream(*source, 12345)) return 1;
  source->Fill(23, 145, 67);
  const std::string second = Snapshot(*source);
  if (!writer.Stream(*source, 67890)) return 1;
  StreamReader reader(&stream);
  uint32_t hold = 0;
  bool read = reader.PeekNext(target, &hold);
  if (!Check(read, *target, hold, first, 12345, "first peek")) return 1;
  if (repeat_peek) {
    for (int i = 0; i < 3; ++i) {
      target->Clear();
      read = reader.PeekNext(target, &hold);
      if (!Check(read, *target, hold, first, 12345, "repeated peek")) return 1;
    }
  }
  target->Clear();
  read = reader.GetNext(target, &hold);
  if (!Check(read, *target, hold, first, 12345, "consume peek")) return 1;
  read = reader.GetNext(target, &hold);
  if (!Check(read, *target, hold, second, 67890, "next frame")) return 1;
  if (reader.GetNext(target, &hold)) return 1;
  reader.Rewind();
  if (!reader.PeekNext(target, NULL)) return 1;
  reader.Rewind();
  read = reader.GetNext(target, &hold);
  if (!Check(read, *target, hold, first, 12345, "rewind peek")) return 1;
  return 0;
}

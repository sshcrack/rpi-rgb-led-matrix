#include "content-streamer.h"
#include "emulator.h"
#include <cstdint>
#include <memory>
using namespace rgb_matrix;
int main() {
  RGBMatrix::Options options;
  options.rows = 32;
  options.cols = 64;
  options.chain_length = 2;
  EmulatorOptions emulator_options;
  emulator_options.headless = true;
  std::unique_ptr<EmulatorMatrix> matrix(EmulatorMatrix::Create(options, emulator_options));
  if (!matrix || matrix->width() != 128 || matrix->height() != 32) return 1;
  FrameCanvas *first = matrix->CreateFrameCanvas();
  FrameCanvas *second = matrix->CreateFrameCanvas();
  first->SetPixel(3, 4, 11, 22, 33);
  uint8_t r = 0, g = 0, b = 0;
  if (!first->GetPixel(3, 4, &r, &g, &b) || r != 11 || g != 22 || b != 33) return 2;
  second->CopyFrom(*first);
  if (!second->GetPixel(3, 4, &r, &g, &b) || r != 11 || g != 22 || b != 33) return 3;
  MemStreamIO stream;
  StreamWriter writer(&stream);
  if (!writer.Stream(*first, 12345)) return 4;
  second->Clear();
  StreamReader reader(&stream);
  uint32_t hold = 0;
  if (!reader.GetNext(second, &hold) || hold != 12345) return 5;
  if (!second->GetPixel(3, 4, &r, &g, &b) || r != 11 || g != 22 || b != 33) return 6;
  return 0;
}

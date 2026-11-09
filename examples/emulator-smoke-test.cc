#include "content-streamer.h"
#include "emulator.h"
#include <cstdint>
#include <memory>
#include <vector>
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
  // Packed RGB and Color bulk writes must use the software framebuffer on
  // SDL/Linux as well as the native Windows headless backend.
  const int width = first->width(), height = first->height();
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  std::vector<Color> colors(static_cast<size_t>(width) * height);
  for (size_t i = 0; i < colors.size(); ++i) {
    colors[i] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i / width),
                 static_cast<uint8_t>(i * 13)};
    rgb[i * 3] = colors[i].r;
    rgb[i * 3 + 1] = colors[i].g;
    rgb[i * 3 + 2] = colors[i].b;
  }
  first->SetRGB(0, 0, width, height, rgb.data());
  second->SetPixels(0, 0, width, height, colors.data());
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const auto &c = colors[y * width + x];
    for (auto *canvas : {first, second})
      if (!canvas->GetPixel(x, y, &r, &g, &b) || r != c.r || g != c.g || b != c.b)
        return 7;
  }
  first->Clear();
  const uint8_t clipped[] = {1,2,3, 4,5,6, 7,8,9, 10,11,12, 13,14,15, 16,17,18};
  first->SetRGB(-1, -1, 3, 2, clipped);
  first->SetRGB(0, 0, width, height, nullptr);
  first->SetPixels(0, 0, width, height, nullptr);
  if (!first->GetPixel(0, 0, &r, &g, &b) || r != 13 || g != 14 || b != 15) return 8;
  if (!first->GetPixel(1, 0, &r, &g, &b) || r != 16 || g != 17 || b != 18) return 9;
  if (!first->GetPixel(2, 0, &r, &g, &b) || r != 0 || g != 0 || b != 0) return 10;
  return 0;
}

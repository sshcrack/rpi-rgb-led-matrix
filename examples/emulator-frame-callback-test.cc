// EmulatorMatrix::SetFrameCallback() delivers every shown frame in-process.

#include "emulator.h"
#include "pixel-mapper.h"

#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace rgb_matrix;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)

struct Received {
  int calls = 0;
  int width = 0, height = 0;
  std::vector<uint8_t> rgb;
  std::thread::id thread;
};

static EmulatorMatrix::FrameCallback Record(Received *received) {
  return [received](const uint8_t *rgb, int width, int height) {
    ++received->calls;
    received->width = width;
    received->height = height;
    received->rgb.assign(rgb, rgb + static_cast<size_t>(width) * height * 3);
    received->thread = std::this_thread::get_id();
  };
}

int main() {
  RGBMatrix::Options options;
  options.rows = 16;
  options.cols = 32;
  options.brightness = 50;
  EmulatorOptions emulator_options;
  emulator_options.headless = true;  // No window, no export: only the callback.
  std::unique_ptr<EmulatorMatrix> matrix(EmulatorMatrix::Create(options, emulator_options));
  CHECK(matrix);

  Received received;
  matrix->SetFrameCallback(Record(&received));
  FrameCanvas *offscreen = matrix->CreateFrameCanvas();
  offscreen->Fill(200, 100, 40);
  offscreen->SetPixel(31, 15, 2, 4, 6);
  offscreen = matrix->SwapOnVSync(offscreen);
  CHECK(received.calls == 1);
  CHECK(received.thread == std::this_thread::get_id());
  CHECK(received.width == 32 && received.height == 16);
  CHECK(received.rgb[0] == 100 && received.rgb[1] == 50 && received.rgb[2] == 20);
  const size_t last = (15 * 32 + 31) * 3;
  CHECK(received.rgb[last] == 1 && received.rgb[last + 1] == 2 && received.rgb[last + 2] == 3);

  // Like the other outputs, an unchanged frame is not delivered twice.
  matrix->Render();
  matrix->SwapOnVSync(nullptr);
  CHECK(received.calls == 1);
  matrix->SetPixel(0, 0, 0, 0, 0);
  matrix->Render();
  CHECK(received.calls == 2 && received.rgb[0] == 0);
  matrix->SetBrightness(100);
  matrix->Render();
  CHECK(received.calls == 3 && received.rgb[3] == 200);

  // A replacing callback gets the current frame on the next Render().
  Received replacement;
  matrix->SetFrameCallback(Record(&replacement));
  matrix->Render();
  CHECK(replacement.calls == 1 && received.calls == 3);
  CHECK(replacement.rgb[3] == 200);

  // Removing the callback stops delivery.
  matrix->SetFrameCallback(nullptr);
  offscreen->Fill(1, 1, 1);
  matrix->SwapOnVSync(offscreen);
  CHECK(replacement.calls == 1 && received.calls == 3);

  // Frames are delivered as shown: the visible size with a pixel mapper.
  RGBMatrix::Options rotated = options;
  rotated.pixel_mapper_config = "Rotate:90";
  std::unique_ptr<EmulatorMatrix> rotated_matrix(EmulatorMatrix::Create(rotated, emulator_options));
  CHECK(rotated_matrix);
  Received rotated_frames;
  rotated_matrix->SetFrameCallback(Record(&rotated_frames));
  rotated_matrix->SetPixel(0, 31, 255, 0, 0);
  rotated_matrix->Render();
  CHECK(rotated_frames.calls == 1 && rotated_frames.width == 16 && rotated_frames.height == 32);
  CHECK(rotated_frames.rgb[(31 * 16) * 3] == 127);
  return 0;
}

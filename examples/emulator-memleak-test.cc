#include "matrix-factory.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using namespace rgb_matrix;

int main(int argc, char *argv[]) {
  MatrixFactory::Options options;
  options.led_options.cols = 32;
  options.led_options.rows = 32;
  options.led_options.chain_length = 1;
  options.led_options.parallel = 1;
  options.emulator_options.display_scale = 1;
  options.emulator_options.headless = true;

  if (!MatrixFactory::ParseOptionsFromFlags(&argc, &argv, &options)) {
    return 1;
  }

  options.use_emulator = true;

  RGBMatrixBase *matrix = MatrixFactory::CreateMatrix(options);
  if (matrix == NULL) {
    fprintf(stderr, "Failed to create matrix\n");
    return 1;
  }

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();
  if (offscreen == NULL) {
    fprintf(stderr, "Failed to create offscreen canvas\n");
    delete matrix;
    return 1;
  }

  // Run a few frames
  for (int i = 0; i < 10; i++) {
    offscreen->Fill(255, 0, 0);
    offscreen = matrix->SwapOnVSync(offscreen);
    usleep(1000);
  }

  matrix->Clear();
  delete matrix;

  fprintf(stderr, "=== MEMLEAK TEST COMPLETE ===\n");
  return 0;
}

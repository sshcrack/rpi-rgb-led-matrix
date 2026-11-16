// Checks that MatrixFactory accepts every emulator flag in both the
// "--flag=value" and "--flag value" forms, removes consumed flags cleanly,
// and prints no spurious warnings.
#include "matrix-factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

using namespace rgb_matrix;

static int failures = 0;
static void Check(bool ok, const char *what) {
  if (!ok) { fprintf(stdout, "FAIL: %s\n", what); ++failures; }
}

struct ParseResult {
  bool ok;
  MatrixFactory::Options options;
  std::vector<std::string> remaining;
  std::string stderr_text;
};

static ParseResult Parse(std::vector<std::string> args) {
  ParseResult result;
  result.options.led_options.hardware_mapping = "regular";
  std::vector<char *> argv;
  for (auto &a : args) argv.push_back(&a[0]);
  argv.push_back(nullptr);
  int argc = static_cast<int>(args.size());
  char **argv_ptr = argv.data();

  fflush(stderr);
  FILE *capture = tmpfile();
  const int saved = dup(fileno(stderr));
  dup2(fileno(capture), fileno(stderr));
  result.ok = MatrixFactory::ParseOptionsFromFlags(&argc, &argv_ptr, &result.options);
  fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  rewind(capture);
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), capture)) > 0) result.stderr_text.append(buf, n);
  fclose(capture);

  for (int i = 0; i < argc; ++i) result.remaining.push_back(argv_ptr[i] ? argv_ptr[i] : "<null>");
  Check(argv_ptr[argc] == nullptr, "argv must stay null-terminated");
  return result;
}

int main() {
  {
    auto r = Parse({"prog", "--led-emulator", "--led-emulator-scale=7",
                    "--led-emulator-title=Hello", "--led-emulator-refresh=30",
                    "--led-emulator-hardware-timing", "--led-emulator-headless",
                    "--led-emulator-frame-export=/tmp/frame.bin",
                    "--led-rows=16", "--led-cols", "48", "positional"});
    Check(r.ok, "equals form parses");
    Check(r.options.use_emulator, "--led-emulator enables emulator");
    Check(r.options.emulator_options.display_scale == 7, "scale=7");
    Check(r.options.emulator_options.window_title == "Hello", "title");
    Check(r.options.emulator_options.refresh_rate_hz == 30, "refresh");
    Check(r.options.emulator_options.emulate_hardware_timing, "hardware timing");
    Check(r.options.emulator_options.headless, "headless");
    Check(r.options.emulator_options.frame_export_path == "/tmp/frame.bin", "frame export");
    Check(r.options.led_options.rows == 16 && r.options.led_options.cols == 48, "core flags");
    Check(r.remaining == std::vector<std::string>({"prog", "positional"}),
          "only positional arguments remain");
    Check(r.stderr_text.empty(), "no warnings for valid flags");
    if (!r.stderr_text.empty()) printf("stderr was: %s\n", r.stderr_text.c_str());
  }
  {
    auto r = Parse({"prog", "--led-emulator-scale", "9", "--led-emulator",
                    "--led-emulator-title", "Two words", "--led-emulator-refresh", "45"});
    Check(r.ok, "space form parses");
    Check(r.options.emulator_options.display_scale == 9, "scale 9 (space form, before --led-emulator)");
    Check(r.options.emulator_options.window_title == "Two words", "title (space form)");
    Check(r.options.emulator_options.refresh_rate_hz == 45, "refresh (space form)");
    Check(r.remaining == std::vector<std::string>({"prog"}), "space-form values consumed");
    Check(r.stderr_text.empty(), "no warnings for space form");
  }
  {
    auto r = Parse({"prog", "--led-emulator-scale=4"});
    Check(r.ok && !r.options.use_emulator, "emulator flags alone do not enable the emulator");
    Check(r.options.emulator_options.display_scale == 4, "scale parsed without --led-emulator");
    Check(r.remaining.size() == 1 && r.stderr_text.empty(), "consumed silently");
  }
  {
    auto r = Parse({"prog", "--led-emulator", "--led-emulator-scale=abc"});
    Check(!r.ok, "non-numeric scale is rejected");
    r = Parse({"prog", "--led-emulator", "--led-emulator-scale=0"});
    Check(!r.ok, "zero scale is rejected");
    r = Parse({"prog", "--led-emulator", "--led-emulator-scale"});
    Check(!r.ok, "missing scale value is rejected");
  }
  {
    auto r = Parse({"prog", "--led-emulator", "--led-emulator-scael=3"});
    Check(r.stderr_text.find("--led-emulator-scael=3") != std::string::npos &&
          r.stderr_text.find("Typo") != std::string::npos,
          "misspelled emulator flag warns once");
    Check(r.stderr_text.find("Typo") == r.stderr_text.rfind("Typo"), "warned only once");
  }
  {
    auto r = Parse({"prog", "--led-emulator", "--led-emulator-headless", "--led-emulator-no-headless"});
    Check(r.ok && !r.options.emulator_options.headless, "--led-emulator-no-headless");
  }
  if (failures == 0) printf("matrix-factory-flags-test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}

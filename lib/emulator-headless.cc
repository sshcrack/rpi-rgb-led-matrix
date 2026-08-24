#include "emulator.h"

#ifdef ENABLE_EMULATOR

#include "framebuffer-interface.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace rgb_matrix {
namespace internal {

class HeadlessFramebuffer final : public FramebufferInterface {
public:
  HeadlessFramebuffer(int width, int height)
      : width_(width), height_(height),
        pixels_(static_cast<size_t>(width) * static_cast<size_t>(height) * 3U, 0) {}

  int width() const override { return width_; }
  int height() const override { return height_; }

  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    const size_t offset = static_cast<size_t>(y * width_ + x) * 3U;
    pixels_[offset] = r;
    pixels_[offset + 1] = g;
    pixels_[offset + 2] = b;
  }

  bool GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    const size_t offset = static_cast<size_t>(y * width_ + x) * 3U;
    if (r) *r = pixels_[offset];
    if (g) *g = pixels_[offset + 1];
    if (b) *b = pixels_[offset + 2];
    return true;
  }

  void Clear() override { std::fill(pixels_.begin(), pixels_.end(), uint8_t{0}); }

  void Fill(uint8_t r, uint8_t g, uint8_t b) override {
    for (size_t i = 0; i < pixels_.size(); i += 3) {
      pixels_[i] = r;
      pixels_[i + 1] = g;
      pixels_[i + 2] = b;
    }
  }

  bool SetPWMBits(uint8_t value) override {
    if (value < 1 || value > 11) return false;
    pwm_bits_ = value;
    return true;
  }
  uint8_t pwmbits() override { return pwm_bits_; }
  void set_luminance_correct(bool on) override { luminance_correct_ = on; }
  bool luminance_correct() const override { return luminance_correct_; }
  void SetBrightness(uint8_t value) override {
    brightness_ = std::max<uint8_t>(1, std::min<uint8_t>(100, value));
  }
  uint8_t brightness() override { return brightness_; }

  void Serialize(const char **data, size_t *len) const override {
    *data = reinterpret_cast<const char *>(pixels_.data());
    *len = pixels_.size();
  }

  bool Deserialize(const char *data, size_t len) override {
    if (!data || len != pixels_.size()) return false;
    std::memcpy(pixels_.data(), data, len);
    return true;
  }

  void CopyFrom(const FramebufferInterface *other) override {
    if (!other || other == this || other->width() != width_ || other->height() != height_) return;
    const char *data = nullptr;
    size_t len = 0;
    other->Serialize(&data, &len);
    Deserialize(data, len);
  }

private:
  int width_;
  int height_;
  std::vector<uint8_t> pixels_;
  uint8_t brightness_ = 100;
  uint8_t pwm_bits_ = 8;
  bool luminance_correct_ = false;
};

}  // namespace internal

RGBMatrix::Options::Options()
    : hardware_mapping("regular"), rows(32), cols(32), chain_length(1), parallel(1),
      pwm_bits(11), pwm_lsb_nanoseconds(130), pwm_dither_bits(0), brightness(100),
      scan_mode(0), row_address_type(0), multiplexing(0), disable_hardware_pulsing(false),
      show_refresh_rate(false), inverse_colors(false), led_rgb_sequence("RGB"),
      pixel_mapper_config(nullptr), panel_type(nullptr), limit_refresh_rate_hz(0),
      disable_busy_waiting(false) {}

bool RGBMatrix::Options::Validate(std::string *err_in) const {
  std::string scratch;
  std::string *err = err_in ? err_in : &scratch;
  bool valid = true;
  const auto fail = [&](const char *message) {
    err->append(message);
    err->push_back('\n');
    valid = false;
  };

  if (rows < 8 || rows > 64 || rows % 2 != 0) fail("rows must be even and between 8 and 64");
  if (cols < 16) fail("cols must be at least 16");
  if (chain_length < 1) fail("chain_length must be positive");
  if (parallel < 1 || parallel > 3) fail("parallel must be between 1 and 3");
  if (brightness < 1 || brightness > 100) fail("brightness must be between 1 and 100");
  if (pwm_bits < 1 || pwm_bits > 11) fail("pwm_bits must be between 1 and 11");
  if (scan_mode < 0 || scan_mode > 1) fail("scan_mode must be 0 or 1");
  if (row_address_type < 0 || row_address_type > 5) fail("row_address_type must be between 0 and 5");
  if (multiplexing < 0) fail("multiplexing must not be negative");
  if (pwm_lsb_nanoseconds < 50 || pwm_lsb_nanoseconds > 3000) fail("pwm_lsb_nanoseconds must be between 50 and 3000");
  if (pwm_dither_bits < 0 || pwm_dither_bits > 2) fail("pwm_dither_bits must be between 0 and 2");
  if (!led_rgb_sequence || std::strlen(led_rgb_sequence) != 3 ||
      (!std::strchr(led_rgb_sequence, 'R') && !std::strchr(led_rgb_sequence, 'r')) ||
      (!std::strchr(led_rgb_sequence, 'G') && !std::strchr(led_rgb_sequence, 'g')) ||
      (!std::strchr(led_rgb_sequence, 'B') && !std::strchr(led_rgb_sequence, 'b'))) {
    fail("led_rgb_sequence must contain R, G and B");
  }
  if (!valid && !err_in) std::fprintf(stderr, "%s", err->c_str());
  return valid;
}

RuntimeOptions::RuntimeOptions()
    : gpio_slowdown(1), daemon(0), drop_privileges(0), do_gpio_init(false),
      drop_priv_user(nullptr), drop_priv_group(nullptr) {}

EmulatorOptions::EmulatorOptions()
    : display_scale(1), window_title("RGB Matrix Headless Emulator"),
      emulate_hardware_timing(false), refresh_rate_hz(60), frame_export_path(""),
      headless(true) {}

class EmulatorMatrix::Impl {
public:
  Impl(const RGBMatrix::Options &options, const EmulatorOptions &emulator_options)
      : width(std::max(1, options.cols * options.chain_length)),
        height(std::max(1, options.rows * options.parallel)),
        brightness(std::max(1, std::min(100, options.brightness))),
        pwm_bits(std::max(1, std::min(11, options.pwm_bits))),
        emulator_options(emulator_options) {}

  void ExportFrame() const {
    if (emulator_options.frame_export_path.empty() || !active) return;
    const std::string tmp = emulator_options.frame_export_path + ".tmp";
    FILE *file = std::fopen(tmp.c_str(), "wb");
    if (!file) return;
    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    std::fwrite(&w, sizeof(w), 1, file);
    std::fwrite(&h, sizeof(h), 1, file);
    const float factor = static_cast<float>(brightness) / 100.0f;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        uint8_t r = 0, g = 0, b = 0;
        active->GetPixel(x, y, &r, &g, &b);
        const uint8_t pixel[3] = {
            static_cast<uint8_t>(r * factor),
            static_cast<uint8_t>(g * factor),
            static_cast<uint8_t>(b * factor)};
        std::fwrite(pixel, 1, sizeof(pixel), file);
      }
    }
    std::fclose(file);
    std::remove(emulator_options.frame_export_path.c_str());
    std::rename(tmp.c_str(), emulator_options.frame_export_path.c_str());
  }

  int width;
  int height;
  uint8_t brightness;
  uint8_t pwm_bits;
  bool luminance_correct = false;
  EmulatorOptions emulator_options;
  FrameCanvas *active = nullptr;
  std::vector<FrameCanvas *> canvases;
};

EmulatorMatrix::EmulatorMatrix(Impl *impl) : impl_(impl) {}
EmulatorMatrix::~EmulatorMatrix() {
  for (auto *canvas : impl_->canvases) delete canvas;
  delete impl_;
}

EmulatorMatrix *EmulatorMatrix::Create(const RGBMatrix::Options &options,
                                       const EmulatorOptions &emulator_options) {
  std::string error;
  if (!options.Validate(&error)) return nullptr;
  auto *matrix = new EmulatorMatrix(new Impl(options, emulator_options));
  matrix->CreateFrameCanvas();
  return matrix;
}

int EmulatorMatrix::width() const { return impl_->width; }
int EmulatorMatrix::height() const { return impl_->height; }
void EmulatorMatrix::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) { if (impl_->active) impl_->active->SetPixel(x, y, r, g, b); }
bool EmulatorMatrix::GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const { return impl_->active && impl_->active->GetPixel(x, y, r, g, b); }
void EmulatorMatrix::Clear() { if (impl_->active) impl_->active->Clear(); }
void EmulatorMatrix::Fill(uint8_t r, uint8_t g, uint8_t b) { if (impl_->active) impl_->active->Fill(r, g, b); }

FrameCanvas *EmulatorMatrix::CreateFrameCanvas() {
  auto *canvas = new FrameCanvas(new internal::HeadlessFramebuffer(impl_->width, impl_->height));
  canvas->SetBrightness(impl_->brightness);
  canvas->SetPWMBits(impl_->pwm_bits);
  canvas->set_luminance_correct(impl_->luminance_correct);
  impl_->canvases.push_back(canvas);
  if (!impl_->active) impl_->active = canvas;
  return canvas;
}

FrameCanvas *EmulatorMatrix::SwapOnVSync(FrameCanvas *other, unsigned) {
  FrameCanvas *previous = impl_->active;
  if (other) impl_->active = other;
  return previous;
}

bool EmulatorMatrix::ApplyPixelMapper(const PixelMapper *mapper) { return mapper == nullptr; }
void EmulatorMatrix::SetBrightness(uint8_t value) {
  impl_->brightness = std::max<uint8_t>(1, std::min<uint8_t>(100, value));
  for (auto *canvas : impl_->canvases) canvas->SetBrightness(impl_->brightness);
}
uint8_t EmulatorMatrix::brightness() { return impl_->brightness; }
bool EmulatorMatrix::SetPWMBits(uint8_t value) {
  if (value < 1 || value > 11) return false;
  impl_->pwm_bits = value;
  for (auto *canvas : impl_->canvases) canvas->SetPWMBits(value);
  return true;
}
uint8_t EmulatorMatrix::pwmbits() { return impl_->pwm_bits; }
void EmulatorMatrix::set_luminance_correct(bool on) {
  impl_->luminance_correct = on;
  for (auto *canvas : impl_->canvases) canvas->set_luminance_correct(on);
}
bool EmulatorMatrix::luminance_correct() const { return impl_->luminance_correct; }
bool EmulatorMatrix::StartRefresh() { return true; }
void EmulatorMatrix::Render() { impl_->ExportFrame(); }

bool ParseEmulatorOptionsFromFlags(int *argc, char ***argv, EmulatorOptions *options, bool remove_consumed_flags) {
  if (!argc || !argv || !*argv || !options) return false;
  int write_index = 1;
  for (int i = 1; i < *argc; ++i) {
    const char *option = (*argv)[i];
    if (!option) continue;
    bool consumed = false;
    if (std::strncmp(option, "--led-emulator-scale=", 21) == 0) {
      options->display_scale = std::atoi(option + 21); consumed = true;
    } else if (std::strncmp(option, "--led-emulator-title=", 21) == 0) {
      options->window_title = option + 21; consumed = true;
    } else if (std::strncmp(option, "--led-emulator-refresh=", 23) == 0) {
      options->refresh_rate_hz = std::atoi(option + 23); consumed = true;
    } else if (std::strcmp(option, "--led-emulator-hardware-timing") == 0) {
      options->emulate_hardware_timing = true; consumed = true;
    } else if (std::strncmp(option, "--led-emulator-frame-export=", 28) == 0) {
      options->frame_export_path = option + 28; consumed = true;
    } else if (std::strcmp(option, "--led-emulator-headless") == 0) {
      options->headless = true; consumed = true;
    }
    if (!remove_consumed_flags || !consumed) (*argv)[write_index++] = (*argv)[i];
  }
  if (remove_consumed_flags) {
    for (int i = write_index; i < *argc; ++i) (*argv)[i] = nullptr;
    *argc = write_index;
  }
  return true;
}

void PrintEmulatorFlags(FILE *out, const EmulatorOptions &defaults) {
  std::fprintf(out,
      "\t--led-emulator-scale=<scale>       : Window scale factor (Default: %d)\n"
      "\t--led-emulator-title=<title>       : Window title (Default: %s)\n"
      "\t--led-emulator-refresh=<hz>        : Refresh rate (Default: %d)\n"
      "\t--led-emulator-frame-export=<path> : Export raw RGB frames\n"
      "\t--led-emulator-headless            : Headless mode (Default: on)\n",
      defaults.display_scale, defaults.window_title.c_str(), defaults.refresh_rate_hz);
}

}  // namespace rgb_matrix

#endif

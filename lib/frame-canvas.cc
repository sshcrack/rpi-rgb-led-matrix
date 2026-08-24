#include "led-matrix.h"
#include "framebuffer-interface.h"

namespace rgb_matrix {

FrameCanvas::~FrameCanvas() { delete frame_; }
int FrameCanvas::width() const { return frame_->width(); }
int FrameCanvas::height() const { return frame_->height(); }
void FrameCanvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) { frame_->SetPixel(x, y, r, g, b); }
void FrameCanvas::SetPixels(int x, int y, int width, int height, Color *colors) { frame_->SetPixels(x, y, width, height, colors); }
void FrameCanvas::Clear() { frame_->Clear(); }
void FrameCanvas::Fill(uint8_t r, uint8_t g, uint8_t b) { frame_->Fill(r, g, b); }
bool FrameCanvas::SetPWMBits(uint8_t value) { return frame_->SetPWMBits(value); }
uint8_t FrameCanvas::pwmbits() { return frame_->pwmbits(); }
void FrameCanvas::set_luminance_correct(bool on) { frame_->set_luminance_correct(on); }
bool FrameCanvas::luminance_correct() const { return frame_->luminance_correct(); }
void FrameCanvas::SetBrightness(uint8_t brightness) { frame_->SetBrightness(brightness); }
uint8_t FrameCanvas::brightness() { return frame_->brightness(); }
void FrameCanvas::Serialize(const char **data, size_t *len) const { frame_->Serialize(data, len); }
bool FrameCanvas::Deserialize(const char *data, size_t len) { return frame_->Deserialize(data, len); }
void FrameCanvas::CopyFrom(const FrameCanvas &other) { frame_->CopyFrom(other.frame_); }
bool FrameCanvas::GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b) const {
  if (x < 0 || x >= width() || y < 0 || y >= height()) return false;
  return frame_->GetPixel(x, y, r, g, b);
}

}  // namespace rgb_matrix

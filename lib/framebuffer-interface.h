#ifndef RPI_RGBMATRIX_FRAMEBUFFER_INTERFACE_H
#define RPI_RGBMATRIX_FRAMEBUFFER_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#include "graphics.h"

namespace rgb_matrix {
namespace internal {

class FramebufferInterface {
public:
  virtual ~FramebufferInterface() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) = 0;
  virtual bool GetPixel(int x, int y, uint8_t *red, uint8_t *green, uint8_t *blue) const = 0;
  virtual void Clear() = 0;
  virtual void Fill(uint8_t red, uint8_t green, uint8_t blue) = 0;

  virtual bool SetPWMBits(uint8_t value) = 0;
  virtual uint8_t pwmbits() = 0;
  virtual void set_luminance_correct(bool on) = 0;
  virtual bool luminance_correct() const = 0;
  virtual void SetBrightness(uint8_t brightness) = 0;
  virtual uint8_t brightness() = 0;

  virtual void Serialize(const char **data, size_t *len) const = 0;
  virtual bool Deserialize(const char *data, size_t len) = 0;
  virtual void CopyFrom(const FramebufferInterface *other) = 0;

  void SetPixels(int x, int y, int width, int height, Color *colors) {
    if (!colors) return;
    for (int row = 0; row < height; ++row) {
      for (int col = 0; col < width; ++col) {
        const Color &color = colors[row * width + col];
        SetPixel(x + col, y + row, color.r, color.g, color.b);
      }
    }
  }
};

}  // namespace internal
}  // namespace rgb_matrix

#endif

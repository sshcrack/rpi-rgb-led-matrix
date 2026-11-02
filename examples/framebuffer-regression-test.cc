#include "led-matrix.h"
#include "content-streamer.h"
#ifdef ENABLE_EMULATOR
#include "emulator.h"
#endif
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using namespace rgb_matrix;
#ifdef ENABLE_EMULATOR
using TestMatrix = EmulatorMatrix;
#else
using TestMatrix = RGBMatrix;
#endif
static std::unique_ptr<TestMatrix> Create(int side = 32, bool inverse = false) {
  RGBMatrix::Options options; options.rows = options.cols = side; options.inverse_colors = inverse;
#ifdef ENABLE_EMULATOR
  EmulatorOptions eo; eo.headless = true;
  return std::unique_ptr<TestMatrix>(EmulatorMatrix::Create(options, eo));
#else
  RuntimeOptions runtime; runtime.do_gpio_init = false; runtime.drop_privileges = -1;
  return std::unique_ptr<TestMatrix>(RGBMatrix::CreateFromOptions(options, runtime));
#endif
}
static std::string Bytes(FrameCanvas *frame) {
  const char *data = nullptr; size_t size = 0; frame->Serialize(&data, &size);
  return std::string(data, size);
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"Failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
  static_assert(!std::is_copy_constructible<StreamReader>::value, "Reader must own buffers uniquely");
  static_assert(!std::is_copy_assignable<StreamReader>::value, "Reader must own buffers uniquely");
  auto matrix = Create(); CHECK(matrix);
  auto *source = matrix->CreateFrameCanvas(), *target = matrix->CreateFrameCanvas();
  source->set_luminance_correct(false);
  const int white[] = {0,128,192,224,240,248,252,254,255,255,255,255};
  for (int bits = 1; bits <= 11; ++bits) {
    CHECK(source->SetPWMBits(bits)); CHECK(source->pwmbits() == bits);
    source->SetPixel(1,1,255,255,255);
    uint8_t r=0,g=0,b=0; CHECK(source->GetPixel(1,1,&r,&g,&b));
#ifndef ENABLE_EMULATOR
    CHECK(r == white[bits] && g == white[bits] && b == white[bits]);
#endif
  }
  source->SetBrightness(37); CHECK(source->brightness() == 37);
  source->set_luminance_correct(true); CHECK(source->luminance_correct());
  source->set_luminance_correct(false); CHECK(!source->luminance_correct());
#ifndef ENABLE_EMULATOR
  auto inverse = Create(32,true); CHECK(inverse);
  auto *inverted = inverse->CreateFrameCanvas();
  for (int bits = 1; bits <= 11; ++bits) {
    inverted->SetPWMBits(bits);
    inverted->set_luminance_correct(false);
    inverted->SetPixel(0,0,0,0,0); uint8_t r=0,g=0,b=0;
    CHECK(inverted->GetPixel(0,0,&r,&g,&b) && r==0 && g==0 && b==0);
    inverted->SetPixel(0,0,255,255,255);
    CHECK(inverted->GetPixel(0,0,&r,&g,&b) && r==white[bits] && r==g && g==b);
  }
  inverted->SetPWMBits(11); inverted->set_luminance_correct(true);
  for (int value : {0,32,128,255}) {
    inverted->SetPixel(0,0,value,value,value); uint8_t r=0,g=0,b=0;
    CHECK(inverted->GetPixel(0,0,&r,&g,&b));
    CHECK(std::abs(int(r)-value) <= 2 && r == g && g == b);
  }
#endif
  MemStreamIO stream;
  StreamWriter writer(&stream); CHECK(writer.Stream(*source,12345));
  const std::string expected = Bytes(source);
  StreamReader reader(&stream); uint32_t hold=0;
  CHECK(reader.PeekNext(target,&hold));
  StreamReader moved(std::move(reader));
  CHECK(!reader.GetNext(target,nullptr)); reader.Rewind(); CHECK(!reader.PeekNext(target,nullptr));
  CHECK(moved.GetNext(target,&hold) && hold==12345 && Bytes(target)==expected);
  StreamReader assigned(&stream); CHECK(assigned.PeekNext(target,nullptr));
  assigned = std::move(moved); assigned.Rewind();
  CHECK(assigned.PeekNext(target,&hold) && Bytes(target)==expected);
  stream.Clear(); auto larger = Create(64); CHECK(larger);
  auto *big = larger->CreateFrameCanvas(); big->Fill(23,145,67);
  StreamWriter replacement(&stream); CHECK(replacement.Stream(*big,67890));
  assigned.Rewind(); CHECK(assigned.PeekNext(big,&hold) && hold==67890);
  CHECK(assigned.GetNext(big,&hold));
  // Oversized/mismatched file headers must be rejected before allocating.
  std::string malformed;
  stream.Rewind(); char buf[4096]; ssize_t count;
  while ((count=stream.Read(buf,sizeof(buf)))>0) malformed.append(buf,count);
  malformed[4]=malformed[5]=malformed[6]=malformed[7]=char(0xff);
  stream.Clear(); stream.Append(malformed.data(),malformed.size()); assigned.Rewind();
  CHECK(!assigned.PeekNext(big,&hold));
  return 0;
}

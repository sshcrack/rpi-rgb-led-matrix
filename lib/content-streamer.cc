// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "content-streamer.h"
#include "led-matrix.h"

#include <cstddef>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <algorithm>
#include <climits>
#include <io.h>
#else
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "gpio-bits.h"

namespace rgb_matrix {

// Pre-c++11 helper
#define STATIC_ASSERT(msg, c) typedef int static_assert_##msg[(c) ? 1 : -1]

namespace {
// We write magic values as integers to automatically detect endian issues.
// Streams are stored in little-endian. This is the ARM default (running
// the Raspberry Pi, but also x86; so it is possible to create streams easily
// on a different x86 Linux PC.
static const uint32_t kFileMagicValue = 0xED0C5A48;
struct FileHeader {
  uint32_t magic;  // kFileMagicValue
  uint32_t buf_size;
  uint32_t width;
  uint32_t height;
  uint64_t future_use1;
  uint64_t is_wide_gpio : 1;
  uint64_t flags_future_use : 63;
};
STATIC_ASSERT(file_header_size_changed, sizeof(FileHeader) == 32);

static const uint32_t kFrameMagicValue = 0x12345678;
struct FrameHeader {
  uint32_t magic;  // kFrameMagic
  uint32_t size;
  uint32_t hold_time_us;  // How long this frame lasts in usec.
  uint32_t future_use1;
  uint64_t future_use2;
  uint64_t future_use3;
};
STATIC_ASSERT(file_header_size_changed, sizeof(FrameHeader) == 32);
}

FileStreamIO::FileStreamIO(int fd) : fd_(fd) {
#ifndef _WIN32
  posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
}
FileStreamIO::~FileStreamIO() {
#ifdef _WIN32
  _close(fd_);
#else
  close(fd_);
#endif
}
void FileStreamIO::Rewind() {
#ifdef _WIN32
  _lseek(fd_, 0, SEEK_SET);
#else
  lseek(fd_, 0, SEEK_SET);
#endif
}
ssize_t FileStreamIO::Read(void *buf, const size_t count) {
#ifdef _WIN32
  return static_cast<ssize_t>(_read(fd_, buf, static_cast<unsigned int>(std::min<size_t>(count, INT_MAX))));
#else
  return read(fd_, buf, count);
#endif
}
ssize_t FileStreamIO::Append(const void *buf, const size_t count) {
#ifdef _WIN32
  return static_cast<ssize_t>(_write(fd_, buf, static_cast<unsigned int>(std::min<size_t>(count, INT_MAX))));
#else
  return write(fd_, buf, count);
#endif
}

void MemStreamIO::Rewind() { pos_ = 0; }
ssize_t MemStreamIO::Read(void *buf, size_t count) {
  if (pos_ >= buffer_.size()) return 0;
  const size_t amount = std::min(count, buffer_.size() - pos_);
  memcpy(buf, buffer_.data() + pos_, amount);
  pos_ += amount;
  return amount;
}
ssize_t MemStreamIO::Append(const void *buf, size_t count) {
  buffer_.append((const char*)buf, count);
  return count;
}

void MemStreamIO::Clear() {
  buffer_.clear();
  pos_ = 0;
}

MemStreamIO::~MemStreamIO() {
  Clear();
}

MemMapViewInput::MemMapViewInput(int fd) : buffer_(nullptr), end_(nullptr), pos_(nullptr) {
#ifdef _WIN32
  struct _stat64 s = {};
  if (_fstat64(fd, &s) < 0 || s.st_size < 0) {
    _close(fd);
    perror("Couldn't get size");
    return;
  }
  const size_t file_size = static_cast<size_t>(s.st_size);
  buffer_ = new char[file_size ? file_size : 1];
  end_ = buffer_ + file_size;
  pos_ = buffer_;
  _lseek(fd, 0, SEEK_SET);
  size_t offset = 0;
  while (offset < file_size) {
    const int amount = _read(fd, buffer_ + offset,
      static_cast<unsigned int>(std::min<size_t>(file_size - offset, INT_MAX)));
    if (amount <= 0) {
      delete[] buffer_;
      buffer_ = end_ = pos_ = nullptr;
      break;
    }
    offset += static_cast<size_t>(amount);
  }
  _close(fd);
#else
  struct stat s;
  if (fstat(fd, &s) < 0) {
    close(fd);
    perror("Couldn't get size");
    return;
  }
  const size_t file_size = s.st_size;
  buffer_ = (char*)mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (buffer_ == MAP_FAILED) {
    buffer_ = nullptr;
    perror("Can't mmap()");
    return;
  }
  end_ = buffer_ + file_size;
  pos_ = buffer_;
#ifdef POSIX_MADV_WILLNEED
  posix_madvise(buffer_, file_size, POSIX_MADV_WILLNEED);
#endif
#endif
}

void MemMapViewInput::Rewind() { pos_ = buffer_; }
ssize_t MemMapViewInput::Read(void *buf, size_t count) {
  if (!pos_ || !end_ || count > static_cast<size_t>(end_ - pos_)) return -1;
  memcpy(buf, pos_, count);
  pos_ += count;
  return static_cast<ssize_t>(count);
}

MemMapViewInput::~MemMapViewInput() {
#ifdef _WIN32
  delete[] buffer_;
#else
  if (buffer_) munmap(buffer_, end_ - buffer_);
#endif
}

// Read exactly count bytes including retries. Returns success.
static bool FullRead(StreamIO *io, void *buf, const size_t count) {
  size_t remaining = count;
  char *char_buffer = static_cast<char *>(buf);
  while (remaining > 0) {
    const ssize_t r = io->Read(char_buffer, remaining);
    if (r <= 0) return false;
    char_buffer += r;
    remaining -= static_cast<size_t>(r);
  }
  return true;
}

static bool FullAppend(StreamIO *io, const void *buf, const size_t count) {
  size_t remaining = count;
  const char *char_buffer = static_cast<const char *>(buf);
  while (remaining > 0) {
    const ssize_t w = io->Append(char_buffer, remaining);
    if (w <= 0) return false;
    char_buffer += w;
    remaining -= static_cast<size_t>(w);
  }
  return true;
}

StreamWriter::StreamWriter(StreamIO *io) : io_(io), header_written_(false) {}
bool StreamWriter::Stream(const FrameCanvas &frame, uint32_t hold_time_us) {
#ifndef MOCK_RPI
  const char *data = nullptr;
  size_t len = 0;
  frame.Serialize(&data, &len);
  if (!data || len > UINT32_MAX) return false;
  if (!header_written_ && !WriteFileHeader(frame, len)) return false;
  FrameHeader h = {};
  h.magic = kFrameMagicValue;
  h.size = static_cast<uint32_t>(len);
  h.hold_time_us = hold_time_us;
  return FullAppend(io_, &h, sizeof(h)) && FullAppend(io_, data, len);
#else
  return true;
#endif
}

bool StreamWriter::WriteFileHeader(const FrameCanvas &frame, size_t len) {
  if (len > UINT32_MAX) return false;
  FileHeader header = {};
  header.magic = kFileMagicValue;
  header.width = static_cast<uint32_t>(frame.width());
  header.height = static_cast<uint32_t>(frame.height());
  header.buf_size = static_cast<uint32_t>(len);
  header.is_wide_gpio = (sizeof(gpio_bits_t) > 4);
  header_written_ = FullAppend(io_, &header, sizeof(header));
  return header_written_;
}

StreamReader::StreamReader(StreamIO *io)
  : io_(io), state_(STREAM_AT_BEGIN), header_frame_buffer_(NULL),
    peeked_(false), peeked_frame_buffer_(NULL), peeked_hold_time_(0) {
  io_->Rewind();
}
StreamReader::~StreamReader() {
  delete [] header_frame_buffer_;
  delete [] peeked_frame_buffer_;
}

void StreamReader::Rewind() {
#ifndef MOCK_RPI
  io_->Rewind();
  state_ = STREAM_AT_BEGIN;
  peeked_ = false;
#endif
}

bool StreamReader::GetNext(FrameCanvas *frame, uint32_t* hold_time_us) {
#ifdef MOCK_RPI
  return true;
#endif
  // If we have a peeked frame, return it and clear the peeked flag.
  if (peeked_) {
    peeked_ = false;
    bool result = frame->Deserialize(peeked_frame_buffer_, frame_buf_size_);
    if (hold_time_us) *hold_time_us = peeked_hold_time_;
    return result;
  }

  if (state_ == STREAM_AT_BEGIN && !ReadFileHeader(*frame)) return false;
  if (state_ != STREAM_READING) return false;

  // Read header and expected buffer size.
  if (!FullRead(io_, header_frame_buffer_,
                sizeof(FrameHeader) + frame_buf_size_)) {
    return false;
  }

  const FrameHeader &h = *reinterpret_cast<FrameHeader*>(header_frame_buffer_);

  // TODO: we might allow for this to be a kFileMagicValue, to allow people
  // to just concatenate streams. In that case, we just would need to read
  // ahead past this header (both headers are designed to be same size)
  if (h.magic != kFrameMagicValue) {
    state_ = STREAM_ERROR;
    return false;
  }

  // In the future, we might allow larger buffers (audio?), but never smaller.
  // For now, we need to make sure to exactly match the size, as our assumption
  // above is that we can read the full header + frame in one FullRead().
  if (h.size != frame_buf_size_)
    return false;

  if (hold_time_us) *hold_time_us = h.hold_time_us;
  return frame->Deserialize(header_frame_buffer_ + sizeof(FrameHeader),
                            frame_buf_size_);
}

bool StreamReader::PeekNext(FrameCanvas *frame, uint32_t* hold_time_us) {
#ifdef MOCK_RPI
  return true;
#endif
  if (peeked_) {
    // Already have a peeked frame, just deserialize and return it.
    bool result = frame->Deserialize(peeked_frame_buffer_, frame_buf_size_);
    if (hold_time_us) *hold_time_us = peeked_hold_time_;
    return result;
  }

  if (state_ == STREAM_AT_BEGIN && !ReadFileHeader(*frame)) return false;
  if (state_ != STREAM_READING) return false;

  // Allocate peeked_frame_buffer_ if needed
  if (!peeked_frame_buffer_)
    peeked_frame_buffer_ = new char [ sizeof(FrameHeader) + frame_buf_size_ ];

  // Read header and expected buffer size.
  if (!FullRead(io_, peeked_frame_buffer_,
                sizeof(FrameHeader) + frame_buf_size_)) {
    return false;
  }

  const FrameHeader &h = *reinterpret_cast<FrameHeader*>(peeked_frame_buffer_);

  if (h.magic != kFrameMagicValue) {
    state_ = STREAM_ERROR;
    return false;
  }

  if (h.size != frame_buf_size_)
    return false;

  peeked_ = true;
  peeked_hold_time_ = h.hold_time_us;
  bool result = frame->Deserialize(peeked_frame_buffer_ + sizeof(FrameHeader),
                                   frame_buf_size_);
  if (hold_time_us) *hold_time_us = h.hold_time_us;
  return result;
}

bool StreamReader::ReadFileHeader(const FrameCanvas &frame) {
  FileHeader header = {};
  if (!FullRead(io_, &header, sizeof(header)) || header.magic != kFileMagicValue) {
    state_ = STREAM_ERROR;
    return false;
  }
  if ((int)header.width != frame.width()
      || (int)header.height != frame.height()) {
    fprintf(stderr, "This stream is for %dx%d, can't play on %dx%d. "
            "Please use the same settings for record/replay\n",
            header.width, header.height, frame.width(), frame.height());
    state_ = STREAM_ERROR;
    return false;
  }
  if (header.is_wide_gpio != (sizeof(gpio_bits_t) == 8)) {
    fprintf(stderr, "This stream was written with %s GPIO width support but "
            "this library is compiled with %d bit GPIO width (see "
            "ENABLE_WIDE_GPIO_COMPUTE_MODULE setting in lib/Makefile)\n",
            header.is_wide_gpio ? "wide (64-bit)" : "narrow (32-bit)",
            int(sizeof(gpio_bits_t) * 8));
    state_ = STREAM_ERROR;
    return false;
  }
  state_ = STREAM_READING;
  frame_buf_size_ = header.buf_size;
  if (!header_frame_buffer_)
    header_frame_buffer_ = new char [ sizeof(FrameHeader) + header.buf_size ];
  return true;
}
}  // namespace rgb_matrix

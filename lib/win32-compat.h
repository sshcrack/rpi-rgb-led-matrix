// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// POSIX string functions used by upstream sources (pixel-mapper.cc) that
// Windows toolchains lack. Force-included by CMake on Windows only, so the
// upstream files stay unchanged.

#ifndef RPI_RGBMATRIX_WIN32_COMPAT_H
#define RPI_RGBMATRIX_WIN32_COMPAT_H

#ifdef _WIN32
#include <string.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

// Like strchr(), but returns the terminating '\0' instead of NULL.
static inline char *rgbmatrix_strchrnul(const char *s, int c) {
  while (*s && *s != (char)c) ++s;
  return (char *)s;
}
#define strchrnul rgbmatrix_strchrnul
#endif  // _WIN32

#endif  // RPI_RGBMATRIX_WIN32_COMPAT_H

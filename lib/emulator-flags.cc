// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2023 Hendrik
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// Command line flag handling shared by the SDL and the headless emulator.

#include "emulator.h"

#ifdef ENABLE_EMULATOR

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace rgb_matrix {
namespace {

const char kPrefix[] = "--led-emulator-";
const size_t kPrefixLen = sizeof(kPrefix) - 1;

// Matches "--led-emulator-<name>=value" or "--led-emulator-<name> value",
// like the core --led-* flags. Returns the value or nullptr if this argument
// is not the flag. On a missing value, sets *error and returns "".
const char *MatchValueFlag(const char *name, char **argv, int argc, int *i,
                           bool *error) {
  const char *arg = argv[*i];
  if (strncmp(arg, kPrefix, kPrefixLen) != 0) return nullptr;
  arg += kPrefixLen;
  const size_t name_len = strlen(name);
  if (strncmp(arg, name, name_len) != 0) return nullptr;
  if (arg[name_len] == '=') return arg + name_len + 1;
  if (arg[name_len] != '\0') return nullptr;  // Longer, different flag name.
  if (*i + 1 < argc && argv[*i + 1] != nullptr) return argv[++*i];
  fprintf(stderr, "Parameter expected after %s%s\n", kPrefix, name);
  *error = true;
  return "";
}

// Matches "--led-emulator-<name>" and "--led-emulator-no-<name>".
bool MatchBoolFlag(const char *name, const char *arg, bool *value) {
  if (strncmp(arg, kPrefix, kPrefixLen) != 0) return false;
  arg += kPrefixLen;
  bool result = true;
  if (strncmp(arg, "no-", 3) == 0) {
    result = false;
    arg += 3;
  }
  if (strcmp(arg, name) != 0) return false;
  *value = result;
  return true;
}

bool ParsePositiveInt(const char *name, const char *value, int *result) {
  char *end = nullptr;
  const long parsed = strtol(value, &end, 10);
  if (!*value || *end || parsed < 1 || parsed > INT_MAX) {
    fprintf(stderr, "Invalid value %s%s=%s (expected a positive integer)\n",
            kPrefix, name, value);
    return false;
  }
  *result = static_cast<int>(parsed);
  return true;
}

}  // namespace

bool ParseEmulatorOptionsFromFlags(int *argc, char ***argv,
                                   EmulatorOptions *options,
                                   bool remove_consumed_flags) {
  if (!argc || !argv || !*argv || !options) return false;
  char **args = *argv;
  const int count = *argc;

  std::vector<char *> kept;
  if (count > 0) kept.push_back(args[0]);
  bool error = false;
  for (int i = 1; i < count; ++i) {
    if (!args[i]) continue;
    const int start = i;
    bool consumed = true;
    const char *value;
    if ((value = MatchValueFlag("scale", args, count, &i, &error))) {
      if (*value) error |= !ParsePositiveInt("scale", value, &options->display_scale);
    } else if ((value = MatchValueFlag("refresh", args, count, &i, &error))) {
      if (*value) error |= !ParsePositiveInt("refresh", value, &options->refresh_rate_hz);
    } else if ((value = MatchValueFlag("title", args, count, &i, &error))) {
      options->window_title = value;
    } else if ((value = MatchValueFlag("frame-export", args, count, &i, &error))) {
      options->frame_export_path = value;
    } else if (MatchBoolFlag("hardware-timing", args[i],
                             &options->emulate_hardware_timing)) {
    } else if (MatchBoolFlag("headless", args[i], &options->headless)) {
    } else {
      consumed = false;
      if (strncmp(args[i], kPrefix, kPrefixLen) == 0) {
        fprintf(stderr, "Option %s starts with %s but it is unknown. Typo?\n",
                args[i], kPrefix);
      }
    }
    if (!consumed || !remove_consumed_flags) {
      for (int k = start; k <= i; ++k) kept.push_back(args[k]);
    }
  }
  if (error) return false;

  if (remove_consumed_flags) {
    const int new_count = static_cast<int>(kept.size());
    for (int i = 0; i < new_count; ++i) args[i] = kept[i];
    for (int i = new_count; i < count; ++i) args[i] = nullptr;
    *argc = new_count;
  }
  return true;
}

void PrintEmulatorFlags(FILE *out, const EmulatorOptions &defaults) {
  fprintf(out,
          "\t--led-emulator-scale=<scale>         : Window scale factor (Default: %d)\n"
          "\t--led-emulator-title=<title>         : Window title (Default: %s)\n"
          "\t--led-emulator-refresh=<hz>          : Refresh rate Hz that swaps are paced to (Default: %d)\n"
          "\t--led-emulator-hardware-timing       : Pace swaps even when headless (Default: %s)\n"
          "\t--led-emulator-frame-export=<path>   : Write each shown frame as raw RGB binary to path (Default: none)\n"
          "\t--led-emulator-headless              : Run without a visible window (Default: %s)\n",
          defaults.display_scale, defaults.window_title.c_str(),
          defaults.refresh_rate_hz,
          defaults.emulate_hardware_timing ? "on" : "off",
          defaults.headless ? "on" : "off");
}

}  // namespace rgb_matrix

#endif  // ENABLE_EMULATOR

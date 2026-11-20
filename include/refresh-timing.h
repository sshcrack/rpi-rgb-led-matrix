// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2026 Hendrik Lind
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

// Timing of the refresh thread: how long each full-panel refresh took.
// A refresh that takes much longer than usual (the thread was held up, e.g. by
// interrupts on its CPU) shows on the panel as flicker or a brightness glitch.
#ifndef RPI_REFRESH_TIMING_H
#define RPI_REFRESH_TIMING_H

#include <stdint.h>

namespace rgb_matrix {

// Cumulative counters since the refresh thread started. Take two snapshots
// with RGBMatrix::GetRefreshTiming() and compare them with
// RefreshTimingWindow::Between() to get the statistics of that time window.
struct RefreshTiming {
  static constexpr int kBucketMicros = 10;
  // Bucket i counts refreshes of [i, i + 1) * kBucketMicros; the last bucket
  // counts everything from (kBuckets - 1) * kBucketMicros (~41 ms) up.
  static constexpr int kBuckets = 4096;

  // 32-bit counters that wrap (lock-free on 32-bit ARM too). Differences
  // between two snapshots stay right for windows below ~71 minutes.
  uint32_t refreshes = 0;
  uint32_t total_micros = 0;
  uint32_t max_micros = 0;  // longest refresh since the start
  uint32_t histogram[kBuckets] = {};
};

struct RefreshTimingWindow {
  uint64_t refreshes = 0;
  double average_micros = 0;
  // Bucket midpoints (kBucketMicros resolution); zero for an empty window.
  uint32_t min_micros = 0;
  uint32_t median_micros = 0;
  uint32_t p99_micros = 0;
  uint32_t max_micros = 0;
  // Refreshes longer than late_factor times the median.
  uint64_t late = 0;

  // Statistics of the refreshes between two snapshots (`after` taken later).
  static RefreshTimingWindow Between(const RefreshTiming &before,
                                     const RefreshTiming &after,
                                     double late_factor = 1.5);
};

}  // namespace rgb_matrix

#endif  // RPI_REFRESH_TIMING_H

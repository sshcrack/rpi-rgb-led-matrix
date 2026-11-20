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

#include "refresh-timing.h"

namespace rgb_matrix {

namespace {
uint32_t Midpoint(int bucket) {
  return static_cast<uint32_t>(bucket) * RefreshTiming::kBucketMicros +
    RefreshTiming::kBucketMicros / 2;
}
}  // namespace

RefreshTimingWindow RefreshTimingWindow::Between(const RefreshTiming &before,
                                                 const RefreshTiming &after,
                                                 double late_factor) {
  RefreshTimingWindow w;
  // The histogram buckets are 32 bit and may wrap; unsigned differences stay
  // right as long as one bucket counts fewer than 2^32 refreshes per window.
  uint64_t counted = 0;
  for (int i = 0; i < RefreshTiming::kBuckets; ++i) {
    counted += static_cast<uint32_t>(after.histogram[i] - before.histogram[i]);
  }
  if (counted == 0) return w;
  w.refreshes = counted;
  w.average_micros =
    static_cast<double>(static_cast<uint32_t>(after.total_micros - before.total_micros))
    / static_cast<double>(counted);

  const uint64_t median_rank = (counted + 1) / 2;   // 1-based
  const uint64_t p99_rank = counted - counted / 100;  // 1-based, nearest rank
  uint64_t seen = 0;
  int median_bucket = -1;
  for (int i = 0; i < RefreshTiming::kBuckets; ++i) {
    const uint32_t n = after.histogram[i] - before.histogram[i];
    if (n == 0) continue;
    const uint32_t value = (i == RefreshTiming::kBuckets - 1)
      ? after.max_micros : Midpoint(i);
    if (seen == 0) w.min_micros = value;
    seen += n;
    if (median_bucket < 0 && seen >= median_rank) {
      median_bucket = i;
      w.median_micros = value;
    }
    if (w.p99_micros == 0 && seen >= p99_rank) w.p99_micros = value;
    w.max_micros = value;
  }

  // Late: the whole bucket lies above late_factor * median.
  const double limit = late_factor * w.median_micros;
  for (int i = median_bucket + 1; i < RefreshTiming::kBuckets; ++i) {
    if (static_cast<double>(i) * RefreshTiming::kBucketMicros >= limit) {
      w.late += static_cast<uint32_t>(after.histogram[i] - before.histogram[i]);
    }
  }
  return w;
}

}  // namespace rgb_matrix

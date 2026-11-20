// Window statistics of the refresh timing counters (no GPIO needed).
#include "refresh-timing.h"

#include <stdio.h>
#include <stdlib.h>

using rgb_matrix::RefreshTiming;
using rgb_matrix::RefreshTimingWindow;

static int failures = 0;
#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

// What the refresh thread does for one refresh of `usec`.
static void Record(RefreshTiming *t, uint32_t usec) {
  int bucket = usec / RefreshTiming::kBucketMicros;
  if (bucket >= RefreshTiming::kBuckets) bucket = RefreshTiming::kBuckets - 1;
  t->histogram[bucket]++;
  t->refreshes++;
  t->total_micros += usec;
  if (usec > t->max_micros) t->max_micros = usec;
}

int main() {
  RefreshTiming *before = new RefreshTiming();
  RefreshTiming *after = new RefreshTiming();

  // Empty window.
  RefreshTimingWindow w = RefreshTimingWindow::Between(*before, *after);
  CHECK(w.refreshes == 0 && w.median_micros == 0 && w.late == 0);

  // Earlier refreshes do not count in the window.
  for (int i = 0; i < 50; ++i) Record(before, 30000);
  *after = *before;

  // 1000 refreshes: 980 at ~11 ms, 10 at 16 ms (< 1.5x), 10 at 20 ms (late).
  for (int i = 0; i < 980; ++i) Record(after, 11000 + (i % 3) * 10);
  for (int i = 0; i < 10; ++i) Record(after, 16000);
  for (int i = 0; i < 10; ++i) Record(after, 20000);
  w = RefreshTimingWindow::Between(*before, *after);
  CHECK(w.refreshes == 1000);
  CHECK(w.min_micros == 11005);
  CHECK(w.median_micros == 11015);
  CHECK(w.p99_micros == 16005);  // rank 990
  CHECK(w.max_micros == 20005);
  CHECK(w.late == 10);
  CHECK(w.average_micros > 11000 && w.average_micros < 11200);
  // A stricter factor counts the 16 ms refreshes too.
  CHECK(RefreshTimingWindow::Between(*before, *after, 1.2).late == 20);

  // Beyond the last bucket the reported maximum is the exact longest refresh.
  Record(after, 90000);
  w = RefreshTimingWindow::Between(*before, *after);
  CHECK(w.max_micros == 90000 && w.late == 11);

  // Counters that wrapped between the snapshots still give the right window.
  RefreshTiming *wrap_before = new RefreshTiming();
  wrap_before->histogram[1100] = 0xFFFFFFF0u;
  wrap_before->total_micros = 0xFFFFFFF0u;
  RefreshTiming *wrap_after = new RefreshTiming(*wrap_before);
  for (int i = 0; i < 32; ++i) Record(wrap_after, 11000);
  w = RefreshTimingWindow::Between(*wrap_before, *wrap_after);
  CHECK(w.refreshes == 32 && w.median_micros == 11005 && w.average_micros == 11000);

  delete before;
  delete after;
  delete wrap_before;
  delete wrap_after;
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  printf("refresh-timing-test: OK\n");
  return EXIT_SUCCESS;
}

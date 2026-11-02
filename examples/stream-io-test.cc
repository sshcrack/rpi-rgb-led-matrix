#include "content-streamer.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

#ifdef _WIN32
#include <io.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static_assert(!std::is_copy_constructible<rgb_matrix::FileStreamIO>::value,
              "A file descriptor must have exactly one owner");
static_assert(!std::is_copy_assignable<rgb_matrix::FileStreamIO>::value,
              "A file descriptor must have exactly one owner");
static_assert(!std::is_copy_constructible<rgb_matrix::MemMapViewInput>::value,
              "A mapping must have exactly one owner");
static_assert(!std::is_copy_assignable<rgb_matrix::MemMapViewInput>::value,
              "A mapping must have exactly one owner");

static int Duplicate(FILE *file) {
#ifdef _WIN32
  return _dup(_fileno(file));
#else
  return dup(fileno(file));
#endif
}

static bool FileRoundTrip() {
  FILE *file = std::tmpfile();
  if (!file) return false;
  bool result = false;
  const int fd = Duplicate(file);
  if (fd >= 0) {
    rgb_matrix::FileStreamIO stream(fd);
    char data[6] = {};
    result = stream.Append("matrix", sizeof(data)) == sizeof(data);
    stream.Rewind();
    result = result && stream.Read(data, sizeof(data)) == sizeof(data) &&
             std::memcmp(data, "matrix", sizeof(data)) == 0;
  }
  std::fclose(file);
  return result;
}

static bool MappingRoundTrip() {
  FILE *file = std::tmpfile();
  if (!file) return false;
  if (std::fwrite("matrix", 1, 6, file) != 6 || std::fflush(file) != 0) {
    std::fclose(file);
    return false;
  }
  bool result = false;
  const int fd = Duplicate(file);
  if (fd >= 0) {
    rgb_matrix::MemMapViewInput stream(fd);
    char data[6] = {};
    result = stream.IsInitialized() && stream.Read(data, 6) == 6 &&
             std::memcmp(data, "matrix", 6) == 0;
    stream.Rewind();
    result = result && stream.Read(data, 6) == 6;
  }
  std::fclose(file);
  return result;
}

#ifndef _WIN32
static volatile sig_atomic_t interruptions = 0;
static void Interrupted(int) { ++interruptions; }

class InterruptTimer {
public:
  InterruptTimer() {
    struct sigaction action = {};
    action.sa_handler = Interrupted;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, &previous_);
    interruptions = 0;
    struct itimerval timer = {};
    timer.it_value.tv_usec = timer.it_interval.tv_usec = 5000;
    setitimer(ITIMER_REAL, &timer, NULL);
  }
  ~InterruptTimer() {
    struct itimerval timer = {};
    setitimer(ITIMER_REAL, &timer, NULL);
    sigaction(SIGALRM, &previous_, NULL);
  }
private:
  struct sigaction previous_;
};

static bool WaitFor(pid_t child) {
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool InterruptedRead() {
  int fds[2];
  if (pipe(fds) != 0) return false;
  const pid_t child = fork();
  if (child == 0) {
    close(fds[0]);
    usleep(50000);
    const bool wrote = write(fds[1], "x", 1) == 1;
    close(fds[1]);
    _exit(wrote ? 0 : 1);
  }
  close(fds[1]);
  if (child < 0) { close(fds[0]); return false; }
  bool result;
  {
    rgb_matrix::FileStreamIO stream(fds[0]);
    InterruptTimer timer;
    char data = 0;
    result = stream.Read(&data, 1) == 1 && data == 'x' && interruptions > 0;
  }
  return WaitFor(child) && result;
}

static bool InterruptedWrite() {
  int fds[2];
  if (pipe(fds) != 0) return false;
  const int flags = fcntl(fds[1], F_GETFL);
  if (flags < 0 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fds[0]); close(fds[1]); return false;
  }
  char block[4096] = {};
  while (write(fds[1], block, sizeof(block)) > 0) {}
  if (errno != EAGAIN || fcntl(fds[1], F_SETFL, flags) < 0) {
    close(fds[0]); close(fds[1]); return false;
  }
  const pid_t child = fork();
  if (child == 0) {
    close(fds[1]);
    usleep(50000);
    const bool read_block = read(fds[0], block, sizeof(block)) == sizeof(block);
    close(fds[0]);
    _exit(read_block ? 0 : 1);
  }
  if (child < 0) { close(fds[0]); close(fds[1]); return false; }
  bool result;
  {
    rgb_matrix::FileStreamIO stream(fds[1]);
    InterruptTimer timer;
    result = stream.Append("x", 1) == 1 && interruptions > 0;
  }
  close(fds[0]);
  return WaitFor(child) && result;
}

static bool FailedMappingPreservesFallback() {
  // As in led-image-viewer, give the mapper its own descriptor. Mapping an
  // empty file fails on POSIX, but must leave the fallback descriptor usable.
  FILE *file = std::tmpfile();
  if (!file) return false;
  const int fallback = Duplicate(file);
  const int mapped = fallback >= 0 ? dup(fallback) : -1;
  bool result = false;
  if (mapped >= 0) {
    rgb_matrix::MemMapViewInput mapping(mapped);
    result = !mapping.IsInitialized() && fcntl(fallback, F_GETFD) >= 0;
  }
  if (fallback >= 0) {
    rgb_matrix::FileStreamIO stream(fallback);
    result = result && stream.Append("x", 1) == 1;
    stream.Rewind();
    char data = 0;
    result = result && stream.Read(&data, 1) == 1 && data == 'x';
  }
  std::fclose(file);
  return result;
}
#endif

static bool Check(bool result, const char *name) {
  if (!result) std::fprintf(stderr, "%s failed\n", name);
  return result;
}

int main() {
  bool passed = Check(FileRoundTrip(), "File round trip");
  passed = Check(MappingRoundTrip(), "Mapping round trip") && passed;
#ifndef _WIN32
  passed = Check(InterruptedRead(), "Interrupted read") && passed;
  passed = Check(InterruptedWrite(), "Interrupted write") && passed;
  passed = Check(FailedMappingPreservesFallback(), "Failed mapping fallback") && passed;
#endif
  return passed ? 0 : 1;
}

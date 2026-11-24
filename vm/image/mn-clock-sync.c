// mn-clock-sync: reads "time <ns>\n" lines from /dev/virtio-ports/mn.clock and steps guest CLOCK_REALTIME forward
// on host wake without ever stepping backward. Exits 0 if the port is absent (older hosts) or on clean EOF.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>

#define MN_CLOCK_PORT "/dev/virtio-ports/mn.clock"
#define NS_PER_SEC    1000000000ULL
#define MAX_JUMP_NS   (315360000ULL * NS_PER_SEC)

static bool parse_time_line(const char *line, uint64_t *out_ns) {
  static const char prefix[] = "time ";
  if (strncmp(line, prefix, sizeof(prefix) - 1) != 0)
    return false;
  const char *p = line + (sizeof(prefix) - 1);
  if (*p < '0' || *p > '9')  // reject leading whitespace, '+'/'-', and empty value
    return false;

  errno = 0;
  char *end = NULL;
  unsigned long long val = strtoull(p, &end, 10);
  if (errno == ERANGE || end == p || *end != '\0' || val == 0)
    return false;
#if ULLONG_MAX > UINT64_MAX
  if (val > UINT64_MAX)
    return false;
#endif
  *out_ns = (uint64_t)val;
  return true;
}

static void maybe_step_clock(uint64_t host_ns) {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
    fprintf(stderr, "mn-clock-sync: clock_gettime failed: %s\n", strerror(errno));
    return;
  }
  if (now.tv_sec < 0 || now.tv_nsec < 0)
    return;
  if ((uint64_t)now.tv_sec > (UINT64_MAX - (uint64_t)now.tv_nsec) / NS_PER_SEC)
    return;

  uint64_t guest_ns = (uint64_t)now.tv_sec * NS_PER_SEC + (uint64_t)now.tv_nsec;
  if (host_ns <= guest_ns)
    return;
  uint64_t diff_ns = host_ns - guest_ns;
  if (diff_ns <= NS_PER_SEC || diff_ns > MAX_JUMP_NS)
    return;

  uint64_t sec = diff_ns / NS_PER_SEC;
  time_t delta_sec = (time_t)sec;
  if (delta_sec < 0 || (uint64_t)delta_sec != sec)
    return;
  long delta_nsec = (long)(diff_ns % NS_PER_SEC);

  struct timex tx;
  memset(&tx, 0, sizeof(tx));
  tx.modes = ADJ_SETOFFSET | ADJ_NANO;
  tx.time.tv_sec = delta_sec;
  tx.time.tv_usec = delta_nsec;
  if (clock_adjtime(CLOCK_REALTIME, &tx) < 0) {
    fprintf(stderr, "mn-clock-sync: clock_adjtime failed: %s\n", strerror(errno));
  } else {
    fprintf(stderr, "mn-clock-sync: stepped CLOCK_REALTIME forward by %lld.%03lds\n",
            (long long)delta_sec, delta_nsec / 1000000L);
  }
}

int main(void) {
  int fd = open(MN_CLOCK_PORT, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    if (errno == ENOENT)
      return 0;
    fprintf(stderr, "mn-clock-sync: open %s failed: %s\n", MN_CLOCK_PORT, strerror(errno));
    return 1;
  }

  char line[256];
  size_t line_len = 0;
  bool discard = false;
  char chunk[256];

  for (;;) {
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "mn-clock-sync: read failed: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
    if (n == 0)
      break;

    for (ssize_t i = 0; i < n; i++) {
      char c = chunk[i];
      if (c == '\n') {
        if (!discard) {
          if (line_len > 0 && line[line_len - 1] == '\r')
            line_len--;
          line[line_len] = '\0';
          uint64_t host_ns = 0;
          if (parse_time_line(line, &host_ns))
            maybe_step_clock(host_ns);
        }
        line_len = 0;
        discard = false;
      } else if (!discard) {
        if (c == '\0' || line_len >= sizeof(line) - 1)
          discard = true;
        else
          line[line_len++] = c;
      }
    }
  }

  close(fd);
  return 0;
}

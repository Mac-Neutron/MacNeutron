#!/bin/sh
# End-to-end MicroVM gamepad (uinput + ff-memless rumble) and raw HID (uhid) test (run with MN_PAD_SELFTEST=1 on
# the host): verifies lockdown=integrity, module.sig_enforce=1, signed uinput & uhid modules, mn-pad.service,
# 70-mn-gamepad.rules permissions for unprivileged user mn, evdev + EVIOCSFF rumble, and hidraw input/output/feature.
# Needs build/vm-work's images, macneutron-vm, and builder (if mn-gamepad-probe needs building).
# Prints "gamepad: ok" or "gamepad: FAIL <rows>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/gamepad.XXXXXX")
trap 'rm -rf "$T"' EXIT

PROBE=$W/share/bin/mn-gamepad-probe
if [ ! -x "$PROBE" ] || [ "$0" -nt "$PROBE" ]; then
  mkdir -p "$W/share/bin"
  cat > "$T/probe.c" <<'EOF'
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static void sleep_ms(unsigned ms) {
  struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
  nanosleep(&ts, NULL);
}

static int open_mn_evdev(uint64_t deadline_ms) {
  while (now_ms() < deadline_ms) {
    DIR *d = opendir("/dev/input");
    if (d) {
      struct dirent *de;
      while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0)
          continue;
        char path[280];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
          continue;
        char phys[128] = {0};
        unsigned long ff_bits[(FF_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
        if (ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) >= 0 &&
            strncmp(phys, "mn.pad", 6) == 0 &&
            ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) >= 0 &&
            (ff_bits[FF_RUMBLE / (8 * sizeof(unsigned long))] &
             (1UL << (FF_RUMBLE % (8 * sizeof(unsigned long))))) != 0) {
          closedir(d);
          printf("evdev-open: ok %s phys=%s\n", path, phys);
          return fd;
        }
        close(fd);
      }
      closedir(d);
    }
    sleep_ms(50);
  }
  return -1;
}

static int open_mn_hidraw(uint64_t deadline_ms) {
  while (now_ms() < deadline_ms) {
    DIR *d = opendir("/dev");
    if (d) {
      struct dirent *de;
      while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "hidraw", 6) != 0)
          continue;
        char path[280];
        snprintf(path, sizeof(path), "/dev/%s", de->d_name);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
          continue;
        char phys[128] = {0};
        if (ioctl(fd, HIDIOCGRAWPHYS(sizeof(phys)), phys) >= 0 &&
            strncmp(phys, "mn.pad", 6) == 0) {
          closedir(d);
          printf("hidraw-open: ok %s phys=%s\n", path, phys);
          return fd;
        }
        close(fd);
      }
      closedir(d);
    }
    sleep_ms(50);
  }
  return -1;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);

  /* Phase 1: uinput + FF_RUMBLE */
  int efd = open_mn_evdev(now_ms() + 20000);
  if (efd < 0) {
    fprintf(stderr, "evdev-open: timeout waiting for mn.pad event device\n");
    return 1;
  }

  bool saw_btn_south = false;
  bool saw_abs_x = false;
  uint64_t deadline = now_ms() + 15000;
  while (now_ms() < deadline && !(saw_btn_south && saw_abs_x)) {
    struct pollfd pfd = { .fd = efd, .events = POLLIN };
    int pr = poll(&pfd, 1, 200);
    if (pr <= 0)
      continue;
    struct input_event evs[32];
    ssize_t n = read(efd, evs, sizeof(evs));
    if (n <= 0)
      continue;
    size_t cnt = (size_t)n / sizeof(struct input_event);
    for (size_t i = 0; i < cnt; i++) {
      if (evs[i].type == EV_KEY && evs[i].code == BTN_SOUTH && evs[i].value != 0)
        saw_btn_south = true;
      if (evs[i].type == EV_ABS && evs[i].code == ABS_X && evs[i].value != 0)
        saw_abs_x = true;
    }
  }
  if (!saw_btn_south || !saw_abs_x) {
    fprintf(stderr, "evdev-input: FAIL btn_south=%d abs_x=%d\n", saw_btn_south, saw_abs_x);
    close(efd);
    return 1;
  }
  printf("evdev-input: ok\n");

  struct ff_effect eff;
  memset(&eff, 0, sizeof(eff));
  eff.type = FF_RUMBLE;
  eff.id = -1;
  eff.u.rumble.strong_magnitude = 49152;
  eff.u.rumble.weak_magnitude = 16384;
  eff.replay.length = 500;
  eff.replay.delay = 0;
  if (ioctl(efd, EVIOCSFF, &eff) < 0) {
    perror("EVIOCSFF");
    close(efd);
    return 1;
  }
  struct input_event play = {0};
  play.type = EV_FF;
  play.code = (uint16_t)eff.id;
  play.value = 1;
  if (write(efd, &play, sizeof(play)) != sizeof(play)) {
    perror("write EV_FF");
    close(efd);
    return 1;
  }
  sleep_ms(700);
  printf("evdev-rumble: ok\n");
  close(efd);

  /* Phase 2: uhid raw HID */
  int hfd = open_mn_hidraw(now_ms() + 20000);
  if (hfd < 0) {
    fprintf(stderr, "hidraw-open: timeout waiting for mn.pad hidraw device\n");
    return 1;
  }

  uint8_t in_buf[256];
  ssize_t in_len = -1;
  deadline = now_ms() + 15000;
  while (now_ms() < deadline) {
    struct pollfd pfd = { .fd = hfd, .events = POLLIN };
    if (poll(&pfd, 1, 200) <= 0)
      continue;
    in_len = read(hfd, in_buf, sizeof(in_buf));
    if (in_len > 0)
      break;
  }
  if (in_len <= 0) {
    fprintf(stderr, "hidraw-input: FAIL no input report received\n");
    close(hfd);
    return 1;
  }
  printf("hidraw-input: ok");
  for (ssize_t i = 0; i < in_len; i++)
    printf(" %02x", in_buf[i]);
  printf("\n");

  uint8_t out_rep[3] = { 0x01, 0xaa, 0x55 };
  if (write(hfd, out_rep, sizeof(out_rep)) != sizeof(out_rep)) {
    perror("hidraw write");
    close(hfd);
    return 1;
  }
  printf("hidraw-output: ok\n");

  uint8_t feat[5];
  memset(feat, 0, sizeof(feat));
  feat[0] = 0x01;
  int flen = ioctl(hfd, HIDIOCGFEATURE(sizeof(feat)), feat);
  if (flen != (int)sizeof(feat)) {
    perror("HIDIOCGFEATURE");
    close(hfd);
    return 1;
  }
  printf("hidraw-feature: ok");
  for (int i = 0; i < flen; i++)
    printf(" %02x", feat[i]);
  printf("\n");

  uint8_t sfeat[5] = { 0x01, 0x12, 0x34, 0x56, 0x78 };
  int sflen = ioctl(hfd, HIDIOCSFEATURE(sizeof(sfeat)), sfeat);
  if (sflen != (int)sizeof(sfeat)) {
    perror("HIDIOCSFEATURE");
    close(hfd);
    return 1;
  }
  printf("hidraw-sfeature: ok\n");

  close(hfd);
  return 0;
}
EOF
  "$ROOT/vm/builder.sh" ssh 'cat > /tmp/mn-gamepad-probe.c && gcc -O2 -Wall -Wextra -Werror -o /tmp/mn-gamepad-probe /tmp/mn-gamepad-probe.c && cat /tmp/mn-gamepad-probe' \
    < "$T/probe.c" > "$PROBE.tmp"
  mv "$PROBE.tmp" "$PROBE"
  chmod +x "$PROBE"
fi

cat > "$T/guest.sh" <<'EOF'
set -eu
echo "user=$(id -un)"
echo "lockdown=$(cat /sys/kernel/security/lockdown)"
echo "sig_enforce=$(cat /sys/module/module/parameters/sig_enforce)"
lsmod | grep -E '^(uinput|uhid)\b' || true
ls -l /dev/uinput /dev/uhid /dev/virtio-ports/mn.pad
svc_state=""
i=0
while [ "$i" -lt 25 ]; do
  for p in /proc/[0-9]*/comm; do
    if [ -r "$p" ] && [ "$(cat "$p" 2>/dev/null || true)" = "mn-pad" ]; then
      svc_state="active"
      break
    fi
  done
  [ "$svc_state" = "active" ] && break
  sleep 0.2
  i=$((i + 1))
done
echo "service=$svc_state"
/mnt/share/bin/mn-gamepad-probe
EOF

MN_PAD_SELFTEST=1 MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 \
  sh "$ROOT/vm/run-guest.sh" "$T/guest.sh" > "$T/out.txt" 2>&1 || true
cat "$T/out.txt"

fail=
/usr/bin/grep -qx 'user=mn' "$T/out.txt" || fail="$fail user"
/usr/bin/grep -q '^lockdown=.*\[integrity\]' "$T/out.txt" || fail="$fail lockdown"
/usr/bin/grep -qx 'sig_enforce=Y' "$T/out.txt" || fail="$fail sig_enforce"
/usr/bin/grep -q '^uinput ' "$T/out.txt" || fail="$fail mod-uinput"
/usr/bin/grep -q '^uhid ' "$T/out.txt" || fail="$fail mod-uhid"
/usr/bin/grep -q '/dev/uinput' "$T/out.txt" || fail="$fail dev-uinput"
/usr/bin/grep -q '/dev/uhid' "$T/out.txt" || fail="$fail dev-uhid"
/usr/bin/grep -q '/dev/virtio-ports/mn.pad' "$T/out.txt" || fail="$fail dev-port"
/usr/bin/grep -qx 'service=active' "$T/out.txt" || fail="$fail service"
/usr/bin/grep -qx 'evdev-input: ok' "$T/out.txt" || fail="$fail evdev-input"
/usr/bin/grep -qx 'evdev-rumble: ok' "$T/out.txt" || fail="$fail evdev-rumble"
/usr/bin/grep -q '^mn-pad-selftest: rumble 49152 16384$' "$T/out.txt" || fail="$fail host-rumble-on"
awk '/^mn-pad-selftest: rumble 49152 16384$/ { on = 1 } on && /^mn-pad-selftest: rumble 0 0$/ { off = 1 } END { exit !off }' \
  "$T/out.txt" || fail="$fail host-rumble-off"
/usr/bin/grep -q '^hidraw-input: ok ' "$T/out.txt" || fail="$fail hidraw-input"
/usr/bin/grep -qx 'hidraw-output: ok' "$T/out.txt" || fail="$fail hidraw-output"
/usr/bin/grep -qx 'mn-pad-selftest: hid-output output 01aa55' "$T/out.txt" || fail="$fail host-hid-output"
/usr/bin/grep -qx 'hidraw-feature: ok 01 ca fe 12 34' "$T/out.txt" || fail="$fail hidraw-feature"
/usr/bin/grep -qx 'hidraw-sfeature: ok' "$T/out.txt" || fail="$fail hidraw-sfeature"
/usr/bin/grep -q '^mn-pad-selftest: hid-set [0-9][0-9]* feature 0112345678$' "$T/out.txt" || fail="$fail host-hid-set"

if [ -z "$fail" ]; then echo "gamepad: ok"; else echo "gamepad: FAIL$fail"; exit 1; fi

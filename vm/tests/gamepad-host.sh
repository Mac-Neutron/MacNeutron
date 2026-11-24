#!/bin/sh
# Builds and runs the MacNeutron host gamepad unit & mock-pipe test suite under ASan + UBSan.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/vm-work/obj
mkdir -p "$OUT"

if ! xcrun --find clang >/dev/null 2>&1; then
  if [ -d /Applications/Xcode.app/Contents/Developer ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  elif [ -d /Library/Developer/CommandLineTools ]; then
    export DEVELOPER_DIR=/Library/Developer/CommandLineTools
  fi
fi

CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"

clang $CFLAGS -std=gnu2x -c -I"$ROOT/vm/host" -o "$OUT/gamepad-test.c.o" "$ROOT/vm/host/gamepad-test.c"
clang $CFLAGS -c -I"$ROOT/vm/host" -o "$OUT/gamepad-test-gamepad.m.o" "$ROOT/vm/host/gamepad.m"
clang $CFLAGS -c -I"$ROOT/vm/host" -o "$OUT/gamepad-test-rumble.m.o" "$ROOT/vm/host/gamepad_rumble.m"
clang $CFLAGS -c -I"$ROOT/vm/host" -o "$OUT/gamepad-test-hid.m.o" "$ROOT/vm/host/gamepad_hid.m"

clang $CFLAGS \
  -o "$OUT/gamepad-test" \
  "$OUT/gamepad-test.c.o" \
  "$OUT/gamepad-test-gamepad.m.o" \
  "$OUT/gamepad-test-rumble.m.o" \
  "$OUT/gamepad-test-hid.m.o" \
  -framework Foundation \
  -framework GameController \
  -framework CoreHaptics \
  -framework IOKit \
  -framework CoreFoundation

"$OUT/gamepad-test"

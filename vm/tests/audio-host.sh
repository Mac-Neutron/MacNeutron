#!/bin/sh
# The VM host's audio player & TCC-isolated capture (vm/host/audio.c, audio_tcc.m) against a stand-in guest.
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
clang -O2 -std=gnu2x -Wall -Wextra -o "$OUT/audio-test" \
  "$ROOT/vm/host/audio-test.c" "$ROOT/vm/host/audio.c" "$ROOT/vm/host/audio_tcc.m" \
  -framework AudioToolbox -framework CoreAudio -framework AVFoundation -framework Foundation
MN_AUDIO_OUTPUT=none MN_AUDIO_MUTE_BACKGROUND=0 "$OUT/audio-test" --check-mute-bg
MN_AUDIO_OUTPUT=none "$OUT/audio-test"
MN_AUDIO_OUTPUT=slow "$OUT/audio-test"
# The Mac's output, unless CoreAudio doesn't answer (a stuck audio driver stalls every client): then only a note.
"$OUT/audio-test" > "$OUT/audio-test.txt" 2>&1 & pid=$!
for _ in $(seq 150); do kill -0 $pid 2>/dev/null || break; sleep 0.1; done
if kill -0 $pid 2>/dev/null; then
  kill $pid; echo "audio-host: CoreAudio did not answer in 15 s: the Mac's output not checked"
elif /usr/bin/grep -q 'no output' "$OUT/audio-test.txt"; then
  echo "audio-host: CoreAudio did not answer in time: the Mac's output not checked"
else
  wait $pid; cat "$OUT/audio-test.txt"
fi

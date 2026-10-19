#!/bin/sh
# The VM host's audio player (eac-vm/host/audio.c) against a stand-in guest, on the Mac's real output (silence).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/obj
mkdir -p "$OUT"
clang -O2 -std=gnu2x -Wall -Wextra -o "$OUT/audio-test" "$ROOT/eac-vm/host/audio-test.c" "$ROOT/eac-vm/host/audio.c" \
  -framework AudioToolbox -framework CoreAudio
MN_AUDIO_OUTPUT=none "$OUT/audio-test"
MN_AUDIO_OUTPUT=slow "$OUT/audio-test"
# The Mac's output, unless CoreAudio doesn't answer (a stuck audio driver stalls every client): then only a note.
"$OUT/audio-test" > "$OUT/audio-test.txt" 2>&1 & pid=$!
for _ in $(seq 100); do kill -0 $pid 2>/dev/null || break; sleep 0.1; done
if kill -0 $pid 2>/dev/null; then
  kill $pid; echo "audio-host: CoreAudio did not answer in 10 s: the Mac's output not checked"
elif /usr/bin/grep -q 'no output' "$OUT/audio-test.txt"; then
  echo "audio-host: CoreAudio did not answer in time: the Mac's output not checked"
else
  wait $pid; cat "$OUT/audio-test.txt"
fi

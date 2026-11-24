# The game's sound path in the guest, against the VM host's player/capture (vm/host/audio.c; MN_AUDIO_OUTPUT=none
# on the host keeps the Mac quiet): ALSA's default device (the mn plugin, vm/guest/alsa-mn.c) under FEX with
# blocking reads/writes (alsa-play / alsa-cap), then Windows programs (waveout.exe / wavein.exe: winmm, mmdevapi,
# winealsa). Playback plays two seconds and must take about that: paced by the host, neither run ahead nor stalled.
# Capture is tested when MN_AUDIO_INPUT=sine (non-zero PCM in 0.85..1.35 s alongside concurrent playback) or
# MN_AUDIO_INPUT=hang (3 repeated alsa-cap failures in < 1 s, playback unaffected). When run directly on macOS,
# drives both MN_AUDIO_INPUT=sine and MN_AUDIO_INPUT=hang via vm/run-guest.sh; when catted into a guest script (as
# vm/tests/vm-setup.sh does), runs inside the guest. Prints "audio: ok" or "audio: FAIL <rows>".
if [ "$(uname -s)" = Darwin ]; then
  set -eu
  ROOT=$(cd "$(dirname "$0")/../.." && pwd)
  T=$(mktemp -d "${TMPDIR:-/tmp}/audio.XXXXXX")
  trap 'rm -rf "$T"' EXIT
  for mode in sine hang; do
    {
      printf 'MN_AUDIO_INPUT=%s\n' "$mode"
      cat "$0"
    } > "$T/guest-$mode.sh"
    echo "=== audio.sh (MN_AUDIO_INPUT=$mode) ==="
    MN_AUDIO_OUTPUT=${MN_AUDIO_OUTPUT:-none} MN_AUDIO_INPUT=$mode MN_RUN_TIMEOUT=${MN_RUN_TIMEOUT:-600} \
      sh "$ROOT/vm/run-guest.sh" "$T/guest-$mode.sh" 2>&1 | tr -d '\r' | tee "$T/$mode.txt"
    /usr/bin/grep -qx 'audio: ok' "$T/$mode.txt"
  done
  exit 0
fi

export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt
B=/mnt/share/bin
Xvfb :0 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
"$B/alsa-play" 2 2>&1 | tee /tmp/alsa.txt
# The same from an i686 process (old WoW64's 32-bit Unix side), with the i686 libasound wine.erofs carries, on an image
# with that side (one without the library then fails these rows).
if [ -d /opt/mn-wine/lib/wine/i386-unix ]; then
  LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 "$B/alsa-play32" 2 2>&1 | tee /tmp/alsa32.txt
  rows32=alsa32
fi
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
wine "$B/waveout.exe" 2>&1 | tr -d '\r' | tee /tmp/waveout.txt
# A 32-bit program's: winealsa in the 32-bit Unix side (old WoW64), when the image has it.
if [ -d /opt/mn-wine/lib/wine/i386-unix ]; then
  LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 wine "$B/waveout32.exe" 2>&1 | tr -d '\r' | tee /tmp/waveout32.txt
  rows32="$rows32 waveout32"
fi

# took <file> [verb]: the "<verb> in <s> s" seconds, in hundredths without leading zeros (the image has no awk)
took() {
  v=$(tr -d '\r' < "$1" | sed -n "s/.* ${2:-played} in \([0-9]*\)\.\([0-9][0-9]\).*/\1\2/p" | head -n 1)
  v=${v#0}; v=${v#0}
  echo "${v:-0}"
}
uptime_cs() {
  read u _ < /proc/uptime
  v="${u%.*}${u#*.}"
  v=${v#0}; v=${v#0}
  echo "${v:-0}"
}

fail=
for row in alsa waveout ${rows32:-}; do
  t=$(took /tmp/$row.txt played)
  { [ "$t" -ge 190 ] && [ "$t" -le 260 ]; } || fail="$fail $row"
done

case "${MN_AUDIO_INPUT:-}" in
  sine)
    "$B/alsa-play" 3 > /tmp/cap-conc-play.txt 2>&1 &
    conc_pid=$!
    if "$B/alsa-cap" 1 48000 2 f32 > /tmp/cap-alsa.txt 2>&1; then
      cat /tmp/cap-alsa.txt
      t=$(took /tmp/cap-alsa.txt captured)
      { [ "$t" -ge 85 ] && [ "$t" -le 135 ]; } || fail="$fail cap-alsa-pace"
    else
      cat /tmp/cap-alsa.txt
      fail="$fail cap-alsa"
    fi
    if [ -d /opt/mn-wine/lib/wine/i386-unix ]; then
      if LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 "$B/alsa-cap32" 1 16000 1 s16 > /tmp/cap-alsa32.txt 2>&1; then
        cat /tmp/cap-alsa32.txt
        t=$(took /tmp/cap-alsa32.txt captured)
        { [ "$t" -ge 85 ] && [ "$t" -le 135 ]; } || fail="$fail cap-alsa32-pace"
      else
        cat /tmp/cap-alsa32.txt
        fail="$fail cap-alsa32"
      fi
    fi
    if wait $conc_pid; then
      cat /tmp/cap-conc-play.txt
      t=$(took /tmp/cap-conc-play.txt played)
      { [ "$t" -ge 280 ] && [ "$t" -le 360 ]; } || fail="$fail cap-conc-play-pace"
    else
      cat /tmp/cap-conc-play.txt
      fail="$fail cap-conc-play"
    fi

    "$B/alsa-play" 3 > /tmp/cap-conc-play-win.txt 2>&1 &
    conc_win_pid=$!
    if wine "$B/wavein.exe" > /tmp/cap-wavein.txt 2>&1; then
      tr -d '\r' < /tmp/cap-wavein.txt
      t=$(took /tmp/cap-wavein.txt captured)
      { [ "$t" -ge 85 ] && [ "$t" -le 135 ]; } || fail="$fail cap-wavein-pace"
    else
      tr -d '\r' < /tmp/cap-wavein.txt
      fail="$fail cap-wavein"
    fi
    if wait $conc_win_pid; then
      cat /tmp/cap-conc-play-win.txt
      t=$(took /tmp/cap-conc-play-win.txt played)
      { [ "$t" -ge 280 ] && [ "$t" -le 360 ]; } || fail="$fail cap-conc-play-win-pace"
    else
      cat /tmp/cap-conc-play-win.txt
      fail="$fail cap-conc-play-win"
    fi
    if [ -d /opt/mn-wine/lib/wine/i386-unix ]; then
      if [ ! -f "$B/wavein32.exe" ]; then
        fail="$fail cap-wavein32-missing"
      elif LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 wine "$B/wavein32.exe" > /tmp/cap-wavein32.txt 2>&1; then
        tr -d '\r' < /tmp/cap-wavein32.txt
        t=$(took /tmp/cap-wavein32.txt captured)
        { [ "$t" -ge 85 ] && [ "$t" -le 135 ]; } || fail="$fail cap-wavein32-pace"
      else
        tr -d '\r' < /tmp/cap-wavein32.txt
        fail="$fail cap-wavein32"
      fi
    fi
    ;;
  hang)
    "$B/alsa-play" 2 > /tmp/hang-conc-play.txt 2>&1 &
    conc_pid=$!
    for i in 1 2 3; do
      t0=$(uptime_cs)
      if "$B/alsa-cap" 1 > /tmp/hang-cap-$i.txt 2>&1; then
        rc=0
      else
        rc=$?
      fi
      t1=$(uptime_cs)
      dt=$((t1 - t0))
      cat /tmp/hang-cap-$i.txt
      { [ "$rc" -ne 0 ] && [ "$dt" -lt 100 ] && grep -qi 'Input/output error' /tmp/hang-cap-$i.txt; } \
        || fail="$fail hang-cap-$i(${dt}cs,rc=$rc)"
    done
    if wait $conc_pid; then
      cat /tmp/hang-conc-play.txt
      t=$(took /tmp/hang-conc-play.txt played)
      { [ "$t" -ge 190 ] && [ "$t" -le 260 ]; } || fail="$fail hang-conc-play-pace"
    else
      cat /tmp/hang-conc-play.txt
      fail="$fail hang-conc-play"
    fi
    if "$B/alsa-play" 2 > /tmp/hang-after-play.txt 2>&1; then
      cat /tmp/hang-after-play.txt
      t=$(took /tmp/hang-after-play.txt played)
      { [ "$t" -ge 190 ] && [ "$t" -le 260 ]; } || fail="$fail hang-after-play-pace"
    else
      cat /tmp/hang-after-play.txt
      fail="$fail hang-after-play"
    fi
    ;;
esac

timeout 20 wineserver -k
if [ -z "$fail" ]; then echo "audio: ok"; else echo "audio: FAIL$fail"; exit 1; fi

# The game's sound path in the guest, against the VM host's player (eac-vm/host/audio.c; MN_AUDIO_OUTPUT=none on the
# host keeps the Mac quiet): ALSA's default device (the mn plugin, eac-vm/guest/alsa-mn.c) under FEX with blocking
# writes (alsa-play), then a Windows program's (waveout.exe: winmm, mmdevapi, winealsa). Each plays two seconds and
# must take about that: paced by the host, neither run ahead (nothing pacing it) nor stalled. Programs from
# eac-vm/guest/build-audio.sh; alsa-play32 too, from an i686 process, on an image with the 32-bit Unix side. Prints
# "audio: ok" or "audio: FAIL <rows>". The plugin paces by its own clock when no host takes the stream, so the host's
# "mn-audio: stream N: ... frames played" lines (two streams or more, none refused) say it was served:
# tests/vm-setup.sh checks them.
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt
B=/mnt/share/bin
Xvfb :0 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
"$B/alsa-play" 2 2>&1 | tee /tmp/alsa.txt
# The same from an i686 process (old WoW64's 32-bit Unix side), with the i686 libasound wine.erofs carries, when it does.
if [ -d /opt/mn-wine/lib/mn-i386 ]; then
  LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 "$B/alsa-play32" 2 2>&1 | tee /tmp/alsa32.txt
  rows32=alsa32
fi
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
wine "$B/waveout.exe" 2>&1 | tr -d '\r' | tee /tmp/waveout.txt
# A 32-bit program's: winealsa in the 32-bit Unix side (old WoW64), when the image has it.
if [ -d /opt/mn-wine/lib/mn-i386 ]; then
  LD_LIBRARY_PATH=/opt/mn-wine/lib/mn-i386 wine "$B/waveout32.exe" 2>&1 | tr -d '\r' | tee /tmp/waveout32.txt
  rows32="$rows32 waveout32"
fi
timeout 20 wineserver -k
# took <file>: the "played in <s> s" seconds, in hundredths (the image has no awk)
took() { sed -n 's/.* played in \([0-9]*\)\.\([0-9][0-9]\).*/\1\2/p' "$1"; }
fail=
for row in alsa waveout ${rows32:-}; do
  t=$(took /tmp/$row.txt)
  { [ -n "$t" ] && [ "$t" -ge 190 ] && [ "$t" -le 260 ]; } || fail="$fail $row"
done
if [ -z "$fail" ]; then echo "audio: ok"; else echo "audio: FAIL$fail"; exit 1; fi

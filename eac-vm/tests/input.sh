# The keyboard and mouse, end to end (run with MN_INPUT_SELFTEST=1 on the host, eac-vm/host/input.c): the host's
# sequence goes through virtio-input and mn-input into Xvfb, where mn-input-check's window sees it. Prints the events
# and "input: ok" or "input: FAIL <what's missing>".
export DISPLAY=:0
B=/mnt/share/bin
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
sleep 2
ls -l /dev/input/ | grep event
"$B/mn-input" :0 > /tmp/mn-input.log 2>&1 &
"$B/mn-input-check" > /tmp/check.txt 2>&1
cat /tmp/mn-input.log /tmp/check.txt
fail=
grep -q '^motion 959 539$' /tmp/check.txt || fail="$fail abs-motion"
grep -q '^key press 38$' /tmp/check.txt || fail="$fail key-a"
grep -q '^key release 38$' /tmp/check.txt || fail="$fail key-a-release"
grep -q '^button press 1$' /tmp/check.txt || fail="$fail button-1"
grep -q '^motion 969 544$' /tmp/check.txt || fail="$fail rel-motion"
grep -q '^button press 4$' /tmp/check.txt || fail="$fail wheel"
if [ -z "$fail" ]; then echo "input: ok"; else echo "input: FAIL$fail"; exit 1; fi

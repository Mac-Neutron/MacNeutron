"""perf.py's parsers (eac-vm/tests/perf-compare.sh): python3 -I eac-vm/tests/perf_test.py"""
import argparse
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf  # noqa: E402

TOP_BATCH = ("top - 10:00:00 up 1 min\n\n"
             "    PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND\n"
             "   41 mn        20   0  100000  50000  1000 R  90.0   1.0   0:10.00 RHI Thread\n"
             "   42 mn        20   0  100000  50000  1000 R  40.0   1.0   0:10.00 dxmt-encode-thr\n"
             "   43 mn        20   0  100000  50000  1000 S  20.0   1.0   0:10.00 RHI Thread\n")

# sample(1)'s call graph: a vCPU thread 60 of 100 samples in the guest, 40 parked; an executor consumer spinning.
SAMPLE = """Call graph:
    100 Thread_1: fc_vcpu 0
    + 100 thread_start  (in libsystem_pthread.dylib) + 8  [0x1]
    +   100 _pthread_start  (in libsystem_pthread.dylib) + 136  [0x2]
    +     60 hv_trap  (in Hypervisor) + 8  [0x3]
    +     40 __psynch_cvwait  (in libsystem_kernel.dylib) + 8  [0x4]
    100 Thread_2
    + 100 thread_start  (in libsystem_pthread.dylib) + 8  [0x1]
    +   100 consume  (in macneutron-vm) + 152  [0x5]
    +     100 mach_absolute_time  (in libsystem_kernel.dylib) + 108  [0x6]

Total number in stack (recursive counted multiple, when >=5):
"""


class Perf(unittest.TestCase):
    def test_parse_time_formats(self):
        for text, s in [("0:01.23", 1.23), ("12:34.56", 754.56), ("1:02:03.45", 3723.45), ("2-01:02:03", 176523.0),
                        ("419:41.14", 25181.14)]:
            self.assertAlmostEqual(perf.parse_time(text), s)

    def test_cpu_delta_new_and_gone(self):
        before = "  10   0:05.00 /x/wine.app/Contents/MacOS/wine\n  11   0:01.00 /x/wine.app/Contents/MacOS/wine\n"
        after = "  10   0:15.00 /x/wine.app/Contents/MacOS/wine\n  12   0:02.00 /x/wine.app/bin/wineserver\n"
        self.assertEqual(perf.cpu_delta(before, after, lambda c: "/wine.app/" in c), (12.0, 1))

    def test_macos_game_processes(self):
        # Wine names a game's process by its Windows path; wineserver and the loader run from the tool's wine.app.
        self.assertTrue(perf.macos_game(r"S:\steamapps\common\SMITE 2\Windows\Hemingway-Win64-Shipping.exe"))
        self.assertTrue(perf.macos_game("/x/macneutron/wine.app/Contents/Resources/bin/wineserver"))
        self.assertFalse(perf.macos_game("/System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer"))

    def test_gpu_busy_mean(self):
        self.assertAlmostEqual(perf.gpu_busy(['"Device Utilization %"=90', '"Device Utilization %"=100']), 95.0)

    def test_fps_skips_exit(self):
        stats = ("# 10:00:05 5.0 s, 600 frames (120.0 fps), every 5 s\n  x\n"
                 "# 10:00:10 5.0 s, 500 frames (100.0 fps), every 5 s\n# 10:00:12 2.0 s, 10 frames (5.0 fps), exit\n")
        self.assertAlmostEqual(perf.fps(stats), 110.0)
        self.assertAlmostEqual(perf.fps(stats[stats.index("# 10:00:10"):]), 100.0)
        self.assertIsNone(perf.fps(""))

    def test_fps_weighs_windows_by_length(self):
        # A short window counts for its length: 10 frames in 1 s and 600 in 5 s are 610 frames in 6 s, not the mean
        # of 10 and 120 fps. present.m names its own period ("every 1 s").
        stats = ("# 10:00:01 1.0 s, 10 frames (10.0 fps), every 1 s\n"
                 "# 10:00:06 5.0 s, 600 frames (120.0 fps), every 5 s\n")
        self.assertAlmostEqual(perf.fps(stats), 610 / 6)

    def test_fps_ignores_lines_that_are_not_windows(self):
        self.assertIsNone(perf.fps("# 10:00:05 0.0 s, 0 frames (0.0 fps), every 5 s\n# nonsense\n"))

    def test_read_slices_bytes_not_characters(self):
        # The offsets are file sizes in bytes: a multi-byte character before them must not shift the slice.
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "f")
            with open(path, "wb") as f:
                f.write("caf\u00e9\n".encode() + b"# window\nlater\n")
            start = len("caf\u00e9\n".encode())
            self.assertEqual(perf._read(path, start, start + len("# window\n")), "# window\n")
            self.assertEqual(perf._read(path, start), "# window\nlater\n")
            self.assertEqual(perf._read(os.path.join(d, "none")), "")

    def test_threads_names_with_spaces(self):
        got = perf.threads(TOP_BATCH + TOP_BATCH.replace("90.0", "50.0"))
        self.assertEqual(got[0], ("RHI Thread", 90.0))
        self.assertEqual(got[1], ("dxmt-encode-thr", 40.0))
        self.assertEqual(perf.threads("no batch here"), [])

    def test_compare_says_how_many_processes_ended(self):
        mac = {"cpu_cores": 4.0, "fps": 100.0, "processes_ended": 2}
        vm = {"cpu_cores": 5.0, "fps": 100.0, "gpu_busy": 97.0, "processes_ended": 0}
        out, _ = self._compare(mac, vm)
        self.assertIn("2 macOS processes ended mid-window", out)
        self.assertNotIn("VM processes ended", out)

    def test_compare_fails_the_gate(self):
        good = {"cpu_cores": 4.0, "fps": 100.0, "gpu_busy": 97.0}
        for what, vm in [("gpu", dict(good, gpu_busy=90.0)), ("fps", dict(good, fps=80.0)),
                         ("cpu", dict(good, cpu_cores=7.0)), ("no fps", dict(good, fps=None))]:
            out, status = self._compare(good, vm)
            self.assertEqual(status, 1, what)
            self.assertIn("FAIL", out, what)
        self.assertEqual(self._compare(good, good)[1], 0)

    def _compare(self, mac, vm):
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for name, data in (("mac", mac), ("vm", vm)):
                paths.append(os.path.join(d, name + ".json"))
                with open(paths[-1], "w") as f:
                    json.dump(data, f)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = perf.compare(argparse.Namespace(macos=paths[0], vm=paths[1]))
        return out.getvalue(), code

    def test_sample_split(self):
        split = perf.sample_split(SAMPLE)
        self.assertAlmostEqual(split["guest"], 0.6)
        self.assertAlmostEqual(split["spin"], 1.0)
        self.assertAlmostEqual(split["wake"], 0.0)

    def test_sample_split_leaves_waits_out(self):
        # A consumer half parked in a timed wait, and an idle workqueue thread (sample(1) shows start_wqthread).
        waits = SAMPLE.replace("Total number in stack", """    100 Thread_3
    + 100 thread_start  (in libsystem_pthread.dylib) + 8  [0x1]
    +   100 consume  (in macneutron-vm) + 152  [0x5]
    +     50 semaphore_timedwait_trap  (in libsystem_kernel.dylib) + 8  [0x7]
    +     50 mach_absolute_time  (in libsystem_kernel.dylib) + 108  [0x6]
    100 Thread_4
    + 100 start_wqthread  (in libsystem_pthread.dylib) + 0  [0x8]

Total number in stack""")
        split = perf.sample_split(waits)
        self.assertAlmostEqual(split["spin"], 1.5)
        self.assertAlmostEqual(split["other"], 0.0)

    def test_sample_split_queue_entries_count_against_the_window(self):
        # sample(1) lists a thread again for each dispatch queue it served, with that queue's few samples: one
        # sample there is 1/100 of a core here, not a whole one.
        queue = SAMPLE.replace("Total number in stack", """    1 Thread_5   DispatchQueue_7: com.apple.root.default-qos  (concurrent)
    + 1 _dispatch_lane_serial_drain  (in libdispatch.dylib) + 900  [0x9]

Total number in stack""")
        self.assertAlmostEqual(perf.sample_split(queue)["other"], 0.01)

    def test_scale_to_measured_cpu(self):
        # sample(1) sees a spinning thread busy even while it waits for a core: its split is shares, scaled to ps.
        self.assertEqual(perf.scale({"guest": 1.0, "spin": 1.0}, 0.5), {"guest": 0.25, "spin": 0.25})
        self.assertEqual(perf.scale({"guest": 0.0}, 0.5), {"guest": 0.0})


if __name__ == "__main__":
    unittest.main()

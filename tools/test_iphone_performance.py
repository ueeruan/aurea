import tempfile
import unittest
from pathlib import Path
from analyze_iphone_performance import summarize, read_report


class IPhoneReportTests(unittest.TestCase):
    def test_unavailable_gpu_is_not_reported_as_zero_time(self):
        out = summarize([{"event": "sample", "phase": "play", "playing": True,
                          "perf": {"previewFps": 24, "gpuTimers": 0, "gpuFrameMs": 0, "ramBytes": 500}}])
        phase = out["phases"]["play"]
        self.assertIsNone(phase["sampledMaxGPUms"])
        self.assertIsNone(phase["maxProcessFootprintBytes"])
        self.assertEqual(phase["meanSampledPreviewFpsWhilePlaying"], 24)

    def test_paused_samples_do_not_reduce_playback_fps(self):
        rows = [{"event": "sample", "playing": playing, "perf": {"previewFps": fps}} for playing, fps in [(True, 30), (False, 0), (True, 20)]]
        self.assertEqual(summarize(rows)["phases"]["unknown"]["meanSampledPreviewFpsWhilePlaying"], 25)

    def test_interrupted_last_line_keeps_previous_samples(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "partial.jsonl"
            path.write_text('{"event":"session_start","schema":1}\n{"event":"memory_warning"}\n{"event":', encoding="utf-8")
            report = read_report(path)
        self.assertFalse(report["complete"])
        self.assertEqual(report["invalidLines"], [3])
        self.assertEqual(report["events"][0]["event"], "memory_warning")


if __name__ == "__main__":
    unittest.main()

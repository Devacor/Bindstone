import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "run_comparison.py"
spec = importlib.util.spec_from_file_location("demoreel_comparison", path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class DemoreelComparisonTests(unittest.TestCase):
	def sample(self, frames=60, width=100, at="3.00", number=10, name="OSSUARY", ms="123.456"):
		return (f"jai_demoreel --perf: {frames} frames per scene @ {width}x40 from scene t={at}, seed 20260705, workers 1\n\n"
			f"vm           | {number} | {name:<12} | {ms:>9} |     34.0 | 0123456789abcdef\n"
			"vm           | mean 123.456 ms/frame over 1 scenes\n")

	def test_row_is_bound_to_the_workload(self):
		row = runner.parse_row(self.sample(), 9, 60, 3.0)
		self.assertEqual((row["backend"], row["hash"], row["ms_per_frame"]), ("vm", "0123456789abcdef", 123.456))
		self.assertEqual(runner.parse_row(self.sample(frames=4, at="3.67"), 9, 4, runner.slice_at(20))["hash"], "0123456789abcdef")
		for output in (self.sample(frames=30), self.sample(width=80), self.sample(at="4.00"), self.sample(number=11),
				self.sample(name="MAELSTROM"), self.sample() + self.sample(), self.sample(ms="0.000")):
			with self.assertRaises(ValueError):
				runner.parse_row(output, 9, 60, 3.0)

	def test_workload_names_the_window(self):
		self.assertEqual(runner.workload(6, 60, 30, 3.0), ["--perf", "60", "--scene", "6", "--w", "100", "--h", "40",
			"--perf-at", "3.0", "--perf-warmup", "30"])
		self.assertNotIn("--perf-warmup", runner.workload(6, 60, 0, 3.0))

	def test_summary_takes_timed_runs_and_samples_slices(self):
		configs = [{"key": "lua", "label": "Lua 5.4"}, {"key": "chai", "label": "ChaiScript 6.1", "sampled": True}]
		rows = [
			{"configuration": "lua", "scene": 9, "phase": "timed", "ms_per_frame": 10.0, "at": 3.0},
			{"configuration": "lua", "scene": 9, "phase": "timed", "ms_per_frame": 12.0, "at": 3.0},
			{"configuration": "lua", "scene": 9, "phase": "timed", "ms_per_frame": 11.0, "at": 3.0},
			{"configuration": "lua", "scene": 9, "phase": "gate", "ms_per_frame": 1000.0, "at": 3.0},
			{"configuration": "chai", "scene": 9, "phase": "timed", "ms_per_frame": 100.0, "at": 3.0},
			{"configuration": "chai", "scene": 9, "phase": "timed", "ms_per_frame": 120.0, "at": 3.0},
			{"configuration": "chai", "scene": 9, "phase": "timed", "ms_per_frame": 300.0, "at": 3.5},
			{"configuration": "chai", "scene": 9, "phase": "timed", "ms_per_frame": 200.0, "at": 3.5}]
		lua, chai = runner.summarize(configs, rows, [9])
		self.assertEqual((lua["ms_per_frame"], lua["runs"], lua["range_ms"]), (11.0, 3, [10.0, 12.0]))
		self.assertEqual(chai["slice_medians_ms"], [110.0, 250.0])
		self.assertEqual(chai["ms_per_frame"], 180.0)
		self.assertEqual(chai["projected_window_ms"], 180.0 * runner.FRAMES)
		self.assertEqual(chai["sampled_frames"], 2 * runner.SLICE_FRAMES)


if __name__ == "__main__":
	unittest.main()

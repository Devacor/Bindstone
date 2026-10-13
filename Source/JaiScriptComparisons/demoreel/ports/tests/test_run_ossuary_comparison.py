import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "run_ossuary_comparison.py"
spec = importlib.util.spec_from_file_location("ossuary_comparison", path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class OssuaryComparisonTests(unittest.TestCase):
	def sample(self, frames=5, width=100, name="OSSUARY"):
		return (f"jai_demoreel --perf: {frames} frames per scene @ {width}x40 from scene t=3.00, seed 20260705, workers 1\n"
			f"vm | 16 | {name} | 123.456 | 90.2 | 0123456789abcdef\n")

	def test_scene_row_is_bound_to_the_workload(self):
		self.assertEqual(runner.parse_scene(self.sample(), 5)["hash"], "0123456789abcdef")
		for output in (self.sample(frames=30), self.sample(width=80), self.sample(name="OTHER"), self.sample()+self.sample(), self.sample().replace("123.456", "0.000")):
			with self.assertRaises(ValueError):
				runner.parse_scene(output, 5)

	def test_summary_excludes_gates_and_contaminated_samples(self):
		config = {"key":"candidate"}
		rows = [
			{"configuration":"candidate","phase":"timed","accepted":True,"layout":"normal","ms_per_frame":10},
			{"configuration":"candidate","phase":"timed","accepted":True,"layout":"normal","ms_per_frame":12},
			{"configuration":"candidate","phase":"timed","accepted":True,"layout":"padding","ms_per_frame":11},
			{"configuration":"candidate","phase":"timed","accepted":True,"layout":"padding","ms_per_frame":12},
			{"configuration":"candidate","phase":"gate","accepted":True,"layout":"normal","ms_per_frame":1000},
			{"configuration":"candidate","phase":"timed","accepted":False,"layout":"normal","ms_per_frame":2000}]
		result = runner.summarize(config, rows)
		self.assertEqual(result["normal_median_ms"], 11)
		self.assertEqual(result["runs_per_layout"], 2)
		self.assertAlmostEqual(result["padding_paired_band_percent"][0], 0)
		self.assertAlmostEqual(result["padding_paired_band_percent"][1], 10)

if __name__ == "__main__":
	unittest.main()

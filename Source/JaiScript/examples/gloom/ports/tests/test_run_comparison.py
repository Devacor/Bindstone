"""Checkpoint recovery must preserve input identity, screening and ABAB order."""
import copy
import importlib.util
import pathlib
import tempfile
import unittest

MODULE=pathlib.Path(__file__).resolve().parents[1]/"run_comparison.py"
spec=importlib.util.spec_from_file_location("run_comparison",MODULE)
comparison=importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class ResumeTests(unittest.TestCase):
	def setUp(self):
		self.temp=tempfile.TemporaryDirectory()
		self.addCleanup(self.temp.cleanup)
		self.out=pathlib.Path(self.temp.name)
		self.config={"name":"fixture Lua","workload":"gloom","target":"gloom_lua","cores":1}
		self.builds={"baseline":self.out/"build"}
		self.meta={"rounds":3,"performance_cpus":[0],"cpu_sets":[{"logical_cpu":0,"core":0}],
			"max_background_cores":1.0,"binary_sha256":{"fixture.exe":"unchanged"},
			"workload_sha256":{"fixture.lua":"unchanged"}}
		self.records=[]
		for layout,ticks,warm in (("normal",300,0),("normal",600,600),("padding",300,0),("padding",600,600)):
			self.add(layout,"gate",ticks,warm,0,0.5)
		self.add("normal","timed",600,600,1,0.5)
		self.add("padding","timed",600,600,1,1.5)

	def add(self,layout,phase,ticks,warm,number,background):
		_,frame,state=next(h for h in comparison.HASHES if h[0]==ticks)
		filename=f"{len(self.records)+1}.log"
		(self.out/filename).write_text(f"lua | 600.000 | 1.000000 | {frame} | {state}\n")
		self.records.append({"sequence":len(self.records)+1,"configuration":"fixture Lua",
			"workload":"gloom","variant":"baseline","layout":layout,"phase":phase,
			"round":number,"command":["fixture.exe","--ticks",str(ticks),"--warmup",str(warm)],
			"affinity_cpus":[0],"log":filename,"hash_match":True,"metric_ms":1.0,
			"exit_code":0,"background_core_equivalents":background,
			"competitors_at_end":[],"competitors_during_run":[],"accepted":background<=1})

	def validate(self,records=None,current=None):
		return comparison.validate_resume(self.meta,current or self.meta,
			self.records if records is None else records,[self.config],self.builds,self.out)

	def test_resumes_after_accepted_prefix_and_keeps_rejected_attempt(self):
		valid,completed=self.validate()
		self.assertEqual([self.config],valid)
		self.assertEqual(1,completed)
		self.assertEqual(("padding",1),comparison.timing_plan(valid,self.builds,3)[completed][2:])
		self.assertEqual(6,len(self.records))

	def test_changed_inputs_or_screening_require_new_session(self):
		for key,value in (("binary_sha256",{"fixture.exe":"changed"}),
			("workload_sha256",{"fixture.lua":"changed"}),("max_background_cores",2.0),
			("rounds",4),("performance_cpus",[1])):
			with self.subTest(key=key):
				current=copy.deepcopy(self.meta);current[key]=value
				with self.assertRaisesRegex(RuntimeError,"changed "+key):
					self.validate(current=current)

	def test_retry_budget_can_change_without_weakening_screening(self):
		current=copy.deepcopy(self.meta);current["max_attempts"]=60
		self.assertEqual(1,self.validate(current=current)[1])

	def test_rejected_sample_cannot_be_relabelled_accepted(self):
		records=copy.deepcopy(self.records);records[-1]["accepted"]=True
		with self.assertRaisesRegex(RuntimeError,"screening verdict"):
			self.validate(records=records)

	def test_recovery_cannot_reorder_a_layout(self):
		records=copy.deepcopy(self.records);records[-1]["layout"]="normal"
		with self.assertRaisesRegex(RuntimeError,"ABAB prefix"):
			self.validate(records=records)

	def test_recorded_metric_must_match_raw_log(self):
		records=copy.deepcopy(self.records);records[-2]["metric_ms"]=0.5
		with self.assertRaisesRegex(RuntimeError,"disagrees with log"):
			self.validate(records=records)

	def test_complete_conformance_is_required(self):
		records=copy.deepcopy(self.records)
		records.pop(0)
		for i,row in enumerate(records,1):row["sequence"]=i
		with self.assertRaisesRegex(RuntimeError,"complete conformance"):
			self.validate(records=records)

	def test_affinity_must_match_recorded_topology(self):
		records=copy.deepcopy(self.records);records[-2]["affinity_cpus"]=[1]
		with self.assertRaisesRegex(RuntimeError,"recorded affinity"):
			self.validate(records=records)

	def test_raw_logs_cannot_escape_the_session_directory(self):
		records=copy.deepcopy(self.records);records[-2]["log"]="../outside.log"
		with self.assertRaises(ValueError):
			self.validate(records=records)


if __name__=="__main__":
	unittest.main()

"""Checkpoint recovery must preserve input identity, screening and ABAB order."""
import copy
import itertools
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

	def test_candidate_retry_keeps_the_cross_variant_prefix_aligned(self):
		self.builds["candidate"]=self.out/"candidate"
		self.records=[]
		for variant in self.builds:
			for layout,ticks,warm in (("normal",300,0),("normal",600,600),("padding",300,0),("padding",600,600)):
				self.add(layout,"gate",ticks,warm,0,0.5)
				self.records[-1]["variant"]=variant
		for variant,background in (("baseline",0.5),("candidate",1.5),("candidate",0.5)):
			self.add("normal","timed",600,600,1,background)
			self.records[-1]["variant"]=variant
		valid,completed=self.validate()
		self.assertEqual(2,completed)
		_,variant,layout,number=comparison.timing_plan(valid,self.builds,3)[completed]
		self.assertEqual(("baseline","padding",1),(variant,layout,number))

	def test_raw_logs_cannot_escape_the_session_directory(self):
		records=copy.deepcopy(self.records);records[-2]["log"]="../outside.log"
		with self.assertRaises(ValueError):
			self.validate(records=records)



class MultiVariantTests(unittest.TestCase):
	def test_every_pair_keeps_the_required_interleaving(self):
		config={"name":"fixture","workload":"gloom","target":"jai_gloom","cores":1}
		builds={name:pathlib.Path(name) for name in ("baseline","raw","fused","both")}
		plan=comparison.timing_plan([config],builds,3)
		self.assertEqual(48,len(plan))
		for before,after in itertools.combinations(builds,2):
			projected=[(variant,layout,number) for _,variant,layout,number in plan if variant in (before,after)]
			expected=[]
			for number in range(1,4):
				expected.extend([(before,"normal",number),(after,"normal",number),
					(before,"padding",number),(after,"padding",number)]*2)
			self.assertEqual(expected,projected)

	def test_named_variants_cannot_replace_or_escape_a_label(self):
		self.assertEqual(["baseline","candidate","move"],list(comparison.variant_builds("a","b",["move=c"])))
		for extra in (["baseline=b"],["candidate=b"],["raw=b","raw=c"],["../raw=b"],["raw="],["no_equal"]):
			with self.subTest(extra=extra),self.assertRaises(ValueError):
				comparison.variant_builds("a","b",extra)

	def test_each_mechanism_gets_its_own_delta_and_busy_rows_are_excluded(self):
		config={"name":"fixture","unit":"ms/tick"}
		builds={"baseline":None,"candidate":None,"move":None}
		records=[]
		for layout in ("normal","padding"):
			for variant,metric in (("baseline",100),("candidate",80),("move",60)):
				for index in range(6):
					records.append({"configuration":"fixture","variant":variant,"layout":layout,
						"phase":"timed","accepted":True,"metric_ms":metric*(1.02 if layout=="padding" else 1)})
		records.append({**records[-1],"accepted":False,"metric_ms":99999})
		rows=comparison.difference_rows([config],builds,records)
		self.assertEqual(6,len(rows))
		for row in rows:
			want={("baseline","candidate"):-20,("baseline","move"):-40,("candidate","move"):-25}[
				(row["baseline_variant"],row["candidate_variant"])]
			self.assertAlmostEqual(want,row["median_delta_percent"])
			for value in row["paired_delta_band_percent"]:self.assertAlmostEqual(want,value)

	def test_missing_accepted_side_is_rejected(self):
		config={"name":"fixture","unit":"ms/tick"}
		rows=[{"configuration":"fixture","variant":variant,"layout":"normal","phase":"timed",
			"accepted":True,"metric_ms":value} for variant,value in (("baseline",100),("candidate",80),("baseline",100))]
		with self.assertRaisesRegex(RuntimeError,"Incomplete paired series"):
			comparison.difference_rows([config],{"baseline":None,"candidate":None},rows)

	def test_legacy_difference_schema_is_preserved(self):
		config={"name":"fixture","unit":"ms/tick"}
		records=[{"configuration":"fixture","variant":variant,"layout":"normal",
			"phase":"timed","accepted":True,"metric_ms":metric} for variant,metric in (("baseline",100),("candidate",80))]
		row=comparison.difference_rows([config],{"baseline":None,"candidate":None},records)[0]
		self.assertNotIn("baseline_variant",row)
		self.assertAlmostEqual(-20,row["median_delta_percent"])

if __name__=="__main__":
	unittest.main()

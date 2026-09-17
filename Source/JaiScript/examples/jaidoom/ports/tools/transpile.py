#!/usr/bin/env python3
"""Generate the reviewed Lua port from JaiScript AST exports (never text substitution).

The generator is workload-scoped and rejects unsupported syntax. Generated modules
are ordinary Lua tables/functions/coroutines; no per-element proxy interpreter.
"""
import argparse, json, pathlib, re, hashlib
U="?"
def arr(t=U):return ("a",t)
def mp(t=U,fields=None):return ("m",t,tuple(sorted((fields or {}).items())))
def cls(n):return ("c",n)
def kind(t):return t[0] if isinstance(t,tuple) else t
def element(t):return t[1] if kind(t) in ("a","m") else U
def mix(a,b):
	if a==b:return a
	if "any" in (a,b):return "any"
	if a in (U,"null"):return b
	if b in (U,"null"):return a
	ka,kb=kind(a),kind(b)
	if ka==kb=="a":return arr(mix(a[1],b[1]))
	if ka==kb=="m":
		f=dict(a[2])
		for k,v in b[2]:f[k]=mix(f.get(k,U),v)
		return mp(mix(a[1],b[1]),f)
	if a in ("int","float","number","char") and b in ("int","float","number","char"):return "number" if "float" in (a,b) or "number" in (a,b) else "int"
	return "any"
def declared(s):
	s=(s or "any").rstrip("&")
	if s in ("any","auto"):return U
	if s.startswith("array<"):return arr(declared(s[6:-1]))
	if s.startswith("map<"):return mp()
	if s.startswith("shared_ptr<"):return cls(s[11:-1])
	if s in ("int","float","string","bool","char","void"):return s
	return cls(s)
RESERVED=set("and break do else elseif end false for function goto if in local nil not or repeat return then true until while".split())
def name(s):return s+"_jd" if s in RESERVED else s
def quote(s):
	# Lua decimal byte escapes preserve JaiScript's UTF-8 byte strings.
	return '"'+''.join(('\\%03d'%c) if c<32 or c>=127 else '\\"' if c==34 else '\\\\' if c==92 else chr(c) for c in s.encode("utf-8"))+'"'
def walk(n):
	if isinstance(n,dict):
		yield n
		for x in n.values():yield from walk(x)
	elif isinstance(n,list):
		for x in n:yield from walk(x)

class Port:
	def __init__(self,modules):
		self.modules=modules;self.globals={"ARGS":arr("string"),"PI":"float","TAU":"float","E":"float"}
		self.functions={};self.fields={};self.params={};self.alias={};self.env={};self.owner=None
		self.fixed={};self.changes=0
		for mod,nodes in modules.items():
			for n in nodes:
				if n["kind"]=="class":
					self.fields[n["name"]]={m["name"]:declared(m["type"]) for m in n["members"] if m["kind"]=="var"}
					for m in n["members"]:
						if m["kind"]=="function":self.functions[n["name"]+"."+m["name"]]=m
				elif n["kind"]=="function":self.functions[n["name"]]=n
				elif n["kind"]=="var":
					t=declared(n["type"]);self.globals[n["name"]]=t
					if t!=U:self.fixed[n["name"]]=t
		for key,n in self.functions.items():self.params[key]=[declared(p["type"]) for p in n["params"]]
		for unused in range(24):
			self.changes=0
			for nodes in modules.values():
				self.env={};self.owner=None
				for n in nodes:
					if n["kind"]=="var":self.scan(n,True)
			for key,n in self.functions.items():
				self.owner=key.split(".")[0] if "." in key else None
				self.env={p["name"]:t for p,t in zip(n["params"],self.params[key])};self.alias={}
				self.scan(n["body"])
			if not self.changes:break
		self.rounds=unused+1
	def get(self,n):
		if n in self.env:return self.env[n]
		if self.owner and n in self.fields[self.owner]:return self.fields[self.owner][n]
		return self.globals.get(n,U)
	def settype(self,n,t,global_decl=False):
		if global_decl or (n not in self.env and n in self.globals):
			if n in self.fixed:return
			old=self.globals.get(n,U);new=mix(old,t)
			if new!=old:self.globals[n]=new;self.changes+=1
		else:self.env[n]=mix(self.env.get(n,U),t)
	def refine(self,n,t):
		if not n:return
		if n["kind"]=="id":
			self.settype(n["name"],t)
			if n["name"] in self.alias:self.refine(self.alias[n["name"]],t)
		elif n["kind"]=="binary" and n["op"]=="[":
			parent=n["left"];pt=self.infer(parent)
			self.refine(parent,mp(t,dict(pt[2])) if kind(pt)=="m" else arr(t))
	def field(self,t,n):
		if kind(t)=="c":return self.fields.get(t[1],{}).get(n,U)
		if kind(t)=="m":return dict(t[2]).get(n,t[1])
		return U
	def infer(self,n):
		if not n:return "null"
		k=n["kind"]
		if k=="literal":return {0:"null",1:"int",2:"float",3:"string",4:"char",5:"bool"}[n["tag"]]
		if k=="id":return self.get(n["name"])
		if k=="this":return cls(self.owner)
		if k=="new":return declared(n["type"])
		if k=="cast":return declared(n["type"])
		if k=="array":
			t=U
			for v in n["values"]:t=mix(t,self.infer(v))
			return arr(t)
		if k=="map":
			fs={};t=U
			for key,v in n["entries"]:
				vt=self.infer(v);t=mix(t,vt)
				if key["kind"]=="literal" and key["tag"]==3:fs[key["value"]]=vt
			return mp(t,fs)
		if k=="member":return self.field(self.infer(n["object"]),n["name"])
		if k=="binary":
			a,b=self.infer(n["left"]),self.infer(n["right"]);op=n["op"]
			if op=="[":
				if a=="string":return "char"
				if kind(a)=="m" and n["right"]["kind"]=="literal":return dict(a[2]).get(n["right"]["value"],a[1])
				return element(a)
			if op in ["==","!=","<",">","<=",">=","&&","||"]:return "bool"
			if op in ["&","|","^","<<",">>"]:return "int"
			if op=="+" and "string" in (a,b):return "string"
			if a=="float" or b=="float":return "float"
			if a in ("int","char") and b in ("int","char"):return "int"
			return "number"
		if k=="unary":return "bool" if n["op"]=="!" else self.infer(n["value"])
		if k=="ternary":return mix(self.infer(n["then"]),self.infer(n["else"]))
		if k=="assign":return self.infer(n["value"])
		if k=="call":
			c=n["callee"];args=n["args"]
			if c["kind"]=="id":
				fn=c["name"]
				if fn=="parallel_transform" and len(args)==2 and args[1].get("kind")=="id" and args[1]["name"] in self.functions:
					return arr(declared(self.functions[args[1]["name"]]["return"]))
				if fn in self.functions:
					d=self.functions[fn]
					return "co" if d["coroutine"] else declared(d["return"])
				return {"to_char":"char","to_string":"string","format_value":"string","read_file":"string","clock_ms":"int","file_exists":"bool","dir_exists":"bool","writable_dir":"bool","from_json":mp(),"to_json":"string","parent_dir":"string","file_name":"string","bin_dir":"string","root_script_dir":"string","app_data_dir":"string","parallel_transform":arr("string")}.get(fn,U)
			if c["kind"]=="member":
				fn=c["name"];t=self.infer(c["object"])
				if c.get("static") and c["object"].get("name")=="math":
					if fn in ("itrunc","ifloor","iceil"):return "int"
					if fn=="random":return "int" if len(args)==2 and all(self.infer(x) in ("int","char") for x in args) else "float"
					return "float"
				if fn in ("size","find","rfind","find_last_of","to_int"):return "int"
				if fn in ("has","starts_with","ends_with","done","tic"):return "bool"
				if fn in ("substr","join","to_lower","to_upper","view_frame"):return "string"
				if fn=="get":return element(t)
				key=t[1]+"."+fn if kind(t)=="c" else ""
				if key in self.functions:return declared(self.functions[key]["return"])
				return "void"
		return U
	def scan(self,n,global_decl=False):
		if not n:return
		k=n["kind"]
		if k=="var":
			self.scan(n["value"])
			t=declared(n["type"]);v=self.infer(n["value"])
			if t==U:t=v
			if global_decl:self.settype(n["name"],t,True)
			else:self.env[n["name"]]=t
			if (n["type"] or "").endswith("&") and n["value"]:self.alias[n["name"]]=n["value"]
			return
		if k=="call":
			for x in n["args"]:self.scan(x)
			c=n["callee"]
			if c["kind"]=="member" and c["name"]=="push" and n["args"]:self.refine(c["object"],arr(self.infer(n["args"][0])))
			if c["kind"]=="id" and c["name"] in self.functions:
				key=c["name"];fn=self.functions[key]
				for i,(p,v) in enumerate(zip(fn["params"],n["args"])):
					if declared(p["type"])==U:
						old=self.params[key][i];new=mix(old,self.infer(v))
						if new!=old:self.params[key][i]=new;self.changes+=1
			return
		if k=="assign":
			self.scan(n["value"])
			self.refine(n["target"],self.infer(n["value"]));return
		if k=="range":
			self.scan(n["container"]);self.env[n["name"]]=element(self.infer(n["container"]));self.scan(n["body"]);return
		for field,v in n.items():
			if field in ("kind","line"):continue
			if isinstance(v,dict):self.scan(v)
			elif isinstance(v,list):
				for x in v:
					if isinstance(x,dict):self.scan(x)
	def ident(self,n):
		if n not in self.env and self.owner and n in self.fields[self.owner]:return "self."+name(n)
		return name(n)
	def truth(self,n):
		x=self.expr(n);t=self.infer(n)
		if t=="bool":return x
		if t in ("int","float","number","char"):return "("+x+" ~= 0)"
		if t=="string":return "("+x+' ~= "")'
		if kind(t) in ("a","m","c","co"):return "("+x+" ~= NULL)"
		return "_j_truth("+x+")"
	def string(self,n):
		x=self.expr(n);t=self.infer(n)
		if t=="string":return x
		if t=="char":return "string.char("+x+")"
		return "_j_str("+x+")"
	def convert(self,x,source,target):
		if target=="int" and source not in ("int","char"):return "_j_int("+x+")"
		if target=="float" and source not in ("float",):return "("+x+" + 0.0)"
		if target=="bool" and source!="bool":return "_j_truth("+x+")"
		return x
	def expr(self,n):
		if not n:return "NULL"
		k=n["kind"]
		if k=="literal":
			v=n["value"];tag=n["tag"]
			if tag==0:return "NULL"
			if tag==3:return quote(v)
			if tag==5:return "true" if v else "false"
			if tag==2:return repr(float(v))
			return str(v)
		if k=="id":return self.ident(n["name"])
		if k=="this":return "self"
		if k=="array":return "_j_array({"+", ".join(self.expr(x) for x in n["values"])+"})"
		if k=="map":return "{"+", ".join("["+self.expr(k)+"]="+self.expr(v) for k,v in n["entries"])+"}"
		if k=="member":return self.expr(n["object"])+"."+name(n["name"])
		if k=="new":
			t=declared(n["type"]);return name(t[1])+".new("+", ".join(self.expr(x) for x in n["args"])+")"
		if k=="cast":return self.convert(self.expr(n["value"]),self.infer(n["value"]),declared(n["type"]))
		if k=="binary":
			a,b=n["left"],n["right"];op=n["op"]
			at,bt=self.infer(a),self.infer(b)
			x,y=self.expr(a),self.expr(b)
			if op=="[":
				if at=="string":return "string.byte("+x+", "+y+" + 1)"
				if kind(at)=="m":return x+"["+y+"]"
				if kind(at)=="a":return x+"["+y+" + 1]"
				return "_j_at("+x+", "+y+")"
			if op in ("&&","||"):return "("+self.truth(a)+(" and " if op=="&&" else " or ")+self.truth(b)+")"
			if op=="+" and "string" in (at,bt):return "("+self.string(a)+" .. "+self.string(b)+")"
			if op=="/":
				if at in ("int","char") and bt in ("int","char"):return "_j_idiv("+x+", "+y+")"
				if at=="float" or bt=="float":return "("+x+" / "+y+")"
				return "_j_div("+x+", "+y+")"
			if op=="%":return "math.fmod("+x+", "+y+")"
			if op==">>":
				if b["kind"]=="literal" and b["tag"]==1 and 0<=b["value"]<63:
					return "("+x+" // "+str(1<<b["value"])+")"
				return "_j_sar("+x+", "+y+")"
			if op=="<<":return "("+x+" << ("+y+" & 63))"
			op={"!=":"~=","^":"~"}.get(op,op)
			if op=="+" and (at in (U,"any") or bt in (U,"any")):return "_j_add("+x+", "+y+")"
			return "("+x+" "+op+" "+y+")"
		if k=="unary":
			op=n["op"];v=n["value"]
			if op=="!":return "(not "+self.truth(v)+")"
			if op=="~":return "(~"+self.expr(v)+")"
			if op in ("++","--"):raise ValueError("value-used inc/dec needs explicit translation")
			return "("+op+self.expr(v)+")"
		if k=="ternary":
			c=self.truth(n["condition"]);a=self.expr(n["then"]);b=self.expr(n["else"])
			if self.infer(n["then"]) in ("bool",U,"any"):
				return "(function() if "+c+" then return "+a+" else return "+b+" end end)()"
			return "("+c+" and "+a+" or "+b+")"
		if k=="call":
			c=n["callee"];args=n["args"];xs=[self.expr(x) for x in args]
			if c["kind"]=="id":
				fn=c["name"]
				if fn=="to_char":return "("+xs[0]+" & 255)"
				if fn=="to_string":return self.string(args[0])
				if fn in self.functions:
					for i,(p,v) in enumerate(zip(self.functions[fn]["params"],args)):
						xs[i]=self.convert(xs[i],self.infer(v),declared(p["type"]))
				return self.expr(c)+"("+", ".join(xs)+")"
			if c["kind"]=="member":
				obj=c["object"];x=self.expr(obj);m=c["name"]
				if c.get("static") and obj.get("name")=="math":
					fn={"itrunc":"_j_int","ifloor":"_j_ifloor","random":"_j_random","random_seed":"_j_random_seed","atan2":"math.atan"}.get(m,"math."+m)
					return fn+"("+", ".join(xs)+")"
				if m=="size":return "(#"+x+")"
				if m=="has":return "("+x+"["+xs[0]+"] ~= nil)"
				if m=="substr":return "_j_substr("+", ".join([x]+xs)+")"
				if m in ("starts_with","ends_with","find","rfind","find_last_of","to_int","get"):
					return "_j_"+m+"("+", ".join([x]+xs)+")"
				if m=="to_lower":return "string.lower("+x+")"
				if m=="to_upper":return "string.upper("+x+")"
				if m=="join":return "table.concat("+", ".join([x]+xs)+")"
				if m=="sort":return "table.sort("+x+")"
				if m=="remove":return "table.remove("+x+", "+xs[0]+" + 1)"
				if m=="push":return "table.insert("+x+", "+xs[0]+")"
				return x+":"+name(m)+"("+", ".join(xs)+")"
		if k=="yield":return "coroutine.yield("+("" if n["value"] is None else self.expr(n["value"]))+")"
		if k=="throw":return "error("+self.expr(n["value"])+")"
		raise ValueError("unsupported expression "+k+" line"+str(n.get("line")))
	def emit(self,s):self.lines.append("\t"*self.indent+s)
	def assign(self,n):
		target=n["target"];value=n["value"];op=n["op"]
		x=self.expr(target);v=self.expr(value)
		if op!="=":
			binop={"kind":"binary","left":target,"right":value,"op":op[:-1]}
			v=self.expr(binop)
		t=self.infer(target)
		# Declared primitive slots, including typed buffers, keep their boundary.
		if target["kind"]=="id":
			d=self.decltypes.get(target["name"],self.globaldecl.get(target["name"],U))
			v=self.convert(v,self.infer(value),d)
		elif target["kind"]=="binary" and target["op"]=="[":
			if kind(self.infer(target["left"]))=="a":v=self.convert(v,self.infer(value),t)
			if x.startswith("_j_at("):
				self.emit("_j_set("+self.expr(target["left"])+", "+self.expr(target["right"])+", "+v+")");return
		self.emit(x+" = "+v)
	def numeric_for(self,n):
		init=n.get("init");cond=n.get("condition");update=n.get("update")
		if not init or init["kind"]!="var" or declared(init["type"])!="int" or not cond or cond["kind"]!="binary":return None
		var=init["name"];left=cond["left"];bound=cond["right"];op=cond["op"]
		if left.get("kind")!="id" or left["name"]!=var or op not in ("<","<=",">",">="):return None
		if bound["kind"]=="id":
			if bound["name"] not in self.env:return None
		elif bound["kind"]!="literal":return None
		step=None
		if update and update["kind"]=="unary" and update["value"].get("name")==var:
			step={"++":1,"--":-1}.get(update["op"])
		elif update and update["kind"]=="assign" and update["target"].get("name")==var and update["value"]["kind"]=="literal":
			if update["op"] in ("+=","-="):step=update["value"]["value"]*(1 if update["op"]=="+=" else -1)
		if step is None or step==0 or (step>0)!=(op in ("<","<=")):return None
		watched={var}
		if bound["kind"]=="id":watched.add(bound["name"])
		for item in walk(n["body"]):
			if item.get("kind")=="assign" and item["target"].get("kind")=="id" and item["target"]["name"] in watched:return None
			if item.get("kind")=="unary" and item.get("op") in ("++","--") and item["value"].get("name") in watched:return None
			if item.get("kind")=="call" and item["callee"].get("kind")=="id":
				fn=self.functions.get(item["callee"]["name"])
				if fn:
					for param,arg in zip(fn["params"],item["args"]):
						if param["ref"] and arg.get("name") in watched:return None
		start=self.convert(self.expr(init["value"]),self.infer(init["value"]),"int")
		end=self.expr(bound)
		if op=="<":end="("+end+" - 1)"
		if op==">":end="("+end+" + 1)"
		return var,start,end,step
	def stmt(self,n,top=False):
		if not n:return
		k=n["kind"]
		if k=="statement":self.stmt(n["value"],top);return
		if k=="block":
			for v in n["body"]:self.stmt(v)
			return
		if k=="extern":return
		if k=="import":self.emit("require("+quote(n["path"].removesuffix(".jai"))+")");return
		if k=="var":
			t=declared(n["type"]);v=n["value"]
			if t==U:t=self.infer(v)
			if not top:self.env[n["name"]]=t;self.decltypes[n["name"]]=declared(n["type"])
			init=self.expr(v) if v else ("0" if t in ("int","float","char") else "false" if t=="bool" else '""' if t=="string" else "NULL")
			init=self.convert(init,self.infer(v),declared(n["type"]))
			self.emit(("" if top else "local ")+name(n["name"])+" = "+init)
			return
		if k=="expression":
			v=n["value"]
			if v["kind"]=="assign":self.assign(v)
			elif v["kind"]=="unary" and v["op"] in ("++","--"):
				x=self.expr(v["value"]);self.emit(x+" = "+x+(" + 1" if v["op"]=="++" else " - 1"))
			elif v["kind"]=="call" and top and v["callee"].get("name")=="main":self.emit("_jd_exit = main()")
			elif v["kind"] in ("call","yield","throw"):self.emit(self.expr(v))
			else:self.emit("do local _jd_unused = "+self.expr(v)+" end")
			return
		if k=="if":
			self.emit("if "+self.truth(n["condition"])+" then");self.indent+=1;self.stmt(n["then"]);self.indent-=1
			if n["else"]:self.emit("else");self.indent+=1;self.stmt(n["else"]);self.indent-=1
			self.emit("end");return
		if k=="for":
			numeric=self.numeric_for(n)
			if numeric:
				var,start,end,step=numeric
				self.serial+=1;label="_jd_continue_"+str(self.serial);self.loops.append(label)
				self.env[var]="int";self.decltypes[var]="int"
				self.emit("for "+name(var)+" = "+start+", "+end+", "+str(step)+" do");self.indent+=1
				self.emit("do");self.indent+=1;self.stmt(n["body"]);self.indent-=1;self.emit("end")
				self.emit("::"+label+"::");self.indent-=1;self.emit("end");self.loops.pop();return
		if k in ("while","for","range"):
			self.serial+=1;label="_jd_continue_"+str(self.serial);self.loops.append(label)
			self.emit("do");self.indent+=1
			if k=="for":self.stmt(n["init"])
			if k=="range":
				rname="_jd_range_"+str(self.serial);idx="_jd_index_"+str(self.serial)
				self.emit("local "+rname+" = "+self.expr(n["container"]));self.emit("local "+idx+" = 1")
				self.emit("while "+idx+" <= #"+rname+" do");self.indent+=1
				self.env[n["name"]]=element(self.infer(n["container"]));self.decltypes[n["name"]]=declared(n["type"])
				self.emit("do");self.indent+=1;self.emit("local "+name(n["name"])+" = "+rname+"["+idx+"]")
			else:
				self.emit("while "+(self.truth(n["condition"]) if n.get("condition") else "true")+" do")
				self.indent+=1;self.emit("do");self.indent+=1
			self.stmt(n["body"]);self.indent-=1;self.emit("end");self.emit("::"+label+"::")
			if k=="for" and n["update"]:self.stmt({"kind":"expression","value":n["update"]})
			if k=="range":self.emit(idx+" = "+idx+" + 1")
			self.indent-=1;self.emit("end");self.indent-=1;self.emit("end");self.loops.pop();return
		if k=="continue":self.emit("goto "+self.loops[-1]);return
		if k=="break":self.emit("break");return
		if k=="return":
			if n["value"]:self.emit("return "+self.convert(self.expr(n["value"]),self.infer(n["value"]),self.ret))
			else:self.emit("return")
			return
		if k=="function":self.function(n);return
		if k=="class":self.classdef(n);return
		raise ValueError("unsupported statement "+k+" at "+str(n.get("line")))
	def function(self,n,owner=None):
		override=pathlib.Path(__file__).parent/"lua_overrides"/(n["name"]+".lua")
		if owner is None and override.exists():
			for line in override.read_text(encoding="utf-8").splitlines():self.emit(line)
			return
		saved=(self.env,self.owner,self.decltypes,self.ret)
		key=(owner+"." if owner else "")+n["name"];self.owner=owner
		self.env={p["name"]:t for p,t in zip(n["params"],self.params[key])}
		self.decltypes={p["name"]:declared(p["type"]) for p in n["params"]};self.ret=declared(n["return"])
		args=", ".join(name(p["name"]) for p in n["params"])
		ctor=owner and n["name"]==owner
		fn=name(owner)+(".new" if ctor else ":"+name(n["name"])) if owner else name(n["name"])
		self.emit("function "+fn+"("+args+")");self.indent+=1
		if ctor:
			self.emit("local self = setmetatable({}, "+name(owner)+")")
			for field in self.classnodes[owner]["members"]:
				if field["kind"]=="var":self.emit("self."+name(field["name"])+" = "+self.expr(field["value"]))
		if n["coroutine"]:self.emit("return _j_coroutine(function()");self.indent+=1
		self.stmt(n["body"])
		if n["coroutine"]:self.indent-=1;self.emit("end)")
		if ctor:self.emit("return self")
		self.indent-=1;self.emit("end")
		self.env,self.owner,self.decltypes,self.ret=saved
	def classdef(self,n):
		self.emit(name(n["name"])+" = {}");self.emit(name(n["name"])+".__index = "+name(n["name"]))
		for m in n["members"]:
			if m["kind"]=="function":self.function(m,n["name"])
	def generate(self):
		self.globaldecl={n["name"]:declared(n["type"]) for ns in self.modules.values() for n in ns if n["kind"]=="var"}
		self.classnodes={n["name"]:n for ns in self.modules.values() for n in ns if n["kind"]=="class"}
		result={}
		for mod,nodes in self.modules.items():
			self.env={};self.owner=None;self.decltypes={};self.ret=U;self.lines=[];self.indent=0;self.serial=0;self.loops=[]
			self.emit("-- Generated from "+mod+".jai by tools/transpile.py; see PORTING.md.")
			for n in nodes:self.stmt(n,True)
			result[mod]="\n".join(self.lines)+"\n"
		return result

def main():
	p=argparse.ArgumentParser();p.add_argument("--ast",type=pathlib.Path,required=True);p.add_argument("--output",type=pathlib.Path,required=True)
	a=p.parse_args();modules={f.name.removesuffix(".jai.json"):json.loads(f.read_text(encoding="utf-8")) for f in sorted(a.ast.glob("*.jai.json"))}
	port=Port(modules);a.output.mkdir(parents=True,exist_ok=True)
	for mod,text in port.generate().items():(a.output/(mod+".lua")).write_text(text,encoding="utf-8")
	(a.ast.parent/"inferred-types.json").write_text(json.dumps({"rounds":port.rounds,"globals":port.globals,"params":port.params},indent=2)+"\n",encoding="utf-8")
	print("Generated",len(modules),"modules; type propagation rounds",port.rounds)
if __name__=="__main__":main()

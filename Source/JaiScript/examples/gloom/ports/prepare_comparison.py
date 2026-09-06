#!/usr/bin/env python3
"""Fetch the comparison's pinned runtimes under JaiScript/out and patch sol2's version guards."""
import argparse
import hashlib
import json
import pathlib
import tarfile
import urllib.request

ARCHIVES = (
	("lua54", "lua-5.4.8", "https://www.lua.org/ftp/lua-5.4.8.tar.gz",
	 "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae"),
	("lua55", "lua-5.5.1", "https://www.lua.org/ftp/lua-5.5.1.tar.gz",
	 "1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce"),
	("luajit_jit", "LuaJIT-24c20c94e7db195b640854619577441f9b4bc6be",
	 "https://codeload.github.com/LuaJIT/LuaJIT/tar.gz/24c20c94e7db195b640854619577441f9b4bc6be",
	 "178c656b62bb796e536a23f1d3bb09e1824e4dd28eaf2a4758b295ac328e32e6"),
	("sol2", "sol2-3.3.0", "https://codeload.github.com/ThePhD/sol2/tar.gz/refs/tags/v3.3.0",
	 "b82c5de030e18cb2bcbcefcd5f45afd526920c517a96413f0b59b4332d752a1e"),
)

def main():
	root = pathlib.Path(__file__).resolve().parents[3]
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--deps", type=pathlib.Path, default=root / "out/astra/deps")
	args = parser.parse_args()
	deps = args.deps.resolve()
	deps.relative_to((root / "out").resolve())
	deps.mkdir(parents=True, exist_ok=True)
	manifest = {}
	for name, folder, url, expected in ARCHIVES:
		archive = deps / (folder + ".tar.gz")
		if not archive.exists():
			with urllib.request.urlopen(url, timeout=60) as response:
				archive.write_bytes(response.read())
		actual = hashlib.sha256(archive.read_bytes()).hexdigest()
		if actual != expected:
			raise RuntimeError("SHA256 mismatch for " + name)
		destination = deps / folder
		if not destination.exists():
			with tarfile.open(archive) as bundle:
				bundle.extractall(deps, filter="data")
		manifest[name] = {"root": str(destination), "url": url, "sha256": actual}
		print(name + ": verified " + actual)
	# This changes version admission and the removed Lua error-code fallback only.
	# Lua's real 5.5 header and version number remain visible to sol2 and the adapter.
	compat = pathlib.Path(manifest["sol2"]["root"]) / "include/sol/compatibility"
	for filename, old, new in (
		("compat-5.3.h", "LUA_VERSION_NUM > 504", "LUA_VERSION_NUM > 505"),
		("compat-5.4.h", "LUA_VERSION_NUM == 504", "LUA_VERSION_NUM >= 504"),
	):
		path = compat / filename
		text = path.read_text()
		if old in text:
			path.write_text(text.replace(old, new), newline="\n")
		elif new not in text:
			raise RuntimeError("Unexpected sol2 compatibility header: " + filename)
	state_header = compat.parent / "state.hpp"
	text = state_header.read_text()
	old_ctor = ": unique_base(lua_newstate(alfunc, alpointer)), state_view(unique_base::get()) {"
	new_ctor = """#if LUA_VERSION_NUM >= 505
		: unique_base(lua_newstate(alfunc, alpointer, luaL_makeseed(nullptr))), state_view(unique_base::get()) {
#else
		: unique_base(lua_newstate(alfunc, alpointer)), state_view(unique_base::get()) {
#endif"""
	if "#if LUA_VERSION_NUM >= 505" not in text:
		if old_ctor not in text:
			raise RuntimeError("Unexpected sol2 state constructor")
		state_header.write_text(text.replace(old_ctor, new_ctor), newline="\n")
	manifest["sol2"]["patch"] = ["admit LUA_VERSION_NUM 505", "LUA_ERRGCMM fallback for >= 504",
		"custom allocator constructor passes luaL_makeseed(nullptr) for Lua 5.5"]
	(deps / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")

if __name__ == "__main__":
	main()

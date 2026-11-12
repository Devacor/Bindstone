#!/usr/bin/env python3
"""Fetch the comparison's pinned runtimes into out/deps, patch sol2's version guards and ChaiScript, and fill third_party/.

Every archive is checked against its SHA-256 before it is extracted. A third_party/<folder> that already exists is kept.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import stat
import tarfile
import urllib.request
import zipfile

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
	("angelscript", "angelscript-2.38.0", "https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip",
	 "b33b5dbcda10317ef67d628353d83246984ce6fcac102d4dc2aed121eba52e6f"),
	("chaiscript", "ChaiScript-6.1.0", "https://codeload.github.com/ChaiScript/ChaiScript/tar.gz/refs/tags/v6.1.0",
	 "3ca9ba6434b4f0123b5ab56433e3383b01244d9666c85c06cc116d7c41e8f92a"),
	# The Squirrel the ports were measured with: the repository head after 3.2, which still reports "Squirrel 3.2 stable"
	("squirrel", "squirrel-f77074bdd6152d230609146a3d424c6f49e3770f",
	 "https://codeload.github.com/albertodemichelis/squirrel/tar.gz/f77074bdd6152d230609146a3d424c6f49e3770f",
	 "870e0415d37e292c15478b296648150de5e8af91f9c37286bc8e9bb25992056f"),
)
# The AngelScript SDK zip holds everything under sdk/; the CMake root is that folder.
SOURCE_ROOTS = {"angelscript": "sdk"}
# third_party/<folder>, where the comparison build looks by default, and the fetched source root it receives
THIRD_PARTY = (("lua", "lua54"), ("sol2", "sol2"), ("squirrel", "squirrel"), ("chaiscript", "chaiscript"),
	("angelscript", "angelscript"))
CHAISCRIPT_PATCH = pathlib.Path(__file__).resolve().parent / "chaiscript-6.1.0.patch"

def extract_tar(archive, deps, destination):
	with tarfile.open(archive) as bundle:
		# Python 3.11.0 has no extraction filters. Pinned archives contain
		# only directories and regular files, all under their source root.
		for member in bundle.getmembers():
			if not (member.isdir() or member.isfile()):
				raise RuntimeError("Archive link/device refused: " + member.name)
			(deps / member.name).resolve().relative_to(destination.resolve())
		bundle.extractall(deps)

def extract_zip(archive, destination):
	with zipfile.ZipFile(archive) as bundle:
		for member in bundle.infolist():
			if stat.S_ISLNK(member.external_attr >> 16):
				raise RuntimeError("Archive link refused: " + member.filename)
			(destination / member.filename).resolve().relative_to(destination.resolve())
		bundle.extractall(destination)

def read_patch(patch):
	"""{file: [(old lines, new lines) per hunk]} from a unified diff."""
	result = {}
	hunks = None
	for line in patch.read_text(encoding="utf-8").splitlines():
		if line.startswith("--- "):
			continue
		if line.startswith("+++ "):
			hunks = result.setdefault(line[4:].split("/", 1)[1], [])
		elif line.startswith("@@"):
			hunks.append(([], []))
		elif hunks:
			old, new = hunks[-1]
			if not line.startswith("+"):
				old.append(line[1:])
			if not line.startswith("-"):
				new.append(line[1:])
	return result

def apply_patch(patch, root):
	"""Each hunk must already be applied or match exactly once."""
	for name, hunks in read_patch(patch).items():
		path = root / name
		text = "\n".join(path.read_text(encoding="utf-8").splitlines()) + "\n"
		for old, new in hunks:
			before, after = "\n".join(old) + "\n", "\n".join(new) + "\n"
			if text.count(after) == 1:
				continue
			if text.count(before) != 1:
				raise RuntimeError("Unexpected source for " + patch.name + ": " + name)
			text = text.replace(before, after)
		path.write_text(text, encoding="utf-8", newline="\n")

def main():
	root = pathlib.Path(__file__).resolve().parents[2]
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--deps", type=pathlib.Path, default=root / "out/deps")
	parser.add_argument("--third-party", type=pathlib.Path, default=root / "third_party")
	args = parser.parse_args()
	deps = args.deps.resolve()
	deps.relative_to((root / "out").resolve())
	deps.mkdir(parents=True, exist_ok=True)
	manifest = {}
	for name, folder, url, expected in ARCHIVES:
		archive = deps / (folder + (".zip" if url.endswith(".zip") else ".tar.gz"))
		if not archive.exists():
			# Some hosts (angelcode.com) refuse urllib's default User-Agent header.
			request = urllib.request.Request(url, headers={"User-Agent": "prepare_comparison/1"})
			with urllib.request.urlopen(request, timeout=60) as response:
				archive.write_bytes(response.read())
		actual = hashlib.sha256(archive.read_bytes()).hexdigest()
		if actual != expected:
			raise RuntimeError("SHA256 mismatch for " + name)
		destination = deps / folder
		if not destination.exists():
			if archive.suffix == ".zip":
				extract_zip(archive, destination)
			else:
				extract_tar(archive, deps, destination)
		root_dir = destination / SOURCE_ROOTS.get(name, "")
		manifest[name] = {"root": str(root_dir), "url": url, "sha256": actual}
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
	# The ChaiScript the ports run: a null literal (the demoreel port's cue table holds null entries) and a file loader hook
	apply_patch(CHAISCRIPT_PATCH, pathlib.Path(manifest["chaiscript"]["root"]))
	manifest["chaiscript"]["patch"] = [CHAISCRIPT_PATCH.name]
	(deps / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
	for folder, name in THIRD_PARTY:
		destination = args.third_party.resolve() / folder
		if destination.exists():
			print("third_party/" + folder + ": kept")
		else:
			shutil.copytree(manifest[name]["root"], destination)
			print("third_party/" + folder + ": " + pathlib.Path(manifest[name]["root"]).relative_to(deps).as_posix())

if __name__ == "__main__":
	main()

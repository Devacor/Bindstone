#!/usr/bin/env python3
"""Protocol tests for jaiscript_lsp: drives the server over stdio like an editor does.

  python lsp_test.py [--exe PATH] [--update] [--only fixtures|gatorquest|latency] [--verbose]

fixtures    golden responses for every feature on tests/fixtures (--update rewrites golden/fixtures.json)
gatorquest  examples/gatorquest: zero diagnostics on the clean program, definition/references/hover/
            completion/rename spot checks
latency     examples/gatorquest: time from an edit to its published diagnostics
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path
from urllib.parse import unquote

HERE = Path(__file__).resolve().parent
JAISCRIPT = HERE.parents[2]
FIXTURES = HERE / "fixtures"
GOLDEN = HERE / "golden" / "fixtures.json"
GATORQUEST = JAISCRIPT / "examples" / "gatorquest" / "scripts"


def default_exe():
    for config in ("x64-Release BENCHMARKS", "x64-Release", "x64-Debug"):
        candidate = JAISCRIPT / "out" / "build" / config / "bin" / ("jaiscript_lsp.exe" if os.name == "nt" else "jaiscript_lsp")
        if candidate.exists():
            return candidate
    return None


def norm_uri(value):
    """One spelling per file: percent-decoded, lower-case drive (the server answers VS Code's own form)."""
    if not isinstance(value, str) or not value.startswith("file://"):
        return value
    value = unquote(value)
    if len(value) > 9 and value[9] == ":":
        value = value[:8] + value[8].lower() + value[9:]
    return value


def uri_of(path):
    return norm_uri(Path(path).resolve().as_uri())


class Client:
    def __init__(self, exe, root, args=()):
        self.proc = subprocess.Popen([str(exe), *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.next_id = 1
        self.responses = {}
        self.diagnostics = {}
        self.diagnostic_log = []
        self.cv = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.versions = {}
        self.texts = {}
        result = self.request("initialize", {
            "processId": os.getpid(),
            "rootUri": uri_of(root),
            "workspaceFolders": [{"uri": uri_of(root), "name": Path(root).name}],
            "capabilities": {},
            "initializationOptions": {"host": "runner"},
        })
        self.capabilities = result["capabilities"]
        self.notify("initialized", {})

    def _read(self):
        stream = self.proc.stdout
        while True:
            length = None
            while True:
                line = stream.readline()
                if not line:
                    return
                line = line.strip()
                if not line:
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":")[1])
            body = stream.read(length)
            message = json.loads(body.decode("utf-8"))
            with self.cv:
                if "id" in message and "method" not in message:
                    self.responses[message["id"]] = message
                elif message.get("method") == "textDocument/publishDiagnostics":
                    params = message["params"]
                    params["uri"] = norm_uri(params["uri"])
                    self.diagnostics[params["uri"]] = params
                    self.diagnostic_log.append((time.perf_counter(), params))
                self.cv.notify_all()

    def _send(self, message):
        body = json.dumps(message).encode("utf-8")
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()

    def request(self, method, params, timeout=120):
        rid = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        deadline = time.time() + timeout
        with self.cv:
            while rid not in self.responses:
                remaining = deadline - time.time()
                if remaining <= 0:
                    raise TimeoutError(method)
                self.cv.wait(remaining)
            message = self.responses.pop(rid)
        if "error" in message:
            return {"__error__": message["error"]["message"]}
        return message.get("result")

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def open(self, path):
        text = Path(path).read_text(encoding="utf-8")
        uri = uri_of(path)
        self.versions[uri] = 1
        self.texts[uri] = text
        self.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "jaiscript", "version": 1, "text": text}})
        return uri

    def change(self, uri, start, end, text):
        self.versions[uri] += 1
        self.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": self.versions[uri]},
            "contentChanges": [{"range": {"start": start, "end": end}, "text": text}],
        })
        return self.versions[uri]

    def wait_diagnostics(self, uri, version=None, timeout=120):
        deadline = time.time() + timeout
        with self.cv:
            while True:
                params = self.diagnostics.get(uri)
                if params is not None and (version is None or params.get("version") == version):
                    return params
                remaining = deadline - time.time()
                if remaining <= 0:
                    raise TimeoutError("diagnostics for " + uri)
                self.cv.wait(remaining)

    def settle(self, timeout=300):
        deadline = time.time() + timeout
        while time.time() < deadline:
            status = self.request("jaiscript/status", {})
            if status["pending"] == 0:
                return status
            time.sleep(0.05)
        raise TimeoutError("background checks")

    def close(self):
        try:
            self.request("shutdown", None, timeout=10)
            self.notify("exit", None)
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def position_of(text, needle, occurrence=1, delta=0):
    index = -1
    for _ in range(occurrence):
        index = text.index(needle, index + 1)
    index += delta
    line = text.count("\n", 0, index)
    line_start = text.rfind("\n", 0, index) + 1
    character = len(text[line_start:index].encode("utf-16-le")) // 2
    return {"line": line, "character": character}


def relativize(value, root):
    prefix = uri_of(root) + "/"
    if isinstance(value, str):
        return norm_uri(value).replace(prefix, "")
    if isinstance(value, list):
        return [relativize(v, root) for v in value]
    if isinstance(value, dict):
        return {relativize(k, root): relativize(v, root) for k, v in value.items()}
    return value


class Checks:
    def __init__(self, verbose):
        self.failures = []
        self.passed = 0
        self.verbose = verbose

    def check(self, condition, what, detail=""):
        if condition:
            self.passed += 1
            if self.verbose:
                print("  ok   " + what)
        else:
            self.failures.append(what + (": " + detail if detail else ""))
            print("  FAIL " + what + (": " + str(detail)[:400] if detail else ""))


def fixture_words():
    words = set()
    for path in FIXTURES.rglob("*.jai"):
        token = ""
        for ch in path.read_text(encoding="utf-8") + " ":
            if ch.isalnum() or ch == "_":
                token += ch
            else:
                if token:
                    words.add(token)
                token = ""
    return words


def run_fixtures(exe, update, checks):
    print("fixtures")
    client = Client(exe, FIXTURES)
    results = {}
    try:
        main = FIXTURES / "program" / "main.jai"
        lib = FIXTURES / "program" / "lib.jai"
        main_uri = client.open(main)
        main_text = client.texts[main_uri]
        lib_text = lib.read_text(encoding="utf-8")
        client.settle()
        results["diagnostics.main"] = client.wait_diagnostics(main_uri)["diagnostics"]
        status = client.request("jaiscript/status", {})
        results["status.program"] = sorted((relativize(u["uri"], FIXTURES), u["diagnostics"]) for u in status["units"])

        def at(text, needle, occurrence=1, delta=0):
            return position_of(text, needle, occurrence, delta)

        def ask(name, method, uri, text, needle, occurrence=1, delta=0, extra=None):
            params = {"textDocument": {"uri": uri}, "position": at(text, needle, occurrence, delta)}
            if extra:
                params.update(extra)
            results[name] = client.request(method, params)

        lib_uri = uri_of(lib)
        ask("hover.field", "textDocument/hover", main_uri, main_text, "h.hp", 1, 2)
        ask("hover.method_override", "textDocument/hover", main_uri, main_text, "h.heal", 1, 2)
        ask("hover.local_auto", "textDocument/hover", main_uri, main_text, "party =", 1, 0)
        ask("hover.class", "textDocument/hover", main_uri, main_text, "Hero h", 1, 0)
        ask("hover.namespace_fn", "textDocument/hover", main_uri, main_text, "shout", 1, 0)
        ask("hover.doc_comment", "textDocument/hover", main_uri, main_text, "twice", 1, 0)
        ask("hover.enum_value", "textDocument/hover", main_uri, main_text, "green", 1, 0)
        ask("hover.host_fn", "textDocument/hover", main_uri, main_text, "to_string", 1, 0)
        ask("hover.param", "textDocument/hover", main_uri, main_text, "xs)", 2, 0)
        ask("hover.base_ref_method", "textDocument/hover", main_uri, main_text, "a.power", 1, 2)
        ask("definition.global", "textDocument/definition", main_uri, main_text, "LIMIT", 1, 0)
        ask("definition.class", "textDocument/definition", main_uri, main_text, "Hero(", 1, 0)
        ask("definition.member_via_element", "textDocument/definition", main_uri, main_text, "party[0].heal", 1, 9)
        ask("definition.namespace_fn", "textDocument/definition", main_uri, main_text, "twice", 1, 0)
        ask("definition.local", "textDocument/definition", main_uri, main_text, "sum;", 1, 0)
        ask("definition.import_path", "textDocument/definition", main_uri, main_text, "lib.jai", 1, 1)
        ask("definition.enum", "textDocument/definition", main_uri, main_text, "Color", 1, 0)
        ask("references.field_hp", "textDocument/references", main_uri, main_text, "h.hp", 1, 2, {"context": {"includeDeclaration": True}})
        ask("references.class_Hero", "textDocument/references", lib_uri, lib_text, "class Hero", 1, 6, {"context": {"includeDeclaration": True}})
        ask("references.local_x", "textDocument/references", main_uri, main_text, "auto x", 1, 5, {"context": {"includeDeclaration": True}})
        ask("references.method_heal_base", "textDocument/references", lib_uri, lib_text, "void heal", 1, 5, {"context": {"includeDeclaration": False}})
        ask("references.method_power", "textDocument/references", lib_uri, lib_text, "int power", 1, 4, {"context": {"includeDeclaration": True}})
        ask("references.namespace", "textDocument/references", main_uri, main_text, "util::", 1, 0, {"context": {"includeDeclaration": True}})
        ask("highlight.local_sum", "textDocument/documentHighlight", main_uri, main_text, "sum = 0", 1, 0)
        ask("signature.ctor", "textDocument/signatureHelp", main_uri, main_text, "\"gator\"", 1, 0)
        ask("signature.method", "textDocument/signatureHelp", main_uri, main_text, "h.heal(5", 1, 7)
        ask("signature.namespace_fn", "textDocument/signatureHelp", main_uri, main_text, "util::twice(1", 1, 12)
        ask("rename.prepare_local", "textDocument/prepareRename", main_uri, main_text, "sum = 0", 1, 0)
        ask("rename.local", "textDocument/rename", main_uri, main_text, "sum = 0", 1, 0, {"newName": "acc"})
        ask("rename.field", "textDocument/rename", main_uri, main_text, "h.hp", 1, 2, {"newName": "health"})
        ask("rename.method_hierarchy", "textDocument/rename", lib_uri, lib_text, "void heal", 1, 5, {"newName": "mend"})
        ask("rename.refuse_host", "textDocument/rename", main_uri, main_text, "print", 1, 0, {"newName": "say"})
        ask("rename.refuse_collision", "textDocument/rename", main_uri, main_text, "total(", 1, 0, {"newName": "run"})
        results["symbols.document.lib"] = client.request("textDocument/documentSymbol", {"textDocument": {"uri": lib_uri}})
        results["symbols.workspace.her"] = sorted(
            (s["name"], relativize(s["location"]["uri"], FIXTURES)) for s in client.request("workspace/symbol", {"query": "her"}))

        words = fixture_words()

        def completion(name, text_edit_line, needle, delta):
            # type the edit, ask for completion at its end, then undo it
            pos = at(main_text, needle, 1, delta)
            version = client.change(main_uri, pos, pos, text_edit_line)
            end = {"line": pos["line"], "character": pos["character"] + len(text_edit_line)}
            result = client.request("textDocument/completion", {"textDocument": {"uri": main_uri}, "position": end})
            client.change(main_uri, pos, end, "")
            labels = sorted({item["label"] for item in result["items"] if item["label"] in words})
            results[name] = labels
            return version

        completion("completion.member_object", "h.", "h.heal(5);", 0)
        completion("completion.member_base_ref", "a.", "print(", 0)
        completion("completion.member_partial", "h.he", "h.heal(5);", 0)
        completion("completion.namespace", "util::", "string s", 0)
        completion("completion.enum", "Color.", "string s", 0)
        completion("completion.scope_locals", "", "print(", 0)
        completion("completion.static_less_call", "party[0].", "print(", 0)
        client.settle()

        errors_root = FIXTURES / "errors" / "root.jai"
        root_uri = client.open(errors_root)
        client.settle()
        results["diagnostics.errors.root"] = client.wait_diagnostics(root_uri)["diagnostics"]
        for leaf in ("typed_leaf.jai", "broken_leaf.jai"):
            leaf_uri = uri_of(FIXTURES / "errors" / leaf)
            results["diagnostics.errors." + leaf] = client.diagnostics.get(leaf_uri, {}).get("diagnostics")

        # an edit that breaks and then fixes the syntax: the error appears at the edited line and goes away
        pos = at(main_text, "int hp = h.hp;", 1, 0)
        version = client.change(main_uri, pos, pos, "int broken = ;\n\t")
        broken = client.wait_diagnostics(main_uri, version)["diagnostics"]
        end = {"line": pos["line"] + 1, "character": 1}
        version = client.change(main_uri, pos, end, "")
        fixed = client.wait_diagnostics(main_uri, version)["diagnostics"]
        results["diagnostics.edit_breaks"] = broken
        results["diagnostics.edit_fixes"] = fixed
    finally:
        client.close()

    results = relativize(results, FIXTURES)
    if update or not GOLDEN.exists():
        GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        GOLDEN.write_text(json.dumps(results, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        print("  wrote " + str(GOLDEN))
    golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
    actual = json.loads(json.dumps(results, sort_keys=True))
    for name in sorted(set(golden) | set(actual)):
        checks.check(golden.get(name) == actual.get(name), "golden " + name,
                     "expected " + json.dumps(golden.get(name))[:300] + "\n       got " + json.dumps(actual.get(name))[:300])


def run_gatorquest(exe, checks):
    print("gatorquest")
    client = Client(exe, GATORQUEST.parent)
    try:
        root = GATORQUEST / "gatorquest.jai"
        root_uri = client.open(root)
        start = time.perf_counter()
        client.wait_diagnostics(root_uri)
        status = client.settle()
        print("  program checked in %.0f ms (%d units)" % ((time.perf_counter() - start) * 1000, len(status["units"])))
        noisy = [(relativize(u["uri"], GATORQUEST.parent), u["diagnostics"]) for u in status["units"] if u["diagnostics"]]
        checks.check(len(status["units"]) >= 33, "every unit of the program is checked", str(len(status["units"])))
        checks.check(not noisy, "zero diagnostics on the clean program", str(noisy))
        for uri, params in client.diagnostics.items():
            checks.check(not params["diagnostics"], "no published diagnostics: " + relativize(uri, GATORQUEST.parent), str(params["diagnostics"])[:300])

        text = client.texts[root_uri]

        def at(needle, occurrence=1, delta=0):
            return position_of(text, needle, occurrence, delta)

        def definition(needle, occurrence=1, delta=0):
            r = client.request("textDocument/definition", {"textDocument": {"uri": root_uri}, "position": at(needle, occurrence, delta)})
            return (relativize(r["uri"], GATORQUEST.parent), r["range"]["start"]["line"] + 1) if r else None

        checks.check(definition("CHARS[slot]") == ("scripts/gq_state.jai", 103), "definition CHARS -> gq_state.jai:103", str(definition("CHARS[slot]")))
        checks.check(definition("Character& c") == ("scripts/gq_types.jai", 141), "definition Character -> gq_types.jai:141", str(definition("Character& c")))
        checks.check(definition("mob_live(m)") == ("scripts/gq_mobs.jai", 130), "definition mob_live -> gq_mobs.jai:130", str(definition("mob_live(m)")))
        checks.check(definition("c.name", 1, 2) == ("scripts/gq_types.jai", 144), "definition c.name -> Character.name", str(definition("c.name", 1, 2)))
        checks.check(definition("c.pack.size", 1, 2) == ("scripts/gq_types.jai", 151), "definition c.pack -> Character.pack", str(definition("c.pack.size", 1, 2)))

        hover = client.request("textDocument/hover", {"textDocument": {"uri": root_uri}, "position": at("mob_live(m)")})
        checks.check(hover and "bool mob_live(int m)" in hover["contents"]["value"], "hover mob_live shows its signature", str(hover))
        hover = client.request("textDocument/hover", {"textDocument": {"uri": root_uri}, "position": at("Character& c", 1, 11)})
        checks.check(hover and "Character& c" in hover["contents"]["value"], "hover c shows Character&", str(hover))
        hover = client.request("textDocument/hover", {"textDocument": {"uri": root_uri}, "position": at("Character& c")})
        checks.check(hover and "class Character" in hover["contents"]["value"] and "array<Item> pack;" in hover["contents"]["value"], "hover Character lists its fields", str(hover)[:200])

        refs = client.request("textDocument/references", {"textDocument": {"uri": root_uri}, "position": at("mob_live(m)"), "context": {"includeDeclaration": True}})
        files = {}
        for r in refs:
            files[relativize(r["uri"], GATORQUEST.parent)] = files.get(relativize(r["uri"], GATORQUEST.parent), 0) + 1
        expected = 0
        for path in GATORQUEST.glob("*.jai"):
            expected += len(re.findall(r"\bmob_live\b", path.read_text(encoding="utf-8")))
        checks.check(len(refs) == expected, "references mob_live: every call site and the declaration across files (%d)" % expected, "%d %s" % (len(refs), files))

        pos = at("string skills")
        version = client.change(root_uri, pos, pos, "c.\n\t")
        result = client.request("textDocument/completion", {"textDocument": {"uri": root_uri}, "position": {"line": pos["line"], "character": pos["character"] + 2}})
        client.change(root_uri, pos, {"line": pos["line"] + 1, "character": 1}, "")
        labels = {item["label"] for item in result["items"]}
        checks.check({"pack", "gold", "skill", "name", "fame"} <= labels, "completion c. lists Character fields", str(sorted(labels))[:300])

        combat = GATORQUEST / "gq_combat.jai"
        combat_uri = client.open(combat)
        client.wait_diagnostics(combat_uri)
        rename = client.request("textDocument/rename", {"textDocument": {"uri": root_uri}, "position": at("mob_live(m)"), "newName": "mob_exists"})
        edits = sum(len(v) for v in rename.get("changes", {}).values()) if isinstance(rename, dict) and "changes" in rename else 0
        checks.check(edits == expected, "rename mob_live edits every occurrence across files", str(rename)[:300])
        client.settle()
    finally:
        client.close()


def edit_latency(client, uri, rounds=30):
    lines = client.texts[uri].split("\n")
    line = next(i for i, l in enumerate(lines) if l.startswith("\t") and l.strip().endswith(";") and "return" not in l)
    samples = []
    for i in range(rounds):
        pos = {"line": line, "character": 1}
        probe = "int lsp_probe_%d = %d;\n\t" % (i, i)
        t0 = time.perf_counter()
        version = client.change(uri, pos, pos, probe)
        client.wait_diagnostics(uri, version)
        samples.append((time.perf_counter() - t0) * 1000)
        version = client.change(uri, pos, {"line": line + 1, "character": 1}, "")
        client.wait_diagnostics(uri, version)
    samples.sort()
    return statistics.median(samples), samples[int(len(samples) * 0.95) - 1], samples[-1], line


def run_latency(exe, checks):
    print("latency")
    client = Client(exe, GATORQUEST.parent)
    try:
        client.open(GATORQUEST / "gatorquest.jai")
        combat_uri = client.open(GATORQUEST / "gq_combat.jai")
        actions_uri = client.open(GATORQUEST / "gq_actions.jai")
        client.wait_diagnostics(actions_uri)
        client.settle()
        median, p95, worst, line = edit_latency(client, actions_uri)
        print("  edit -> diagnostics on gq_actions.jai (1085 lines): median %.1f ms, p95 %.1f ms, max %.1f ms" % (median, p95, worst))
        median, p95, worst, line = edit_latency(client, combat_uri)
        print("  edit -> diagnostics on gq_combat.jai: median %.1f ms, p95 %.1f ms, max %.1f ms" % (median, p95, worst))
        root_uri = uri_of(GATORQUEST / "gatorquest.jai")
        root_text = client.texts[root_uri]
        t0 = time.perf_counter()
        client.request("textDocument/references", {"textDocument": {"uri": root_uri}, "position": position_of(root_text, "mob_live(m)"), "context": {"includeDeclaration": True}})
        first_refs = (time.perf_counter() - t0) * 1000
        t0 = time.perf_counter()
        client.request("textDocument/references", {"textDocument": {"uri": root_uri}, "position": position_of(root_text, "CHARS[slot]"), "context": {"includeDeclaration": True}})
        warm_refs = (time.perf_counter() - t0) * 1000
        print("  references across the program: %.0f ms cold (every unit's model built), %.1f ms warm" % (first_refs, warm_refs))
        combat_text = client.texts[combat_uri]
        t0 = time.perf_counter()
        client.request("textDocument/hover", {"textDocument": {"uri": combat_uri}, "position": position_of(combat_text, "mob_live(", 1, 0)})
        print("  hover: %.1f ms" % ((time.perf_counter() - t0) * 1000))
        pos = {"line": line, "character": 1}
        version = client.change(combat_uri, pos, pos, "CHARS[0].\n\t")
        t0 = time.perf_counter()
        items = client.request("textDocument/completion", {"textDocument": {"uri": combat_uri}, "position": {"line": line, "character": 10}})
        print("  completion after CHARS[0].: %.1f ms (%d items)" % ((time.perf_counter() - t0) * 1000, len(items["items"])))
        client.change(combat_uri, pos, {"line": line + 1, "character": 1}, "")
        t0 = time.perf_counter()
        pos = {"line": line, "character": 1}
        version = client.change(combat_uri, pos, pos, "int typo_probe = \"text\";\n\t")
        params = client.wait_diagnostics(combat_uri, version)
        error_ms = (time.perf_counter() - t0) * 1000
        checks.check(any("typo_probe" in d["message"] for d in params["diagnostics"]), "a type error typed into gq_combat.jai is reported", str(params["diagnostics"])[:300])
        print("  edit introducing a type error -> its diagnostic: %.1f ms" % error_ms)
        client.change(combat_uri, pos, {"line": line + 1, "character": 1}, "")
        t0 = time.perf_counter()
        version = client.change(combat_uri, {"line": 0, "character": 0}, {"line": 0, "character": 0}, "int lsp_new_global = 1;\n")
        client.wait_diagnostics(combat_uri, version)
        client.settle()
        print("  edit changing the program surface -> whole program rechecked: %.0f ms" % ((time.perf_counter() - t0) * 1000))
        checks.check(median < 100.0, "median edit -> diagnostics latency under 100 ms", "%.1f ms" % median)
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe")
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--only", choices=["fixtures", "gatorquest", "latency"])
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    exe = Path(args.exe) if args.exe else default_exe()
    if not exe or not exe.exists():
        print("jaiscript_lsp not found; build the jaiscript_lsp target or pass --exe")
        return 2
    print("server: " + str(exe))
    checks = Checks(args.verbose)
    if args.only in (None, "fixtures"):
        run_fixtures(exe, args.update, checks)
    if args.only in (None, "gatorquest"):
        run_gatorquest(exe, checks)
    if args.only in (None, "latency"):
        run_latency(exe, checks)
    print("%d passed, %d failed" % (checks.passed, len(checks.failures)))
    return 1 if checks.failures else 0


if __name__ == "__main__":
    sys.exit(main())

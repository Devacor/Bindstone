# Parallel method capture probes

On the frozen dce797ff snapshot, an admitted method reading a captured string
table can fail on both backends.
The same read written in the parallel body passes the paired control.
The opt-in unsafe path also exposes cold getter and method ownership failures.

The probe registers the standard library, selects a backend, sets eight workers,
and runs this with unsafe parallel disabled:

```jai
array<string> palette = ["x"];
class Cell {
	int v = 0;
	Cell(int n) { v = n; }
	int calc() { return v + palette[0].size(); }
}
array<Cell> cells = [];
for (int i = 0; i < 64; ++i) { cells.push(Cell(i)); }
for (int round = 0; round < 3; ++round) {
	parallel_for (auto& cell : cells) { cell.v = cell.calc(); }
}
cells[63].v; // expected: 66
```

The control replaces the loop assignment with
`cell.v = cell.v + palette[0].size();`. The inputs and writes are otherwise
the same; each iteration writes its own Cell and reads the shared palette.

| Probe on dce797ff plus the checker | Interpreter | VM |
|---|---:|---:|
| Admitted captured-table method, 20 processes | 6 crashes, 14 correct | 3 crashes, 17 correct |
| Inline control, 20 processes | 20 correct | 20 correct |
| Same method on the earlier 6e2bc1f5 runtime, 5 processes | 3 crashes, 2 correct | 1 crash, 4 correct |

Captured-method failures include
`strong_ptr control-block derivation hit a dead or foreign block`.
The earlier build uses its matching headers and library, so the reproduction
predates the current checker increment and bool/int conversion rule.

The seven-shape matrix also runs admission-enabled and unsafe modes, five
processes per backend and mode:

| Shape | Admission enabled | Unsafe override |
|---|---|---|
| Own method reading an int field | correct on both backends | VM failed once |
| Explicit this.inner() sibling call | correct on both backends | failed every repetition on both |
| Bare sibling call | rejected with the documented this.inner() requirement | ownership failures |
| Method on a nested field | rejected as not admitted | getter-ID cache assertion on both |
| Getter through this.x | rejected as not admitted | getter-ID cache assertion on both |
| Worker-local string literal | correct on both backends | VM failed once |
| Captured string table inside method | failures above | ownership failures on both |

The nested getter failures report
`string_symbolizer: getter-id miss during a parallel region`.
Some unsafe runs terminate with access violations; one baseline captured-table
run instead reports an array of size zero where the input contains one element.

Fable reviewed the reproductions and owns the runtime fixes. His stated direction
is to preserve runtime name warming, method pins and reference-count protections
when unsafe mode suppresses script-semantic admission restrictions. The admitted
captured-table method additionally needs its recorded capture/borrow path to be
honored. The inline control isolates the failing method path; it does not by
itself prove the precise bad read.

These counts demonstrate reproduction, not an estimated application failure
rate. They use small, repeated fixtures under the recorded development load.
No timing claim is made. The registered Debug suites, fuzz campaigns and existing
110-case refcount-detector suite passed in the same checker verification cycle;
these additional cases expose coverage gaps outside those registered tests.

Raw evidence under out/astra/unsafe-20260909/:
probe.cpp, build.ps1, build-identity.json, results.json, summary.json,
safe-method-vs-inline.json and the .jai programs. baseline/ contains the earlier
build and replay. first-run.json retains the initial getter sample that used
unsupported bare x; the reported matrix uses this.x.


The separate registry-loss fix in 50ad165b was replayed with the same standalone
Release VM harness: all 1000 fault-suite processes passed, five tests per process.
The earlier loaded run had 10 failures in 1000 processes. Raw:
out/astra/registry-20260908/fixed-row83-vm/{runs,audit}.json and process logs;
the earlier failures remain in during-client-and-canary-build/.


Owner resolution: Fable landed the runtime protections and provisioned method
environment in 459053f2. The follow-up c487a0e9 separates method/body capture
scopes, provisions enclosing-frame locals and adds permanent capture/registry
regressions. Fable reports the shadowing control returns 75 in all four
backend/mode combinations, both combined Debug suites pass 3011 tests, and the
isolated client passes 76/76 assets plus login/workbench gates. Those follow-up
results are owner-reported; the raw Astra reproductions above remain tied to
their original binaries.

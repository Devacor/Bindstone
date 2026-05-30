# JSON parse benchmark — fast_json vs rapidjson

A standalone, single-TU head-to-head that parses a real scene file into a full DOM with
both parsers and compares wall-clock parse time, throughput, and structural parity.

- `fast_json.hpp` — a lean, arena-backed JSON DOM parser written for this comparison.
  Zero per-node heap allocation (all values in one contiguous arena, all string bytes in
  one contiguous char arena), `std::from_chars` numbers, SSE2 bulk string scanning, an
  integer fast-path, and the rapidjson "stack → bulk-move into arena" container trick.
- `bench_json.cpp` — the harness. Reads a JSON file, parses it N times with each parser
  (fresh DOM each iteration, representative of a cold scene load), prints median/min ms,
  MiB/s, the speedup, and a structural checksum (node/member/element/string counts, int &
  double sums, type histogram) so any parse divergence shows up immediately.

## Build & run (VS x64 dev shell)

```bat
cl /nologo /O2 /Ob2 /Oi /Ot /std:c++20 /EHsc /DNDEBUG ^
   /I"D:\git\Bindstone\External\cereal\include" bench_json.cpp /Fe:bench_json.exe

bench_json.exe "D:\git\Bindstone\Scenes\map.scene2" 80
```

rapidjson is the copy cereal vendors (`cereal/external/rapidjson`). It runs scalar by
default; add `/DCEREAL_RAPIDJSON_SSE42` to give rapidjson its SSE4.2 path (it barely helps
on this short-string, minified data, so the lead below holds against rapidjson at its best).

## Result (map.scene2, 3.69 MiB, 585,906 nodes; idle machine, 80 iters)

| parser    | median ms | min ms | MiB/s |
|-----------|-----------|--------|-------|
| rapidjson | ~12.4     | ~11.0  | ~300  |
| fast_json | ~8.5      | ~8.0   | ~430  |

**fast_json is ~1.45× faster than rapidjson**, with exact structural parity (every
checksum matches). Number mix for this scene: 124k ints, 107k doubles, 25k bools, 265k
strings — so `from_chars` and bulk string copy are the dominant levers.

## Why the production reader (`json_archive.hpp`) is slower, and the path forward

`fast_json` is a *flat* DOM. The production `json_archive_reader` parses into a
`script_value` DOM whose objects are `std::map<script_value, script_value>` — a red-black
tree with a heap node + string-content comparison per member, plus a per-object
`unordered_map` index rebuilt on open. The 2026-05 audit flagged this as the dominant
scene-load cost (high severity, found independently by four review dimensions).

Techniques already folded back into `json_archive.hpp` from this bench (safe, shipped):
`std::from_chars` numbers (also fixes a crash bug), scalar bulk-copy string parsing,
in-place literal/whitespace scanning, and the writer's escape path.

Deferred (bigger, measure-then-merge): replace the object DOM's `std::map` with a flat
member layout (or a SAX path that feeds property loaders directly, eliminating the
materialize-then-rewalk double traversal), and make `ObjectState`'s index lazy with a
sequential fast-path. Those are where the remaining scene-load wins are.

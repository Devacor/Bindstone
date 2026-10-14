# fast_json — a flat JSON DOM parser for parse-speed comparisons

`fast_json.hpp` is a lean, arena-backed JSON DOM parser written as a speed reference for the
production `json_archive_reader`. Zero per-node heap allocation (all values in one contiguous
arena, all string bytes in one contiguous char arena), `std::from_chars` numbers, SSE2 bulk
string scanning, an integer fast-path, and the rapidjson "stack → bulk-move into arena"
container trick. A host can time it against `json_archive_reader` on the same file.

## Result (historical: a 3.69 MiB scene, 585,906 nodes; idle machine, 80 iters)

| parser    | median ms | min ms | MiB/s |
|-----------|-----------|--------|-------|
| rapidjson | ~12.4     | ~11.0  | ~300  |
| fast_json | ~8.5      | ~8.0   | ~430  |

**fast_json is ~1.45× faster than rapidjson**, with exact structural parity (node, member,
element and string counts, int and double sums, type histogram). Number mix for this file:
124k ints, 107k doubles, 25k bools, 265k strings — so `from_chars` and bulk string copy are
the dominant levers.

## Why the production reader (`json_archive.hpp`) is slower, and the path forward

`fast_json` is a *flat* DOM. The production `json_archive_reader` parses into a
`script_value` DOM whose objects are maps with a per-object index rebuilt on open.

Techniques already folded back into `json_archive.hpp` from this parser (safe, shipped):
`std::from_chars` numbers (also fixes a crash bug), scalar bulk-copy string parsing,
in-place literal/whitespace scanning, and the writer's escape path.

Deferred (bigger, measure-then-merge): a flat member layout for the object DOM (or a SAX
path that feeds property loaders directly, eliminating the materialize-then-rewalk double
traversal), and a lazy `ObjectState` index with a sequential fast-path.

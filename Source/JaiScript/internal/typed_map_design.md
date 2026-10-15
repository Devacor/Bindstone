# Typed map storage: an int-key window and typed values

**Goal.** Two changes to `script_map`, behind the same script-visible API and semantics:

1. A map whose integer keys cluster finds them in O(1), with no hash, no probe and no key read: a window of entry
   pointers indexed by `k - base`. This is Lua's array part, generalized with a base offset, so a window starting
   anywhere qualifies (JaiDoom's sprite maps are keyed by lump numbers from 1028 up; Lua's `1..n` array part would not
   catch them).
2. Typed values: `map<K,int>`, `map<K,float>` and `map<K,bool>` store raw 8-byte values instead of 32-byte
   `script_value`s inside 88-byte entries, and a proven `map<int,int>` read lands a raw int in a typed slot the way
   `typed_int_index_row` does for arrays.

Read with `typed_array_design.md` (the same kind/tag discipline), `reference_model.md` (map+key references) and
`invariants.md`.

## Why

With the kind-specialized int probe and the per-map entry pool in place, a map lookup costs two cache misses when the
working set is evicted between lookups: the slot line (`slots_[i].e`) and the entry line (the key read of an 88-byte
entry). In the JaiDoom timedemo map lookup is 4.6% of samples; the int-key probe is 3.4%, of which 1.7% is the entry's
key read and 1.0% the slot load, and the hash and compare arithmetic about 0.2%. Probe counts are 1.3 to 2.0.

A window removes the probe: a 124-key map's window is 124 pointers (1 KB) that stay resident, and a hit reads no key.
The entry is still touched for its value, so the window recovers the slot and probe share, not the entry line. Values
held where the window points (stage B) are what removes the second miss. Neither is built.

## What does not change

- Key order where it is observed (iteration, serialization), hashed lookup everywhere else.
- Keys never coerce: an int key 1 and a float key 1.0 stay distinct; `map<string,int>` rejects an int key.
- `map<var,var>` stays fully dynamic.
- Entries never move: a pointer or reference to a key or value stays valid until that entry is erased.
- The lane memo stays invisible; map+key references re-resolve by key.
- No Debug/Release difference; both backends at parity; hot reload keeps live maps.

## Stage A: the int-key window (prototyped, not adopted)

An int-key window (entry pointers indexed by `k - base`, opened at rehash when int keys are dense) was prototyped and
not adopted: it slowed every map that did not qualify. A later attempt must add no field reads or insert bookkeeping for
maps without a window. `script_map`'s int-key path is `find_int_entry`. The prototype's shape:

Same TYPEID, same `strong_ptr<script_map>` node identity, same entries.

- **Every entry stays in the hash part** (open addressing over pooled, node-stable entries). The window is a second
  index over the int keys: `std::vector<entry*> window_` with `window_base_`, where `window_[i]` is the entry of int key
  `window_base_ + i` or null. Bookkeeping: `ints_` (int keys, a bound int counted: it equals the int key of its value),
  `windowed_` (int keys inside the window) and the int keys' bounds `int_min_` / `int_max_` (an erase leaves them wider).
- **Invariant**: every int key whose value lies inside the window is indexed in it. So a lookup inside the window is
  final (hit or miss); outside it, the probe runs only while `ints_ != windowed_`.
- **Opening and rebuilding**: at a rehash (the hash part doubling), when some int key lies outside the window, the map
  holds at least `window_min_keys` (4) int keys and their bounds span at most four per key, the window is rebuilt over
  `[min, max]` of the int keys, indexing all of them. The test is O(1) on the tracked bounds; the rebuild is one pass
  over the slots (no sort), amortized like the rehash. A sparse map never builds one; a map of fewer than 4 int keys
  never allocates one.
- **Growing in place**: a new int key outside the window joins it while every int key is inside (`ints_ == windowed_`
  before it) and the grown span stays at most four per key. Upward growth resizes; downward growth takes the window's
  span again as slack when the density allows, so a descending fill stays linear. A key that cannot join waits outside
  for the next rehash.
- **Erase** clears the key's window slot. **Copy** takes the source's window shape and indexes its own entries.
  **Move** and **clear** move or drop the window with the entries.
- **Lookup lanes**: the int key path of `lookup`, `lookup_int`, `recall` and `find_or_insert` tests the window first;
  the probe is unchanged. String and other keys take the ordinary path.

Placement inside the window does not depend on insertion order: a clustered set filled in any order ends indexed at the
rehash after it becomes quarter-dense, and every later key in range joins at once.

Why it lost: the window wins only where it applies and costs every other map: the extra fields lengthen the map object
every lookup reads, and every int insert pays the bookkeeping. JaiDoom never opens a window: its sprite maps fill lazily
in first-use order, so their lump keys sit near 10% of their span, under the quarter density a window needs. A placement
rule that catches lazily filled sparse key sets is the open problem.

## Stage B (proposal): values held inline

Typed values (int, float, bool) in raw storage, and a window that reaches the value without touching an 88-byte entry,
so a hit is one resident line. Hard parts:

1. **Entry lvalues on typed values.** `m[k] += 1`, `auto& r = m[k]` and `f(m[k])` with a `T&` parameter reach the
   reference holder's map+key mode, which hands out `&value`. A raw int has no `script_value` to point at, so the holder
   gets a `typed_entry()` mode mirroring `typed_element()`: materialize into the holder's scratch, commit through the
   map's typed store (`ref_write_through` / `ref_store_through` gain the entry twin). Value kind any needs none of this.
2. **Value kind**: chosen from the declared type at mint (the boundary kernel's map branch, exactly as `script_array`
   picks its kind), with the `dynamic_slot` twin for `var` slots that arrays have; every store runs the existing slot
   conversion table.
3. **Node stability**: values that move with the window would break the "entries never move" guarantee the header
   documents; inline values need storage that never moves (pages) or a holder audit of every `&it->second` user that
   keeps the pointer across a store.
4. **Hot read lanes**: only the fast check (int key, inside the window, present) stays inline in the VM's map-read
   lanes; the rest goes out of line (`JAI_NOINLINE`). Storing pairs in dense pages and placing keys only on admission
   (no migration) leaves a scrambled clustered fill mostly in the hash part and slows string reads through the larger
   inline lookup.
5. **Parallel.** A window hit is pure loads: no allocation, no counts. Worker stores follow the ordinary rules.

**Stage C (proposal, only if measured worthwhile): typed string keys** (a raw `strong_ptr<script_string>` with no `script_value`
around it). Short string keys already compare 8 bytes beside the hash; their remaining cost is the slot and entry loads.

## Gates

Each stage is its own gated landing with a same-directory A/B, every run from a fresh copy of its exe (byte-identical
copies time apart by their file-cache pages).

- Suites on both backends, fuzz (default and `--no-proofs`), the proof differential gate, the map memo and reference
  tests; Map Tests pin storage against an ordered model, mixed dense and hash keys under iteration and serialization,
  out-of-order, descending and far keys, erase and re-insert, copies, moves and bound int keys.
- A/B: JaiDoom, Gator Quest, OSSUARY, GLOOM, the dict kernels, and map microbenchmarks that do not cluster (string keys,
  sparse ints, small and sparse map builds) to show the window costs nothing there.

## Risks

- **Memory**: at most four pointers per int key in the window, none for a sparse map or one under 4 int keys.
- **Rebuild spikes**: a rebuild runs only at a rehash and only when the bounds are quarter-dense; one pass over the
  slots.
- **Order drift**: none; iteration still reads the hash part's entries through the sorted view.

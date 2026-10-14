# Typed map storage: a dense integer part and typed values

**Goal.** Two changes to `script_map`, behind the same script-visible API and semantics:

1. A map whose integer keys cluster indexes them in O(1), with no hash, no probe and no key read:
   `value = pages[(k - base) >> 6][(k - base) & 63]`. This is Lua's array part, generalized with a base offset, so a
   window starting anywhere qualifies (JaiDoom's sprite maps are keyed by lump numbers from 1028 up; Lua's `1..n` array
   part would not catch them).
2. Typed values: `map<K,int>`, `map<K,float>` and `map<K,bool>` store raw 8-byte values instead of 32-byte
   `script_value`s inside 88-byte entries, and a proven `map<int,int>` read lands a raw int in a typed slot the way
   `typed_int_index_row` does for arrays.

Read with `typed_array_design.md` (the same kind/tag discipline), `reference_model.md` (map+key references) and
`invariants.md`.

## Why

With the kind-specialized int probe and the per-map entry pool in place, a map lookup costs two cache misses when the
working set is evicted between lookups: the slot line (`slots_[i].e`) and the entry line (the key read of an 88-byte
entry). In the JaiDoom timedemo map lookup is 4.6% of samples; the int-key probe is 3.4%, of which 1.7% is the entry's
key read and 1.0% the slot load, and the hash and compare arithmetic about 0.2%. Probe counts are 1.3 to 2.0. Removing
the cost means not touching those lines: index straight to the value, and keep values small enough that a 124-entry map
stays resident (124 raw ints are 1 KB).

## What does not change

- Key order where it is observed (iteration, serialization), hashed lookup everywhere else (the map-order ruling).
- Keys never coerce: an int key 1 and a float key 1.0 stay distinct; `map<string,int>` rejects an int key as today.
- `map<var,var>` stays fully dynamic.
- The lane memo stays invisible; map+key references re-resolve by key.
- No Debug/Release difference; both backends at parity; hot reload keeps live maps.

## Representation

Same TYPEID, same `strong_ptr<script_map>` node identity.

- **Hash part**: unchanged (open addressing, pooled node-stable entries) for every key that is not dense.
- **Dense part** (any map that holds int keys; key kind any or int): `int64 base`, a page table `std::vector<page*>`
  (the table may reallocate, pages never move) and a per-page presence bitmap. A page holds 64 values:
  `script_value[64]` for value kind any, `int64[64]` / `double[64]` / bytes for typed values. A pointer to a dense value
  stays valid until its key is erased, the guarantee entries give today.
- **Placement**: an int key `k` is dense while `base <= k < base + 64 * pages`. Otherwise it goes to the hash part,
  unless adding it keeps the occupied int span at least 25% dense; then the window grows or re-bases and the hash
  part's int keys in the new window move in. The move is invisible: references re-resolve by key and the memo is
  generation-checked. A window costs one page-table pointer per 64 keys and a page only where a key lives; a sparse map
  stays hashed because the density test fails.
- **Value kind**: chosen from the declared type at mint (the boundary kernel's map branch, exactly as `script_array`
  picks its kind): any, int, float or bool, with the `dynamic_slot` twin for `var` slots that arrays have. Typed values
  live raw in dense pages and in a raw-value entry variant in the hash part; every store runs the existing slot
  conversion table (an int into a float value converts, a string into an int raises the same text as today).
- **Typed string keys** (a raw `strong_ptr<script_string>` with no `script_value` around it) are a possible later
  stage, measured first: short string keys already compare 8 bytes beside the hash, and their remaining cost is the
  same slot and entry loads.

## The hard parts

1. **Entry lvalues on typed values.** `m[k] += 1`, `auto& r = m[k]` and `f(m[k])` with a `T&` parameter reach the
   reference holder's map+key mode, which hands out `&value`. A raw int has no `script_value` to point at, so the holder
   gets a `typed_entry()` mode mirroring `typed_element()`: materialize into the holder's scratch, commit through the
   map's typed store (`ref_write_through` / `ref_store_through` gain the entry twin). Value kind any needs none of this:
   dense pages hold real `script_value`s.
2. **Order.** Dense keys come out sorted; the sorted view merges them with the sorted hash-part keys under
   `script_value <=>`, where every int ranks together, so iteration and serialization output are byte-identical.
   Equality and the map hash go through the same per-entry funnel.
3. **Parity.** Both backends reach maps only through `script_map`'s API (`find_entry`, `recall`, `lookup`, the
   iterator, `operator[]`), so the dense part lives behind it. The typed read lane is the VM's, guarded on the node's
   kinds and falling back to the general read; the interpreter stays the oracle.
4. **Hot read lanes.** The dense probe sits in the VM's scalar map-read lane, which runs on every map read: only the
   fast check (int key, inside the window, present) stays inline; everything else is out of line (`JAI_NOINLINE`), so
   lanes that never see a dense map keep their frames.
5. **Parallel.** A dense hit is pure loads: no allocation, no counts. Worker stores follow today's rules.

## Staging

Each stage is its own gated landing with a same-directory A/B.

- **Stage A: the dense part, value kind any.** Generic: every map with clustered int keys gains (JaiDoom's sprite and
  lump maps, `map<var,var>` too), with no new reference mode and no typed lanes.
  - First: an audit of every `&it->second` / `&e->kv.second` user that holds the pointer across a store, and the
    migration battery written red (window growth, re-base, erase, iteration order and serialization mixing dense and
    hash keys, copies, hot reload, parallel reads).
  - Gates: suites on both backends, fuzz (default and `--no-proofs`), the proof differential gate, the map memo and
    reference tests.
  - A/B: JaiDoom, Gator Quest, OSSUARY, GLOOM, the dict kernels, and a map-heavy case that does not cluster (string keys
    and sparse ints) to show the dense part costs nothing there.
- **Stage B: typed values** (int, float, bool) in both parts, the `typed_entry()` holder mode, and a typed read lane for
  proven `map<int,int>` / `map<int,float>` reads. After stage A is measured.
- **Stage C (only if measured worthwhile): typed string keys.**

## Risks

- **A pointer into a map value held across a store**: dense pages never move, keeping today's guarantee; the stage-A
  audit enumerates the holders.
- **Re-base spikes**: amortized doubling, and a test that alternates far-apart keys.
- **Memory on sparse int keys**: the 25% density rule; the battery pins the worst case.
- **Order drift**: pinned by the existing map-order tests plus new ones mixing dense and hash keys.

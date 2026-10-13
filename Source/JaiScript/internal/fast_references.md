# Fast references (design study)

Status (Dev, 2026-10-07):

- **Built:** layer 1's field-slot memory.
- **Next:** layer 2 (non-atomic pins for script owners), designed against the one-allocation instance (an intrusive count on
  the combined allocation); then layer 2b (split pool counter); then layer 1's array-element generation. Map entries
  follow lever C's node-stable table.
- **Rejected:** layer 3.
- **Parked as research:** layer 4.

A reference (`var& r = o.f`, a `T&` parameter, `for (auto& x : xs)`, `auto& e = m[k]`) is a `script_value` whose storage is a
`strong_ptr<reference_holder>`. The holder pins what it names and finds the target again on every touch. The six holder
modes are listed in invariants.md §3 and docs/reference_model.md. This note costs four ways to make a touch, a mint and a
release cheaper. Each keeps today's observable rules:

- a removed field raises `Reference to a removed field`;
- a removed element or map entry raises its own text;
- re-class and hot reload re-resolve;
- parallel workers never write shared state and never touch a count another thread can reach;
- debugger locals show the referent;
- destructor timing is unchanged.

## Where references cost today

All numbers are from Release stack samples of member4 (RelSym build, 100 µs interval, core 12). Shares are of each kernel's
samples.

| kernel | mint (pool acquire, owner pin, holder init) | release (pool return, pin drop, holder dtor) | find on touch | lvalue re-resolve at the call (`bind_site_reference`, `resolve_ref_lvalue`, admission) |
|---|---|---|---|---|
| tree (dyn and typed) | 5.5% | 3.6% | 4.4% (field cell, slot scan) | 6.5% |
| nbodies dyn | 1.7% (element refs `var& bi = bodies[i]`) | 1.6% | under 1% | none |
| particles | none outside the cell block (the block's rebind memo serves the alias) | none | none | none |
| Gator Quest | under 1% | under 1% | under 1% | under 1% |

- **tree** mints about 1.3M field references per run: two per `split`/`merge` call, from `split(o.left, …, o.left, …)`.
  The owner pin is a `std::shared_ptr<class_instance>`, so each mint and each release is one atomic read-modify-write.
  The pool's outstanding count adds one atomic per mint and one per release.
- **nbodies dyn** after the cells' KILL_SLOT skip spends its element references inside blocks. Outside them, `var& bi`
  mints once per outer iteration.
- **particles** and **Gator Quest** are dominated by cell work and object layout, not references.

## Layer 1: a resolved target plus one validator per mode

**Mechanism.** The holder keeps `script_value* target` next to its pin, plus one validator that proves the target is
still the live slot. A touch is one compare and one load. On a miss it re-resolves exactly as today and refreshes the
pair. The pin stays the authority for lifetime; the pointer is a cache. That keeps invariants.md §3 ("never a raw address
plus a lifetime guess") true in substance, and its wording changes to "never an unvalidated address".

| mode | validator | miss path | notes |
|---|---|---|---|
| field | owner's `class_definition*` + its `field_shape_epoch()` | `find_field_value(id)` → null raises the removed-field text | **Built (slot form).** The holder remembers the slot, not the address, so a touch is `field_slot_value(slot)` (bounds + INVALID check). The address form saves one more indirection. It depends on a field cell staying put while its class and epoch hold (lever A's A3 guarantee; also relied on by the member ICs and the cells' rebind memo). |
| cell | none | none | Already direct: the holder owns the value inline. |
| map entry | none, once map nodes are stable (lever C) | `find(key)`; a missing key raises | Today every touch hashes the key. With node-stable entries, erase must invalidate: the holder needs an entry generation or the map an erase epoch. |
| vector element | the array node's buffer generation (bumped on reallocation, erase and shrink) | `container_index < size` → today's bounds text | A generation counter costs one increment on every growth and erase path of `script_array`. Typed (raw-buffer) elements keep materializing; the pointer form applies to `script_value` buffers only. |
| CPP-MEMBER, PROPERTY, string element, component | unchanged | unchanged | Their touch runs code by design. |

**Semantics kept.**
- Removed-field and removed-element raises: the validator misses, then the re-resolve raises as today.
- Re-class: the class pointer changes.
- Hot reload: the epoch bumps.
- Parallel: the cache is never written while a parallel region is active. Workers read it, or re-resolve on a miss
  without storing the result.
- Debugger and destructors: untouched, because the pointer never owns anything.

**Size.**
- Field form: about 40 lines (built).
- Element generation: about 60 lines across script_array's mutators, plus the holder check.
- Map form: waits on lever C.

**Estimated gain.**
- tree: about −2% beyond today's slot memory (the remaining field-cell lookup).
- nbodies: under 1%.
- particles and Gator: none measurable.

**Risk: low** (the field form), **medium** (elements, because every mutator must bump).

## Layer 2: non-atomic pins for script-owned owners

**Mechanism.** Field references pin `std::shared_ptr<class_instance>`: atomic counts, two RMWs per reference lifetime, plus
`note_field_ref_minted`. A script instance is always held through an `object_holder` (`strong_ptr`, non-atomic). Pinning
the field reference through that holder, with `owner_instance` kept as a raw pointer, replaces the two atomics with two
plain counts. C++-bound and hybrid owners (the native object, binder objects reached through `property.view.instance`) keep
`std::shared_ptr`, because their lifetime is shared with C++.

**Parallel workers.** `strong_ptr` counts are already non-atomic everywhere (parallel_design.md §0, §4). Soundness rests
on chunk-exclusive reachable sets proven at the barrier: a worker touches only its own chunk's control blocks, and any
cross-chunk alias is deep-cloned. A worker minting a field reference into its own element bumps that element's holder
count, which is inside its exclusive set and is the same situation as copying a handle there today. References reachable
from an element already force that element onto the clone path (§4a).

**The pool.** `reference_holder_pool::return_block` decrements an atomic `outstanding` count, including when a pooled
holder is released on a worker thread (that release then deletes the block). Making the count plain needs two counters:
an owner-thread count, plus an atomic count of foreign returns for teardown's orphan check. That is a separate,
optional step worth about 1% of tree.

**Semantics kept.** Lifetime is unchanged: the holder keeps the instance alive exactly as long as before. Destructor
timing is unchanged: the last release still runs the instance's destruction. Removed-field and re-class behaviour do not
depend on the pin's type.

**Size.** About 80 lines: `reference_holder` gains `strong_ptr<object_holder> owner_holder`, and `owner_instance` is read
through it. Every `owner_instance->` use (value.cpp, ref_lvalue.hpp, the VM's alias head) reads the raw pointer.

**Estimated gain.**
- tree: −3 to −4%.
- nbodies, particles and Gator: none measurable.

**Risk: medium**, because it touches instance ownership. Coordinate with lever A, which is reworking `class_instance`
and `object_holder`.

## Layer 3: a reference inline in the 32-byte value, with no holder

**The layout today.** `script_value` is `type_info_ptr` (8 bytes) + `engine*` (8) + `storage` (16): an 8-byte payload
union, a 1-byte index and 7 bytes of padding. Two static_asserts pin this: `sizeof(script_value) == 32` and
`sizeof(storage) == 16`.

**What a reference needs.** A pin (8 bytes, the owner's handle), a target (8 bytes, or 6 with 48-bit compression), a
mode, and for fields a validator (class pointer + epoch, 12-16 bytes).

**What is free.** The 8-byte payload plus 7 bytes of padding, 120 bits in all. `type_info_` must stay, because it carries
the reference's declared type (the `T&` stamp the store rules read), and `engine_` must stay (every value carries its
engine).

**Verdict.** A pin plus a compressed target plus a mode byte fits only with pointer compression, and only with no room
for a validator. Field references would then have to re-find on every touch, which loses layer 1. Element and map modes
would also lose their index or key, which today live in the holder.

**What every consumer would need.** 152 `TYPEID_REFERENCE` / `get_reference_holder()` sites in 12 files, 78 of them in
vm_backend.cpp, assume a holder. All of them, plus `deref_slow`, `assign_through`, copy and destroy, would gain an inline
branch.

**Estimated gain.** The mint and release columns above, about 9% of tree, plus one indirection per touch.

**Cost.** A size-sensitive rewrite of the value's core, and pointer compression is a platform assumption.

**Not recommended.** Layers 1 and 2 recover about two thirds of the same cycles without touching the value.

## Layer 4: non-escaping references without a pin

**Mechanism.** A reference the checker proves never escapes its frame could be a bare slot address with no holder: a
by-reference parameter, a range-for reference, or an `auto&` local that is never captured, stored, returned or passed to
an escaping parameter.

**What the proof must also show.** The referent cannot die while the reference lives. For a field of a caller's object,
the callee could drop the last handle to that object (`root.left = null` through another reference, or a reassigned
global), so "not escaping" is not enough. The proof needs either an ownership argument (the caller holds a handle on a
frame slot the callee cannot reach) or a pin.

**The VM side.**
- The value stack reallocates and relocates by memcpy. Raw slot pointers survive only through its rebase chokepoint
  (`rebase_window_pointers`), which today serves the counted-for states.
- Frame-slot targets need index-based addressing (frame base + slot), not raw pointers.
- Field and element targets need the owner alive, which brings the pin back.

**Estimated gain.** At most the whole reference lifecycle at a call: in tree, the mint, the release and the call-side
re-resolve, about 15% (the remaining 4% find-on-touch would stay).

**Risk: high.** A new escape and lifetime analysis, with a use-after-free if it is wrong.

**Assessment.** Possible later, for frame-local targets first: a parameter bound to a caller's local, where the
caller's frame provably outlives the callee and index addressing removes the reallocation issue.

## Summary

| layer | tree | nbodies dyn | particles | Gator Quest | risk | size | dependency |
|---|---|---|---|---|---|---|---|
| 1 (field form, built) | −1 to −2% so far | — | — | — | low | ~40 lines | lever A A3 (cells stay put) |
| 1 (elements, maps) | — | under 1% | — | — | medium | ~60 lines + map | lever C (node-stable maps) |
| 2 (non-atomic pins) | −3 to −4% | — | — | — | medium | ~80 lines | lever A (instance ownership) |
| 2b (pool counter) | about −1% | — | — | — | low | ~20 lines | none |
| 3 (inline references) | about −9% at best | small | — | — | high | large | value layout |
| 4 (pin-free non-escaping) | up to −15% | small | — | — | high | large | checker escape and lifetime analysis |

Order: layer 2 on the one-allocation instance, then layer 2b, then layer 1's element generation, then its map-entry
form. Layer 3 is not built. Layer 4 is a later study, starting with frame-local targets.

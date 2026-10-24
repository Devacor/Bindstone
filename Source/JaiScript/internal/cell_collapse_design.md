# Cell collapse: one representation for cells and values

The 16-byte `script_value` (internal/value16_design.md) has the cells' own shape: an 8-byte payload and a word whose low byte
is the storage index. This note records what collapsing the VM's two representations into one takes, what landed, and what
the profiles say the rest is worth.

## Stage 1: a cell is laid out as a value (landed)

`vm_backend::cell_word` replaces the call record's three parallel arrays (`payload`, `kind`, `stamp`):

```
bytes 0..7    bits          int64 / a float's bits / a borrowed string node / 0 or 1 for a bool / an alias's element index
bytes 8..15   tag word      bits 0..7 the kind (0 empty, else the storage index); bits 9..63 the stamp (null when the kind is pinned)
```

It is `static_assert`ed to `script_value`'s size with the kind at offset 8, the value's index byte. What went:

- **Conversions where the words coincide.** A scalar enters a cell as the value's payload word
  (`script_value::unchecked_payload_bits`, `cell_bits`) and a flush writes it back as one word
  (`unchecked_set_payload_bits`), one path for int, float and bool where there were three. Only a bool is normalized (its
  value stores one byte); a string's payload word is its node, which is what the cell borrows. The per-kind ternaries and
  if-chains at the entry, the re-reads after a call, the landings, the member and element reads and the raw constants are one
  helper; `cell_flush_one` and `env_flush_one` are one in-place row and one boxing row each (`cell_scalar`).
- **One register for the block loop.** `cell_regs` carries one `cells` pointer where it carried `payload` and `kinds`; the lean
  lane likewise. In the `/Z7` build the block loop's dispatch head now keeps the image base in a register across dispatches
  (before: two `lea` per dispatched cell op; value16_design.md "S0").
- **Entry and raw-entry clears** are one `memset` of the cells in use (kind and stamp clear together).
- **The writeback scan** reads the dirty bytes as a bit mask (eight bytes gathered by one multiply), drops the dead temps with the
  liveness mask in one AND per 64 cells and splits environment cells by `env_mask`, instead of testing each dirty byte's cell.

Not changed: proofs, the landing rules, miss and flush protocols, the dirty bytes, the raw-call protocol, the jaibite (the
cells are run-time state only), the interpreter.

## Where the entry and exit time goes

RIP sampler over `/O2 /Ob2 /Z7` builds (no LTCG), OSSUARY scene 15, 100x40, one worker, two runs each; shares of all samples.

| function | master | stage 1 |
|---|---|---|
| `cells_run_frame` | 48.4% | 47.3% |
| `cells_enter` | 3.1% | 3.0% |
| `cells_writeback` (flushes inline) | 3.6% | 3.0% |
| `cells_sites_reenter` + `cells_restore_sites` + `inline_site_holds` | 3.2% | 3.3% |

Line attribution inside them (master):

- **The entry** is the environment cells: the memo-row check and the per-cell `env_operands` read for the `SK_*` globals the
  OSSUARY functions read in their loops (0.7 + 0.4 pp). The slot cells' guard and unboxing together are under 0.5 pp.
- **The writeback** was half the dirty-byte scan (most dirty cells at an exit are dead temps) and half the flushes. Stage 1's
  mask scan removed most of the first half; what remains (1.9 pp) is the flushes of slot and environment cells.
- **The site re-entry** (container sites, inlined leaves) is per entry and is not cell representation.

OSSUARY's entries are many because its hot functions (`sk_capsules`, `sk_glows`, `sk_skull`'s socket loop) are small, read
globals and arrays, and run a short loop per call: every call enters a block, re-validates its environment cells and sites,
and flushes on exit.

## Stage 2: slot-backed cells (designed, not built)

The mandate's end state: a pinned slot cell's storage is its window slot, so its entry copies nothing and its exit flushes
nothing. The slot's header (kind and stamp) is written once, at frame entry or the declaration; typed operations touch only the
payload word.

What it needs:

1. **One address space for every cell.** Cell operations index one base (`cells[k]`). For slot cells to be window slots, the
   compiler renumbers cells: a slot-backed cell takes its slot's index, every other cell (temps, which may carry one cell per kind
   on one slot; environment cells; a range index; an alias's element index) an index at or above `local_count`. Those must sit
   contiguous with the window, so a frame with cells reserves its extension above its locals. The extension holds cell words
   that are not values (a borrowed string node, an element index as a reference kind), so the general form's truncation, the
   debugger's locals walk and the window rebase must all skip it, or it must be cleared to null before any general code runs.
2. **The cell registers follow the stack.** The window lives in `stack_`, which reallocates when it grows (`rebase_window_pointers`,
   invariants §2b). A block's calls push their arguments onto it, so `cell_regs::cells` must be re-derived after every door, raw
   call and boundary, or the stack must reserve the frame's extension and its callees' argument space before a block runs.
3. **Raw frames get their window.** A raw-entered frame program has no built window ("Raw typed frames": built on first need).
   Slot-backed cells make its window its cells: the raw entry writes its parameters' headers and payloads into the reserved
   window and `build_raw_window` stops filling nulls over them.
4. **Stores that change a slot's kind or ownership stay full stores.** A cell that may hold a string, an adopted (`var`) kind or
   a reference stays an extension cell; only pinned `int`/`float`/`bool` slots are slot-backed, so a payload write never drops
   a refcount.

Expected value, from the profiles above: the slot flushes and the slot half of the entry, about 2-2.5% of OSSUARY. The larger
entry costs (environment cells, sites) are untouched by it; they go only when a call does not re-enter a block, which is research
item 2 (a frame program that holds its loops, entered raw with its environment reads and sites resolved), a compiler change in
`vm_compiler.cpp`'s region formation.

Points 1-3 change the frame layout contract (window size, what a window slot may hold, when the window exists), so they wait for a
ruling.

## Room for borrowed references (research item 4)

A `T&` to a field or element that never escapes (`ref_escaping` false) is meant to become a frame-local pair instead of a pooled
`reference_holder`. The cell word takes it without a layout change: the kind byte gets one new value outside the storage
indices (`k_cell_borrowed_ref`, which no `script_value` carries), the payload word holds the referent's address (the field's or
element's `script_value*`), and the tag word's high bits, which a borrowed reference needs for no stamp, hold the
re-validation key (the owning node or instance, 512-aligned, or a slot generation index) the pair checks before use. Under
stage 2 such a cell is an extension cell, never a window slot: the general form must not see a kind it cannot destroy, so a
flush materializes it as an ordinary field or element reference, as an alias cell (`TYPEID_REFERENCE` with an element-index
payload) is materialized by `cell_flush_one` today.

# The 16-byte value

`script_value` is 16 bytes on every host: an 8-byte payload and an 8-byte tag word.

```
bytes 0..7    payload       int64 / double / char / bool / one strong_ptr / weaker_ptr / parallel borrow
bytes 8..15   tag word      bits 0..7   storage index (TYPEID_*), read as one byte
                            bit  8      k_engine_only: the pointer above is the value's engine, not a type
                            bits 9..63  a 512-aligned pointer: the declared type (type_info) or the engine
```

## Decisions

- **One value type, not two.** The brief proposed a 16-byte internal value and a 32-byte host value converted at the
  boundary. Every host-facing container accessor (`script_array` elements, map entries, `as<std::vector<...>>`, signal
  arguments, archive reads) hands out `script_value&` into internal storage, so a second type would put a conversion
  (or a proxy) on every one of them and double the surface both backends and the binders compile against. The tagged
  word keeps one type, keeps `get_engine()` and `get_type_info()` as they were, and leaves the public API source
  compatible: no host, example or docs sample changes.
- **The engine is reachable from the value with no static state.** A stamped value's engine is its type's `owner`
  (every `type_info` the engine interns, and every chunk-owned declared type, records the engine that made it). An
  unstamped value with an engine (`script_value(std::monostate{}, eng)`, a value whose stamp was cleared) stores the
  engine pointer itself with `k_engine_only`. A value with neither (an AST literal) has a zero tag, as before. No
  table, no `thread_local`, no static lookup: multiple engines on multiple threads stay independent.
- **The declared type stays a pointer, not a 32-bit id.** A 32-bit id needs a table to resolve, and the table needs
  the engine, which the value no longer carries separately; the pointer resolves itself and keeps every hot guard
  (`type_info_raw() == expected`) a compare of one masked word. `type_info` and `engine` are `alignas(512)`, which
  frees bits 0..8 for the index and the flag while keeping every high bit of the pointer, so tagged heap pointers
  (Android's top-byte tags, MTE) survive untouched.
- **The index stays a byte load.** `storage::idx_()` reads the word's low byte (little-endian, static-asserted), so
  dispatch on the index is the same `movzx` it was. Payload assignments write the index byte and leave the type bits;
  copy and move constructors carry the whole word; the value's copy and move assignments set the tag bits before the
  payload, keeping the release-last order (invariants §2c).
- **The interpreter moves with the VM.** There is one value type, so both backends hold the same 16 bytes.
- **32-bit hosts.** The tag word stays 8 bytes (a 4-byte pointer above the index), so the value is 16 bytes there too.

## Costs

- `get_engine()` on a stamped value is one dependent load (`type_info::owner`). The VM and interpreter keep their own
  `engine_` and read it from context on hot paths; the value's engine is read on boundary and cold paths.
- `type_info` objects round up to 512 bytes each (the engine interns a few hundred).
- `set_engine` on a stamped value cannot name a different engine than its stamp's (Debug-asserted); no caller does.

## Ledger

| stage | sha | notes |
|---|---|---|
| 1 layout | 91a6c4a7e | 16-byte value. A/B vs 3f2f97007, 5 rounds: dict -13/-8%, particles -6/-7%, sort +26/+29% (store-forwarding stall: a byte index store, then an 8-byte tag read), fib_recursive_dyn +18%, GLOOM +1.3%, OSSUARY +3.8%, Gator +3.0%, JaiDOOM 0% |
| 2 whole-word tag | 1905c36f8 | index written as the whole word; one-compare `stamped_as` at the VM landings; type_info slabs. A/B 5 rounds (busy machine, both sides ~10% slower in absolute terms): sort -16/-19%, dict -12/-9%, particles -7/-8%, fib_recursive_dyn -10%, queen_typed -6%; GLOOM +1.8%, OSSUARY +1.8%, JaiDOOM +5.3% (1.70 vs 1.79 ms/tic), Gator -0.8%; mandelbrot_dyn +9%, spectral_norm +5%, string2float_dyn +6%. Peak memory: container kernels -8..-19 MB, Gator +6.8 MB, JaiDOOM +1.6 MB, GLOOM +0.7 MB |

## Verdict at stage 2

This is below the landing bar. The container-heavy kernels win by 7-19%, but no whole program wins by 1.5%, and GLOOM,
OSSUARY and JaiDOOM regress past it. A RIP sampler over /Z7 builds shows the register-cell code (`cells_run_frame`, which
is 46-82% of those programs) running the same instructions under different register allocation: MSVC re-spills the
image base in the dispatch head of that 29 KB function. The value layout is not on that path, so the whole-program
deltas fall inside the per-build codegen variance of the cell loop. Gator's memory rises because every interned type
is now 512 bytes.

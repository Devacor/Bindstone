# Prelaunch correctness fixes — 2026-09-05

This batch addresses engine lifetime, native C++ conversions, malformed saved
destructuring declarations, and nested script-class deserialization. Numeric edge
cases and the broader performance work are deliberately separate.

## Decisions and compatibility

- Engines have stable addresses. The engine object is neither copyable nor movable;
  moving its `shared_ptr` owner remains supported. Existing values, callbacks, and
  backends retain their valid engine address. Script-value moves are unchanged.
- A registered C++ class converts only to its exact native type or a declared C++
  base. Real C++ cast functions adjust multiple/virtual-inheritance base pointers;
  transitive conversions compose those functions. Cast metadata is engine-local.
  An object holder without a registered native definition is no longer blindly
  interpreted as an arbitrary requested class. Bind the native class and its base
  relationships with `dynamic_binder` before using these conversion paths.
- Native references borrow the checked object. They do not require shared
  ownership, and by-value arguments can copy from borrowed objects. A requested
  `shared_ptr` must have actual ownership, either directly or recovered from the
  holder's weak owner. The resulting alias shares the original control block.
- Cast validation uses `checked_result` internally. Existing public C++ binding
  adapters translate errors into exceptions; both script backends consume the
  same adapters. Checked retired-layer errors deliberately omit the class name,
  avoiding inconsistent symbol/numeric formatting and error-time interning.
- JSON restores generic objects as string-keyed maps, including nested maps.
  Binary and Base64 keep generic maps untyped because their keys need not be
  strings. All three formats use the same recursive class-reconstruction walk,
  including classes inside arrays, maps, and fields. Unknown types retain the
  existing map fallback. No archive wire format changes.
- The `.jaibite` reader rejects destructuring name/slot count mismatches and
  missing initializers before execution. It uses the existing reader error
  mechanism. This is a targeted structural check, not a claim that every possible
  malformed AST is validated or that `.jaibite` is an untrusted-code sandbox.

## Performance boundaries

The arithmetic and bytecode dispatch loops are unchanged. Binding-time cached
class IDs retain a lookup-free exact-type receiver path. Immediate holder peeks
do not copy reference-counted handles. Typed native arrays cast each slot's actual
class without boxing the element; an `array<Base>` can contain different derived
classes with different base offsets. `script_value` retains its existing layout
and move guarantees.

## Regression coverage

`Prelaunch Regressions` covers nested class restoration in all three formats,
JSON key-type enforcement, checked errors, retired-layer adapter diagnostics,
multiple/virtual/transitive and abstract native bases, typed arrays of mixed
subclasses, unrelated-type rejection, reference arguments, and weak ownership.
`Core Engine` additionally pins the move traits and movable shared ownership.

## Verification

Final MSVC x64 Debug and Release builds succeeded, including the standalone
JaiScript tests/runner and the Bindstone Release integration build.

| Configuration | Interpreter | VM |
| --- | --- | --- |
| Debug | 2,607 passed | 2,607 passed |
| Release (benchmarks enabled) | 2,727 passed | 2,727 passed |

The batch adds 16 prelaunch regressions and two engine-lifetime tests. Both build
directories were reconfigured so the new test file participates in both backends.
The totals above also include the concurrent, separately owned fuzz-test changes
present in the working tree; those files are not part of this commit.

`Bindstone.exe -verifyassets` passed all 76 checks with exit code zero using the
final Release binary. It printed an SDL video-subsystem warning in headless mode,
but reported no failed asset checks. `git diff --check` also passed.

One initial concurrent Release run failed
`execute_file_stamps_stack_frame_file`. That existing test writes and removes
fixed filenames in the system temporary directory, which concurrent processes
share. The isolated test and subsequent serial full interpreter run both passed,
consistent with a fixture collision. Run full backend suites serially until those
fixtures are isolated per process. This batch does not change that test harness.

The borrowed-argument overload regression was an ownership-adapter issue: it
attempted a `shared_ptr` conversion before falling back to a reference, so rejecting
non-owning shared pointers prevented the fallback. Reference parameters now use
the checked borrowing path directly; actual `shared_ptr` parameters still require
ownership. The existing overload regression and the new borrowed-copy coverage
pass on both backends.

These runs establish correctness coverage, not a controlled before/after
performance comparison. Extreme numeric behavior remains unchanged for a separate
follow-up.

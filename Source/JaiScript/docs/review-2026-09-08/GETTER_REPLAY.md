# Getter replay across optimized fallback paths

A native property getter is observable. A speculative fast path that invokes it
and then retries the ordinary operation can change both side effects and results.

## Reproducer

Bind a getter-only native property that returns `array<int>` and counts reads.
Reset the count after reference/argument binding, inside this function:

```cpp
int read(array<int>& xs, int i) {
	clear_reads();
	int v = xs[i];
	return v;
}
read(box.values, 2);
```

The local-reference spelling `auto& xs = box.values` reproduces the same issue.
The getter returns `[10,20]` on its first two reads and `[10,20,30]` on the third.

| Backend / source | Proofs | Getter reads inside function | Outcome |
|---|---|---:|---|
| Interpreter | on/off | 1 | Bounds error for index 2, size 2 |
| VM 605b6520 | on/off | 2 | Same bounds error |
| VM d2dc8754 | off | 2 | Same bounds error |
| VM d2dc8754 | on | 3 | Returns 30 |
| VM c85ea0dd | on/off | 1 | Same bounds error as interpreter |

A constant getter also exposed the replay through its call count. In-range
indexing, direct `box.values[i]` and `return xs[i]` were passing controls.

## Correction and regression

`c85ea0dd` makes the affected lanes decline getter-backed references before
materializing them. The ordinary path performs the one logical read. The change
covers the older typed-declaration/store paths, typed loads, pushes and moves.
Admission checks must avoid invoking a getter that a fallback would invoke again.

`144c6343` adds the permanent growing-getter regression in
`source/tests/vm/getter_evaluation_tests.cpp`. It preserves the exact
indexed-declaration script shape and catches at the C++ boundary. Moving the
value through an extra script call can change escape analysis and select a
different compiler path, so the test keeps that conversion outside the script.

The final build passed 118 suites and 2752 tests on each Debug backend.
GLOOM300 frame `7fecc09815bb64e0` and state `3580805725` pass on both.
All 48 constant-getter and 16 growing-getter processes on the corrected build
agree between backends, with one getter read and preserved bounds errors.

## Evidence

- `Source/JaiScript/out/astra/getter-boundary-20260908/`: 96 constant-getter
  probes, old/new executables, source and results.
- `getter-boundary-20260908/growing/`: 32 probes showing the error-to-value
  divergence, with `RESULT.md` and `results.json`.
- `getter-fixed-20260908/getter-probes/`: 64 corrected probes.
- `getter-fixed-20260908/verification-audit.json`: final gates and control audit.
- `getter-fixed-20260908/regression-full-interpreter.log` and
  `regression-full-vm.log`: full Debug suite results.

These are behavioral checks. No performance or padding-band claim is made from
their execution times. The separate padded game comparison measures the retained
typed-load implementation together with this correctness repair.

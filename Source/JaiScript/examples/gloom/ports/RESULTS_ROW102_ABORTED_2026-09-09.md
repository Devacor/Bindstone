# Row 102 comparison stopped at the conformance gate

The comparison of `d6c9959b` and `4bd53021` did not complete. The padded candidate JaiDOOM runner exited with `0xC0000005` during warm-up in round 2. This attempt earns no whole-program performance credit. The failed row remains in the evidence; it is not converted into a busy-machine exclusion or removed to resume the timings.

## What ran

Both versions were archived from Git and built in isolated directories with the same Release flags. Fifty workload, host and build inputs were verified identical. The uninstrumented runners used the established pinned-P-core, warmed, normal/padded, three-round interleaving protocol for GLOOM serial, GLOOM eight workers and the JaiDOOM timedemo. The configured background limit was 1.0 core-equivalent; competing builds and workload processes were screened.

The attempt recorded 68 processes: 20 conformance gates, 43 accepted timing samples, four busy-machine exclusions and one failed process. GLOOM gates included 300 ticks at seed 666 (`7fecc09815bb64e0`, `3580805725`) and 600 ticks (`dfd969ed5a336dca`, `1503537018`). Every completed JaiDOOM replay matched `1746772898`.

Run 68 used the same padded candidate executable as successful gate 20 and timing runs 42 and 46. It exited with decimal code 3221225477, hexadecimal `0xC0000005`, after 3.877 seconds. The output ends after the warm-up timedemo header. There is no measured replay, timing result or final hash. No competing process was detected during that run; the measured background load was 0.967 core-equivalent.

Three earlier successful launches and one failed launch of this padded executable are too little exposure to estimate a failure probability. The normal candidate completed four launches in this attempt. Neither observation establishes that one layout is reliable.

## Crash attribution

Astra launched the exact preserved padded executable under an owned-process Windows debugger. Three ordinary debugger launches completed cleanly. With `_NO_DEBUG_HEAP=1`, four completed launches were followed by an access violation on attempt 5. This setting keeps debugger attachment from selecting the Windows debug heap; these are diagnostic runs, never benchmark samples.

The debugger recorded the first- and second-chance exception at executable RVA `0x4ad601`. Private-symbol resolution against the matching PDB identifies `vm_compiler::compile_binary`'s operand chooser at frozen source line 2520, with `vm_compiler::operand_expect` inlined at line 2264.

The chooser computes `operand_expect(static_cast<const binary_expr*>(e.get()))` before testing whether `e` is a subscript. Its inputs include identifier and literal leaves. Reading binary-expression metadata through those pointers can read beyond the actual node. The proposed correction computes the expectation only after the subscript-shape guard succeeds. Fable independently reported the same access with AddressSanitizer and owns the compiler correction and its gates.

The initial minidump write failed with `0x800703e6`: the diagnostic host used the wrong packing for `MINIDUMP_EXCEPTION_INFORMATION`. That file is invalid and is not evidence of a captured stack. The SDK specifies four-byte packing. A corrected capture attempt was stopped once Fable's sanitizer result independently confirmed the exact fault location. The exception record and PDB-resolved instruction address are valid evidence; no full stack is claimed.

The workload directory's 15 generated `.jaibite` AST cache files were preserved before diagnostic retries. Their presence is recorded, not asserted as the cause. The fault is in compilation, including lazy compilation on a function's first call, so appearing after the timedemo header does not place it in VM instruction execution.

## Evidence and next gate

Paths below are relative to `Source/JaiScript/out/astra/row102-audit-20260909/`:

- `comparison/metadata.json`, `comparison/runs.json`, `comparison/runner.py`, `comparison-run.log`: immutable inputs, exact commands and interrupted order.
- `comparison/068-jaiscript-w0-candidate-padding-timed.log`, `crash.json`: failed process.
- `candidate/release/bin/jaiscript_padding.exe` and `.pdb`, `candidate/source-inputs.json`: exact executable, symbols and frozen source.
- `build-flag-audit.json`, `workload-equivalence.json`, `baseline/source-inputs.json`: build and workload provenance.
- `diagnostic/debug-runs.json`, `diagnostic/no-debug-heap/debug-runs.json`, `diagnostic/no-debug-heap/fault-symbols.jsonl`: diagnostic launches, exception records and private-symbol resolution.
- `diagnostic/packed-dump/STOPPED.json` and `stopped-controller.json`: termination of only Astra's owned debugger controller and child.
- `crash-caches/manifest.json`: preserved cache files and hashes.

A new audited window must use a frozen, gated compiler correction and complete all three rounds and both layouts. The old failure remains separately disclosed. No medians, padding bands or speedup percentages from this incomplete attempt are published as final results.

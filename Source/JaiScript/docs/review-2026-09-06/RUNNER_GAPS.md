# Combined GLOOM and JaiDOOM runner: operator checklist

What an operator meets on a fresh checkout when following
`examples/gloom/ports/README.md` (`f9d16f0e`), read from the scripts, not from a run.

## Before the first command

- [ ] Python 3.11 or newer on `PATH` as `python` (`build_comparison.ps1:20` calls it by that
      name; `prepare_comparison.py:42` relies on 3.11 tar behavior).
- [ ] An x64 Visual Studio Native Tools shell (`build_comparison.ps1:16` refuses without
      `cl.exe`). CMake and Ninja on `PATH`.
- [ ] Network access for `prepare_comparison.py`: it downloads Lua 5.4.8, Lua 5.5.1, sol2
      3.3.0 and LuaJIT `v2.1` and verifies pinned SHA256 hashes. An offline machine cannot
      build the Lua configurations; `--engines jai` still needs the JaiScript build the same
      script produces.
- [ ] The build root must be under `Source/JaiScript/out/astra/` (`build_comparison.ps1:14`
      throws otherwise). The README's `-OutputRoot` sentence says "another directory under
      `Source/JaiScript/out`"; the script enforces `out/astra`. One of the two should change.
- [ ] JaiDOOM inputs: `examples/jaidoom/wads/doom1.wad` is git-ignored
      (`examples/jaidoom/wads/.gitignore`) and fetched by the game's bootstrap
      (`examples/jaidoom/README.md:12-22`). The runner reads it directly
      (`run_comparison.py:198,227`) and hashes it as a workload input; on a checkout that
      never ran `bootstrap.bat`, the timedemo workload fails before any timing. The README
      of the runner does not mention the bootstrap step.

## Elevation

- [ ] Timing runs do not need elevation.
- [ ] `capture_profile.ps1` requires an Administrator PowerShell (`#Requires
      -RunAsAdministrator`) because WPR starts a kernel session; it refuses when another WPR
      recording is active. Astra's report states this wrapper was inspected but never run end
      to end. The ETL-to-table step (`profile_process.ps1` plus the xperf commands quoted in
      `docs/GLOOM_PROFILE_2026-09.md:347-348`) needs the Windows Performance Toolkit
      (`xperf.exe`) and symbol resolution against the matching PDBs; neither prerequisite is
      listed in the README.

## While it runs

- [ ] The quiet gate (`run_comparison.py:99`) recognizes competitors by process name:
      `cl.exe`, `link.exe`, `ninja.exe`, `msbuild.exe`, `bindstone.exe`, and names starting
      with `jai_gloom`, `gloom_lua`, `jaiscript`. A Foundry run (`jaiscript_tests.exe`) is
      caught by the prefix; a fuzz run (`jaiscript_fuzz.exe`) too. Not recognized: the
      `codex`/`node` agent processes, Python scripts, a browser, a second copy of this
      runner. The background-core ceiling (default 0.20 cores, `run_comparison.py:171`) is
      the only defense against those; the README's "Timing runs require a clear
      build/test/benchmark process list" should name the list.
- [ ] A rejected sample is kept in `runs.json` and excluded from `summary.json`; the run
      does not stop or retry. An operator has to read `summary.json`'s accepted count to
      know whether three rounds survived. A final line stating accepted/total per
      configuration would make an incomplete session visible.
- [ ] The default output directory `out/astra/whole-program` is reused between sessions
      only if the operator changes it; the README says to use a fresh directory but the
      script does not refuse a non-empty one.

## Reading the result

- [ ] `differences.json` reports candidate deltas per layout; the README correctly says a
      delta above zero is not an established improvement. The document that quotes a result
      should carry both layouts' deltas and both padding bands, as `STACK_COOKIE.md` does.
- [ ] The padding control changes one cold function by 40 statements. It bounds placement
      variation of that one function's neighborhood; it is not a whole-binary layout sweep.
      Numbers inside the band are noise; numbers outside it are still one machine's result.

## Suggested README additions

1. The `bootstrap.bat` prerequisite for the timedemo workload, and that `doom1.wad` is not
   in the repository.
2. The `out/astra` restriction as written in the script.
3. The competitor name list and that agents, editors and Python are not on it.
4. WPT (`xperf`) and PDB requirements for the profile path, and that the wrapper's end-to-end
   run is unverified.
5. An "accepted N of M" summary line in the runner (a code change in `run_comparison.py`,
   Astra's file).

# Bindstone integration check, 2026-09-06

Release client source: master `423e5d8a18eefe949fafeee3d5bfdbc5e3cdca54`.
This is the committed source at the start of the integration check, including
the three container/aggregate contract changes in the brief. Later typed-region
commits are not part of this client binary.

| Command | Exit | Observed result |
|---|---:|---|
| `Bindstone.exe -verifyassets` | 0 | 76 checks passed, 0 failed |
| `Bindstone.exe -loginshot 30` | `0xC0000005` | Login screenshot saved; 30/30 frames completed; native teardown access violation |
| `Bindstone.exe -workbenchsmoke` | 0 | 120 frames completed |

No script or asset failed with a diagnostic from aggregate handle sharing,
nested container enforcement or checked float-to-int conversion. There is no
unambiguous script fix to commit from these runs.

## Native teardown failure

The exact completion and debugger diagnostics were:

```text
Game frames completed (30/30)
SECOND_CHANCE code=0xc0000005
operation=0 target=0xffffffffffffffff
PROCESS_EXIT 0xc0000005
```

A second and third execution under a private debugger reproduced the failure.
The resolved instruction is `Bindstone+0x1e1b3`,
`jai::class_definition::unregister_instance+0x33`,
`class_definition.hpp:1997` in the archived source. The physical stack proceeds
through `class_instance` destruction, captured script-function destruction,
signal receiver/emitter destruction, `Managers::~Managers`, and
`SDL_main` at `main.cpp:965`. The raw log preserves all symbol names and
addresses. Link-time identical-code folding can give shared destructor bodies
an unrelated template specialization's name; those specialization names do not
identify the actual signal payload.

Classification: native lifetime/teardown failure, requiring engine/integration
investigation. It is not evidence of a rejected script or asset. The stack is
consistent with a captured script object outliving the class registry it uses
during destruction. This is an inference, not a proven allocation lifetime
trace or attribution to one of the three contract changes.

Question for the integration owner: which owner must release these retained
script receivers before their engine/class registry is destroyed? Check the
`GameEditor` and `Managers` destruction sequence against the dump before
choosing a fix. No engine code was changed.

## Other diagnostics

The successful headless asset gate wrote:

```text
SDL Error: Video subsystem has not been initialized Line: 785
```

The graphical runs wrote the existing image warning:

```text
libpng warning: iCCP: known incorrect sRGB profile
```

Neither message names a script type violation. The screenshot was inspected:
the Login panel, email/password fields and Login button rendered.

## Build and evidence

All paths below are relative to `Source/JaiScript/out/astra/integration-20260906/`.

- `source-commit.txt` and `source.tar`: fixed source identity. `tree/` is
  a plain archive extraction, not a git worktree. Its `External` junction
  points at the existing dependency sources; no dependency sources were edited.
- `build.ps1`, `link-path.targets` and project build logs: MSVC 19.51,
  v145, Release/x64. External, SDL, MV and client built separately with
  `BuildProjectReferences=false`, four compiler jobs and isolated object/output
  directories. The local link-directory override resolves the isolated libraries.
- `bin/Bindstone.exe` and `bin/Bindstone.pdb`: tested client and symbols.
  The existing repository `libcrypto-1_1-x64.dll` was copied beside the binary.
  An initial launch without that dependency returned `0xC0000135`; it did not
  execute the client and is not an asset failure.
- `runs.json`, `verifyassets.log/.err`, `loginshot.log/.err`,
  `workbenchsmoke.log/.err`: direct process exits and complete diagnostics.
- `run/login_shot.bmp`: isolated screenshot; the repository-root screenshot
  was not overwritten. The run used an Assets junction and private temp/appdata
  directories. No tracked asset changed.
- `login-debug.log`, `login-teardown.dmp`, `crash_capture.cpp` and
  `debug-login.ps1`: reproducible native exception and stack capture.

Verified by running: Release build, all three commands, screenshot, repeated
native exception and private-symbol stack. Inferred from reading: a lifetime
ordering problem is the likely category; causation remains open. No timing
claim or padding-band claim is made for these correctness runs.

Landing checks: full Debug Foundry suites passed on interpreter and VM
(116 suites, 2740 tests each). Both Debug GLOOM backends reproduced frame
`7fecc09815bb64e0` and state `3580805725` at 300 ticks, seed 666.
Logs: `out/astra/debug-brief-interpreter.log`,
`out/astra/debug-brief-vm.log` and `out/astra/gloom-brief-300.log`.

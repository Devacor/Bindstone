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

## Host teardown order

The ownership chain behind the stack, read from the source the client was built from
(`Source/Game`, `Source/SolutionSpecific/BindstoneClient`):

| Step | Owner | Anchor |
|---|---|---|
| The login screen connects named script receivers to the lobby signals: `[self]` captures the page object, `[email, password]` capture its fields | `Assets/Interface/Login/initialize.script:18`, `Assets/Interface/Login/loginButton.script:6`, `Assets/Interface/interfaceManager.script:7` | script |
| Those signals are the three `jai::signal_emitter` members of `StandardMessages`, exposed to script as the `messages` global by a non-owning `shared_ptr` | `Source/Game/managers.h:20-24`, `Source/Game/game.cpp:17-19`, `Source/Game/game.cpp:51` | host |
| `StandardMessages messages` is a member of `Managers` | `Source/Game/managers.h:44` | host |
| `Managers managers` is declared before `Game game` in `GameEditor`; `Game` constructs the engine from `managers.services` and owns it as `jaiEngine_` | `Source/Game/gameEditor.h:95`, `Source/Game/gameEditor.h:105`, `Source/Game/game.cpp:44-47`, `Source/Game/game.h:140` | host |
| Inside `Game`, `jaiEngine_` is declared after `gameData`, `ourGui`, `ourInstance` and `rootScene` | `Source/Game/game.h:134-140` | host |
| `-loginshot` builds one `GameEditor` on the stack and returns | `Source/SolutionSpecific/BindstoneClient/main.cpp:956-957` | host |

Members destroy in reverse declaration order, so `GameEditor`'s teardown runs `editor`,
then `game` (whose `jaiEngine_` is the engine's last owner: the class registry and every
`class_definition` die here), then `managers`, whose `messages` emitters destroy the named
receivers, whose script callables destroy their captured environments, whose
`class_instance` values reach into the destroyed definitions. That is the stack in the dump.
The same inversion exists one level down: `rootScene`, `ourGui` and `ourInstance` are
destroyed after `jaiEngine_`, so scene components holding script receivers (`Clickable`
callbacks, creature scripts) sit on the same edge whenever they capture a script object.

Engine side, landed in the JaiScript tree: `class_definition::~class_definition` orphans its
live instances (`class_def_` and `registry_slot_` reset), so an instance that outlives its
definition destroys without touching the registry. Regression test
`script_objects_outliving_their_engine_destroy_safely` (`source/tests/language/prelaunch_regression_tests.cpp`)
keeps three script values (an instance, a closure capturing one, nested containers of them)
past `engine` destruction and drops them.

Host side, the smallest ordering fix (proposed, not applied): let the engine outlive every
host member that can hold a script value.

```cpp
// Source/Game/game.h: move the engine to the FIRST member so it is destroyed last within Game
class Game {
	...
private:
	std::shared_ptr<jai::engine> jaiEngine_;   // was after rootScene (game.h:140)
	GameData gameData;
	std::unique_ptr<MV::InterfaceManager> ourGui;
	std::unique_ptr<GameInstance> ourInstance;
	std::shared_ptr<MV::Scene::Node> rootScene;
	...
public:
	std::shared_ptr<jai::engine> scriptShared() const { return jaiEngine_; }
};

// Source/Game/gameEditor.h: a keep-alive declared BEFORE managers, filled in the ctor body
class GameEditor {
	...
	std::shared_ptr<jai::engine> engineKeepAlive_;   // first member: destroyed after managers
	MV::TapDevice mouse;
	Managers managers;
	...
};

// Source/Game/gameEditor.cpp, end of GameEditor::GameEditor's body
engineKeepAlive_ = game.scriptShared();
```

`Game::Game` initializes `jaiEngine_` from its `Managers&` argument only, so the move is
legal (members initialize in declaration order; nothing before it is read). The keep-alive
cannot be an initializer (`managers` would not exist yet), hence the body assignment. With
both in place the engine dies after `managers.messages`, `rootScene`, `ourGui` and
`ourInstance`, and the receivers' script objects unregister against a live registry. The
engine-side fix makes the client safe without this change; the host change removes the
inversion for every future capture, including host code that runs script from a destructor.

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

# Bindstone Development Guide

Bindstone is a game engine built on MutedVision (MV) framework. Dev builds this in Visual Studio.

## Building

### For Bindstone Integration Work

**Solution file:** `C:\git\Bindstone\bindstone.sln`

**Main targets:**
- `BindstoneClient_Windows` - Main client executable
- `MutedVision_Windows` - Core engine library

**Building from the command line WORKS** (build a project's `.vcxproj` directly and pass
`/p:SolutionDir=` — the include paths use `$(SolutionDir)`, which is only the reason a
naive invocation fails). Example (verified, ~35s clean rebuild of the engine lib):

```bash
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d C:\git\Bindstone && msbuild \"VSProjects\MutedVision\MutedVision_Windows\MutedVision_Windows.vcxproj\" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=C:\git\Bindstone\ /m 2>&1'"
```

Use `/t:Rebuild` for a clean build. MSBuild auto-resolves project-to-project references.
This lets Claude verify JaiScript↔Bindstone integration without a round-trip through the IDE.
(Visual Studio still works too; Dev can build/run there as usual.)

When fixing integration issues between JaiScript and Bindstone:
1. Claude modifies the necessary files
2. Build via the command above (or Dev builds in VS) to verify
3. Iterate as needed

### For JaiScript-Only Work

When adding features or fixing bugs in JaiScript itself (not integration), use the JaiScript build system:

```bash
# Build JaiScript tests (Release BENCHMARKS config)
powershell.exe -Command "cmd /c 'call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cd /d \"C:\git\Bindstone\Source\JaiScript\out\build\x64-Release BENCHMARKS\" && \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe\" --build . --target jaiscript_tests 2>&1'"

# Run JaiScript tests
powershell.exe -Command "cd 'C:\git\Bindstone\Source\JaiScript\out\build\x64-Release BENCHMARKS'; ./bin/jaiscript_tests.exe 2>&1"
```

See `Source/JaiScript/CLAUDE.md` for detailed JaiScript development instructions.

## Tests & Benchmarks (MV/engine)

The engine has a Foundry-based regression runner separate from JaiScript's:

- **`Source/MV/Tests/`** — `build_tests.bat` builds `mv_tests.exe` (JaiScript Foundry framework +
  `main_test_runner.cpp`, linked against `mutedvision.lib`). Suites guard the matrix and
  scene-graph transform invariants, including a deterministic **perf gate**
  (`recalculateMatrixCalls == 0` over idle frames of a static `Clipped` subtree). Run:
  `Source\MV\Tests\mv_tests.exe --verbose`. See `Source/MV/Tests/README.md`.
- **`Source/MV/Bench/`** — standalone micro/scene/frame benchmarks (`bench_math`, `bench_scene`,
  `bench_frame`) with before/after numbers in `RESULTS_math.md`. These measure; they don't assert.

Both link the engine the same way (`/MD /DSDL_MAIN_HANDLED` + gl3w.c +
SDL/Win libs); the renderer is constructed headless via `Draw2D::makeHeadless()`.

## Project Structure

- `Source/MV/` - MutedVision engine (rendering, UI, AI, networking)
- `Source/JaiScript/` - JaiScript scripting language (has its own CLAUDE.md)
- `Source/Game/` - Game-specific code
- `Source/Editor/` - Level editor code
- `External/` - Third-party dependencies (glm, asio, gl3w, Box2D, spine, openssl, etc.)

## Key Patterns

### Signal System (jai::signal)

MV uses JaiScript's signal system for event handling:

```cpp
// In header - private emitter, public signal
class MyClass {
private:
    jai::signal_emitter<void(std::shared_ptr<MyClass>)> onEventSignal;
public:
    jai::signal<void(std::shared_ptr<MyClass>)> onEvent;

    MyClass() : onEvent(onEventSignal) {}

    void triggerEvent() {
        onEventSignal(shared_from_this());
    }
};

// Connecting to signals
myObj->onEvent.connect([](std::shared_ptr<MyClass> obj) {
    // Handle event
});
```

### Type aliases
- `jai::signal<T>::shared_receiver_type` - Type for holding signal connections
- `jai::receiver<T>::shared_type` - Same as above (snake_case)

## Code Style

Match the surrounding engine, which is terse:
- **Names carry the meaning.** Internal helpers get long descriptive names (`deviceBackSharedCache`,
  `reloadedPackedTextureChild`); public/fluent APIs stay short and chainable (`texture()`, `bounds()`,
  `blend()`). Prefer a clear name over a comment.
- **Few comments.** Only explain non-obvious *why* (a gotcha, an invariant, a deliberate deviation),
  in one line. No narration of *what* the code does, no change-history/war-story comments, no banners.
- Match local indentation (tabs in most MV/scene files).

## Rendering / texture lifetime (RHI port)

- Texture **data** (decoded surface) is separate from its GPU **binding** (`BoundTexture` or legacy
  GL name). `LoadedTextureData::bindToDevice()` builds the binding from the retained surface, then
  releases the surface — the binding is the live copy.
- **Recovery is reload(), not a retained surface.** If the binding is lost (context loss) or all
  handles drop (refcounted `globalLookup`, so the entry frees and re-loads on next use), recovery goes
  through `reloadImplementation()`: re-decode for file textures, regenerate for dynamic/surface
  textures (their custom load option / `surfaceGenerator`). Any change here must keep reload able to
  reconstruct from source.

## Git Commits

- One-line commit messages only (title, no body). Match the style of `git log --oneline`.
- No co-author trailers.

## Git Safety

- NEVER checkout files without explicit permission
- NEVER force push or amend pushed commits
- Dev handles all git operations

# Bindstone Development Guide

Bindstone is a game engine built on MutedVision (MV) framework. Dev builds this in Visual Studio.

## Building

### For Bindstone Integration Work

**Solution file:** `D:\git\Bindstone\bindstone.sln`

**Main targets:**
- `BindstoneClient_Windows` - Main client executable
- `MutedVision_Windows` - Core engine library

**Building from the command line WORKS** (build a project's `.vcxproj` directly and pass
`/p:SolutionDir=` — the include paths use `$(SolutionDir)`, which is only the reason a
naive invocation fails). Example (verified, ~35s clean rebuild of the engine lib):

```bash
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d d:\git\Bindstone && msbuild \"VSProjects\MutedVision\MutedVision_Windows\MutedVision_Windows.vcxproj\" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=d:\git\Bindstone\ /m 2>&1'"
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
# Build JaiScript tests (Release)
powershell.exe -Command "cmd /c 'call \"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cd /d d:\git\Bindstone\Source\JaiScript\out\build\x64-Release && \"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe\" --build . --target jaiscript_tests 2>&1'"

# Run JaiScript tests
powershell.exe -Command "cd 'd:\git\Bindstone\Source\JaiScript\out\build\x64-Release'; ./bin/jaiscript_tests.exe 2>&1"
```

See `Source/JaiScript/CLAUDE.md` for detailed JaiScript development instructions.

## Project Structure

- `Source/MV/` - MutedVision engine (rendering, UI, AI, networking)
- `Source/JaiScript/` - JaiScript scripting language (has its own CLAUDE.md)
- `Source/Game/` - Game-specific code
- `Source/Editor/` - Level editor code
- `External/` - Third-party dependencies (ChaiScript, Cereal, SDL, etc.)

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

## Git Safety

- NEVER checkout files without explicit permission
- NEVER force push or amend pushed commits
- Dev handles all git operations

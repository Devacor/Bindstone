# AGENTS.md — Bindstone / MutedVision

Bindstone is a game built on the MutedVision (MV) engine, scripted in JaiScript. Dev builds and runs it in Visual
Studio; agents build and verify it from the command line (below).

## For agents
- Many folders have their own AGENTS.md for subsystem details; `Source/JaiScript/AGENTS.md` covers the language.
- This file covers what cuts across folders: building, gates, style, serialization, git.
- When in doubt, check existing code patterns in similar files.
- Change, build, run the gate that covers the change, iterate. A JaiScript change that MV consumes is verified by
  building `MutedVision_Windows` (and the client when game code is involved), not only JaiScript's own suite.
- While on a task, point out code that egregiously breaks the style guide; fix it and say so when the section is small.
- Internal notes: `Source/JaiScript/internal/` holds the tracked design notes (invariants, typed regions, proofs, ...).
  `.internal/` (repo root: Bindstone plans and handoffs) and `Source/JaiScript/.internal/` (ledgers, censuses, perf
  records) hold gitignored status documents; they exist only in the main checkout, not in worktrees, and nothing tracked
  may point at them.

---

## Repository at a Glance

| Path / Folder   | Description |
|-----------------|-------------|
| `Source/`       | All custom game and engine code |
| `Source/MV`     | MutedVision (MV) game engine with all core systems |
| `Source/Game`   | Bindstone-specific game code. Client and servers share one code base; server-only code is wrapped in `#ifdef BINDSTONE_SERVER` |
| `Source/Editor` | Legacy in-game editor (scene/BindSnap authoring), kept until `Source/Workbench` reaches authoring parity |
| `Source/Workbench` | The editor: dockable panels, reflection-driven inspector, undo/redo, gizmos, hot-reloaded `.jai` panels (`Bindstone.exe -workbench`) |
| `Source/JaiScript` | JaiScript: the scripting language, property system and archives everything above serializes through (own AGENTS.md, CMake build, test suite) |
| `Source/JaiScriptComparisons` | JaiScript against ChaiScript, Lua, LuaJIT, Luau and Squirrel: the game ports, `comparison_benchmarks`, the kernel suite's Lua side (own CMake project that adds `Source/JaiScript`; its README has the path map) |
| `Source/SolutionSpecific` | Entry points for each executable (client, Android client, game server, lobby server) |
| `Assets/`       | Art, JSON catalogs, Spine animations, `.scene`/`.bindsnap` files, JaiScript `.script` gameplay/UI scripts and `.jai` Workbench panels |
| `Tools/`        | Generators and harnesses (`Creatures/generate_creatures.py`, `Balance/arena.py`, `LocalServer/`) |
| `VSProjects/`   | Windows/Android Visual Studio projects |
| `External/`     | Third-party libs (Box2D, Asio, glm, gl3w, spine, openssl, libpqxx, Vulkan-Headers, …). Do not make changes in this folder. |

## Building

Solution: `<repo>\Bindstone.sln` (`<repo>` = this checkout; the drive letter differs per machine, so substitute it in
every command below). Main targets: `BindstoneClient_Windows` (the client) and `MutedVision_Windows` (the engine lib).

Build a project's `.vcxproj` directly and pass `/p:SolutionDir=`: the include paths use `$(SolutionDir)`, which is the
only reason a naive invocation fails. MSBuild resolves project-to-project references; `/t:Rebuild` builds clean.

```bash
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d <repo> && msbuild \"VSProjects\MutedVision\MutedVision_Windows\MutedVision_Windows.vcxproj\" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=<repo>\ /m 2>&1'"
```

Swap in `VSProjects\BindstoneClient\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj` for the client.

Configurations (x64):
- `Release` is the edit loop. `BindstoneFastIteration` (`<repo>\Directory.Build.props`, default `true`) builds it
  without `/GL`, `/LTCG` and `/OPT:REF,ICF` and links incrementally: a one-file edit takes seconds instead of over a
  minute, and the client runs a few percent slower than Shipping. `/p:BindstoneFastIteration=false` puts LTCG back
  into Release (the `/GL` flip recompiles everything).
- `Shipping` is the full-LTCG build (`/GL`, `/LTCG`, `/OPT:REF,ICF`, non-incremental link; output
  `Builds\Windows\x64\Shipping\`). It is what ships, and Bindstone performance numbers (arena sim, pathfinding bench,
  frame timing) are measured on it.
- MutedVision compiles JaiScript's `Source\JaiScript\source\implementation\**\*.cpp` as separate translation units
  through one wildcard item, so a JaiScript edit recompiles one file. `source\jaiscript.cpp` is the one-file build for
  other hosts and is not part of the Windows build; JaiScript's CMake configure fails when it, the files on disk and
  `JAISCRIPT_SOURCES` disagree. Any other new `.cpp` goes into the right `.vcxitems` (see `VSProjects/AGENTS.md`).

### Game gates

The client (`Builds\Windows\x64\<Config>\Bindstone.exe`, run from `<repo>`) carries headless self-checks; the exit code
is the verdict:
- `-verifyassets` — loads and round-trips every scene, `.bindsnap` and catalog, evaluates the gameplay scripts and
  static-checks the interface scripts (details in "The gate" below).
- `-loginshot [frames]` — renders the Login page for N frames and writes `login_shot.bmp`.
- `-workbenchsmoke` / `-workbenchshot` — 120-frame Workbench self-test; the latter writes `workbench_shot.bmp`.

### JaiScript work

JaiScript has its own CMake build and test suite; `Source/JaiScript/AGENTS.md` has the workflow. In short:

```bash
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d <repo>\Source\JaiScript && cmake --build out/build/x64-Debug --target jaiscript_tests 2>&1'"
<repo>\Source\JaiScript\out\build\x64-Debug\bin\jaiscript_tests.exe
<repo>\Source\JaiScript\out\build\x64-Debug\bin\jaiscript_tests.exe --backend=vm
```

The JaiScript CMake configurations are separate from the Bindstone ones and unaffected by them.

## Script Debugging (JaiScript DAP)

Script debugging is on by default (localhost only, free until a session attaches). Each process listens for VS Code on
127.0.0.1: Client 52472 / LobbyServer 52473 / GameServer 52474 (extra engines in a process take the next free port in
+10 steps; the bound port is logged at startup). Attach: install `Source/JaiScript/tools/vscode-jaiscript`, F5 →
"Attach to JaiScript" with that port. Both backends debug identically; `MV_SCRIPT_INTERPRETER=1` forces the
interpreter. Opt out: `--no-script-debug` (client) or `MV_SCRIPT_NO_DEBUG=1`. `MV_SCRIPT_HOT_RELOAD=1` re-evaluates
gameplay `.script` files when they change. Wiring: `MV::makeScriptEngine` (`Source/Game/Script/script.cpp`), knobs in
`Source/MV/Script/script.h`.

## Tests & Benchmarks (MV/engine)

- `Source/MV/Tests/` — `build_tests.bat` builds `mv_tests.exe` (JaiScript's Foundry framework + `main_test_runner.cpp`,
  linked against `mutedvision.lib`). Suites guard the matrix and scene-graph transform invariants, scene serialization,
  textures, pathfinding and script debugging, including a deterministic perf gate (`recalculateMatrixCalls == 0` over
  idle frames of a static `Clipped` subtree). Run `Source\MV\Tests\mv_tests.exe --verbose`; see its `README.md`.
- `Source/MV/Bench/` — standalone micro/scene/frame/path benchmarks with before/after numbers in `RESULTS_*.md`. These
  measure; they don't assert.

Both link the engine the same way (`/MD /DSDL_MAIN_HANDLED` + gl3w.c + SDL/Win libs); the renderer is constructed
headless via `Draw2D::makeHeadless()`.

## Style Guide
* **Tabs** for indentation (never spaces). *If you see mixed spaces: fix the block.*
* camelCase for *most things* (members, locals, functions). Function parameters get `a_` prefix: `int a_count`.
* ALL_CAPS for defines, global variables, and enums
* CapitalFirstCamelCase for type names (enum, class, struct)
* Prefer uniform `{}` initialization.
* **1TBS** One True Brace braces – opening brace stays on same line. Do not omit braces for single statments.
* When calculating intermediate results in functions use `result` as the local variable name that will `return`.
* **Names carry the meaning.** Internal helpers get long descriptive names (`deviceBackSharedCache`,
  `reloadedPackedTextureChild`); public/fluent APIs stay short and chainable (`texture()`, `bounds()`, `blend()`).
  Prefer a clear name over a comment.
* **Few comments.** Only a non-obvious *why* (a gotcha, an invariant, a deliberate deviation), in one line. No
  narration of *what* the code does, no change-history comments, no banners.

<details>
<summary>Examples</summary>

- Basic function and if/else:
```
bool is_negative(int a_x) {
    if (a_x < 0) {
        return true;
    } else {
        return false;
    }
}
```
- Switch statement example:
```
std::string getDifficultyString(int a_difficulty) {
	std::string result; //When calculating intermediate results for later return in a function, use the variable "result"
	switch(difficulty){
		case 0:
			result = "EASY";
		break;
		case 1:
			result = "MEDIUM";
		break;
		case 2:
			result = "HARD";
		break;
		default:
			return "ERROR";
	}
	return result;
}
```
- Large variable initialization example:
```
std::string getDifficultyString2(int a_difficulty) {
	static std::map<int, std::string> difficulties { //end of line brace, then indent.
		{0, "EASY"},
		{1, "MEDIUM"},
		{2, "HARD"}
	};

	if (auto it = difficulties.find(level); it != difficulties.end()) { //Do feel free to use scoped assignments when it makes things like this easier.
		return it->second;
	}
	return "ERROR";
}
```
- For a trivial type with braces which can easily go on one line, that's okay to do something like std::vector sequence {0, 1, 2, 5, 7, 12};
- Example accessor on one line: int getValue() const {return value;}
- Example lambda formats:
```
void Function() {
	callAnotherFunctionWithCallback([](){std::cout << "this is fine to keep the call on a single line";});
}

void Function2() {
	auto inlineLambda = []() {
		std::cout << "Here let's just have it on more than one line if it looks more like a basic control block.";
	};
}

void Function3(){
	callAnotherFunctionWithComplexCallback([]() {
		//Here we imagine multiple complex lines.
		//The logic should look sort of like callAnotherFunctionWithComplexCallback is an "if" statement or control block.
		//Because we view it as a kind of "control block" we should match our other control block spacing.
	});
}
```
- Example of basic class:
```
class Cat {
public:
	Cat(std::string name, std::function<void ()> a_onMeow = {}) : //: on this line
		name(std::move(a_name)),    //member initializers set up like so (and indented)
		onMeow(std::move(onMeow)) {	//note the bracket on this line, then an empty space to separate the body of the function.

		std::cout << "Cat was constructed";
	}

private:
	int age {0}; // prefer inline defaults when appropriate like this when useful. It's okay to omit if the class is unlikely to have more constructors added, and already initializes the variable in all constructors.
	std::string name; // no reasonable default needed, just "" so no default value.
	std::function<void ()> onMeow;
};
```
- Header files should end with a new line. Generally speaking all files should.
- Serialization goes through JaiScript archives (next chapter); every level of a serializable class carries `save`/`load`/`load_and_construct`.
</details>

## Signals

MV uses JaiScript's signals: a private emitter and a public signal view over it. Serialized signals are
`JAI_SIGNAL_PROPERTY` (next chapter).

```cpp
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

myObj->onEvent.connect([](std::shared_ptr<MyClass> obj) { /* handle */ });
```

Hold a connection in `jai::signal<T>::shared_receiver_type` (same as `jai::receiver<T>::shared_type`).

## Serialization & Property System

Bindstone serializes through JaiScript's property system and archives (`Source/JaiScript`);
MV's wrappers live in `Source/MV/Serialization/serialize.h`. One declaration feeds the file
format, the Workbench inspector, script access and reflection.

### Declaring properties
```cpp
class Sprite : public jai::property_owner<Sprite, Drawable> {      // Derived first, then bases
	friend Node;
	friend jai::access;                                             // save/load stay protected
	...
	JAI_PROPERTY((std::string), componentId);                       // serialized member
	JAI_PROPERTY((int), count, 0);                                  // with a default
	JAI_NAMED_PROPERTY((uint16_t), "subdivisions", ourSubdivisions, 0); // file key differs from member
	JAI_TRANSIENT_PROPERTY((BoxAABB<>), localBounds);               // reflected, never written
	JAI_DELETED_PROPERTY((int), oldField);                          // consumes a removed key on load
	JAI_SIGNAL_PROPERTY(ButtonSignalSignature, onAccept);           // emitter onAcceptSignal + view onAccept; script receivers persist
};
```
Types are always parenthesized (commas in templates). Macros:
`Source/JaiScript/include/jaiscript/properties/macros.hpp` and `signals/signal_property.hpp`.
A `jai::property<T>` reads and assigns like the member it wraps (`sprite->points[0]`,
`ourTextures->find(0)` through a smart pointer).

### The save/load ladder
Every level owns a `property_mgr`; each level saves its own then calls the base (`sprite.h`):
```cpp
template<class Archive>
void save(Archive& archive, std::uint32_t const) const {
	property_mgr.save(archive);
	Drawable::save(archive, 0);
}

template<class Archive>
void load(Archive& archive, std::uint32_t const) {
	property_mgr.load(archive);
	Drawable::load(archive, 0);
}

template<typename Archive>
static void load_and_construct(Archive& ar, jai::serialization::construct<Sprite>& construct) {
	construct(std::shared_ptr<Node>());   // ctor args; the shared_ptr is registered before load runs
	construct->load(ar, 0);
	construct->initialize();
}
```
All levels write into one flat object; JSON loads seek each property by name, so unknown keys are
ignored and missing keys keep the constructor default (`JAI_DELETED_PROPERTY` only swallows a key
you want consumed). `Component` ends the chain with `archive(property_mgr)`. `Node` reads `nodeId`
first and constructs with the `Draw2D` from the `MV::Services` user context (`node.h`). Plain data
types (`NetworkAction` subclasses, `BattleEffectData`) implement `void serialize(Archive&)` over
`jai::serialization::make_nvp("key", member)`.

### Polymorphic identity
```cpp
static jai::registrar<MV::Scene::Collider, MV::Services, jai::bases<MV::Scene::Component>> _hookCollider("Collider",
	[](jai::dynamic_binder<MV::Scene::Collider>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	builder.auto_bind();
});
```
One registrar per concrete type, in its `.cpp` (`collider.cpp`, `clickable.cpp`), gives the script
name, the `$type` tag and the base links a load through `shared_ptr<Base>` needs. `jai::bases<...>`
lists the C++ bases an archive may hold the type as. Types with no registrar relate through
`jai::serialization::register_relation<Derived, Base>` (`textures.cpp`). A level with neither is
saved sliced as its nearest registered base.

### Entry points
- `MV::toJson` / `fromJson<T>` / `toBinaryString` / `fromBinaryString<T>` / `toBase64` (`serialize.h`)
  pull the `jai::engine` from `MV::Services`.
- `Node::load` / `loadBinary` / `saveJai` / `saveBinary` / `loadChild` (`node.h`) read and write
  `.scene` and `.bindsnap` files; the reader carries `MV::Services` as user context and a
  `LoadOptions` so `load_complete()` runs `postLoadStep()` once on the root.
- `serializable(false)` on a node or component keeps runtime-built objects (creatures, buildings,
  effects) out of the file.

### Wire-format contract
`{"$id": N, "$type": "Sprite", "$val": {...}}` at an object's first reference, `{"$id": N}` for every
later `shared_ptr` to it, `{"$ref": N}` for a `weak_ptr` (`componentOwner`). Properties are written in
`property_mgr` order (a `std::map`, so `childComponents` precedes `childNodes`): a Button's views carry
their payload under `activeView`/`idleView`/`disabledView` and the owner's `childNodes` entries are
id-only. Reading in an order other than the written one yields nulls, so never reorder
`childNodes`/`childComponents`, and never rename a property key without re-saving the corpus.

### The gate
`Bindstone.exe -verifyassets` loads every shipped `.scene`/`.bindsnap` headless, round-trips it
JSON→binary→JSON, compares the resaved `$type` multiset with the original (a sliced component shows
up here), round-trips `NetworkAction`s, loads the three catalogs, evaluates every gameplay `main.script`/
`mainClient.script`, static-checks every interface script and checks the Login button's script
receiver loads armed. Run it after any change to a property, registrar or loader.

### Rules
- `friend jai::access` on every level; `save`/`load`/`load_and_construct` stay protected.
- New level = `property_owner<Derived, Base>` + registrar + the three functions above.
- Adding a `JAI_PROPERTY` is free; renaming one changes the file key (`JAI_NAMED_PROPERTY` keeps
  the old key).
- Signals are `JAI_SIGNAL_PROPERTY`, never a hand-written property.
- `cloneHelper` copies what `clone` must carry; check it when adding a property.

## Rendering / texture lifetime (RHI port)

- Texture **data** (decoded surface) is separate from its GPU **binding** (`BoundTexture` or legacy GL name).
  `LoadedTextureData::bindToDevice()` builds the binding from the retained surface, then releases the surface — the
  binding is the live copy.
- **Recovery is reload(), not a retained surface.** If the binding is lost (context loss) or all handles drop
  (refcounted `globalLookup`, so the entry frees and re-loads on next use), recovery goes through
  `reloadImplementation()`: re-decode for file textures, regenerate for dynamic/surface textures (their custom load
  option / `surfaceGenerator`). Any change here must keep reload able to reconstruct from source.

## Git

- Dev handles all git operations. Never check out files without explicit permission; never force push or amend pushed
  commits.
- Commit messages are one line: a title, no body, matching `git log --oneline`. No co-author or other trailers.

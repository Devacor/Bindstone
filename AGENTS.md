# AGENTS.md — Bindstone / MutedVision

## For LLMs/Agents
- Many primary folders have their own AGENTS.md for subsystem details
- This file covers cross-cutting concerns (style, serialization)
- When in doubt, check existing code patterns in similar files

---

## Repository at a Glance

| Path / Folder   | Description |
|-----------------|-------------|
| `Source/`       | All custom game and engine code
| `Source/MV`     | MutedVision (MV) game engine with all core systems
| `Source/Game`   | Bindstone-specific game code. Client and servers share one code base; server-only code is wrapped in `#ifdef BINDSTONE_SERVER` |
| `Source/Editor` | Legacy in-game editor (scene/BindSnap authoring), kept until `Source/Workbench` reaches authoring parity |
| `Source/Workbench` | The editor: dockable panels, reflection-driven inspector, undo/redo, gizmos, hot-reloaded `.jai` panels (`Bindstone.exe -workbench`) |
| `Source/JaiScript` | JaiScript: the scripting language, property system and archives everything above serializes through (own CLAUDE.md) |
| `Assets/`       | Art, JSON catalogs, Spine animations, `.scene`/`.bindsnap` files, JaiScript `.script` gameplay/UI scripts and `.jai` Workbench panels |
| `VSProjects/`   | Windows Visual Studio solutions |
| `External/`     | Third-party libs (Box2D, Asio, glm, gl3w, spine, openssl/wolfssl, libpqxx, Vulkan-Headers, …). Do not make changes in this folder. |

## Style Guide
* **Tabs** for indentation (never spaces). *If you see mixed spaces: fix the block.*
* camelCase for *most things* (members, locals, functions). Function parameters get `a_` prefix: `int a_count`.
* ALL_CAPS for defines, global variables, and enums
* CapitalFirstCamelCase for type names (enum, class, struct)
* Prefer uniform `{}` initialization.
* **1TBS** One True Brace braces – opening brace stays on same line. Do not omit braces for single statments.
* When calculating intermediate results in functions use `result` as the local variable name that will `return`.
* While focusing on a primary task, also point out areas that do not conform to the style if it is egregious and needs a pass, or fix it and explain if it is a small section.

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
receiver loads armed. Run it
after any change to a property, registrar or loader.

### Rules
- `friend jai::access` on every level; `save`/`load`/`load_and_construct` stay protected.
- New level = `property_owner<Derived, Base>` + registrar + the three functions above.
- Adding a `JAI_PROPERTY` is free; renaming one changes the file key (`JAI_NAMED_PROPERTY` keeps
  the old key).
- Signals are `JAI_SIGNAL_PROPERTY`, never a hand-written property.
- `cloneHelper` copies what `clone` must carry; check it when adding a property.

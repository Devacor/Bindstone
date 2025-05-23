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
| `Source/Game`   | Bindstone Specific game code. Both client and server share a code base with server code wrapped in #ifndef BINDSTONE_SERVER #endif
| `Source/Editor` | All of the editor code. The editor allows for creating and modifying game assets similar to Unreal Engine or Unity editors, and focuses on prefabs and scenes (same concept in game.)
| `Assets/`       | Art, JSON, Spine animations, **ChaiScript** gameplay scripts |
| `VSProjects/`   | Windows Visual Studio solutions |
| `External/`     | Third-party libs (SDL2, Box2D, Asio, ChaiScript, cereal, glm, …) | Safe to ignore these unless referencing, do not make changes in this folder.

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
- We use the C++11 library cereal to serialize times, we typically want to include a version and use the versioned save/load, but can just use the versioned serialize if we aren't using properties in that class.

## Serialization & Property System

Bindstone uses a reflection-lite/data-driven layer on top of [cereal](https://uscilab.github.io/cereal/).

### Quick Reference
```cpp
// In class declaration (inheriting from Component/Drawable/etc):
MV_PROPERTY(type, name, default_value)              // Auto-serialized member
MV_OBSERVABLE_PROPERTY(type, name, default_value)   // + change notifications  
MV_DELETED_PROPERTY(type, name)                     // Consumes old data

// Access like normal members:
myComponent->position = Point<>(10, 20);
float opacity = myComponent->opacity;
```

### Core Concepts
- **`Property<T>`** - Wraps values, auto-registers with parent, handles serialization (`MV/Utility/properties.hpp`)
- **`PropertyRegistry`** - Container in each serializable class storing property pointers
- **`ObservableProperty<T>`** - Property variant that emits change notifications
- **`DeletedProperty<T>`** - Placeholder for removed fields to maintain compatibility

### Standard Serialization Pattern

Every serializable class should follow this pattern (see Drawable.cpp for reference):

```cpp
// In header file
class MyDrawable : public Drawable {
    // ... your properties ...
};

// In cpp file - ALWAYS include these for polymorphic types
CEREAL_REGISTER_TYPE(MV::Scene::MyDrawable);
CEREAL_CLASS_VERSION(MV::Scene::MyDrawable, 0);  // Start at 0, increment when structure changes
CEREAL_REGISTER_DYNAMIC_INIT(mv_scene_mydrawable);

// In class implementation
template <class Archive>
void save(Archive& archive, std::uint32_t const /*version*/) const {
    // Configure any conditional serialization here - optional and unlikely to be common
    points.serializeEnabled(serializePoints());  // Example from Drawable
    
    // Save base class AFTER configuration
    archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
    // Properties auto-save through base class chain
}

template <class Archive>
void load(Archive& archive, std::uint32_t const version) {
    // Handle version migration
    if (version == 0) {
        // Version 0 format
        points.serializeEnabled(serializePoints());
    }
    // Add more versions as needed
    
    // Load base class
    archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
}

// Required for cereal construction
template <class Archive>
static void load_and_construct(Archive& archive, cereal::construct<MyDrawable>& construct, 
                               std::uint32_t const version) {
    construct(std::shared_ptr<Node>());  // Construct with null owner
    construct->load(archive, version);   // Load data
    construct->initialize();             // Post-load init
}
```

### Version Migration Examples

```cpp
// When properties need different handling per version
template <class Archive>
void load(Archive& archive, std::uint32_t const version) {
    if (version == 0) {
        // Original format - manual key order for pre-property system
        properties.load(archive, {"shouldDraw", "texture", "position"});
    }
    else if (version == 1) {
        // Added new field
        properties.load(archive, {"shouldDraw", "texture", "position", "rotation"});
    }
    else if (version == 2) {
        // Renamed field
        properties.load(archive, {}, {{"texture", "textures"}});  // rename map
    }
    else {
        // Current version - properties handle themselves
        points.serializeEnabled(serializePoints());
    }
    
    archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
}
```

### Common Patterns

#### 1. Declaring Properties
```cpp
class MyComponent : public Component {
    MV_PROPERTY(std::string, name, "");
    MV_PROPERTY(Point<>, position, {});
    MV_OBSERVABLE_PROPERTY(float, opacity, 1.0f);
    
    // Optional custom clone behavior via lambda (4th parameter)
    MV_PROPERTY(std::vector<Item>, items, {}, 
        [](auto& src, auto& dst) {
            dst->clear();
            for (auto& item : src) dst->push_back(item.clone());
        });
};
```

#### 2. Observable Property Callbacks
```cpp
myComponent->opacity.onChanged.connect([](const float& newVal, const float& oldVal, bool fromLoad) {
    if (!fromLoad) {  // Only react to runtime changes
        std::cout << "Opacity changed from " << oldVal << " to " << newVal;
    }
});
```

### Key Rules
- **Always** include save/load/load_and_construct in classes derived from any class that is set up with a PropertyRegistry.
- **Always** only have one PropertyRegistry per class heirarchy in the base serializable level of that heirarchy.
- **Always** register polymorphic types (concrete/non-derivable types are excluded) with cereal (CEREAL_REGISTER_TYPE, etc.)
- **Always** start version at 0, increment for structural changes. If you migrate from a version without properties to one with properties, incrementing is needed. If you simply add new properties, you do not.
- Properties serialize by key name, not declaration order
- Call `properties.save(ar)`/`properties.load(ar)` only in the base class that owns the registry
- Use `cereal::base_class<ParentClass>(this)` to chain serialization
- Properties have many convenience pass-throughs: `sprite->name = "Bob"` `sprite->points[0]` (operator[] and operator() pass through), `sprite->ourAnchors->size()` (operator -> can access member functions and variables of its underlying class. If the underlying class is something like a shared_ptr then operator-> will call the underlying shared_ptr's object rather than requiring a second level of de-reference.)
- Do evaluate the cloneHelper function to see what the "clone" behavior should be for a given property. If a property was not cloned, it should override a do-nothing clone method. If a property is cloned, but does a deep copy, use that implementation in the Property and remove it from the cloneHelper.

### Common Mistakes to Avoid
- Don't create multiple PropertyRegistries in a class hierarchy
- Don't forget CEREAL_REGISTER_TYPE for polymorphic types
- Don't call properties.save/load in derived classes (only base) unless dealing with versioned migration (see Drawable::load for a complex example.)
- Don't make every member variable a property, save property creation for types we want to serialize (mentioned in the save/load functions.)
- Don't try to convert Signal or SignalRegister type variables to Property. They do need to be serialized in some cases (they save script bindings), however they have custom rules for how to do so and basically are best used with basic archive(cereal::make_nvp("onPress", onPressSignal)) (for example)

### When to Increment Cereal Class Version
- YES: Converting from manual serialization to properties
- YES: Changing fundamental structure
- NO: Adding new MV_PROPERTY fields
- NO: Adding new methods
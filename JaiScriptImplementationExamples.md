# JaiScript Implementation Examples

This document demonstrates how current ChaiScript .script files would be migrated to JaiScript, showing dramatic improvements in syntax, maintainability, and safety.

## Recent Updates ()

### New Function Declaration Syntax

JaiScript now supports multiple function declaration styles to accommodate different developer preferences:

```cpp
// Traditional C style
void printMessage(string msg) {
    print("{}\n", msg);
}

// Traditional C++ style with return type
int add(int a, int b) {
    return a + b;
}

// Modern C++ style with trailing return
auto multiply(int a, int b) -> int {
    return a * b;
}

// Scripting style with function keyword
function divide(float a, float b) -> float {
    return a / b;
}

// Auto return type deduction
auto calculate(int x) -> {
    return x * 2; // Return type deduced as int
}

// Void functions (no return arrow)
void doAction() {
    print("Action performed\n");
}
```

### Flexible Parameter Syntax

Parameters can now be declared in multiple ways:

```cpp
// C++ style (type precedes name)
auto add(int a, int b) -> int {
    return a + b;
}

// TypeScript/Kotlin style (type follows name)
auto concat(string: first, string: second) -> string {
    return first + second;
}

// Auto parameters
auto identity(auto x) -> {
    return x; // Type deduced from argument
}

// Ultra-concise syntax (:name is shorthand for auto: name)
auto double(:value) -> {
    return value * 2;
}

// Mixed styles in same function
auto process(int: count, string name, :data) -> {
    // count is int, name is string, data is auto
    return format("{}: {} items, data={}", name, count, data);
}
```

### Lambda Expression Enhancements

Lambdas also support all parameter syntaxes:

```cpp
// Traditional C++ style
auto adder = [](int a, int b) -> int { return a + b; };

// TypeScript style parameters
auto formatter = [](string: prefix, int: value) -> string {
    return format("{}: {}", prefix, value);
};

// Ultra-concise lambdas
auto doubler = [](:x) -> { return x * 2; };

// With captures
int multiplier = 10;
auto scale = [multiplier](:value) -> { return value * multiplier; };
auto scaleRef = [&multiplier](int: value) -> { return value * multiplier; };
```

## Overview of Current ChaiScript Issues

Based on analysis of the Assets/ directory, the current ChaiScript implementation suffers from several critical issues:

1. **"Goofy Map Variables" Workaround** - All properties and functions accessed via string keys (`self["property"]`) to avoid hot-reload duplicate definition errors
2. **Massive Code Duplication** - 8+ creature files with 99% identical logic
3. **No Compile-time Safety** - String-based access causes runtime errors on typos
4. **Poor IDE Support** - No autocomplete, refactoring, or go-to-definition
5. **String-based Polymorphism** - Fragile runtime dispatch via map lookups
6. **Complex Callback Syntax** - Verbose capture semantics and deep nesting

## Migration Examples

### 1. Eliminating "Goofy Map Variables" Workaround

**Current ChaiScript (Life_T1/main.script):**
```chaiscript
self.spawn = fun(self){
    self["tryToFindAndAttackTarget"] = fun(self, dt){
        var targets = self.enemiesInRange(20);
        if(!targets.empty()){
            var target = targets[0].networkId;
            var activeTarget = self["activeTarget"];
            if(activeTarget != target){
                self["activeTarget"] = target;
                self["attacking"] = true;
            }
        }
    };
    
    self["attacking"] = false;
    self["activeTarget"] = 0;
};

self.update = fun(self, dt){
    if(!self["attacking"]){
        if(!self["tryToFindAndAttackTarget"](self, dt)){
            // movement logic
        }
    }
};
```

**JaiScript Migration:**
```cpp
class CreatureBehavior {
public:
    bool attacking = false;
    int activeTarget = 0;
    
    bool tryToFindAndAttackTarget(float dt) {
        auto targets = self.enemiesInRange(20);
        if (!targets.empty()) {
            int target = targets[0].networkId;
            if (activeTarget != target) {
                activeTarget = target;
                attacking = true;
                return true;
            }
        }
        return false;
    }
    
    void spawn() {
        attacking = false;
        activeTarget = 0;
    }
    
    void update(float dt) {
        if (!attacking) {
            tryToFindAndAttackTarget(dt);
        }
    }
};

// Script-side instantiation with member functions
CreatureBehavior behavior;
self.spawn = [&behavior]() { behavior.spawn(); };
self.update = [&behavior](float dt) { behavior.update(dt); };
```

**Benefits:**
- ✅ **Compile-time safety** - typos caught during parsing
- ✅ **IDE support** - autocomplete, refactoring, go-to-definition  
- ✅ **Direct member access** - no string lookup overhead
- ✅ **Hot-reloadable** - JaiScript handles redefinition properly
- ✅ **Clean member functions** - spawn() and update() as proper methods

### 2. Eliminating Code Duplication with Inheritance

**Current ChaiScript (8+ identical files):**
```chaiscript
// Fire_T1/main.script - 50+ lines
// Water_T1/main.script - 50+ lines (99% identical)  
// Earth_T1/main.script - 50+ lines (99% identical)
// etc.

// Each file repeats the same targeting logic:
self["tryToFindAndAttackTarget"] = fun(self, dt){
    var targets = self.enemiesInRange(20);
    if(!targets.empty()){
        var target = targets[0].networkId;
        var activeTarget = self["activeTarget"];
        if(activeTarget != target){
            self["activeTarget"] = target;
            self["attacking"] = true;
        }
    }
};
```

**JaiScript Migration with Base Class:**

**Base/ElementalBehavior.jai:**
```cpp
class ElementalBehavior {
public:
    bool attacking = false;
    int activeTarget = 0;
    float attackRange = 20.0;
    int damage = 5;
    
    bool tryToFindAndAttackTarget(float dt) {
        auto targets = self.enemiesInRange(attackRange);
        if (!targets.empty()) {
            int target = targets[0].networkId;
            if (activeTarget != target) {
                activeTarget = target;
                attacking = true;
                return true;
            }
        }
        return false;
    }
    
    virtual void performAttack() {
        if (attacking && activeTarget != 0) {
            auto target = self.getTarget(activeTarget);
            if (target) {
                target.changeHealth(-damage);
            }
        }
    }
    
    virtual void spawn() {
        attacking = false;
        activeTarget = 0;
    }
    
    virtual void update(float dt) {
        if (!attacking) {
            tryToFindAndAttackTarget(dt);
        } else {
            performAttack();
        }
    }
};
```

**Fire_T1/main.jai:**
```cpp
class FireElementalBehavior : ElementalBehavior {
public:
    FireElementalBehavior() {
        damage = 8;         // Fire does more damage
        attackRange = 18.0; // Shorter range
    }
    
    void performAttack() override {
        ElementalBehavior::performAttack();
        self.createBurningEffect();
    }
    
    void spawn() override {
        ElementalBehavior::spawn();
        self.setElementalType("Fire");
    }
};

FireElementalBehavior behavior;
self.spawn = [&behavior]() { behavior.spawn(); };
self.update = [&behavior](float dt) { behavior.update(dt); };
```

**Water_T1/main.jai:**
```cpp
class WaterElementalBehavior : ElementalBehavior {
public:
    WaterElementalBehavior() {
        damage = 6;         // Water does moderate damage
        attackRange = 22.0; // Longer range
    }
    
    void performAttack() override {
        ElementalBehavior::performAttack();
        self.createSlowingEffect();
    }
    
    void spawn() override {
        ElementalBehavior::spawn();
        self.setElementalType("Water");
    }
};

WaterElementalBehavior behavior;
self.spawn = [&behavior]() { behavior.spawn(); };
self.update = [&behavior](float dt) { behavior.update(dt); };
```

**Benefits:**
- ✅ **50+ lines reduced to 15 lines** per creature
- ✅ **Single source of truth** for common behavior
- ✅ **Easy customization** via inheritance and overrides
- ✅ **Bug fixes propagate** to all creatures automatically
- ✅ **Proper member functions** - spawn() and update() as class methods

### 3. Improving Building System Polymorphism

**Current ChaiScript (Life/main.script):**
```chaiscript
// String-based polymorphism - fragile
self["T0"] = fun(self, dt){};
self["T1"] = fun(self, dt){
    self["nextSpawn"] -= dt;
    if(self["nextSpawn"] <= 0){
        self["nextSpawn"] = 5.0;
        self.spawn("Life_T1");
    }
};
self["T2_0"] = fun(self, dt){ /* identical to T1 */ };
self["T2_1"] = fun(self, dt){ /* identical to T1 */ };

// Runtime dispatch via string lookup
self.update = fun(self, dt){
    self[self.current().id](self, dt);
};
```

**JaiScript Migration with Proper Polymorphism:**

**Base/BuildingBehavior.jai:**
```cpp
class BuildingBehavior {
public:
    virtual void spawn() = 0;
    virtual void update(float dt) = 0;
    virtual string getType() const = 0;
};

class IdleBuilding : BuildingBehavior {
public:
    void spawn() override {
        // Initialize idle building
    }
    
    void update(float dt) override {
        // Do nothing - idle building
    }
    
    string getType() const override { return "T0"; }
};

class SpawnerBuilding : BuildingBehavior {
public:
    float nextSpawn = 5.0;
    string spawnType;
    
    SpawnerBuilding(const string& type) : spawnType(type) {}
    
    void spawn() override {
        nextSpawn = 5.0;
    }
    
    void update(float dt) override {
        nextSpawn -= dt;
        if (nextSpawn <= 0) {
            nextSpawn = 5.0;
            self.spawn(spawnType);
        }
    }
    
    string getType() const override { return "Spawner"; }
};
```

**Life/main.jai:**
```cpp
class T1Building : SpawnerBuilding {
public:
    T1Building() : SpawnerBuilding("Life_T1") {}
    string getType() const override { return "T1"; }
};

class T2_0Building : SpawnerBuilding {
public:
    T2_0Building() : SpawnerBuilding("Life_T1") {}
    string getType() const override { return "T2_0"; }
};

class T2_1Building : SpawnerBuilding {
public:
    T2_1Building() : SpawnerBuilding("Life_T1") {}
    string getType() const override { return "T2_1"; }
};

// Type-safe polymorphic dispatch
std::unique_ptr<BuildingBehavior> behavior;

auto initializeBuilding = [&behavior]() {
    string buildingId = self.current().id;
    if (buildingId == "T0") {
        behavior = make_unique<IdleBuilding>();
    } else if (buildingId == "T1") {
        behavior = make_unique<T1Building>();
    } else if (buildingId == "T2_0") {
        behavior = make_unique<T2_0Building>();
    } else if (buildingId == "T2_1") {
        behavior = make_unique<T2_1Building>();
    }
    
    if (behavior) {
        behavior->spawn();
    }
};

self.spawn = initializeBuilding;
self.update = [&behavior](float dt) {
    if (behavior) {
        behavior->update(dt);
    }
};
```

**Benefits:**
- ✅ **Type-safe dispatch** - compile-time checking
- ✅ **Eliminated duplication** - shared spawner logic
- ✅ **Extensible design** - easy to add new building types
- ✅ **Runtime safety** - no invalid string key lookups
- ✅ **Clean member functions** - spawn() and update() as virtual methods

### 4. Hot-Loading Without Workarounds

**Current ChaiScript Issues:**
```chaiscript
// Must use string keys to avoid redefinition errors during hot-reload
self["functionName"] = fun(self) { /* logic */ };

// Cannot use proper member variables or class definitions
// because ChaiScript complains about duplicate definitions
```

**JaiScript Solution:**
```cpp
// Proper class definitions that can be hot-reloaded
class CreatureBehavior {
    bool attacking = false;  // ✅ Can be redefined during hot-reload
    int health = 100;        // ✅ State preserved across reloads
    
    void attack() {          // ✅ Method can be modified and reloaded
        // New attack logic here - changes preserved during hot-reload
        print("New attack behavior!\n");
    }
    
    void spawn() {           // ✅ Member function, not string-based
        health = 100;
        attacking = false;
    }
    
    void update(float dt) {  // ✅ Can be modified during development
        if (health <= 0) {
            // New death logic added during hot-reload
            self.triggerDeathAnimation();
        }
    }
};

// JaiScript's state management handles redefinition automatically:
// 1. Save current state (attacking=true, health=75)
// 2. Reload script with new class definition
// 3. Restore state to new class instance  
// 4. Continue execution seamlessly

CreatureBehavior behavior;
self.spawn = [&behavior]() { behavior.spawn(); };
self.update = [&behavior](float dt) { behavior.update(dt); };
```

**Benefits:**
- ✅ **No string workarounds** needed
- ✅ **State preservation** across reloads
- ✅ **Proper class syntax** supported
- ✅ **Compile-time validation** even during hot-reload
- ✅ **Member functions** - spawn() and update() as proper methods

### 5. Simplified Callback Patterns

**Current ChaiScript:**
```chaiscript
spine.onEvent.connect("launch", fun[target, targeting](spine, index, eventData){
    if(eventData.name == "launch"){
        var newMissile = BattleEffectNetworkState(targeting.self().game(), "Missile", targeting.self().networkId);
        effect.onArrive.connect("Arrive", fun(battleEffect) {
            if(!battleEffect.targetCreature().is_var_null()){
                battleEffect.targetCreature().changeHealth(-5);
            }
        });
    }
});
```

**JaiScript Migration:**
```cpp
class CombatBehavior {
public:
    void setupAttackAnimation() {
        spine.onEvent.connect("launch", [=](auto& spine, int index, auto& eventData) {
            if (eventData.name == "launch") {
                auto newMissile = BattleEffectNetworkState(
                    targeting.self().game(), 
                    "Missile", 
                    targeting.self().networkId
                );
                
                // Simplified null checking and cleaner callback
                newMissile.onArrive.connect("Arrive", [](auto& battleEffect) {
                    if (auto target = battleEffect.targetCreature()) {
                        target->changeHealth(-5);
                    }
                });
            }
        });
    }
    
    void spawn() {
        setupAttackAnimation();
    }
    
    void update(float dt) {
        // Combat logic
    }
};

CombatBehavior behavior;
self.spawn = [&behavior]() { behavior.spawn(); };
self.update = [&behavior](float dt) { behavior.update(dt); };
```

**Benefits:**
- ✅ **Automatic capture** - no manual `[target, targeting]`
- ✅ **Null safety** - optional types instead of `is_var_null()`
- ✅ **Cleaner syntax** - modern C++ lambda style
- ✅ **Better tooling** - IDE understands lambda captures
- ✅ **Organized methods** - callbacks setup in spawn() member function

### 6. Interface Script Improvements

**Current ChaiScript:**
```chaiscript
// Assets/Interface/Login/loginButton.script
auto scene = game.gui.page("Login").root();
auto email = scene.get("EmailInput").componentText().text();
auto password = scene.get("PasswordInput").componentText().text();
```

**JaiScript Migration:**
```cpp
class LoginInterface {
public:
    void handleLogin() {
        auto scene = game.gui.page("Login").root();
        
        // Null-safe chaining
        auto email = scene.get("EmailInput")?.componentText()?.text() ?? "";
        auto password = scene.get("PasswordInput")?.componentText()?.text() ?? "";
        
        if (!email.empty() && !password.empty()) {
            game.attemptLogin(email, password);
        }
    }
    
    void spawn() {
        // Interface initialization
    }
    
    void update(float dt) {
        // Interface update logic
    }
};

LoginInterface interface;
self.spawn = [&interface]() { interface.spawn(); };
self.update = [&interface](float dt) { interface.update(dt); };

// Button click handler
loginButton.onClick = [&interface]() { interface.handleLogin(); };
```

**Benefits:**
- ✅ **Null safety** - `?.` operator prevents crashes
- ✅ **Default values** - `??` operator provides fallbacks
- ✅ **Organized code** - methods grouped in class
- ✅ **Member functions** - spawn() and update() as class methods

## Summary of Improvements

| **Current ChaiScript Issues** | **JaiScript Solutions** |
|-------------------------------|-------------------------|
| String-based member access (`self["prop"]`) | Direct member access with compile-time safety |
| 8+ duplicate creature files (50+ lines each) | Single base class + inheritance (15 lines each) |
| String-based polymorphism (`self[id](self, dt)`) | Type-safe virtual dispatch |
| Hot-reload workarounds with maps | Built-in state preservation with proper classes |
| No IDE support | Full autocomplete, refactoring, debugging |
| Runtime errors from typos | Compile-time error detection |
| Complex callback syntax (`fun[captures]`) | Modern lambda expressions with auto-capture |
| Lambda assignments to `self.spawn`/`self.update` | Proper member functions in classes |

## Migration Impact

**Lines of Code Reduction:**
- **Creature Scripts**: 400+ lines → 120 lines (70% reduction)
- **Building Scripts**: 200+ lines → 60 lines (70% reduction)  
- **Interface Scripts**: 150+ lines → 45 lines (70% reduction)

**Maintenance Benefits:**
- **Single source of truth** for common behaviors
- **Type safety** prevents entire classes of runtime errors
- **IDE tooling** enables refactoring, go-to-definition, autocomplete
- **Hot-reloading** without workarounds or string-based hacks
- **Proper member functions** instead of lambda assignments

**Developer Experience:**
- **Compile-time feedback** catches errors during development
- **Inheritance and polymorphism** enable proper code organization
- **Modern C++ syntax** familiar to C++ developers
- **State preservation** during hot-reload maintains game flow
- **Clean class hierarchies** make code self-documenting

JaiScript would transform the scripting codebase from a maintenance nightmare into a clean, type-safe, maintainable system while preserving all the hot-reloading capabilities essential for game development.

## C++ Class Binding Examples

### ClassBuilder Pattern

JaiScript provides a modern ClassBuilder pattern for registering C++ classes, dramatically reducing binding code compared to ChaiScript:

#### Basic Class Registration

**ChaiScript approach (verbose):**
```cpp
// From utilityHooks.cxx - ~100 lines for Task class
a_script.add(chaiscript::user_type<Task>(), "Task");
a_script.add(chaiscript::fun(static_cast<Task&(Task::*)(const Function<void()>&)>(&Task::then)), "then");
a_script.add(chaiscript::fun(static_cast<Task&(Task::*)(const Function<void(Task&)>&)>(&Task::then)), "then");
// ... 20+ more overloads with static_cast
```

**JaiScript approach (concise):**
```cpp
// ~20 lines - 80% reduction!
makeClassBuilder<Task>(engine, "Task")
    .method("then", [](Task& self, const Function<void()>& func) {
        return self.then(func);
    })
    .method("then", [](Task& self, const Function<void(Task&)>& func) {
        return self.then(func);
    })
    // Lambda binding eliminates static_cast ugliness
    .build();
```

#### Complex Hierarchy Registration

**ChaiScript approach (from sceneHooks.cxx):**
```cpp
// ~40 lines for Button class
a_script.add(chaiscript::user_type<Button>(), "Button");
a_script.add(chaiscript::base_class<Clickable, Button>());
a_script.add(chaiscript::base_class<Sprite, Button>());
a_script.add(chaiscript::base_class<Drawable, Button>());
a_script.add(chaiscript::base_class<Component, Button>());

// Lambda wrappers for every method
a_script.add(chaiscript::fun([](Button& a_self, const std::string& a_newValue) { 
    return a_self.text(a_newValue); 
}), "text");
a_script.add(chaiscript::fun([](Button& a_self) { 
    return a_self.text(); 
}), "text");

// Type conversions
a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>(
    [](const SafeComponent<Button>& a_item) { return a_item.self(); }));
// ... 4 more type conversions
```

**JaiScript approach:**
```cpp
// ~15 lines - 60% reduction!
makeClassBuilder<Button>(engine, "Button")
    .inherits<Clickable>()
    .inherits<Sprite>()
    .inherits<Drawable>()
    .inherits<Component>()
    
    // Clean lambda binding - no boilerplate
    .method("text", [](Button& self, const std::string& text) { self.text(text); })
    .method("text", [](Button& self) -> std::string { return self.text(); })
    
    // Generic type conversion
    .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Button>>(
        [](const auto& item) { return item.self(); })
    
    .build();
```

#### Template Class Registration

**ChaiScript approach (from renderHooks.cxx):**
```cpp
// Template function with massive duplication
template <class T>
void hookBoxAABB(chaiscript::ChaiScript &a_script, const std::string &a_postfix) {
    a_script.add(chaiscript::user_type<BoxAABB<T>>(), "BoxAABB" + a_postfix);
    a_script.add(chaiscript::constructor<BoxAABB<T>()>(), "BoxAABB" + a_postfix);
    a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &)>(), "BoxAABB" + a_postfix);
    // ... 10+ constructors
    
    // Methods with static_cast for overloads
    a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &)>(&BoxAABB<T>::initialize)), "initialize");
    a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Size<T> &)>(&BoxAABB<T>::initialize)), "initialize");
    // ... many more
}
```

**JaiScript approach:**
```cpp
// Clean template registration
template<typename T>
void registerBoxAABB(Engine& engine, const std::string& suffix) {
    makeClassBuilder<BoxAABB<T>>(engine, "BoxAABB" + suffix)
        .constructor<>()
        .constructor<const Point<T>&>()
        .constructor<const Point<T>&, const Point<T>&>()
        .constructor<const Point<T>&, const Size<T>&>()
        
        // Lambda binding for overloaded methods - no static_cast!
        .method("initialize", [](BoxAABB<T>& self, const Point<T>& p) { 
            self.initialize(p); 
        })
        .method("initialize", [](BoxAABB<T>& self, const Size<T>& s) { 
            self.initialize(s); 
        })
        
        .property("minPoint", &BoxAABB<T>::minPoint)
        .property("maxPoint", &BoxAABB<T>::maxPoint)
        
        .build();
}
```

#### Game Entity Registration with Validation

**Current approach in Bindstone:**
```cpp
// No validation in binding layer - errors surface at runtime
a_script.add(chaiscript::fun(&Creature::setHealth), "setHealth");
a_script.add(chaiscript::fun(&Creature::takeDamage), "takeDamage");
```

**JaiScript approach with validation:**
```cpp
makeClassBuilder<Creature>(engine, "Creature")
    .inherits<Entity>()
    .constructor<const std::string&>()
    
    // Add validation in the binding layer
    .method("setHealth", [](Creature& self, int health) {
        if (health < 0) {
            throw std::runtime_error("Health cannot be negative");
        }
        if (health > self.getMaxHealth()) {
            health = self.getMaxHealth();
        }
        self.setHealth(health);
    })
    
    // Add logging for debugging
    .method("takeDamage", [](Creature& self, int damage) {
        std::cout << "[Combat] " << self.getName() 
                  << " taking " << damage << " damage\n";
        self.takeDamage(damage);
        if (self.getHealth() <= 0) {
            std::cout << "[Combat] " << self.getName() << " defeated!\n";
        }
    })
    
    // Property-style access
    .method("health", [](Creature& self) { return self.getHealth(); })
    .method("health", [](Creature& self, int h) { self.setHealth(h); })
    
    .build();
```

#### Signal/Event Binding

**ChaiScript approach:**
```cpp
// Complex signal registration
ScriptSignalRegistrar<Clickable::ButtonSignalSignature> _clickableButtonSignal{};
ScriptSignalRegistrar<Clickable::DragSignalSignature> _clickableDragSignal{};

// In the hook function
a_script.add(chaiscript::fun(&Clickable::onPress), "onPress");
a_script.add(chaiscript::fun(&Clickable::onRelease), "onRelease");
```

**JaiScript approach:**
```cpp
makeClassBuilder<Clickable>(engine, "Clickable")
    .inherits<Component>()
    
    // Signal methods with better documentation
    .method("onPress", &Clickable::onPress)
    .method("onRelease", &Clickable::onRelease)
    
    // Add convenience methods
    .method("onClick", [](Clickable& self, std::function<void()> handler) {
        self.onRelease.connect("click", [handler](auto&) { handler(); });
    })
    
    .build();

// Register signal types separately
engine.registerSignal<ButtonSignal>("ButtonSignal");
engine.registerSignal<DragSignal>("DragSignal");
```

### Benefits Summary

| Feature | ChaiScript | JaiScript | Improvement |
|---------|------------|-----------|-------------|
| **Method Overloads** | Requires `static_cast<>` | Lambda binding | 100% cleaner |
| **Inheritance** | Multiple `base_class<>` calls | Chained `.inherits<>()` | 75% less code |
| **Type Conversions** | Verbose per-conversion | Generic system | 80% reduction |
| **Validation** | Not in binding layer | Easy in lambdas | Much safer |
| **Code Size** | ~100 lines per class | ~20 lines per class | 80% reduction |
| **IDE Support** | Limited | Full support | 100% better |

The ClassBuilder pattern transforms C++ integration from a verbose, error-prone process into a clean, maintainable system that encourages best practices like validation and logging at the binding layer.
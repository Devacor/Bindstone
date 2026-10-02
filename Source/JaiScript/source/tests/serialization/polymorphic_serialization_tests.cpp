#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/dynamic_binder_serialization.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/properties.hpp>
#include <jaiscript/detail/static_type_name.hpp>
#include "serialization_roundtrip.hpp"

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// ============================================================================
// C++ Base Classes for Testing
// ============================================================================

// Simple C++ base class (renamed to avoid ODR conflict with static_binder_tests Entity)
class SerEntity {
public:
    int id = 0;
    std::string name = "unnamed";

    virtual ~SerEntity() = default;

    virtual std::string get_type_name() const { return "SerEntity"; }
};

// C++ derived class
class SerCharacter : public SerEntity {
public:
    int health = 100;
    int max_health = 100;

    std::string get_type_name() const override { return "SerCharacter"; }
};

// Fresh hierarchies used ONLY to pin that .base_class<>() and register_relation<> register the
// polymorphic assignability edge (the B5 load check). Isolated so the edge can only come from
// the test under it.
struct EdgeBase { virtual ~EdgeBase() = default; };
struct EdgeDerived : EdgeBase {};
struct RelBase { virtual ~RelBase() = default; };
struct RelDerived : RelBase {};

// Another C++ derived class
class SerItem : public SerEntity {
public:
    float weight = 1.0f;
    int stack_count = 1;

    std::string get_type_name() const override { return "SerItem"; }
};

// Deeply nested hierarchy
class SerPlayer : public SerCharacter {
public:
    int experience = 0;
    int level = 1;

    std::string get_type_name() const override { return "SerPlayer"; }
};

// ============================================================================
// Test Suite
// ============================================================================

class polymorphic_serialization_tests : public suite {
public:
    polymorphic_serialization_tests() : suite("Polymorphic Serialization Tests") {}

    void forge_tests() override {

        // =====================================================================
        // PURE SCRIPT CLASS TESTS
        // =====================================================================

        test("script_class_json_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            // Define a simple script class
            eng->execute(R"(
                class Monster {
                    var name = "goblin";
                    var health = 50;
                    var damage = 10;
                    var is_boss = false;
                }
            )");

            // Create instance, modify, serialize, deserialize
            eng->execute(R"(
                var monster = Monster();
                monster.name = "Dragon";
                monster.health = 500;
                monster.damage = 75;
                monster.is_boss = true;

                var json_str = to_json(monster);
                var loaded = from_json(json_str);
            )");

            // Verify round-trip
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("Dragon"));
            check_eq(eng->execute("loaded.health").as<script_int>(), 500);
            check_eq(eng->execute("loaded.damage").as<script_int>(), 75);
            check_eq(eng->execute("loaded.is_boss").as<bool>(), true);
        });

        test("script_class_binary_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Weapon {
                    var name = "sword";
                    var damage = 10;
                    var durability = 100.0;
                }
            )");

            eng->execute(R"(
                var weapon = Weapon();
                weapon.name = "Excalibur";
                weapon.damage = 999;
                weapon.durability = 100.0;

                var binary_data = to_binary(weapon);
                var loaded = from_binary(binary_data);
            )");

            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("Excalibur"));
            check_eq(eng->execute("loaded.damage").as<script_int>(), 999);
            check_eq(eng->execute("loaded.durability").as<script_float>(), 100.0);
        });

        test("script_class_with_nested_objects", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Vector2 {
                    var x = 0.0;
                    var y = 0.0;
                }

                class Transform {
                    var position = null;
                    var scale = 1.0;
                    var rotation = 0.0;

                    Transform() {
                        position = Vector2();
                    }
                }
            )");

            eng->execute(R"(
                var transform = Transform();
                transform.position.x = 100.0;
                transform.position.y = 200.0;
                transform.scale = 2.5;
                transform.rotation = 45.0;

                var json_str = to_json(transform);
                var loaded = from_json(json_str);
            )");

            check_eq(eng->execute("loaded.position.x").as<script_float>(), 100.0);
            check_eq(eng->execute("loaded.position.y").as<script_float>(), 200.0);
            check_eq(eng->execute("loaded.scale").as<script_float>(), 2.5);
            check_eq(eng->execute("loaded.rotation").as<script_float>(), 45.0);
        });

        // =====================================================================
        // C++ CLASS ROUND-TRIP TESTS
        // =====================================================================

        test("cpp_class_json_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            // Register C++ Entity class
            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            // Create, modify, serialize, deserialize
            eng->execute(R"(
                var entity = Entity();
                entity.id = 42;
                entity.name = "TestEntity";

                var json_str = to_json(entity);
                var loaded = from_json(json_str);
            )");

            check_eq(eng->execute("loaded.id").as<script_int>(), 42);
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("TestEntity"));
        });

        test("cpp_class_binary_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            eng->execute(R"(
                var entity = Entity();
                entity.id = 123;
                entity.name = "BinaryEntity";

                var binary_data = to_binary(entity);
                var loaded = from_binary(binary_data);
            )");

            check_eq(eng->execute("loaded.id").as<script_int>(), 123);
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("BinaryEntity"));
        });

        // C++ class inheritance with base_class<>() now automatically includes
        // inherited properties in serialization. No need to re-declare base properties.

        test("cpp_derived_class_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            // Register hierarchy - inherited properties are automatically included
            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            dynamic_binder<SerCharacter>(*eng, "Character")
                .constructor<>()
                .base_class<SerEntity>()
                // Only declare Character-specific properties - id/name inherited automatically
                .property("health", &SerCharacter::health)
                .property("max_health", &SerCharacter::max_health)
                .build();

            eng->execute(R"(
                var hero = Character();
                hero.id = 1;
                hero.name = "Hero";
                hero.health = 80;
                hero.max_health = 100;

                var json_str = to_json(hero);
                var loaded = from_json(json_str);
            )");

            // Verify all fields including inherited
            check_eq(eng->execute("loaded.id").as<script_int>(), 1);
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("Hero"));
            check_eq(eng->execute("loaded.health").as<script_int>(), 80);
            check_eq(eng->execute("loaded.max_health").as<script_int>(), 100);
        });

        test("cpp_deep_inheritance_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            // Register full hierarchy: Entity -> Character -> Player
            // Each class only declares its own properties - inheritance is automatic
            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            dynamic_binder<SerCharacter>(*eng, "Character")
                .constructor<>()
                .base_class<SerEntity>()
                .property("health", &SerCharacter::health)
                .property("max_health", &SerCharacter::max_health)
                .build();

            dynamic_binder<SerPlayer>(*eng, "Player")
                .constructor<>()
                .base_class<SerCharacter>()
                .property("experience", &SerPlayer::experience)
                .property("level", &SerPlayer::level)
                .build();

            eng->execute(R"(
                var player = Player();
                player.id = 999;
                player.name = "Champion";
                player.health = 150;
                player.max_health = 150;
                player.experience = 5000;
                player.level = 10;

                var json_str = to_json(player);
                var loaded = from_json(json_str);
            )");

            // Verify entire inheritance chain
            check_eq(eng->execute("loaded.id").as<script_int>(), 999);
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("Champion"));
            check_eq(eng->execute("loaded.health").as<script_int>(), 150);
            check_eq(eng->execute("loaded.max_health").as<script_int>(), 150);
            check_eq(eng->execute("loaded.experience").as<script_int>(), 5000);
            check_eq(eng->execute("loaded.level").as<script_int>(), 10);
        });

        // =====================================================================
        // SCRIPT INHERITANCE TESTS
        // =====================================================================

        test("script_class_inheritance_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Animal {
                    var name = "animal";
                    var age = 0;
                }

                class Dog : Animal {
                    var breed = "unknown";
                    var is_good_boy = true;
                }
            )");

            eng->execute(R"(
                var dog = Dog();
                dog.name = "Buddy";
                dog.age = 5;
                dog.breed = "Golden Retriever";
                dog.is_good_boy = true;

                var json_str = to_json(dog);
                var loaded = from_json(json_str);
            )");

            // Verify all fields including inherited
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("Buddy"));
            check_eq(eng->execute("loaded.age").as<script_int>(), 5);
            check_eq(eng->execute("loaded.breed").as<std::string>(), std::string("Golden Retriever"));
            check_eq(eng->execute("loaded.is_good_boy").as<bool>(), true);
        });

        test("script_deep_inheritance_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Base {
                    var base_val = 1;
                }

                class Middle : Base {
                    var middle_val = 2;
                }

                class Derived : Middle {
                    var derived_val = 3;
                }
            )");

            eng->execute(R"(
                var obj = Derived();
                obj.base_val = 10;
                obj.middle_val = 20;
                obj.derived_val = 30;

                var json_str = to_json(obj);
                var loaded = from_json(json_str);
            )");

            check_eq(eng->execute("loaded.base_val").as<script_int>(), 10);
            check_eq(eng->execute("loaded.middle_val").as<script_int>(), 20);
            check_eq(eng->execute("loaded.derived_val").as<script_int>(), 30);
        });

        // =====================================================================
        // MIXED INHERITANCE TESTS (Script inheriting from C++)
        // Note: Script classes inheriting from C++ classes with serialization
        // is a complex feature that may require additional infrastructure.
        // These tests document the intended behavior for future implementation.
        // =====================================================================

        // TODO: script_inheriting_cpp_class_round_trip
        // A script class extending a C++ class should serialize both the
        // C++ properties and script-defined fields, and correctly restore
        // the mixed inheritance hierarchy on deserialization.

        // TODO: script_inheriting_cpp_derived_class
        // A script class extending a C++ derived class (full chain:
        // C++ base -> C++ derived -> Script class) should serialize and
        // deserialize the complete hierarchy.

        // =====================================================================
        // TYPE PRESERVATION TESTS
        // =====================================================================

        test("type_preserved_after_deserialization", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class TypedClass {
                    var value = 42;
                    var type_marker = "TypedClass";
                }
            )");

            eng->execute(R"(
                var obj = TypedClass();
                obj.value = 99;
                var json_str = to_json(obj);
                var loaded = from_json(json_str);
            )");

            // Verify the loaded object has correct values (type preserved)
            check_eq(eng->execute("loaded.value").as<script_int>(), 99);
            check_eq(eng->execute("loaded.type_marker").as<std::string>(), std::string("TypedClass"));
        });

        test("cpp_type_preserved_after_deserialization", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            eng->execute(R"(
                var entity = Entity();
                entity.id = 1;
                entity.name = "TestEntity";
                var json_str = to_json(entity);
                var loaded = from_json(json_str);
            )");

            // Verify type preserved by checking fields
            check_eq(eng->execute("loaded.id").as<script_int>(), 1);
            check_eq(eng->execute("loaded.name").as<std::string>(), std::string("TestEntity"));
        });

        test("derived_type_preserved_not_sliced", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            dynamic_binder<SerCharacter>(*eng, "Character")
                .constructor<>()
                .base_class<SerEntity>()
                .property("health", &SerCharacter::health)
                .property("max_health", &SerCharacter::max_health)
                .build();

            eng->execute(R"(
                var hero = Character();
                hero.health = 75;
                var json_str = to_json(hero);
                var loaded = from_json(json_str);
            )");

            // Verify deserialized type retained all fields (no slicing)
            // If sliced to Entity, we wouldn't have health field
            check_eq(eng->execute("loaded.health").as<script_int>(), 75);
            check_eq(eng->execute("loaded.max_health").as<script_int>(), 100);  // Character default
        });

        // =====================================================================
        // POST-DESERIALIZE HOOK TESTS WITH INHERITANCE
        // =====================================================================

        test("post_load_called_on_derived_class", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class BaseWithHook {
                    var value = 0;
                    var base_hook_called = false;

                    function post_load(version) {
                        base_hook_called = true;
                    }
                }

                class DerivedWithHook : BaseWithHook {
                    var derived_value = 0;
                    var derived_hook_called = false;

                    override function post_load(version) {
                        derived_hook_called = true;
                        // Note: In current implementation, derived overrides base
                    }
                }
            )");

            eng->execute(R"(
                var obj = DerivedWithHook();
                obj.value = 10;
                obj.derived_value = 20;

                var json_str = to_json(obj);
                var loaded = from_json(json_str);
            )");

            // Derived hook should be called
            check_eq(eng->execute("loaded.derived_hook_called").as<bool>(), true);
            check_eq(eng->execute("loaded.value").as<script_int>(), 10);
            check_eq(eng->execute("loaded.derived_value").as<script_int>(), 20);
        });

        test("cpp_class_post_load_with_inheritance", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            // Use static variables to track hook calls
            static bool character_hook_called = false;
            static int character_hook_version = 0;
            character_hook_called = false;
            character_hook_version = 0;

            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            dynamic_binder<SerCharacter>(*eng, "Character")
                .constructor<>()
                .base_class<SerEntity>()
                .property("health", &SerCharacter::health)
                .property("max_health", &SerCharacter::max_health)
                .post_load_hook([](SerCharacter& self, int version) {
                    character_hook_called = true;
                    character_hook_version = version;
                    // Ensure health doesn't exceed max
                    if (self.health > self.max_health) {
                        self.health = self.max_health;
                    }
                })
                .build();

            // Create JSON with health > max_health to test hook correction
            std::string json = R"({"_type_":"Character","_version_":2,"id":1,"name":"Test","health":150,"max_health":100})";
            eng->add_global("test_json", script_value(json, eng.get()));

            eng->execute("var loaded = from_json(test_json);");

            // Hook should have been called and corrected health
            check(character_hook_called);
            check_eq(character_hook_version, 2);
            check_eq(eng->execute("loaded.health").as<script_int>(), 100);  // Clamped to max
        });

        // =====================================================================
        // ARRAY/COLLECTION OF POLYMORPHIC TYPES
        // =====================================================================

        test("array_of_script_objects_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Point {
                    var x = 0;
                    var y = 0;
                }
            )");

            eng->execute(R"(
                var points = [];
                for (auto i = 0; i < 3; i = i + 1) {
                    var p = Point();
                    p.x = i * 10;
                    p.y = i * 20;
                    points.push(p);
                }

                var json_str = to_json(points);
                var loaded = from_json(json_str);
            )");

            check_eq(eng->execute("loaded.size()").as<script_int>(), 3);
            check_eq(eng->execute("loaded[0].x").as<script_int>(), 0);
            check_eq(eng->execute("loaded[1].x").as<script_int>(), 10);
            check_eq(eng->execute("loaded[2].x").as<script_int>(), 20);
        });

        test("map_with_object_values_round_trip", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Config {
                    var enabled = true;
                    var value = 0;
                }
            )");

            eng->execute(R"(
                var configs = {};

                var audio = Config();
                audio.enabled = true;
                audio.value = 80;
                configs["audio"] = audio;

                var video = Config();
                video.enabled = false;
                video.value = 60;
                configs["video"] = video;

                var json_str = to_json(configs);
                var loaded = from_json(json_str);
            )");

            check_eq(eng->execute("loaded[\"audio\"].enabled").as<bool>(), true);
            check_eq(eng->execute("loaded[\"audio\"].value").as<script_int>(), 80);
            check_eq(eng->execute("loaded[\"video\"].enabled").as<bool>(), false);
            check_eq(eng->execute("loaded[\"video\"].value").as<script_int>(), 60);
        });

        // =====================================================================
        // VERSION MIGRATION WITH INHERITANCE
        // =====================================================================

        test("version_migration_inherited_class", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class Vehicle {
                    var speed = 0;
                    var fuel = 100;
                }

                class Car : Vehicle {
                    var wheels = 4;
                    var doors = 4;
                    var migrated = false;

                    function post_load(version) {
                        if (version < 2) {
                            // Old versions didn't have doors, default to 4
                            doors = 4;
                            migrated = true;
                        }
                    }
                }
            )");

            // Simulate v1 JSON (no doors field)
            std::string v1_json = R"({"_type_":"Car","_version_":1,"speed":60,"fuel":75,"wheels":4})";
            eng->add_global("v1_json", script_value(v1_json, eng.get()));

            eng->execute("var loaded = from_json(v1_json);");

            check_eq(eng->execute("loaded.speed").as<script_int>(), 60);
            check_eq(eng->execute("loaded.fuel").as<script_int>(), 75);
            check_eq(eng->execute("loaded.wheels").as<script_int>(), 4);
            check_eq(eng->execute("loaded.doors").as<script_int>(), 4);  // Migrated default
            check_eq(eng->execute("loaded.migrated").as<bool>(), true);
        });

        // =====================================================================
        // CROSS-FORMAT CONSISTENCY
        // =====================================================================

        test("json_binary_consistency_with_inheritance", [&]() {
            auto eng = make_engine();
            stdlib::register_json_functions(*eng);

            dynamic_binder<SerEntity>(*eng, "Entity")
                .constructor<>()
                .property("id", &SerEntity::id)
                .property("name", &SerEntity::name)
                .build();

            dynamic_binder<SerCharacter>(*eng, "Character")
                .constructor<>()
                .base_class<SerEntity>()
                .property("health", &SerCharacter::health)
                .property("max_health", &SerCharacter::max_health)
                .build();

            eng->execute(R"(
                var original = Character();
                original.id = 42;
                original.name = "TestChar";
                original.health = 85;
                original.max_health = 100;

                // Round-trip through JSON
                var json_str = to_json(original);
                var from_json_obj = from_json(json_str);

                // Round-trip through Binary
                var binary_data = to_binary(original);
                var from_binary_obj = from_binary(binary_data);
            )");

            // Both should produce identical results
            check_eq(eng->execute("from_json_obj.id").as<script_int>(), 42);
            check_eq(eng->execute("from_binary_obj.id").as<script_int>(), 42);

            check_eq(eng->execute("from_json_obj.name").as<std::string>(),
                     eng->execute("from_binary_obj.name").as<std::string>());

            check_eq(eng->execute("from_json_obj.health").as<script_int>(),
                     eng->execute("from_binary_obj.health").as<script_int>());
        });
    }
};

// No registrar anywhere: identity comes implicitly from property_owner.
class ImplicitRegBase : public jai::property_owner<ImplicitRegBase> {
public:
    JAI_PROPERTY((int), baseValue, 1);
};

class ImplicitRegDerived : public jai::property_owner<ImplicitRegDerived, ImplicitRegBase> {
public:
    JAI_PROPERTY((std::string), derivedTag, "");
};

// property_owner AND an explicit registrar: the explicit name must win.
class RenamedRegType : public jai::property_owner<RenamedRegType, ImplicitRegBase> {
public:
    JAI_PROPERTY((int), marker, 7);
};
static jai::registrar<RenamedRegType, void> _renamedRegistration("CustomRegName");

namespace {
	// Anonymous-namespace spellings differ per compiler, but stripping to the bare last
	// segment yields the same clean identifier everywhere ("AnonNamespaceProbe").
	struct AnonNamespaceProbe { virtual ~AnonNamespaceProbe() = default; };

	// And a property_owner in an anonymous namespace must round-trip implicitly.
	class AnonImplicitDerived : public jai::property_owner<AnonImplicitDerived, ImplicitRegBase> {
	public:
		JAI_PROPERTY((int), anonValue, 5);
	};
}

// In-class name pin: implicit registration must use jai_type_name when declared
// (the hatch for template instantiations and classes whose identifier may change).
class InClassNamedType : public jai::property_owner<InClassNamedType, ImplicitRegBase> {
public:
    static constexpr const char* jai_type_name = "PinnedName";
    JAI_PROPERTY((int), pinned, 3);
};

// Multiple inheritance: the SECOND base sits at a non-zero offset within the derived
// object, so reconstructing a shared_ptr<PORightBase> from a dynamic POMultiDerived needs
// pointer adjustment. property_owner registers the Derived->Base casters automatically.
class POLeftBase : public jai::property_owner<POLeftBase> {
public:
    JAI_PROPERTY((int), leftVal, 0);
};
class PORightBase : public jai::property_owner<PORightBase> {
public:
    JAI_PROPERTY((int), rightVal, 0);
};
class POMultiDerived : public jai::property_owner<POMultiDerived, POLeftBase, PORightBase> {
public:
    JAI_PROPERTY((int), derivedVal, 0);
};

// Non-property_owner MI hierarchy registered through the manual escape hatches: an explicit
// registrar for identity + a manual base relation for the offset.
struct MIRawLeft {
    virtual ~MIRawLeft() = default;
    int l = 0;
    template<class Ar> void serialize(Ar& ar) { ar(jai::serialization::make_nvp("l", l)); }
};
struct MIRawRight {
    virtual ~MIRawRight() = default;
    int r = 0;
    template<class Ar> void serialize(Ar& ar) { ar(jai::serialization::make_nvp("r", r)); }
};
struct MIRawDerived : MIRawLeft, MIRawRight {
    int d = 0;
    template<class Ar> void serialize(Ar& ar) { ar(jai::serialization::make_nvp("d", d)); MIRawLeft::serialize(ar); MIRawRight::serialize(ar); }
};
static jai::registrar<MIRawDerived, void> _miRawReg("MIRawDerived");
static const bool _miRawRelation = [] {
    jai::serialization::register_relation<MIRawDerived, MIRawRight>{};
    return true;
}();

// Auto-named registrar: no name string passed — the script name and serialization $type
// are both derived from the C++ type. Not a property_owner, so the only registration is
// the registrar's, making the derived name unambiguous.
struct AutoNamedRegType {
    virtual ~AutoNamedRegType() = default;
    int value = 0;
    template<class Archive>
    void serialize(Archive& ar) { ar(jai::serialization::make_nvp("value", value)); }
};
static jai::registrar<AutoNamedRegType, void> _autoNamedRegistration;  // <-- no name

// Polymorphic, serializable, but never registered: saving through a base pointer
// must throw instead of silently slicing.
class UnregPolyBase {
public:
    virtual ~UnregPolyBase() = default;
    int value = 1;
    template<class Archive>
    void serialize(Archive& ar) {
        ar(jai::serialization::make_nvp("value", value));
    }
};

class UnregPolyDerived : public UnregPolyBase {
public:
    int extra = 2;
    template<class Archive>
    void serialize(Archive& ar) {
        ar(jai::serialization::make_nvp("extra", extra));
        UnregPolyBase::serialize(ar);
    }
};

class implicit_registration_tests : public suite {
public:
    implicit_registration_tests() : suite("Implicit Polymorphic Registration Tests") {}

    void forge_tests() override {

        test("static_type_name_extraction", [&]() {
            check_eq(std::string("ImplicitRegBase"), std::string(jai::detail::static_unqualified_type_name<ImplicitRegBase>()));
            check_eq(std::string("jai::foundry::tests::ImplicitRegBase"), std::string(jai::detail::static_type_name<ImplicitRegBase>()));
            check(jai::detail::static_unqualified_type_name<std::vector<int>>().empty(), "template instantiations must be excluded from implicit naming");
            // Anonymous-namespace types strip to their bare identifier (the compiler-specific
            // marker is in a discarded middle segment) — no namespace qualification kept.
            check_eq(std::string("AnonNamespaceProbe"), std::string(jai::detail::static_unqualified_type_name<AnonNamespaceProbe>()));
        });

        test("anonymous_namespace_property_owner_round_trip", [&]() {
            auto eng = make_engine();
            std::shared_ptr<ImplicitRegBase> original = std::make_shared<AnonImplicitDerived>();

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            auto json = w.str();
            check(json.find("\"AnonImplicitDerived\"") != std::string::npos, "expected the bare implicit name: " + json);
            check(json.find("AN::") == std::string::npos, "no namespace qualification should leak into the type name: " + json);

            auto loaded = roundtrip_json(*eng, original);
            check(std::dynamic_pointer_cast<AnonImplicitDerived>(loaded) != nullptr, "dynamic type lost for anonymous-namespace type");
        });

        test("property_owner_implicit_type_tag_written", [&]() {
            auto eng = make_engine();
            auto derived = std::make_shared<ImplicitRegDerived>();
            derived->derivedTag = std::string("tagged");
            std::shared_ptr<ImplicitRegBase> original = derived;

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            auto json = w.str();
            check(json.find("\"$type\"") != std::string::npos, "expected a $type discriminator: " + json);
            check(json.find("ImplicitRegDerived") != std::string::npos, "expected the implicit class name: " + json);
        });

        // A tampered $type naming a registered but UNRELATED type must be rejected on load,
        // not silently reinterpreted as the requested base (type confusion, B5).
        test("polymorphic_load_rejects_unrelated_type", [&]() {
            auto eng = make_engine();
            std::shared_ptr<ImplicitRegBase> original = std::make_shared<ImplicitRegDerived>();
            (void)std::make_shared<POLeftBase>();  // odr-use so POLeftBase is registered

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            std::string json = w.str();

            // Relabel the concrete type as POLeftBase — registered, but not a subtype of
            // ImplicitRegBase — simulating a malicious save/network payload.
            const std::string concrete = "ImplicitRegDerived";
            auto pos = json.find(concrete);
            check(pos != std::string::npos, "expected the concrete $type in: " + json);
            json.replace(pos, concrete.size(), "POLeftBase");

            jai::serialization::json_archive_reader r(json, eng.get());
            std::shared_ptr<ImplicitRegBase> out;
            check_throws([&]() { r(out); }, "unrelated $type must be rejected, not mis-cast");
        });

        test("property_owner_implicit_round_trip", [&]() {
            auto eng = make_engine();
            auto derived = std::make_shared<ImplicitRegDerived>();
            derived->derivedTag = std::string("round");
            std::shared_ptr<ImplicitRegBase> original = derived;

            auto fromJson = roundtrip_json(*eng, original);
            check(fromJson != nullptr, "json round-trip returned null");
            auto jsonDerived = std::dynamic_pointer_cast<ImplicitRegDerived>(fromJson);
            check(jsonDerived != nullptr, "dynamic type lost through json round-trip");
            check_eq(std::string("round"), jsonDerived->derivedTag.get());

            auto fromBinary = roundtrip_binary(*eng, original);
            check(fromBinary != nullptr, "binary round-trip returned null");
            auto binaryDerived = std::dynamic_pointer_cast<ImplicitRegDerived>(fromBinary);
            check(binaryDerived != nullptr, "dynamic type lost through binary round-trip");
            check_eq(std::string("round"), binaryDerived->derivedTag.get());
        });

        test("property_owner_auto_registers_base_casters", [&]() {
            // property_owner<Derived, Left, Right> registers Derived->base adjusters at
            // static init. Verify the second base's adjuster directly (its subobject is at
            // a non-zero offset) — the registry produces the same pointer a real
            // static_cast<PORightBase*> would.
            auto obj = std::make_shared<POMultiDerived>();
            void* derivedAddr = obj.get();
            void* expectedRight = static_cast<PORightBase*>(obj.get());
            check(expectedRight != derivedAddr, "test premise: PORightBase subobject must be at a non-zero offset");

            void* adjusted = jai::serialization::polymorphic_registry::instance().adjust_to_base(
                std::type_index(typeid(POMultiDerived)), std::type_index(typeid(PORightBase)), derivedAddr);
            check(adjusted == expectedRight, "auto-registered caster must adjust a POMultiDerived* to its PORightBase subobject");

            // First base is at offset zero; adjuster (if any) must be identity.
            void* expectedLeft = static_cast<POLeftBase*>(obj.get());
            void* adjustedLeft = jai::serialization::polymorphic_registry::instance().adjust_to_base(
                std::type_index(typeid(POMultiDerived)), std::type_index(typeid(POLeftBase)), derivedAddr);
            check(adjustedLeft == expectedLeft, "first-base adjustment must match static_cast");
        });

        test("multiple_inheritance_manual_base_relation", [&]() {
            auto eng = make_engine();
            auto obj = std::make_shared<MIRawDerived>();
            obj->l = 5; obj->r = 6; obj->d = 7;
            std::shared_ptr<MIRawRight> asRight = obj;
            check(static_cast<const void*>(asRight.get()) != static_cast<const void*>(obj.get()),
                "test premise: MIRawRight subobject must be at a non-zero offset");

            auto loaded = roundtrip_json(*eng, asRight);
            check(loaded != nullptr, "manual MI round-trip returned null");
            check_eq(6, loaded->r);                    // adjusted base data correct
            auto full = std::dynamic_pointer_cast<MIRawDerived>(loaded);
            check(full != nullptr, "manual MI dynamic type lost");
            check_eq(5, full->l);
            check_eq(7, full->d);
        });

        test("registrar_auto_name_no_string_feeds_both_registries", [&]() {
            // Script-name registry got the bare class name, with no string ever passed.
            auto scriptName = jai::type_name_registry::instance().get_name<AutoNamedRegType>();
            check(scriptName.has_value(), "auto-named type missing from type_name_registry");
            check_eq(std::string("AutoNamedRegType"), std::string(*scriptName));
            // Serialization registry got the same name, from the same derivation.
            auto* entry = jai::serialization::polymorphic_registry::instance().find(std::type_index(typeid(AutoNamedRegType)));
            check(entry != nullptr, "auto-named type missing from polymorphic_registry");
            check_eq(std::string("AutoNamedRegType"), entry->name);
        });

        test("explicit_registrar_overrides_implicit_name", [&]() {
            auto eng = make_engine();
            std::shared_ptr<ImplicitRegBase> original = std::make_shared<RenamedRegType>();

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            auto json = w.str();
            check(json.find("CustomRegName") != std::string::npos, "explicit registrar name must win: " + json);
            check(json.find("\"RenamedRegType\"") == std::string::npos, "implicit name must not be used once an explicit one exists: " + json);

            auto loaded = roundtrip_json(*eng, original);
            check(std::dynamic_pointer_cast<RenamedRegType>(loaded) != nullptr, "round-trip through the explicit name failed");
        });

        test("in_class_jai_type_name_pins_the_name", [&]() {
            auto eng = make_engine();
            std::shared_ptr<ImplicitRegBase> original = std::make_shared<InClassNamedType>();

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            auto json = w.str();
            check(json.find("PinnedName") != std::string::npos, "jai_type_name must be used: " + json);
            check(json.find("InClassNamedType") == std::string::npos, "derived class identifier must not leak into the data: " + json);

            auto loaded = roundtrip_json(*eng, original);
            check(std::dynamic_pointer_cast<InClassNamedType>(loaded) != nullptr, "round-trip through the pinned name failed");
        });

        test("unregistered_polymorphic_save_throws", [&]() {
            auto eng = make_engine();
            std::shared_ptr<UnregPolyBase> original = std::make_shared<UnregPolyDerived>();

            check_throws([&]() {
                jai::serialization::json_archive_writer w(0, eng.get());
                w(original);
            }, "saving an unregistered polymorphic type through a base pointer must throw");

            // Static type == dynamic type stays legal without registration.
            auto exact = std::make_shared<UnregPolyDerived>();
            exact->extra = 9;
            auto loaded = roundtrip_json(*eng, exact);
            check(loaded != nullptr, "non-polymorphic-context save must still work");
            check_eq(9, loaded->extra);
        });

        // dynamic_binder::base_class<>() must register the polymorphic assignability edge (not
        // just engine inheritance), so a shared_ptr<Base> with a derived $type passes the B5 load
        // check. Regression for the FileTextureDefinition-through-base load failure.
        test("base_class_registers_polymorphic_assignability_edge", [&]() {
            auto eng = make_engine();
            dynamic_binder<EdgeDerived>(*eng, "EdgeDerived").constructor<>().base_class<EdgeBase>().build();
            auto& reg = jai::serialization::polymorphic_registry::instance();
            check_true(reg.is_assignable(std::type_index(typeid(EdgeDerived)), std::type_index(typeid(EdgeBase))));
            check_false(reg.is_assignable(std::type_index(typeid(EdgeBase)), std::type_index(typeid(EdgeDerived))));
        });

        // register_relation<Derived, Base> declares the same edge externally (for friend-access /
        // third-party types that can't use property_owner or .base_class<>()).
        test("register_relation_registers_assignability_edge", [&]() {
            jai::serialization::register_relation<RelDerived, RelBase> _r;
            auto& reg = jai::serialization::polymorphic_registry::instance();
            check_true(reg.is_assignable(std::type_index(typeid(RelDerived)), std::type_index(typeid(RelBase))));
        });
    }
};

// ============================================================================
// Script layers: a script class deriving a bound C++ class (docs/script_extensible.md)
// ============================================================================

// Two C++ bases, one per lookup path: HybridCreature carries no slot (the engine map),
// SlotCreature derives script_extensible (the slot). Both registered polymorphically for
// the archive $type path and bound per-engine so scripts can derive them.
struct HybridCreature {
    virtual ~HybridCreature() = default;
    int health = 100;
    std::string label = "creature";
    virtual std::string speak() { return "grr"; }
    template<class Ar> void serialize(Ar& ar) {
        ar(jai::serialization::make_nvp("health", health), jai::serialization::make_nvp("label", label));
    }
};
static jai::registrar<HybridCreature, void> _hybridCreatureReg("HybridCreature");

struct SlotCreature : jai::script_extensible {
    virtual ~SlotCreature() = default;
    int health = 100;
    std::string label = "creature";
    virtual std::string speak() { return "grr"; }
    template<class Ar> void serialize(Ar& ar) {
        ar(jai::serialization::make_nvp("health", health), jai::serialization::make_nvp("label", label));
    }
};
static jai::registrar<SlotCreature, void> _slotCreatureReg("SlotCreature");

// A base whose copy constructor is deleted: copying its hybrid must refuse (S4).
struct NoCopyCreature : jai::script_extensible {
    NoCopyCreature() = default;
    NoCopyCreature(const NoCopyCreature&) = delete;
    NoCopyCreature& operator=(const NoCopyCreature&) = delete;
    virtual ~NoCopyCreature() = default;
    int health = 100;
};

struct hybrid_holder {
    std::shared_ptr<HybridCreature> pet;
    int tag = 0;
    template<class Ar> void serialize(Ar& ar) {
        ar(jai::serialization::make_nvp("pet", pet), jai::serialization::make_nvp("tag", tag));
    }
};

struct slot_holder {
    std::shared_ptr<SlotCreature> pet;
    int tag = 0;
    template<class Ar> void serialize(Ar& ar) {
        ar(jai::serialization::make_nvp("pet", pet), jai::serialization::make_nvp("tag", tag));
    }
};

static_assert(std::is_base_of_v<jai::script_extensible, ImplicitRegDerived>, "every property_owner is script_extensible");
static_assert(std::is_base_of_v<jai::script_extensible, POMultiDerived>, "multi-base property_owner reaches a slot through its bases");

class hybrid_serialization_tests : public suite {
public:
    hybrid_serialization_tests() : suite("Hybrid Serialization Tests") {}

    // Every assertion runs on both bases so the slot and map paths cannot drift.
    std::shared_ptr<engine> hybrid_engine() {
        auto eng = make_engine();
        stdlib::register_json_functions(*eng);
        dynamic_binder<HybridCreature>(*eng, "HybridCreature")
            .constructor<>()
            .property("health", &HybridCreature::health)
            .property("label", &HybridCreature::label)
            .method("speak", &HybridCreature::speak)
            .build();
        dynamic_binder<SlotCreature>(*eng, "SlotCreature")
            .constructor<>()
            .property("health", &SlotCreature::health)
            .property("label", &SlotCreature::label)
            .method("speak", &SlotCreature::speak)
            .build();
        dynamic_binder<NoCopyCreature>(*eng, "NoCopyCreature")
            .constructor<>()
            .property("health", &NoCopyCreature::health)
            .build();
        eng->execute(R"(
            class Voidkin : HybridCreature {
                int chomp = 3;
                int untouched = 11;
                Voidkin() : super() {}
                string speak() override { return "voidkin"; }
            }
            class Slotkin : SlotCreature {
                int chomp = 3;
                int untouched = 11;
                Slotkin() : super() {}
                string speak() override { return "slotkin"; }
            }
            class Lockkin : NoCopyCreature {
                int chomp = 3;
                Lockkin() : super() {}
            }
            var v = Voidkin();
            v.health = 42;
            v.label = "gnasher";
            v.chomp = 7;
            var s = Slotkin();
            s.health = 42;
            s.label = "gnasher";
            s.chomp = 7;
        )");
        return eng;
    }

    // The observable identity of a layer reached from C++: the boundary return IS the layer
    // (script field, override, inherited C++ property all resolve through it).
    template<typename Base>
    void check_layer(const std::shared_ptr<engine>& eng, const std::shared_ptr<Base>& object, const char* global_name,
                     const char* speak, script_int chomp = 7, script_int health = 42) {
        check(eng->script_layer(*object) != nullptr, "object carries a script layer");
        eng->add_global(global_name, eng->make_object(object));
        std::string g(global_name);
        check_eq(chomp, eng->execute(g + ".chomp").as_int(), "script field through the layer");
        check_eq(std::string(speak), eng->execute(g + ".speak()").as<std::string>(), "script override dispatches");
        check_eq(health, eng->execute(g + ".health").as_int(), "C++ property reads through _cpp_object");
    }

    template<typename Base>
    void archive_round_trips(const char* var, const char* cls, const char* speak) {
        auto eng = hybrid_engine();
        auto original = eng->execute(var).as<std::shared_ptr<Base>>();
        check(original != nullptr, "hybrid extracts to its C++ base");
        check_eq(42, original->health);

        jai::serialization::json_archive_writer w(0, eng.get());
        w(original);
        std::string json = w.str();
        check(json.find(cls) != std::string::npos, "$type is the script class: " + json);
        check(json.find("$script") != std::string::npos, "script fields ride along: " + json);

        jai::serialization::json_archive_reader r(json, eng.get());
        std::shared_ptr<Base> loaded;
        r(loaded);
        check(loaded != nullptr);
        check(loaded.get() != original.get(), "a fresh object, not the original");
        check_eq(42, loaded->health);
        check_eq(std::string("gnasher"), loaded->label);
        check_layer(eng, loaded, "loadedJson", speak);

        auto loadedBin = roundtrip_binary(*eng, original);
        check(loadedBin != nullptr);
        check_eq(42, loadedBin->health);
        check_layer(eng, loadedBin, "loadedBinary", speak);
    }

    template<typename Holder, typename Base>
    void holder_round_trips(const char* var, const char* speak) {
        auto eng = hybrid_engine();
        Holder original;
        original.pet = eng->execute(var).as<std::shared_ptr<Base>>();
        original.tag = 5;

        auto json = jai::to_json(*eng, original);
        auto loaded = jai::from_json<Holder>(*eng, json);
        check_eq(5, loaded.tag);
        check(loaded.pet != nullptr);
        check_eq(42, loaded.pet->health);
        check_layer(eng, loaded.pet, "loadedHeldJson", speak);

        auto binary = jai::to_binary(*eng, original);
        auto loadedBin = jai::from_binary<Holder>(*eng, binary);
        check_eq(5, loadedBin.tag);
        check(loadedBin.pet != nullptr);
        check_eq(42, loadedBin.pet->health);
        check_layer(eng, loadedBin.pet, "loadedHeldBinary", speak);
    }

    // A script class deriving a SCRIPT class that derives the C++ base: cpp_base_class_ lives
    // only on the middle class, so every layer path chain-walks (find_cpp_base_class).
    template<typename Base>
    void two_level_chain(const char* mid, const char* leaf, const char* speak) {
        auto eng = hybrid_engine();
        std::string decl = std::string("class ") + leaf + " : " + mid + R"( {
                int fangs = 1;
                )" + leaf + R"(() : super() {}
                string speak() override { return ")" + speak + R"("; }
            }
            var deep = )" + leaf + R"(();
            deep.health = 64;
            deep.chomp = 8;
            deep.fangs = 4;)";
        eng->execute(decl);
        auto original = eng->execute("deep").as<std::shared_ptr<Base>>();
        check(original != nullptr, "two-level hybrid extracts to the C++ base");

        jai::serialization::json_archive_writer w(0, eng.get());
        w(original);
        std::string json = w.str();
        check(json.find(leaf) != std::string::npos, "$type is the most-derived script class: " + json);

        jai::serialization::json_archive_reader r(json, eng.get());
        std::shared_ptr<Base> loaded;
        r(loaded);
        check(loaded != nullptr && loaded->health == 64);
        check_layer(eng, loaded, "loadedDeep", speak, 8, 64);
        check_eq((script_int)4, eng->execute("loadedDeep.fangs").as_int(), "leaf-class script field survives");

        auto loadedBin = roundtrip_binary(*eng, original);
        check(loadedBin != nullptr && loadedBin->health == 64, "binary round trip");
        check(eng->script_layer(*loadedBin) != nullptr, "binary re-attach");
    }

    // A C++ boundary return of a base pointer hands back THE layer: fields and overrides
    // visible, mutation through the return reaches the live instance, a local bound to the
    // return shares it.
    template<typename Base>
    void boundary_identity(const char* var, const char* speak) {
        auto eng = hybrid_engine();
        auto ptr = eng->execute(var).as<std::shared_ptr<Base>>();
        eng->add_function("fetch", [ptr]() { return ptr; });
        check_eq((script_int)7, eng->execute("fetch().chomp").as_int(), "script field visible through boundary return");
        check_eq(std::string(speak), eng->execute("fetch().speak()").as<std::string>(), "override dispatches through boundary return");
        eng->execute("fetch().chomp = 9;");
        check_eq((script_int)9, eng->execute(std::string(var) + ".chomp").as_int(), "mutation through the return reaches the live instance");
        check_eq((script_int)9, eng->execute("var held = fetch(); held.chomp").as_int(), "a local bound to the return shares the instance");
    }

    // engine::extend: an existing C++ object gains a layer with no constructor - field
    // defaults apply, the object carries the layer, the identity survives serialization.
    template<typename Base>
    void extend_existing(const char* cls, const char* speak) {
        auto eng = hybrid_engine();
        auto cpp = std::make_shared<Base>();
        cpp->health = 55;
        auto layer = eng->extend(cpp, cls);
        check(layer.is_object(), "extend returns the layer");
        auto first = eng->script_layer(*cpp);
        check(first != nullptr, "the object carries its layer");
        (void)eng->extend(cpp, cls);
        check(eng->script_layer(*cpp) == first, "extending again with the same class keeps the layer");
        eng->add_function("adopted", [cpp]() { return cpp; });
        check_eq((script_int)3, eng->execute("adopted().chomp").as_int(), "field initializers ran on extend");
        check_eq((script_int)11, eng->execute("adopted().untouched").as_int(), "every declared initializer ran");
        check_eq((script_int)55, eng->execute("adopted().health").as_int(), "C++ state reads through the layer");
        check_eq(std::string(speak), eng->execute("adopted().speak()").as<std::string>(), "override dispatches on the extended object");
        eng->execute("adopted().chomp = 12;");

        std::shared_ptr<Base> base = cpp;
        auto loaded = roundtrip_binary(*eng, base);
        check(loaded != nullptr && loaded->health == 55);
        check_layer(eng, loaded, "loadedExtended", speak, 12, 55);
        check_eq((script_int)11, eng->execute("loadedExtended.untouched").as_int(), "an initialized, never-written field survives the round trip");
    }

    // A plain C++ object crossing the boundary registers no layer.
    template<typename Base>
    void no_pollution() {
        auto eng = hybrid_engine();
        auto plain = std::make_shared<Base>();
        eng->add_global("plainObj", eng->make_object(plain));
        check_eq((script_int)100, eng->execute("plainObj.health").as_int());
        check(eng->script_layer(*plain) == nullptr, "a pure C++ wrapper must not claim a layer");
    }

    // Copy semantics follow the C++ base (S4): assignment deep-copies a copyable base into a
    // second identity; a non-copyable base refuses at the copy site; capture shares.
    template<typename Base>
    void copy_semantics(const char* var) {
        auto eng = hybrid_engine();
        std::string v(var);
        eng->execute("var copy = " + v + "; copy.chomp = 99; copy.health = 1;");
        check_eq((script_int)7, eng->execute(v + ".chomp").as_int(), "assignment copied the script layer");
        check_eq((script_int)42, eng->execute(v + ".health").as_int(), "assignment copied the C++ object too");
        auto original = eng->execute(var).as<std::shared_ptr<Base>>();
        auto copied = eng->execute("copy").as<std::shared_ptr<Base>>();
        check(original.get() != copied.get(), "two C++ identities after a copy");
        check(eng->script_layer(*copied) != nullptr, "the copy carries its own layer");
        eng->execute("auto pin = [" + v + "]() -> void { " + v + ".chomp = 55; }; pin();");
        check_eq((script_int)55, eng->execute(v + ".chomp").as_int(), "by-value capture shares the hybrid");
    }

    void forge_tests() override {

        test("hybrid_archive_round_trip", [&]() {
            archive_round_trips<HybridCreature>("v", "Voidkin", "voidkin");
            archive_round_trips<SlotCreature>("s", "Slotkin", "slotkin");
        });

        test("hybrid_inside_owning_struct_round_trip", [&]() {
            holder_round_trips<hybrid_holder, HybridCreature>("v", "voidkin");
            holder_round_trips<slot_holder, SlotCreature>("s", "slotkin");
        });

        // Property reads are by name, not position: consuming the holder's members in the
        // opposite of written order must work in both archive formats (the $type/$script/$val
        // block included).
        test("hybrid_out_of_order_property_reads", [&]() {
            auto eng = hybrid_engine();
            hybrid_holder original;
            original.pet = eng->execute("v").as<std::shared_ptr<HybridCreature>>();
            original.tag = 5;

            {
                jai::serialization::json_archive_writer w(0, eng.get());
                w(original);
                jai::serialization::json_archive_reader r(w.str(), eng.get());
                std::string tn; uint32_t ver{};
                r.begin_object(tn, ver);
                int tag{};
                r.serialize("tag", tag);
                std::shared_ptr<HybridCreature> pet;
                r.serialize("pet", pet);
                r.end_object();
                check_eq(5, tag);
                check(pet != nullptr && pet->health == 42, "json member read out of written order");
            }
            {
                jai::serialization::binary_archive_writer w(eng.get());
                w(original);
                auto& d = w.data();
                jai::serialization::binary_archive_reader r(d.data(), d.size(), eng.get());
                std::string tn; uint32_t ver{};
                r.begin_object(tn, ver);
                int tag{};
                r.serialize("tag", tag);
                std::shared_ptr<HybridCreature> pet;
                r.serialize("pet", pet);
                r.end_object();
                check_eq(5, tag);
                check(pet != nullptr && pet->health == 42, "binary member read out of written order");
            }
        });

        test("hybrid_stdlib_value_round_trip", [&]() {
            auto eng = hybrid_engine();
            for (const char* var : {"v", "s"}) {
                std::string x(var);
                eng->execute("var " + x + "j = from_json(to_json(" + x + ")); var " + x + "b = from_binary(to_binary(" + x + "));");
                check_eq((script_int)42, eng->execute(x + "j.health").as_int());
                check_eq(std::string("gnasher"), eng->execute(x + "j.label").as<std::string>());
                check_eq((script_int)7, eng->execute(x + "j.chomp").as_int());
                check_eq((script_int)42, eng->execute(x + "b.health").as_int());
                check_eq((script_int)7, eng->execute(x + "b.chomp").as_int());
            }
            check_eq(std::string("voidkin"), eng->execute("vj.speak()").as<std::string>());
            check_eq(std::string("slotkin"), eng->execute("sb.speak()").as<std::string>());
        });

        test("cpp_object_handle_stays_out_of_json", [&]() {
            auto eng = hybrid_engine();
            for (const char* var : {"v", "s"}) {
                std::string json = eng->execute(std::string("to_json(") + var + ")").as<std::string>();
                check(json.find("_cpp_object") == std::string::npos, "the _cpp_object handle must not leak: " + json);
                check(json.find("chomp") != std::string::npos, "script fields serialize: " + json);
                check(json.find("health") != std::string::npos, "inherited C++ properties serialize: " + json);
            }
        });

        test("hybrid_two_level_script_chain_round_trip", [&]() {
            two_level_chain<HybridCreature>("Voidkin", "Voidspawn", "voidspawn");
            two_level_chain<SlotCreature>("Slotkin", "Slotspawn", "slotspawn");
        });

        test("hybrid_boundary_return_is_owning_instance", [&]() {
            boundary_identity<HybridCreature>("v", "voidkin");
            boundary_identity<SlotCreature>("s", "slotkin");
        });

        test("extend_existing_cpp_object", [&]() {
            extend_existing<HybridCreature>("Voidkin", "voidkin");
            extend_existing<SlotCreature>("Slotkin", "slotkin");
        });

        test("pure_cpp_crossing_registers_no_layer", [&]() {
            no_pollution<HybridCreature>();
            no_pollution<SlotCreature>();
        });

        test("hybrid_copy_semantics_follow_the_cpp_base", [&]() {
            copy_semantics<HybridCreature>("v");
            copy_semantics<SlotCreature>("s");
            auto eng = hybrid_engine();
            eng->execute("var locked = Lockkin();");
            std::string message;
            try { eng->execute("var stolen = locked;"); } catch (const std::exception& e) { message = e.what(); }
            check(message.find("nocopy class 'Lockkin'") != std::string::npos,
                  "a hybrid over a non-copyable base is nocopy by inheritance: " + message);
            check_eq((script_int)3, eng->execute("locked.chomp").as_int(), "the original is untouched");
        });

        test("extend_errors_name_both_types", [&]() {
            auto eng = hybrid_engine();
            auto slot = std::make_shared<SlotCreature>();
            std::string message;
            try { (void)eng->extend(slot, "Voidkin"); } catch (const std::exception& e) { message = e.what(); }
            check(message.find("Voidkin") != std::string::npos && message.find("SlotCreature") != std::string::npos,
                  "a class over the wrong C++ base names both: " + message);
            check(eng->script_layer(*slot) == nullptr, "a refused extend attaches nothing");

            message.clear();
            try { (void)eng->extend(slot, "NoSuchClass"); } catch (const std::exception& e) { message = e.what(); }
            check(message.find("NoSuchClass") != std::string::npos, "unknown script class is named: " + message);

            std::shared_ptr<SlotCreature> null_object;
            check_throws([&]() { (void)eng->extend(null_object, "Slotkin"); }, "extending null throws");

            // the script builtin reports through the error channel: catchable in script
            eng->add_global("rawSlot", eng->make_object(slot));
            check_eq(std::string("refused"), eng->execute(R"(
                var outcome = "silent";
                try { extend(rawSlot, Voidkin); } catch (e) { outcome = "refused"; }
                outcome;
            )").as<std::string>(), "script sees the rejection as an exception");
            check_throws([&]() { eng->execute("extend(rawSlot, 5);"); }, "a non-class second argument is refused");
            check_throws([&]() { eng->execute("extend(rawSlot);"); }, "arity is enforced");
        });

        // Re-derive: another class over an already layered object replaces the layer, carrying
        // the fields both classes declare and retiring the old layer; the same class is idempotent.
        test("extend_rederives_and_is_idempotent", [&]() {
            auto eng = hybrid_engine();
            eng->execute(R"(
                class Slotspawn : Slotkin {
                    int fangs = 1;
                    Slotspawn() : super() {}
                    string speak() override { return "slotspawn"; }
                }
            )");
            auto slot = std::make_shared<SlotCreature>();
            eng->add_global("raw", eng->make_object(slot));
            eng->add_global("same", eng->make_object(slot));
            eng->execute("extend(raw, Slotkin); raw.chomp = 9;");
            check(eng->script_layer(*slot) != nullptr, "script extend attached the layer");
            check_eq((script_int)9, eng->execute("extend(raw, Slotkin).chomp").as_int(), "same class returns the live layer");
            check_eq((script_int)9, eng->execute("extend(raw, \"Slotkin\").chomp").as_int(), "a string names the class too");
            check_eq((script_int)9, eng->execute("extend(same, Slotkin).chomp").as_int(), "another handle to the object is the same layer");

            check(eng->execute("raw").get_class_instance() == eng->script_layer(*slot), "raw is the layer after the first extend");
            eng->execute("extend(raw, Slotspawn);");
            check_eq(std::string("Slotspawn"), eng->script_layer(*slot)->get_class_definition()->get_name(), "the object carries the new layer");
            check(eng->execute("raw").get_class_instance() == eng->script_layer(*slot), "raw is still the layer after the re-derive");
            check_eq(std::string("Slotspawn"), eng->execute("raw").get_class_instance()->get_class_definition()->get_name(), "raw's instance is Slotspawn");
            check_eq((script_int)9, eng->execute("raw.chomp").as_int(), "a field both classes declare carries over");
            check(eng->script_layer(*slot)->has_field(eng->symbolize("fangs")), "the new class's field exists on the instance");
            check_eq((script_int)1, eng->execute("raw.fangs").as_int(), "the new class's own fields take their initializers");
            check_eq(std::string("slotspawn"), eng->execute("raw.speak()").as<std::string>(), "the new class dispatches");
            check_eq((script_int)1, eng->execute("same.fangs").as_int(), "a wrapper made before the extend resolves to the layer");
            check_eq(std::string("slotspawn"), eng->execute("same.speak()").as<std::string>(), "and dispatches the layer's overrides");
            check_eq((script_int)100, eng->execute("same.health").as_int(), "and still reaches the object");
            eng->execute("same.chomp = 12;");
            check_eq((script_int)12, eng->execute("raw.chomp").as_int(), "writes through the stale wrapper land on the layer");
        });

        // The handle you passed in IS the layer afterwards: no rebinding.
        test("extend_upgrades_the_handle_in_place", [&]() {
            auto eng = hybrid_engine();
            auto slot = std::make_shared<SlotCreature>();
            eng->add_global("kin", eng->make_object(slot));
            eng->execute("extend(kin, Slotkin); kin.chomp = 17;");
            eng->add_global("again", eng->make_object(slot));
            check_eq((script_int)17, eng->execute("again.chomp").as_int(), "the object's layer is the handle that was extended");
            check_eq(std::string("slotkin"), eng->execute("kin.speak()").as<std::string>(), "overrides dispatch on the upgraded handle");
            auto plain = std::make_shared<HybridCreature>();
            eng->add_global("pk", eng->make_object(plain));
            eng->execute("extend(pk, Voidkin); pk.chomp = 21;");
            eng->add_global("pk2", eng->make_object(plain));
            check_eq((script_int)21, eng->execute("pk2.chomp").as_int(), "the map path upgrades in place too");
        });

        // A pure script instance re-derives in place too: the instance keeps its fields and
        // gains the derived class's.
        test("extend_rederives_pure_script_instances", [&]() {
            auto eng = hybrid_engine();
            eng->execute(R"(
                class Pt { int a = 1; }
                class Pt3 : Pt { int b = 2; Pt3() : super() {} }
                var p = Pt();
                p.a = 5;
            )");
            check_eq((script_int)5, eng->execute("extend(p, Pt).a").as_int(), "same class returns the instance");
            eng->execute("extend(p, Pt3);");
            check_eq((script_int)5, eng->execute("p.a").as_int(), "base field kept");
            check_eq((script_int)2, eng->execute("p.b").as_int(), "derived field initialized on the same instance");
            eng->execute("extend(p, Pt3).a = 6;");
            check_eq((script_int)6, eng->execute("p.a").as_int(), "chained access reaches the instance");
            check_throws([&]() { eng->execute("extend(p, Pt);"); }, "a class that does not derive the instance's class is refused");
        });

        // An extended object owns its layer: when the object dies, a layer a script still holds
        // is retired (C++ access reports, fields stay). A slotless object's layer is engine-owned
        // and keeps the object alive until script_layer_detach.
        test("extended_object_owns_its_layer", [&]() {
            auto eng = hybrid_engine();
            auto slot = std::make_shared<SlotCreature>();
            eng->add_global("dying", eng->extend(slot, "Slotkin"));
            eng->execute("dying.chomp = 4;");
            slot.reset();
            check_eq((script_int)4, eng->execute("dying.chomp").as_int(), "script fields survive the object");
            check_throws([&]() { eng->execute("dying.health"); }, "C++ access through a dead object's layer reports");

            auto plain = std::make_shared<HybridCreature>();
            HybridCreature* raw = plain.get();
            eng->add_global("kept", eng->extend(plain, "Voidkin"));
            plain.reset();
            check_eq((script_int)100, eng->execute("kept.health").as_int(), "the engine-owned layer keeps a slotless object alive");
            std::shared_ptr<HybridCreature> alias(std::shared_ptr<HybridCreature>(), raw);
            check(!eng->script_layer_detach(alias).empty(), "detach releases it");
            check_throws([&]() { eng->execute("kept.health"); }, "the detached layer is retired");
        });

        // S6/D7: the layer moves between engines through the name-keyed representation. Slot
        // types move on extend; map types move through the explicit detach + extend pair.
        test("script_layer_moves_between_engines", [&]() {
            auto a = hybrid_engine();
            auto b = hybrid_engine();

            auto slot = std::make_shared<SlotCreature>();
            slot->health = 33;
            (void)a->extend(slot, "Slotkin");
            a->add_global("ka", a->make_object(slot));
            a->execute("ka.chomp = 21;");

            (void)b->extend(slot, "Slotkin");
            check(b->script_layer(*slot) != nullptr, "b owns the layer now");
            check(a->script_layer(*slot) == nullptr, "a no longer owns a layer for it");
            b->add_global("kb", b->make_object(slot));
            check_eq((script_int)21, b->execute("kb.chomp").as_int(), "script state moved by name");
            check_eq((script_int)33, b->execute("kb.health").as_int(), "the C++ object is the same one");
            b->execute("kb.chomp = 22;");
            check_eq((script_int)21, a->execute("ka.chomp").as_int(), "a's retired layer keeps its own fields");
            check_throws([&]() { a->execute("ka.health"); }, "a's retired layer no longer reaches the object");
            check_eq(33, slot->health, "the object itself is untouched");

            auto plain = std::make_shared<HybridCreature>();
            (void)a->extend(plain, "Voidkin");
            a->add_global("pa", a->make_object(plain));
            a->execute("pa.chomp = 31;");
            std::string carried = a->script_layer_detach(plain);
            check(carried.find("31") != std::string::npos, "detach returns the fields: " + carried);
            check(a->script_layer(*plain) == nullptr, "detach cleared a's map entry");
            (void)b->extend(plain, "Voidkin", carried);
            b->add_global("pb", b->make_object(plain));
            check_eq((script_int)31, b->execute("pb.chomp").as_int(), "explicit two-step migration carries fields");
            check_eq(std::string(""), a->script_layer_detach(plain), "detaching an unlayered object is a no-op");
        });

        // The mixin's own state door: to_json gives {"$type","$script"}, from_json hydrates the
        // live layer, or re-derives first when $type names another class.
        test("a_constructorless_script_class_constructs_its_cpp_base", [&]() {
            // C++: the implicit default constructor runs the base's default constructor first
            auto eng = hybrid_engine();
            eng->execute(R"(
                class Plainkin : SlotCreature { int chomp = 3; }
                var p = Plainkin();
                p.health = 7;
                p.chomp = 17;
                class Deepkin : Plainkin { int fangs = 2; }
                var d = Deepkin();
                d.health = 9;
            )");
            check_eq((script_int)7, eng->execute("p.health").as_int(), "the inherited C++ property stores and reads");
            check_eq((script_int)17, eng->execute("p.chomp").as_int());
            auto object = eng->execute("p").as<std::shared_ptr<SlotCreature>>();
            check(object != nullptr, "the instance extracts to its C++ base");
            check_eq(7, object->health, "the store reached the C++ object");
            check_eq((script_int)9, eng->execute("d.health").as_int(), "a two-level constructor-less chain too");
            check_eq((script_int)2, eng->execute("d.fangs").as_int());
        });

        test("a_script_layer_saves_only_its_script_fields", [&]() {
            auto eng = hybrid_engine();
            auto slot = std::make_shared<SlotCreature>();
            slot->health = 55;
            eng->add_global("kin", eng->extend(slot, "Slotkin"));
            eng->execute("kin.chomp = 17;");
            std::string state = slot->script_layer_to_json();
            check(state.find("\"chomp\":17") != std::string::npos, "the script field: " + state);
            check(state.find("health") == std::string::npos, "an inherited C++ property is the C++ object's, not the layer's: " + state);
            jai::serialization::json_archive_writer w(0, eng.get());
            w(std::shared_ptr<SlotCreature>(slot));
            std::string json = w.str();
            check(json.find("null") == std::string::npos, "no null inherited property in the archive: " + json);
            check(json.find("55") != std::string::npos, "the C++ property saves through the C++ object: " + json);
        });

        test("script_extensible_json_state_round_trip", [&]() {
            auto eng = hybrid_engine();
            eng->execute(R"(
                class Slotspawn : Slotkin {
                    int fangs = 1;
                    Slotspawn() : super() {}
                }
            )");
            auto slot = std::make_shared<SlotCreature>();
            check_eq(std::string(""), slot->script_layer_to_json(), "no layer, no state");
            check(!slot->script_layer_from_json("{}"), "no layer to hydrate");
            eng->add_global("kin", eng->extend(slot, "Slotkin"));
            eng->execute("kin.chomp = 17;");
            std::string state = slot->script_layer_to_json();
            check(state.find("\"$type\"") != std::string::npos && state.find("Slotkin") != std::string::npos &&
                  state.find("17") != std::string::npos, "state carries the class and the fields: " + state);

            auto twin = std::make_shared<SlotCreature>();
            eng->add_global("twin", eng->extend(twin, "Slotkin"));
            check(twin->script_layer_from_json(state), "same class hydrates in place");
            check_eq((script_int)17, eng->execute("twin.chomp").as_int(), "fields hydrated by name");

            eng->execute("kin.chomp = 18;");
            std::string spawnState = std::string("{\"$type\":\"Slotspawn\",\"$script\":{\"chomp\":18,\"fangs\":7}}");
            check(slot->script_layer_from_json(spawnState), "another $type re-derives");
            check_eq(std::string("Slotspawn"), eng->script_layer(*slot)->get_class_definition()->get_name());
            eng->add_global("spawn", eng->make_object(slot));
            check_eq((script_int)7, eng->execute("spawn.fangs").as_int(), "the re-derived layer took the new class's fields");
            check_eq((script_int)18, eng->execute("spawn.chomp").as_int());
        });

        test("property_owner_carries_the_slot", [&]() {
            auto eng = hybrid_engine();
            POMultiDerived multi;
            check(!multi.has_script_layer(), "fresh object: no layer");
            auto probe = eng->execute("Slotkin()");
            auto layer = probe.as<std::shared_ptr<class_instance>>();
            multi.jai_script_slot().attach_script_layer(layer);
            check(multi.script_layer() == layer, "the multi-base slot resolves through the first extensible base");
            check(eng->script_layer(multi) == layer, "engine::script_layer reads the same slot");
            multi.jai_script_slot().detach_script_layer();
            check(!multi.has_script_layer());
        });

        test("binary_writer_unresolvable_type_still_throws", [&]() {
            // The widened door admits types the WRITER's engine can resolve (metadata or a
            // class definition); a foreign engine's writer resolves neither, so the
            // throw-on-unregistered guarantee holds instead of emitting an unloadable payload.
            auto eng = hybrid_engine();
            auto other = make_engine();
            script_value v = eng->execute("v");
            check_throws([&]() {
                jai::serialization::binary_archive_writer writer(other.get());
                writer.write_value(v);
            }, "a type the writer's engine cannot resolve still throws");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::polymorphic_serialization_tests)
FOUNDRY_REGISTER(jai::foundry::tests::implicit_registration_tests)
FOUNDRY_REGISTER(jai::foundry::tests::hybrid_serialization_tests)

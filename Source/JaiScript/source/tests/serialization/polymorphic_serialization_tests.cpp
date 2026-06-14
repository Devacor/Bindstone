#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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

// ============================================================================
// Implicit polymorphic registration (property_owner) + loud unregistered failure
// ============================================================================

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
            auto eng = engine::make();
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
            auto eng = engine::make();
            auto derived = std::make_shared<ImplicitRegDerived>();
            derived->derivedTag = std::string("tagged");
            std::shared_ptr<ImplicitRegBase> original = derived;

            jai::serialization::json_archive_writer w(0, eng.get());
            w(original);
            auto json = w.str();
            check(json.find("\"$type\"") != std::string::npos, "expected a $type discriminator: " + json);
            check(json.find("ImplicitRegDerived") != std::string::npos, "expected the implicit class name: " + json);
        });

        test("property_owner_implicit_round_trip", [&]() {
            auto eng = engine::make();
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

        test("explicit_registrar_overrides_implicit_name", [&]() {
            auto eng = engine::make();
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
            auto eng = engine::make();
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
            auto eng = engine::make();
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
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::polymorphic_serialization_tests)
FOUNDRY_REGISTER(jai::foundry::tests::implicit_registration_tests)

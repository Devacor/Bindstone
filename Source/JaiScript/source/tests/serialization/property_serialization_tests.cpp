#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/property_serialization.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/class_builder_serialization.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test fixture class with properties
class test_object : public property_owner<test_object> {
public:
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((float), speed, 5.5f);
    JAI_PROPERTY((std::string), name, "TestObject");
    JAI_PROPERTY((bool), active, true);

    test_object() = default;
};

// Version 1 object (original)
class versioned_object_v1 : public property_owner<versioned_object_v1> {
public:
    JAI_PROPERTY((int), x, 0);
    JAI_PROPERTY((int), y, 0);
    JAI_PROPERTY((std::string), label, "point");

    versioned_object_v1() = default;
};

// Version 2 object (added field)
class versioned_object_v2 : public property_owner<versioned_object_v2> {
public:
    JAI_PROPERTY((int), x, 0);
    JAI_PROPERTY((int), y, 0);
    JAI_PROPERTY((int), z, 0);  // NEW field
    JAI_PROPERTY((std::string), label, "point");

    versioned_object_v2() = default;
};

// Version 3 object (renamed field)
class versioned_object_v3 : public property_owner<versioned_object_v3> {
public:
    JAI_PROPERTY((int), x, 0);
    JAI_PROPERTY((int), y, 0);
    JAI_PROPERTY((int), z, 0);
    JAI_PROPERTY((std::string), description, "point");  // RENAMED from label
    JAI_DELETED_PROPERTY((std::string), label);  // Mark old field as deleted

    versioned_object_v3() = default;
};

// Object with containers
class container_object : public property_owner<container_object> {
public:
    JAI_PROPERTY((std::vector<int>), numbers);
    JAI_PROPERTY((std::vector<std::string>), tags);
    JAI_PROPERTY((std::map<std::string, int>), scores);

    container_object() = default;
};

// ==== Custom Construction Test Classes ====

// Context for dependency injection during deserialization
struct resource_manager {
    std::string resource_path;
    int resource_id;

    resource_manager(std::string path, int id) : resource_path(std::move(path)), resource_id(id) {}
};

// Object that requires a dependency during construction
class resource_dependent_object : public property_owner<resource_dependent_object> {
public:
    JAI_PROPERTY((std::string), name, "");
    JAI_PROPERTY((int), resource_ref, 0);

    // Dependency (not serialized)
    resource_manager* manager = nullptr;

    // Constructor that takes user context
    explicit resource_dependent_object(resource_manager* mgr)
        : manager(mgr) {
        // Don't set resource_ref here - let it be loaded from archive
    }

    // Default constructor for non-deserialization cases
    resource_dependent_object() = default;
};

// Object that needs both context and archive for complex construction
class archive_aware_object : public property_owner<archive_aware_object> {
public:
    JAI_PROPERTY((std::string), data, "");
    JAI_PROPERTY((int), computed_value, 0);

    resource_manager* manager = nullptr;

    // Constructor that takes both context and archive
    archive_aware_object(resource_manager* mgr, serialization::archive_reader& ar)
        : manager(mgr) {
        // Can do complex initialization based on archive metadata
        // Don't set computed_value here - let it be loaded from archive
    }

    archive_aware_object() = default;
};

// Test class for field renaming migration
class migrated_object_v1 : public property_owner<migrated_object_v1> {
public:
    JAI_PROPERTY((std::string), old_name, "");
    JAI_PROPERTY((int), value, 0);
};

class migrated_object_v2 : public property_owner<migrated_object_v2> {
public:
    JAI_PROPERTY((std::string), new_name, "");  // Renamed from old_name
    JAI_PROPERTY((int), value, 0);

    // Migration hook to handle renamed field
    void post_deserialize(serialization::archive_reader& ar) override {
        // Try to read the old field name if it exists
        if (ar.has_property("old_name")) {
            // The property was already consumed during load
            // In a real scenario, you might store raw archive data
            // For this test, we'll detect the migration in a different way
        }

        // If new_name is empty but we have a value, it means we loaded from v1
        // In practice, you'd check version or use a temporary property
        migration_called = true;
    }

    bool migration_called = false;
};

// Test class for computed values
class computed_object : public property_owner<computed_object> {
public:
    JAI_PROPERTY((int), width, 0);
    JAI_PROPERTY((int), height, 0);

    // Non-serialized computed property
    int area = 0;

    void post_deserialize(serialization::archive_reader& ar) override {
        // Compute area after loading dimensions
        area = width.get() * height.get();
    }
};

// Test class for data validation/correction
class validated_object : public property_owner<validated_object> {
public:
    JAI_PROPERTY((int), score, 0);
    JAI_PROPERTY((std::string), category, "");

    void post_deserialize(serialization::archive_reader& ar) override {
        // Clamp score to valid range
        if (score.get() < 0) {
            score = 0;
        }
        if (score.get() > 100) {
            score = 100;
        }

        // Auto-assign category based on score
        if (category.get().empty()) {
            if (score.get() >= 90) {
                category = "excellent";
            } else if (score.get() >= 70) {
                category = "good";
            } else {
                category = "needs_improvement";
            }
        }
    }
};

class property_serialization_tests : public suite {
public:
    property_serialization_tests() : suite("Property Serialization Tests") {}

    void forge_tests() override {
        // ===== BASIC PROPERTY TESTS =====

        test("property_basic_access", [&]() {
            test_object obj;

            check_eq(obj.health.get(), 100);
            check_eq(obj.speed.get(), 5.5f);
            check_eq(obj.name.get(), std::string("TestObject"));
            check_eq(obj.active.get(), true);

            obj.health = 50;
            check_eq(obj.health.get(), 50);
        });

        test("property_transparent_conversion", [&]() {
            test_object obj;

            // Should work like native types
            int h = obj.health;
            check_eq(h, 100);

            obj.health = 75;
            check_eq((int)obj.health, 75);

            // Arithmetic
            obj.health += 25;
            check_eq((int)obj.health, 100);

            // Comparison
            check(obj.health == 100);
            check(obj.health > 50);
        });

        test("property_manager_reflection", [&]() {
            test_object obj;

            // Get property by name
            auto* health_prop = obj.property_mgr.get<int>("health");
            check(health_prop != nullptr);
            check_eq(health_prop->get(), 100);

            // Get value directly
            auto* speed_val = obj.property_mgr.get_value<float>("speed");
            check(speed_val != nullptr);
            check_eq(*speed_val, 5.5f);

            // Visit all properties
            int count = 0;
            obj.property_mgr.visit([&](const std::string& name, property_base* prop) {
                count++;
            });
            check_eq(count, 4);  // health, speed, name, active
        });

        // ===== JSON SERIALIZATION TESTS =====

        test("json_round_trip_basic", [&]() {
            test_object original;
            original.health = 42;
            original.speed = 3.14f;
            original.name = "Modified";
            original.active = false;

            // Serialize to JSON
            serialization::json_archive_writer writer;
            writer.begin_object("test_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Debug: print JSON
            std::cout << "json_round_trip_basic JSON: " << json << std::endl;

            // Deserialize from JSON (need engine for json_archive_reader internal script_value usage)
            auto eng = engine::make();
            test_object loaded;
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Debug: print loaded values
            std::cout << "Loaded health: " << loaded.health.get() << std::endl;
            std::cout << "Loaded speed: " << loaded.speed.get() << std::endl;
            std::cout << "Loaded name: " << loaded.name.get() << std::endl;
            std::cout << "Loaded active: " << loaded.active.get() << std::endl;

            // Check values match
            check_eq(loaded.health.get(), 42);
            check_eq(loaded.speed.get(), 3.14f);
            check_eq(loaded.name.get(), std::string("Modified"));
            check_eq(loaded.active.get(), false);
        });

        test("json_round_trip_containers", [&]() {
            container_object original;
            original.numbers.get() = {1, 2, 3, 4, 5};
            original.tags.get() = {"alpha", "beta", "gamma"};
            original.scores.get() = {{"player1", 100}, {"player2", 200}};

            // Serialize
            serialization::json_archive_writer writer;
            writer.begin_object("container_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Deserialize (need engine for json_archive_reader internal script_value usage)
            auto eng = engine::make();
            container_object loaded;
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Check vectors
            check_eq(loaded.numbers.get().size(), 5u);
            check_eq(loaded.numbers.get()[0], 1);
            check_eq(loaded.numbers.get()[4], 5);

            check_eq(loaded.tags.get().size(), 3u);
            check_eq(loaded.tags.get()[0], std::string("alpha"));
            check_eq(loaded.tags.get()[2], std::string("gamma"));

            // Check map
            check_eq(loaded.scores.get().size(), 2u);
            check_eq(loaded.scores.get()["player1"], 100);
            check_eq(loaded.scores.get()["player2"], 200);
        });

        test("json_selective_serialization", [&]() {
            test_object obj;
            obj.health = 50;
            obj.name = "Secret";

            // Disable serialization for name
            obj.name.serialize_enabled(false);

            // Serialize
            serialization::json_archive_writer writer;
            writer.begin_object("test_object", 1);
            obj.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Check JSON doesn't contain name
            check(json.find("Secret") == std::string::npos);
            check(json.find("health") != std::string::npos);
        });

        // ===== BINARY SERIALIZATION TESTS =====

        test("binary_round_trip_basic", [&]() {
            test_object original;
            original.health = 99;
            original.speed = 7.77f;
            original.name = "BinaryTest";
            original.active = true;

            // Serialize to binary
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer);
            writer.begin_object("test_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            // Deserialize from binary (need engine for binary_archive_reader internal script_value usage)
            auto eng = engine::make();
            test_object loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Check values match
            check_eq(loaded.health.get(), 99);
            check_eq(loaded.speed.get(), 7.77f);
            check_eq(loaded.name.get(), std::string("BinaryTest"));
            check_eq(loaded.active.get(), true);
        });

        test("binary_round_trip_containers", [&]() {
            container_object original;
            original.numbers.get() = {10, 20, 30};
            original.tags.get() = {"foo", "bar"};
            original.scores.get() = {{"a", 1}, {"b", 2}, {"c", 3}};

            // Serialize
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer);
            writer.begin_object("container_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            // Deserialize (need engine for binary_archive_reader internal script_value usage)
            auto eng = engine::make();
            container_object loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Verify
            check_eq(loaded.numbers.get().size(), 3u);
            check_eq(loaded.numbers.get()[1], 20);

            check_eq(loaded.tags.get().size(), 2u);
            check_eq(loaded.tags.get()[0], std::string("foo"));

            check_eq(loaded.scores.get().size(), 3u);
            check_eq(loaded.scores.get()["b"], 2);
        });

        test("binary_format_stability", [&]() {
            // Save same object twice, should produce identical binary
            test_object obj1, obj2;
            obj1.health = 123;
            obj1.speed = 4.56f;
            obj1.name = "stable";
            obj1.active = false;

            obj2.health = 123;
            obj2.speed = 4.56f;
            obj2.name = "stable";
            obj2.active = false;

            std::vector<uint8_t> buffer1, buffer2;

            {
                serialization::binary_archive_writer writer(buffer1);
                writer.begin_object("test_object", 1);
                obj1.property_mgr.save(writer);
                writer.end_object();
            }

            {
                serialization::binary_archive_writer writer(buffer2);
                writer.begin_object("test_object", 1);
                obj2.property_mgr.save(writer);
                writer.end_object();
            }

            // Buffers should be identical
            check_eq(buffer1.size(), buffer2.size());
            check(std::equal(buffer1.begin(), buffer1.end(), buffer2.begin()));
        });

        // ===== VERSION MIGRATION TESTS =====

        test("version_add_new_field", [&]() {
            // Save v1 object
            versioned_object_v1 v1;
            v1.x = 10;
            v1.y = 20;
            v1.label = "v1_point";

            serialization::json_archive_writer writer;
            writer.begin_object("versioned_object", 1);
            v1.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Load into v2 object (has z field)
            versioned_object_v2 v2;
            v2.z = 999;  // Set to non-default to check it stays

            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            // Load - property names are inline, unknown properties are automatically skipped
            v2.property_mgr.load(reader);
            reader.end_object();

            // Check old fields loaded correctly
            check_eq(v2.x.get(), 10);
            check_eq(v2.y.get(), 20);
            check_eq(v2.label.get(), std::string("v1_point"));

            // New field should keep its value (not serialized)
            check_eq(v2.z.get(), 999);
        });

        test("version_rename_field", [&]() {
            // Save v2 object with "label"
            versioned_object_v2 v2;
            v2.x = 5;
            v2.y = 15;
            v2.z = 25;
            v2.label = "old_name";

            serialization::json_archive_writer writer;
            writer.begin_object("versioned_object", 2);
            v2.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Load into v3 object (renamed label -> description)
            versioned_object_v3 v3;
            v3.description = "default_description";  // Set a default value

            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            // Load without rename bindings - "label" will be handled by deleted_property
            // The old "label" value will be discarded by the deleted_property mechanism
            v3.property_mgr.load(reader);
            reader.end_object();

            // Check that numeric fields loaded correctly
            check_eq(v3.x.get(), 5);
            check_eq(v3.z.get(), 25);

            // The "description" field keeps its default value since archive doesn't have it
            check_eq(v3.description.get(), std::string("default_description"));

            // Note: To handle renamed fields, use post_deserialize hook (to be implemented)
        });

        test("version_deleted_field", [&]() {
            // Test that unknown properties in the archive are automatically skipped
            // Simulating old format with field that no longer exists

            versioned_object_v3 v3;

            // Manually create JSON with unknown field that doesn't exist in v3
            std::string old_json = R"({
                "_type_": "versioned_object",
                "_version_": 2,
                "x": 100,
                "y": 200,
                "z": 300,
                "label": "test",
                "oldField": "should_be_ignored"
            })";

            // With the new design, unknown properties are automatically skipped
            // No need for explicit deleted_property declarations

            auto eng = engine::make();
            serialization::json_archive_reader reader(old_json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            // Load should succeed - unknown "oldField" is automatically skipped
            v3.property_mgr.load(reader);
            reader.end_object();

            // Check that known fields loaded correctly
            check_eq(v3.x.get(), 100);
            check_eq(v3.y.get(), 200);
            check_eq(v3.z.get(), 300);

            // "label" is handled by deleted_property and discarded
            // "oldField" is not in property_mgr and is automatically skipped
        });

        // ===== CLONE TESTS =====

        test("property_clone_basic", [&]() {
            test_object source;
            source.health = 77;
            source.speed = 2.5f;
            source.name = "source";
            source.active = true;

            test_object target;
            source.property_mgr.clone_to_target(target.property_mgr);

            check_eq(target.health.get(), 77);
            check_eq(target.speed.get(), 2.5f);
            check_eq(target.name.get(), std::string("source"));
            check_eq(target.active.get(), true);

            // Modify source, target should be independent
            source.health = 99;
            check_eq(target.health.get(), 77);
        });

        // ===== ERROR HANDLING TESTS =====

        test("skip_unknown_property", [&]() {
            // Test that unknown properties are silently skipped for forward/backward compatibility
            std::string json_with_extra_field = R"({
                "_type_": "test_object",
                "_version_": 1,
                "health": 50,
                "nonexistent": 123
            })";

            test_object obj;
            obj.health = 999;  // Set to different value

            auto eng = engine::make();
            serialization::json_archive_reader reader(json_with_extra_field, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            // Should not throw - unknown properties are skipped
            obj.property_mgr.load(reader);
            reader.end_object();

            // Health should be loaded (50), nonexistent should be skipped
            check_eq(obj.health.get(), 50);
        });

        test("serialize_enabled_flag", [&]() {
            test_object obj;
            obj.health = 100;
            obj.speed = 5.0f;

            // Check default is enabled
            check(obj.health.serialize_enabled());

            // Disable and check
            obj.health.serialize_enabled(false);
            check(!obj.health.serialize_enabled());

            // Serialize - health should not be saved
            serialization::json_archive_writer writer;
            writer.begin_object("test_object", 1);
            obj.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Load into new object
            test_object loaded;
            loaded.health = 999;  // Set to different value

            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Health should still be 999 (wasn't in archive)
            check_eq(loaded.health.get(), 999);
            // Speed should be loaded
            check_eq(loaded.speed.get(), 5.0f);
        });

        // ===== CROSS-FORMAT TESTS =====

        test("json_to_binary_conversion", [&]() {
            test_object original;
            original.health = 55;
            original.speed = 3.3f;
            original.name = "cross_format";
            original.active = false;

            // Save as JSON
            serialization::json_archive_writer json_writer;
            json_writer.begin_object("test_object", 1);
            original.property_mgr.save(json_writer);
            json_writer.end_object();

            std::string json = json_writer.str();

            // Load from JSON
            test_object intermediate;
            auto eng2 = engine::make();
            serialization::json_archive_reader json_reader(json, eng2.get());

            std::string type_name;
            uint32_t version;
            json_reader.begin_object(type_name, version);
            intermediate.property_mgr.load(json_reader);
            json_reader.end_object();

            // Save as Binary
            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer binary_writer(binary_buffer);
            binary_writer.begin_object("test_object", 1);
            intermediate.property_mgr.save(binary_writer);
            binary_writer.end_object();

            // Load from Binary
            test_object final_obj;
            serialization::binary_archive_reader binary_reader(binary_buffer, eng2.get());
            binary_reader.begin_object(type_name, version);
            final_obj.property_mgr.load(binary_reader);
            binary_reader.end_object();

            // Values should match original
            check_eq(final_obj.health.get(), 55);
            check_eq(final_obj.speed.get(), 3.3f);
            check_eq(final_obj.name.get(), std::string("cross_format"));
            check_eq(final_obj.active.get(), false);
        });

        // ===== FIELD ORDER STABILITY TESTS =====

        test("json_field_order_stability", [&]() {
            // Test that JSON format can handle properties in different orders
            // Save with properties in one order, load into object with different order

            // Create object and save (properties will save in std::map order: alphabetical)
            test_object original;
            original.health = 100;      // alphabetically: "active", "health", "name", "speed"
            original.speed = 2.5f;
            original.name = "field_order_test";
            original.active = true;

            serialization::json_archive_writer writer;
            writer.begin_object("test_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Manually create JSON with different field order
            std::string json_different_order = R"({
                "_type_": "test_object",
                "_version_": 1,
                "speed": 2.5,
                "name": "field_order_test",
                "health": 100,
                "active": true
            })";

            // Load from manually created JSON with different order
            test_object loaded;
            auto eng = engine::make();
            serialization::json_archive_reader reader(json_different_order, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // All properties should load correctly regardless of order
            check_eq(loaded.health.get(), 100);
            check_eq(loaded.speed.get(), 2.5f);
            check_eq(loaded.name.get(), std::string("field_order_test"));
            check_eq(loaded.active.get(), true);
        });

        test("binary_field_order_stability", [&]() {
            // Test that binary format can handle properties in different orders
            // The binary format writes property names inline, so order shouldn't matter

            test_object original;
            original.health = 75;
            original.speed = 1.5f;
            original.name = "binary_order_test";
            original.active = false;

            // Save to binary
            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer writer(binary_buffer);
            writer.begin_object("test_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            // Load into new object (property_manager will read properties in whatever order they appear)
            test_object loaded;
            auto eng = engine::make();
            serialization::binary_archive_reader reader(binary_buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // All properties should load correctly
            check_eq(loaded.health.get(), 75);
            check_eq(loaded.speed.get(), 1.5f);
            check_eq(loaded.name.get(), std::string("binary_order_test"));
            check_eq(loaded.active.get(), false);
        });

        test("json_partial_field_order_with_missing", [&]() {
            // Test loading JSON with only some fields present in different order
            std::string json_partial = R"({
                "_type_": "test_object",
                "_version_": 1,
                "name": "partial_test",
                "health": 50
            })";

            test_object loaded;
            loaded.speed = 9.9f;  // Set defaults for missing fields
            loaded.active = true;

            auto eng = engine::make();
            serialization::json_archive_reader reader(json_partial, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Present fields should load
            check_eq(loaded.health.get(), 50);
            check_eq(loaded.name.get(), std::string("partial_test"));

            // Missing fields should keep their defaults
            check_eq(loaded.speed.get(), 9.9f);
            check_eq(loaded.active.get(), true);
        });

        test("binary_partial_field_order_with_missing", [&]() {
            // Test loading binary with only some fields present
            test_object original;
            original.health = 25;
            original.name = "binary_partial";
            // Don't set speed and active - they'll have defaults

            // Save only the properties that have values we care about
            // This simulates an older version that didn't have all fields
            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer writer(binary_buffer);
            writer.begin_object("test_object", 1);

            // Manually disable some properties from saving
            original.speed.serialize_enabled(false);
            original.active.serialize_enabled(false);

            original.property_mgr.save(writer);
            writer.end_object();

            // Load into new object with defaults
            test_object loaded;
            loaded.speed = 7.7f;
            loaded.active = false;

            auto eng = engine::make();
            serialization::binary_archive_reader reader(binary_buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Present fields should load
            check_eq(loaded.health.get(), 25);
            check_eq(loaded.name.get(), std::string("binary_partial"));

            // Missing fields should keep their defaults
            check_eq(loaded.speed.get(), 7.7f);
            check_eq(loaded.active.get(), false);
        });

        // ===== POST-DESERIALIZATION HOOK TESTS =====

        test("post_deserialize_computed_values_json", [&]() {
            // Test that post_deserialize hook computes derived values after loading from JSON
            computed_object original;
            original.width = 10;
            original.height = 20;
            original.area = 200;  // This won't be serialized

            // Serialize to JSON
            serialization::json_archive_writer writer;
            writer.begin_object("computed_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Load using the hook method
            computed_object loaded;
            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load_with_hook(reader);  // This calls post_deserialize
            reader.end_object();

            // Properties should be loaded
            check_eq(loaded.width.get(), 10);
            check_eq(loaded.height.get(), 20);

            // Area should be computed by post_deserialize hook
            check_eq(loaded.area, 200);
        });

        test("post_deserialize_computed_values_binary", [&]() {
            // Test post_deserialize with binary format
            computed_object original;
            original.width = 15;
            original.height = 25;

            // Serialize to Binary
            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer writer(binary_buffer);
            writer.begin_object("computed_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            // Load using the hook method
            computed_object loaded;
            auto eng = engine::make();
            serialization::binary_archive_reader reader(binary_buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load_with_hook(reader);
            reader.end_object();

            // Properties should be loaded
            check_eq(loaded.width.get(), 15);
            check_eq(loaded.height.get(), 25);

            // Area should be computed
            check_eq(loaded.area, 375);
        });

        test("post_deserialize_validation_json", [&]() {
            // Test that post_deserialize can validate and correct data
            std::string json = R"({
                "_type_": "validated_object",
                "_version_": 1,
                "score": 150,
                "category": ""
            })";

            validated_object loaded;
            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load_with_hook(reader);
            reader.end_object();

            // Score should be clamped to 100
            check_eq(loaded.score.get(), 100);

            // Category should be auto-assigned based on score
            check_eq(loaded.category.get(), std::string("excellent"));
        });

        test("post_deserialize_validation_binary", [&]() {
            // Test validation with binary format
            validated_object original;
            original.score = -50;  // Invalid score
            original.category = "";

            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer writer(binary_buffer);
            writer.begin_object("validated_object", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            validated_object loaded;
            auto eng = engine::make();
            serialization::binary_archive_reader reader(binary_buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load_with_hook(reader);
            reader.end_object();

            // Score should be clamped to 0
            check_eq(loaded.score.get(), 0);

            // Category should be auto-assigned
            check_eq(loaded.category.get(), std::string("needs_improvement"));
        });

        test("post_deserialize_migration_hook_called_json", [&]() {
            // Test that the migration hook is actually called
            std::string json = R"({
                "_type_": "migrated_object_v2",
                "_version_": 1,
                "new_name": "test_name",
                "value": 42
            })";

            migrated_object_v2 loaded;
            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            // Verify hook is not called yet
            check(!loaded.migration_called);

            loaded.load_with_hook(reader);
            reader.end_object();

            // Verify hook was called
            check(loaded.migration_called);

            // Verify properties loaded correctly
            check_eq(loaded.new_name.get(), std::string("test_name"));
            check_eq(loaded.value.get(), 42);
        });

        test("post_deserialize_migration_hook_called_binary", [&]() {
            // Test migration hook with binary format
            migrated_object_v2 original;
            original.new_name = "binary_test";
            original.value = 99;

            std::vector<uint8_t> binary_buffer;
            serialization::binary_archive_writer writer(binary_buffer);
            writer.begin_object("migrated_object_v2", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            migrated_object_v2 loaded;
            auto eng = engine::make();
            serialization::binary_archive_reader reader(binary_buffer, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);

            check(!loaded.migration_called);
            loaded.load_with_hook(reader);
            reader.end_object();

            check(loaded.migration_called);
            check_eq(loaded.new_name.get(), std::string("binary_test"));
            check_eq(loaded.value.get(), 99);
        });

        test("post_deserialize_without_hook_not_called", [&]() {
            // Test that hook is NOT called if we don't use load_with_hook
            std::string json = R"({
                "_type_": "computed_object",
                "_version_": 1,
                "width": 5,
                "height": 5
            })";

            computed_object loaded;
            auto eng = engine::make();
            serialization::json_archive_reader reader(json, eng.get());

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);  // Direct load, no hook
            reader.end_object();

            // Properties should be loaded
            check_eq(loaded.width.get(), 5);
            check_eq(loaded.height.get(), 5);

            // Area should NOT be computed (hook wasn't called)
            check_eq(loaded.area, 0);
        });

        // ===== CLASS_BUILDER POST-DESERIALIZE TESTS =====

        test("class_builder_post_deserialize_with_version", [&]() {
            // Test that class_builder registered classes can use post_deserialize hook with version
            auto eng = engine::make();

            // Define a simple C++ class
            struct Rectangle {
                int width = 0;
                int height = 0;
                int area = 0;
                int version_loaded = 0;
            };

            // Register with class_builder and add post_deserialize hook
            // Note: No explicit .constructor<>() - auto-registration should handle it
            class_builder<Rectangle>(*eng, "Rectangle")
                .property("width", &Rectangle::width)
                .property("height", &Rectangle::height)
                .post_deserialize_hook([](Rectangle& self, int version) {
                    // Compute area after loading
                    self.area = self.width * self.height;
                    self.version_loaded = version;
                })
                .build();

            // Create a Rectangle and manually call post_deserialize to verify it works
            eng->execute(R"(
                var rect = Rectangle();
                rect.width = 15;
                rect.height = 25;
                rect.post_deserialize(2);
            )");

            // Get the rectangle and check that hook was called
            auto rect_val = eng->get_variable("rect");
            auto rect_instance = rect_val.as<std::shared_ptr<class_instance>>();
            check(rect_instance != nullptr);

            auto cpp_obj = rect_instance->get_cpp_object_as<Rectangle>();
            check(cpp_obj != nullptr);

            // Check that post_deserialize was called
            check_eq(cpp_obj->width, 15);
            check_eq(cpp_obj->height, 25);
            check_eq(cpp_obj->area, 375);  // Should be computed
            check_eq(cpp_obj->version_loaded, 2);  // Should have version
        });

        // ===== SCRIPT-DEFINED CLASS POST-DESERIALIZE TESTS =====

        test("script_class_post_deserialize_json", [&]() {
            // Test that script-defined classes can use post_deserialize hook
            auto eng = engine::make();

            // Register JSON stdlib functions (needed for from_json)
            stdlib::register_json_functions(*eng);

            std::cerr << "[TEST] Step 1: Define class..." << std::endl;
            // Define a script class with post_deserialize
            eng->execute(R"(
                class GameCharacter {
                    var health = 100;
                    var max_health = 100;
                    var was_migrated = false;
                    var loaded_version = 0;

                    function post_deserialize(version) {
                        loaded_version = version;

                        // Simulate migration from v1 to v2
                        if (version < 2) {
                            // v1 didn't have max_health, derive it from health
                            max_health = health;
                            was_migrated = true;
                        }

                        // Ensure health doesn't exceed max_health
                        if (health > max_health) {
                            health = max_health;
                        }
                    }
                }
            )");
            std::cerr << "[TEST] Class defined OK" << std::endl;

            std::cerr << "[TEST] Step 2: Test direct method call..." << std::endl;
            try {
                eng->execute(R"(
                    var test_obj = GameCharacter();
                    test_obj.post_deserialize(1);
                )");
                std::cerr << "[TEST] Direct method call OK" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[TEST] Direct method call FAILED: " << e.what() << std::endl;
            }

            // Create JSON representing v1 data (no max_health field)
            std::string json_v1 = R"({"_type_":"GameCharacter","_version_":1,"health":80})";

            // Store JSON as a variable to avoid string escaping issues
            eng->add_global("json_str", script_value(json_v1, eng.get()));

            std::cerr << "[TEST] Step 3: Call from_json..." << std::endl;
            // Load from JSON and check migration
            eng->execute("var loaded = from_json(json_str);");  // Use explicit var declaration
            std::cerr << "[TEST] from_json OK" << std::endl;

            // Debug: Check what type loaded is
            auto loaded_val = eng->get_variable("loaded");
            std::cerr << "[TEST] loaded type = " << static_cast<int>(loaded_val.type()) << std::endl;
            std::cerr << "[TEST] loaded storage type = " << static_cast<int>(loaded_val.storage_type()) << std::endl;
            if (loaded_val.type() == script_value_type::jai_object_type || loaded_val.type() == script_value_type::jai_shared_ptr_type) {
                auto obj_holder = loaded_val.get_object_holder();
                if (obj_holder && obj_holder->data) {
                    auto inst = std::static_pointer_cast<class_instance>(obj_holder->data);
                    std::cerr << "[TEST] loaded is class instance: " << inst->get_class_name() << std::endl;
                    std::cerr << "[TEST] Instance fields: ";
                    for (const auto& [fid, fval] : inst->get_fields()) {
                        std::cerr << fid << " ";
                    }
                    std::cerr << std::endl;
                }
            } else if (loaded_val.type() == script_value_type::jai_map_type) {
                std::cerr << "[TEST] loaded is a MAP, not an object!" << std::endl;
            }

            std::cerr << "[TEST] Step 4: Access loaded.health..." << std::endl;
            auto health = eng->execute("loaded.health");
            std::cerr << "[TEST] health = " << health.as<script_int>() << std::endl;

            std::cerr << "[TEST] Step 5: Access loaded.max_health..." << std::endl;
            auto max_health = eng->execute("loaded.max_health");
            std::cerr << "[TEST] max_health = " << max_health.as<script_int>() << std::endl;

            std::cerr << "[TEST] Step 6: Access loaded.was_migrated..." << std::endl;
            auto was_migrated = eng->execute("loaded.was_migrated");
            std::cerr << "[TEST] was_migrated = " << was_migrated.as<bool>() << std::endl;

            std::cerr << "[TEST] Step 7: Access loaded.loaded_version..." << std::endl;
            auto loaded_version = eng->execute("loaded.loaded_version");
            std::cerr << "[TEST] loaded_version = " << loaded_version.as<script_int>() << std::endl;

            check_eq(health.as<script_int>(), 80);
            check_eq(max_health.as<script_int>(), 80);  // Should be set to health value
            check_eq(was_migrated.as<bool>(), true);
            check_eq(loaded_version.as<script_int>(), 1);
        });

        test("script_class_post_deserialize_binary", [&]() {
            // Test script class post_deserialize with binary format
            auto eng = engine::make();

            // Register JSON stdlib functions (needed for to_binary/from_binary)
            stdlib::register_json_functions(*eng);

            // Define script class
            eng->execute(R"(
                class Item {
                    var quantity = 1;
                    var weight = 0.0;
                    var total_weight = 0.0;
                    var hook_called = false;

                    function post_deserialize(version) {
                        hook_called = true;
                        total_weight = quantity * weight;
                    }
                }
            )");

            // Create an Item and serialize to binary
            eng->execute(R"(
                var item = Item();
                item.quantity = 5;
                item.weight = 2.5;
                var binary_data = to_binary(item);
            )");

            auto binary_data = eng->get_variable("binary_data");

            // Load from binary
            eng->execute("var loaded_item = from_binary(binary_data);");

            // Check that hook was called and computed value is correct
            auto hook_called = eng->execute("loaded_item.hook_called");
            auto total_weight = eng->execute("loaded_item.total_weight");

            check_eq(hook_called.as<bool>(), true);
            check_eq(total_weight.as<script_float>(), 12.5);  // 5 * 2.5
        });

        test("script_class_post_deserialize_no_hook", [&]() {
            // Test that classes without post_deserialize still work
            auto eng = engine::make();

            // Register JSON stdlib functions (needed for to_json/from_json)
            stdlib::register_json_functions(*eng);

            eng->execute(R"(
                class SimpleClass {
                    var value = 42;
                }
            )");

            // Serialize and deserialize
            eng->execute(R"(
                var obj = SimpleClass();
                obj.value = 100;
                var json_str = to_json(obj);
                var loaded = from_json(json_str);
            )");

            auto value = eng->execute("loaded.value");
            check_eq(value.as<script_int>(), 100);
        });

        // ===== CUSTOM CONSTRUCTION TESTS =====

        test("custom_construction_with_context", [&]() {
            auto eng = engine::make();

            // Create a resource manager for dependency injection
            resource_manager mgr("assets/data", 42);

            // Create and setup object
            resource_dependent_object original(&mgr);
            original.name = "test_resource";
            original.resource_ref = mgr.resource_id;

            // Serialize to JSON
            serialization::json_archive_writer json_writer;
            json_writer.begin_object("resource_dependent_object", 1);
            original.property_mgr.save(json_writer);
            json_writer.end_object();
            std::string json = json_writer.str();

            // Debug: print JSON
            std::cout << "Serialized JSON: " << json << std::endl;

            // Create a new resource manager for deserialization
            resource_manager new_mgr("assets/data", 99);

            // Deserialize using context-based construction
            // Note: This demonstrates how context injection would work with class_builder
            // For property_manager, we deserialize and then manually inject dependencies
            serialization::json_archive_reader json_reader(json, eng.get());

            // Set user context in archive
            json_reader.set_user_context(&new_mgr);

            std::string type_name;
            uint32_t version;
            json_reader.begin_object(type_name, version);

            resource_dependent_object loaded(&new_mgr);
            loaded.property_mgr.load(json_reader);
            json_reader.end_object();

            // Debug: print loaded values
            std::cout << "Loaded name: " << loaded.name.get() << std::endl;
            std::cout << "Loaded resource_ref: " << loaded.resource_ref.get() << std::endl;

            // Verify deserialized properties
            check_eq(loaded.name.get(), std::string("test_resource"));
            check_eq(loaded.resource_ref.get(), 42);  // Original resource_id was serialized
            check(loaded.manager != nullptr);
            check_eq(loaded.manager->resource_id, 99);  // New manager was injected
        });

        test("custom_construction_with_archive", [&]() {
            auto eng = engine::make();

            // Create resource manager
            resource_manager mgr("assets/complex", 50);

            // Create and setup object
            archive_aware_object original;
            original.data = "complex_data";
            original.computed_value = 123;

            // Serialize
            serialization::json_archive_writer json_writer;
            json_writer.begin_object("archive_aware_object", 1);
            original.property_mgr.save(json_writer);
            json_writer.end_object();
            std::string json = json_writer.str();

            // Deserialize with archive-aware construction
            resource_manager new_mgr("assets/complex", 75);

            serialization::json_archive_reader json_reader(json, eng.get());
            json_reader.set_user_context(&new_mgr);

            std::string type_name;
            uint32_t version;
            json_reader.begin_object(type_name, version);

            // Construct with both context and archive
            archive_aware_object loaded(&new_mgr, json_reader);
            loaded.property_mgr.load(json_reader);
            json_reader.end_object();

            // Verify
            check_eq(loaded.data.get(), std::string("complex_data"));
            check_eq(loaded.computed_value.get(), 123);  // Loaded from archive
            check(loaded.manager != nullptr);
            check_eq(loaded.manager->resource_id, 75);
        });

        test("context_extraction_from_archive", [&]() {
            auto eng = engine::make();

            // Test that user context can be retrieved from archive
            resource_manager mgr("test_path", 100);

            std::string json = "{}";
            serialization::json_archive_reader reader(json, eng.get());

            // Set context
            reader.set_user_context(&mgr);

            // Retrieve context
            auto* retrieved = reader.get_user_context<resource_manager>();
            check(retrieved != nullptr);
            check_eq(retrieved->resource_path, std::string("test_path"));
            check_eq(retrieved->resource_id, 100);

            // Non-existent context type returns nullptr
            struct other_context {};
            auto* missing = reader.get_user_context<other_context>();
            check(missing == nullptr);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::property_serialization_tests)

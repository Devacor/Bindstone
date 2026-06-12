#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/property_serialization.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/dynamic_binder_serialization.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <limits>
#include <cmath>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// ============================================================================
// Smart Pointer Test Types (must be at namespace scope for JAI_PROPERTY)
// ============================================================================

struct sp_inner_data {
    int x = 0;
    std::string label;

    template<typename Archive>
    void save(Archive& ar) const {
        ar.serialize("x", x);
        ar.serialize("label", label);
    }

    template<typename Archive>
    void load(Archive& ar) {
        ar.serialize("x", x);
        ar.serialize("label", label);
    }
};

struct sp_simple_data {
    int value = 0;

    template<typename Archive>
    void save(Archive& ar) const {
        ar.serialize("value", value);
    }

    template<typename Archive>
    void load(Archive& ar) {
        ar.serialize("value", value);
    }
};

struct sp_id_data {
    int id = 0;

    template<typename Archive>
    void save(Archive& ar) const {
        ar.serialize("id", id);
    }

    template<typename Archive>
    void load(Archive& ar) {
        ar.serialize("id", id);
    }
};

struct sp_ptr_holder : public property_owner<sp_ptr_holder> {
    JAI_PROPERTY((std::shared_ptr<sp_simple_data>), data);
    JAI_PROPERTY((int), extra, 0);
};

// ============================================================================
// Test Classes - property_owner Auto-registration
// ============================================================================

// Simple property_owner test class
class conv_player : public property_owner<conv_player> {
public:
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((float), speed, 5.5f);
    JAI_PROPERTY((std::string), name, "Player");

    conv_player() = default;
};

// property_owner with containers
class conv_inventory : public property_owner<conv_inventory> {
public:
    JAI_PROPERTY((std::vector<std::string>), items);
    JAI_PROPERTY((std::map<std::string, int>), quantities);

    conv_inventory() = default;
};

// ============================================================================
// Test Classes - Explicit save/load (uses ar.serialize API)
// ============================================================================

class explicit_save_load_obj {
public:
    int x = 0;
    int y = 0;
    std::string label = "";

    // Explicit save function using ar.serialize("key", value)
    // Templated for CRTP archive support
    template<typename Archive>
    void save(Archive& ar) const {
        ar.serialize("x", x);
        ar.serialize("y", y);
        ar.serialize("label", label);
    }

    // Explicit load function using ar.serialize("key", value)
    template<typename Archive>
    void load(Archive& ar) {
        ar.serialize("x", x);
        ar.serialize("y", y);
        ar.serialize("label", label);
    }
};

// ============================================================================
// Test Classes - Member serialize() function (unified save/load)
// ============================================================================

struct catalog_like_root_obj {
	std::vector<int64_t> data;
	template<typename Archive>
	void serialize(Archive& ar) {
		ar(jai::serialization::make_nvp("data", data));
	}
};

class member_serialize_obj {
public:
    double value = 0.0;
    std::string tag = "";

    // Unified serialize function - works for both reading and writing
    // Uses ar.serialize("key", value) which dispatches correctly based on archive type
    template<typename Archive>
    void serialize(Archive& ar) {
        ar.serialize("value", value);
        ar.serialize("tag", tag);
    }

    // For convenience function compatibility - save version (templated)
    template<typename Archive>
    void save(Archive& ar) const {
        ar.serialize("value", value);
        ar.serialize("tag", tag);
    }

    // For convenience function compatibility - load version (templated)
    template<typename Archive>
    void load(Archive& ar) {
        ar.serialize("value", value);
        ar.serialize("tag", tag);
    }
};

// ============================================================================
// Test Classes - property_owner + Extra Data (Blended)
// ============================================================================

// Blended object: Uses JAI_PROPERTY for some fields, and extra non-property fields
// The save/load methods serialize properties PLUS extra data
class blended_object : public property_owner<blended_object> {
public:
    JAI_PROPERTY((int), score, 0);
    JAI_PROPERTY((std::string), player_name, "");

    // Extra non-property data (serialized manually)
    int extra_data = 0;
    std::vector<float> history;

    blended_object() = default;

    // Blended save: properties via property_mgr + extra fields via ar.serialize
    // Templated for CRTP archive support
    template<typename Archive>
    void save(Archive& ar) const {
        property_mgr.save(ar);
        ar.serialize("extra_data", extra_data);
        ar.serialize("history", history);
    }

    // Blended load: properties via property_mgr + extra fields via ar.serialize
    template<typename Archive>
    void load(Archive& ar) {
        property_mgr.load(ar);
        ar.serialize("extra_data", extra_data);
        ar.serialize("history", history);
    }
};

// ============================================================================
// Context for dependency injection
// ============================================================================

struct conv_test_context {
    std::string service_name = "TestService";
    int multiplier = 1;
};

// ============================================================================
// Test Suite
// ============================================================================

class convenience_function_tests : public suite {
public:
    convenience_function_tests() : suite("Convenience Functions") {}

    void forge_tests() override {
        test("root_object_member_serialize_read", [this]() {
            // Reading a foreign root object {"data": [...]} into a member-serialize type:
            // ar(obj) is inline by design, so the caller enters the document root explicitly.
            auto eng = engine::make();
            std::string text = "{\"data\": [1, 2, 3]}";
            jai::serialization::json_archive_reader ar(text, eng.get());
            catalog_like_root_obj loaded;
            std::string rootType;
            uint32_t rootVersion = 0;
            check(ar.begin_object(rootType, rootVersion), "fresh reader enters document root");
            loaded.serialize(ar);
            ar.end_object();
            check_eq(size_t(3), loaded.data.size(), "root object member serialize element count");
            check_eq(int64_t(2), loaded.data[1], "root object member serialize element value");
        });

        // ================================================================
        // RAW BASE64 TESTS (no serialization, just string encoding)
        // ================================================================

        test("base64_raw_string_roundtrip", [this]() {
            std::string original = "Hello, World!";
            std::string encoded = base64_encode(original);
            std::string decoded = base64_decode(encoded);

            check_eq(decoded, original, "Raw base64 roundtrip");
            check_eq(encoded, std::string("SGVsbG8sIFdvcmxkIQ=="), "Expected base64 encoding");
        });

        test("base64_binary_data_with_nulls", [this]() {
            std::string binary_data;
            binary_data.push_back('\x00');
            binary_data.push_back('\x01');
            binary_data.push_back('\xFF');
            binary_data.push_back('\xFE');

            std::string encoded = base64_encode(binary_data);
            std::string decoded = base64_decode(encoded);

            check_eq(decoded.size(), binary_data.size(), "Binary size preserved");
            check_eq(decoded, binary_data, "Binary data preserved through base64");
        });

        // ================================================================
        // JSON CONVENIENCE FUNCTION TESTS
        // ================================================================

        test("json_basic_types", [this]() {
            auto eng = engine::make();

            // Int
            std::string json_int = to_json(*eng, 42);
            check(json_int.find("42") != std::string::npos, "Int serialized");
            int i = from_json<int>(*eng, json_int);
            check_eq(i, 42, "Int roundtrip");

            // Float
            std::string json_float = to_json(*eng, 3.14);
            check(json_float.find("3.14") != std::string::npos, "Float serialized");

            // String
            std::string json_str = to_json(*eng, std::string("hello world"));
            check(json_str.find("hello world") != std::string::npos, "String serialized");
            std::string s = from_json<std::string>(*eng, json_str);
            check_eq(s, std::string("hello world"), "String roundtrip");

            // Bool
            std::string json_bool = to_json(*eng, true);
            check(json_bool.find("true") != std::string::npos, "Bool serialized");
        });

        // ================================================================
        // BINARY CONVENIENCE FUNCTION TESTS
        // ================================================================

        test("binary_basic_types", [this]() {
            auto eng = engine::make();

            // Int roundtrip
            std::string bin_int = to_binary_string(*eng, 12345);
            int i = from_binary_string<int>(*eng, bin_int);
            check_eq(i, 12345, "Int binary roundtrip");

            // Float roundtrip
            std::string bin_float = to_binary_string(*eng, 2.718);
            double d = from_binary_string<double>(*eng, bin_float);
            check(std::abs(d - 2.718) < 0.001, "Float binary roundtrip");

            // String roundtrip
            std::string bin_str = to_binary_string(*eng, std::string("binary test"));
            std::string loaded_str = from_binary_string<std::string>(*eng, bin_str);
            check_eq(loaded_str, std::string("binary test"), "String binary roundtrip");
        });

        // ================================================================
        // BASE64 SERIALIZATION TESTS
        // ================================================================

        test("base64_serialization_basic", [this]() {
            auto eng = engine::make();

            // Serialize to base64
            std::string base64 = to_base64(*eng, 9999);

            // Base64 should be ASCII-safe
            for (char c : base64) {
                bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
                check(valid, "Base64 contains only valid characters");
            }

            // Deserialize from base64
            int loaded = from_base64<int>(*eng, base64);
            check_eq(loaded, 9999, "Int base64 roundtrip");
        });

        // ================================================================
        // PROPERTY_OWNER AUTO-REGISTRATION TESTS
        // ================================================================

        test("property_owner_json_roundtrip", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Register player with auto-bound properties
            dynamic_binder<conv_player>(eng, "ConvPlayer")
                .build();

            // Create and modify player
            auto original = std::make_shared<conv_player>();
            original->health = 75;
            original->speed = 10.0f;
            original->name = "Hero";

            // Serialize using explicit archive API (property_owner save)
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ConvPlayer", 1);
            original->property_mgr.save(writer);
            writer.end_object();
            std::string json = writer.str();

            // Verify JSON contains expected values
            check(json.find("75") != std::string::npos, "Health in JSON");
            check(json.find("Hero") != std::string::npos, "Name in JSON");

            // Deserialize
            conv_player loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            check_eq(loaded.health.get(), 75, "Health restored");
            check_eq(loaded.name.get(), std::string("Hero"), "Name restored");
            check(std::abs(loaded.speed.get() - 10.0f) < 0.001f, "Speed restored");
        });

        test("property_owner_binary_roundtrip", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<conv_player>(eng, "ConvPlayer").build();

            auto original = std::make_shared<conv_player>();
            original->health = 50;
            original->speed = 7.5f;
            original->name = "Binary Hero";

            // Serialize using binary archive
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object("ConvPlayer", 1);
            original->property_mgr.save(writer);
            writer.end_object();

            // Deserialize
            conv_player loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            check_eq(loaded.health.get(), 50, "Health from binary");
            check_eq(loaded.name.get(), std::string("Binary Hero"), "Name from binary");
        });

        test("property_owner_containers", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<conv_inventory>(eng, "ConvInventory").build();

            auto original = std::make_shared<conv_inventory>();
            original->items.get() = {"sword", "shield", "potion"};
            original->quantities.get() = {{"gold", 100}, {"gems", 5}};

            // JSON roundtrip
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ConvInventory", 1);
            original->property_mgr.save(writer);
            writer.end_object();
            std::string json = writer.str();

            conv_inventory loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            check_eq(loaded.items.get().size(), size_t(3), "Items count");
            check_eq(loaded.items.get()[0], std::string("sword"), "First item");
            check_eq(loaded.quantities.get()["gold"], 100, "Gold quantity");
        });

        // ================================================================
        // EXPLICIT SAVE/LOAD TESTS (ar(obj) dispatches to save/load)
        // ================================================================

        test("explicit_save_load_json", [this]() {
            auto eng = engine::make();

            explicit_save_load_obj original;
            original.x = 10;
            original.y = 20;
            original.label = "test point";

            // Serialize using obj.save(ar) - explicit call
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ExplicitObj", 1);
            original.save(writer);
            writer.end_object();
            std::string json = writer.str();

            // Verify JSON contains expected values
            check(json.find("10") != std::string::npos, "x in JSON");
            check(json.find("20") != std::string::npos, "y in JSON");
            check(json.find("test point") != std::string::npos, "label in JSON");

            // Deserialize using obj.load(ar) - explicit call
            explicit_save_load_obj loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load(reader);
            reader.end_object();

            check_eq(loaded.x, 10, "x restored");
            check_eq(loaded.y, 20, "y restored");
            check_eq(loaded.label, std::string("test point"), "label restored");
        });

        test("explicit_save_load_binary", [this]() {
            auto eng = engine::make();

            explicit_save_load_obj original;
            original.x = 100;
            original.y = 200;
            original.label = "binary point";

            // Serialize using obj.save(ar)
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object("ExplicitObj", 1);
            original.save(writer);
            writer.end_object();

            // Deserialize using obj.load(ar)
            explicit_save_load_obj loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load(reader);
            reader.end_object();

            check_eq(loaded.x, 100, "x from binary");
            check_eq(loaded.y, 200, "y from binary");
            check_eq(loaded.label, std::string("binary point"), "label from binary");
        });

        // ================================================================
        // MEMBER SERIALIZE TESTS (explicit save/load calls)
        // ================================================================

        test("member_serialize_json", [this]() {
            auto eng = engine::make();

            member_serialize_obj original;
            original.value = 3.14159;
            original.tag = "pi";

            // Serialize using obj.save(ar)
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("MemberSerializeObj", 1);
            original.save(writer);
            writer.end_object();
            std::string json = writer.str();

            // Verify JSON content
            check(json.find("3.14159") != std::string::npos, "value in JSON");
            check(json.find("pi") != std::string::npos, "tag in JSON");

            // Deserialize using obj.load(ar)
            member_serialize_obj loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load(reader);
            reader.end_object();

            check(std::abs(loaded.value - 3.14159) < 0.00001, "value restored");
            check_eq(loaded.tag, std::string("pi"), "tag restored");
        });

        test("member_serialize_binary", [this]() {
            auto eng = engine::make();

            member_serialize_obj original;
            original.value = 2.71828;
            original.tag = "euler";

            // Serialize using obj.save(ar)
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object("MemberSerializeObj", 1);
            original.save(writer);
            writer.end_object();

            // Deserialize using obj.load(ar)
            member_serialize_obj loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load(reader);
            reader.end_object();

            check(std::abs(loaded.value - 2.71828) < 0.00001, "value from binary");
            check_eq(loaded.tag, std::string("euler"), "tag from binary");
        });

        // ================================================================
        // BLENDED APPROACH TESTS (property_owner + extra fields via save/load)
        // ================================================================
        // save/load internally call property_mgr.save/load PLUS serialize extra fields

        test("blended_property_owner_json", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<blended_object>(eng, "BlendedObject").build();

            auto original = std::make_shared<blended_object>();
            original->score = 1000;
            original->player_name = "Champion";
            original->extra_data = 42;
            original->history = {1.0f, 2.5f, 3.7f, 4.2f};

            // Serialize using serialize_object_content - dispatches to save() which calls property_mgr.save + extras
            // Must use serialize_object_content because we already opened the object with begin_object
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("BlendedObject", 1);
            writer.serialize_object_content(*original);
            writer.end_object();
            std::string json = writer.str();

            // Verify JSON contains all data (properties + extra)
            check(json.find("1000") != std::string::npos, "Score in JSON");
            check(json.find("Champion") != std::string::npos, "Player name in JSON");
            check(json.find("42") != std::string::npos, "Extra data in JSON");
            check(json.find("history") != std::string::npos, "History key in JSON");

            // Deserialize using ar(obj) - dispatches to load() which calls property_mgr.load + extras
            blended_object loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            reader(loaded);
            reader.end_object();

            check_eq(loaded.score.get(), 1000, "Score restored");
            check_eq(loaded.player_name.get(), std::string("Champion"), "Name restored");
            check_eq(loaded.extra_data, 42, "Extra data restored");
            check_eq(loaded.history.size(), size_t(4), "History size");
            check(std::abs(loaded.history[1] - 2.5f) < 0.001f, "History[1] restored");
        });

        test("blended_binary_roundtrip", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<blended_object>(eng, "BlendedObject").build();

            auto original = std::make_shared<blended_object>();
            original->score = 500;
            original->player_name = "Binary Champ";
            original->extra_data = 99;
            original->history = {10.0f, 20.0f};

            // Serialize using serialize_object_content (we already opened the object)
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object("BlendedObject", 1);
            writer.serialize_object_content(*original);
            writer.end_object();

            // Deserialize - reader(loaded) calls load() which handles begin_object/end_object internally
            blended_object loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.load(reader);
            reader.end_object();

            check_eq(loaded.score.get(), 500, "Score from binary");
            check_eq(loaded.extra_data, 99, "Extra data from binary");
            check_eq(loaded.history.size(), size_t(2), "History size from binary");
        });

        test("property_owner_auto_dispatch", [this]() {
            // Test that property_owner WITHOUT save/load uses property_mgr automatically
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<conv_player>(eng, "ConvPlayer").build();

            auto original = std::make_shared<conv_player>();
            original->health = 777;
            original->name = "AutoPlayer";
            original->speed = 12.5f;

            // Serialize using serialize_object_content - we already opened the object with begin_object
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ConvPlayer", 1);
            writer.serialize_object_content(*original);
            writer.end_object();
            std::string json = writer.str();

            // Deserialize - property_mgr.load reads properties directly (no extra begin_object)
            conv_player loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            check_eq(loaded.health.get(), 777, "Health auto-restored");
            check_eq(loaded.name.get(), std::string("AutoPlayer"), "Name auto-restored");
            check(std::abs(loaded.speed.get() - 12.5f) < 0.001f, "Speed auto-restored");
        });

        // ================================================================
        // USER CONTEXT TESTS
        // ================================================================

        test("json_with_user_context", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            dynamic_binder<conv_player>(eng, "ConvPlayer").build();

            auto original = std::make_shared<conv_player>();
            original->health = 60;
            original->name = "Context Player";

            // Serialize
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ConvPlayer", 1);
            original->property_mgr.save(writer);
            writer.end_object();
            std::string json = writer.str();

            // Deserialize with user context
            conv_test_context ctx;
            ctx.service_name = "GameService";
            ctx.multiplier = 2;

            conv_player loaded;
            serialization::json_archive_reader reader(json, eng.get());
            reader.set_user_context(&ctx);

            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            // Context was available during load (even if not used in this simple case)
            check_eq(loaded.health.get(), 60, "Health with context");
            check_eq(loaded.name.get(), std::string("Context Player"), "Name with context");
        });

        // ================================================================
        // PORTABLE BINARY (ENDIANNESS) TEST
        // ================================================================

        test("binary_little_endian_portable", [this]() {
            auto eng = engine::make();

            // Serialize a known multi-byte value
            int32_t value = 0x12345678;
            std::string binary = to_binary_string(*eng, value);

            // Roundtrip should work regardless of host endianness
            int32_t loaded = from_binary_string<int32_t>(*eng, binary);
            check_eq(loaded, value, "Int32 portable binary roundtrip");

            // Test a 64-bit value
            int64_t big_value = 0x123456789ABCDEF0LL;
            std::string big_binary = to_binary_string(*eng, big_value);
            int64_t big_loaded = from_binary_string<int64_t>(*eng, big_binary);
            check_eq(big_loaded, big_value, "Int64 portable binary roundtrip");
        });

        // ================================================================
        // SMART POINTER ROUND-TRIP TESTS
        // ================================================================
        // Tests the new object-based JSON format:
        //   shared_ptr: {"$id": N, "$val": {...}} or {"$id": N} or null
        //   weak_ptr:   {"$ref": N} or null
        //   unique_ptr: {"$val": {...}} or null

        test("shared_ptr_json_roundtrip", [this]() {
            auto eng = engine::make();

            auto ptr1 = std::make_shared<sp_inner_data>();
            ptr1->x = 42;
            ptr1->label = "hello";

            // Serialize shared_ptr
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object();
            writer.serialize("ptr1", ptr1);
            writer.serialize("ptr1_alias", ptr1);  // Same pointer again - should be a ref
            std::shared_ptr<sp_inner_data> null_ptr;
            writer.serialize("null_ptr", null_ptr);
            writer.end_object();

            std::string json = writer.str();

            // Verify JSON format: $id and $val keys present
            check(json.find("$id") != std::string::npos, "$id key present");
            check(json.find("$val") != std::string::npos, "$val key present");
            check(json.find("null") != std::string::npos, "null present for null_ptr");

            // Deserialize
            std::shared_ptr<sp_inner_data> loaded_ptr1;
            std::shared_ptr<sp_inner_data> loaded_alias;
            std::shared_ptr<sp_inner_data> loaded_null;

            serialization::json_archive_reader reader(json, eng.get());
            reader.begin_object();
            reader.serialize("ptr1", loaded_ptr1);
            reader.serialize("ptr1_alias", loaded_alias);
            reader.serialize("null_ptr", loaded_null);
            reader.end_object();

            // Verify loaded values
            check(loaded_ptr1 != nullptr, "ptr1 not null");
            check_eq(loaded_ptr1->x, 42, "ptr1.x preserved");
            check_eq(loaded_ptr1->label, std::string("hello"), "ptr1.label preserved");

            // Alias should point to same object (de-duplication)
            check(loaded_alias != nullptr, "alias not null");
            check(loaded_ptr1.get() == loaded_alias.get(), "alias is same object as ptr1");

            // Null should remain null
            check(loaded_null == nullptr, "null_ptr stays null");
        });

        test("shared_ptr_binary_roundtrip", [this]() {
            auto eng = engine::make();

            auto ptr1 = std::make_shared<sp_inner_data>();
            ptr1->x = 99;
            ptr1->label = "binary_test";

            // Serialize
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object();
            writer.serialize("ptr1", ptr1);
            writer.serialize("ptr1_alias", ptr1);
            std::shared_ptr<sp_inner_data> null_ptr;
            writer.serialize("null_ptr", null_ptr);
            writer.end_object();

            // Deserialize
            std::shared_ptr<sp_inner_data> loaded_ptr1;
            std::shared_ptr<sp_inner_data> loaded_alias;
            std::shared_ptr<sp_inner_data> loaded_null;

            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            reader.serialize("ptr1", loaded_ptr1);
            reader.serialize("ptr1_alias", loaded_alias);
            reader.serialize("null_ptr", loaded_null);
            reader.end_object();

            check(loaded_ptr1 != nullptr, "ptr1 not null");
            check_eq(loaded_ptr1->x, 99, "ptr1.x preserved");
            check_eq(loaded_ptr1->label, std::string("binary_test"), "ptr1.label preserved");
            check(loaded_alias != nullptr, "alias not null");
            check(loaded_ptr1.get() == loaded_alias.get(), "alias is same object");
            check(loaded_null == nullptr, "null_ptr stays null");
        });

        test("unique_ptr_json_roundtrip", [this]() {
            auto eng = engine::make();

            auto uptr = std::make_unique<sp_simple_data>();
            uptr->value = 123;

            // Serialize
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object();
            writer.serialize("uptr", uptr);
            std::unique_ptr<sp_simple_data> null_uptr;
            writer.serialize("null_uptr", null_uptr);
            writer.end_object();

            std::string json = writer.str();
            check(json.find("$val") != std::string::npos, "$val key present for unique_ptr");
            check(json.find("null") != std::string::npos, "null present for null unique_ptr");

            // Deserialize
            std::unique_ptr<sp_simple_data> loaded_uptr;
            std::unique_ptr<sp_simple_data> loaded_null_uptr;

            serialization::json_archive_reader reader(json, eng.get());
            reader.begin_object();
            reader.serialize("uptr", loaded_uptr);
            reader.serialize("null_uptr", loaded_null_uptr);
            reader.end_object();

            check(loaded_uptr != nullptr, "uptr not null");
            check_eq(loaded_uptr->value, 123, "uptr.value preserved");
            check(loaded_null_uptr == nullptr, "null_uptr stays null");
        });

        test("weak_ptr_json_roundtrip", [this]() {
            auto eng = engine::make();

            auto shared = std::make_shared<sp_simple_data>();
            shared->value = 77;
            std::weak_ptr<sp_simple_data> weak = shared;
            std::weak_ptr<sp_simple_data> null_weak;

            // Serialize - shared_ptr MUST be serialized before weak_ptr
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object();
            writer.serialize("shared", shared);
            writer.serialize("weak", weak);
            writer.serialize("null_weak", null_weak);
            writer.end_object();

            std::string json = writer.str();
            check(json.find("$ref") != std::string::npos, "$ref key present for weak_ptr");

            // Deserialize
            std::shared_ptr<sp_simple_data> loaded_shared;
            std::weak_ptr<sp_simple_data> loaded_weak;
            std::weak_ptr<sp_simple_data> loaded_null_weak;

            serialization::json_archive_reader reader(json, eng.get());
            reader.begin_object();
            reader.serialize("shared", loaded_shared);
            reader.serialize("weak", loaded_weak);
            reader.serialize("null_weak", loaded_null_weak);
            reader.end_object();

            check(loaded_shared != nullptr, "shared not null");
            check_eq(loaded_shared->value, 77, "shared.value preserved");

            auto locked = loaded_weak.lock();
            check(locked != nullptr, "weak can lock");
            check(locked.get() == loaded_shared.get(), "weak points to same shared object");

            check(loaded_null_weak.lock() == nullptr, "null_weak stays expired");
        });

        test("vector_of_shared_ptr_json_roundtrip", [this]() {
            auto eng = engine::make();

            auto a = std::make_shared<sp_id_data>();
            a->id = 1;
            auto b = std::make_shared<sp_id_data>();
            b->id = 2;

            std::vector<std::shared_ptr<sp_id_data>> vec = {a, b, a};  // a appears twice

            // Serialize
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object();
            writer.serialize("vec", vec);
            writer.end_object();

            std::string json = writer.str();

            // Deserialize
            std::vector<std::shared_ptr<sp_id_data>> loaded_vec;

            serialization::json_archive_reader reader(json, eng.get());
            reader.begin_object();
            reader.serialize("vec", loaded_vec);
            reader.end_object();

            check_eq(loaded_vec.size(), size_t(3), "vector size preserved");
            check(loaded_vec[0] != nullptr, "vec[0] not null");
            check_eq(loaded_vec[0]->id, 1, "vec[0].id preserved");
            check(loaded_vec[1] != nullptr, "vec[1] not null");
            check_eq(loaded_vec[1]->id, 2, "vec[1].id preserved");
            check(loaded_vec[2] != nullptr, "vec[2] not null");
            // vec[0] and vec[2] should be the same object (de-duplicated)
            check(loaded_vec[0].get() == loaded_vec[2].get(), "vec[0] and vec[2] are same object");
        });

        test("shared_ptr_property_json_roundtrip", [this]() {
            // Test shared_ptr as a JAI_PROPERTY inside a property_owner
            auto eng = engine::make();

            sp_ptr_holder original;
            original.data = std::make_shared<sp_simple_data>();
            original.data.get()->value = 555;
            original.extra = 10;

            // Serialize
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("sp_ptr_holder", 1);
            original.property_mgr.save(writer);
            writer.end_object();

            std::string json = writer.str();

            // Deserialize
            sp_ptr_holder loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            loaded.property_mgr.load(reader);
            reader.end_object();

            check(loaded.data.get() != nullptr, "data not null");
            check_eq(loaded.data.get()->value, 555, "data.value preserved");
            check_eq(loaded.extra.get(), 10, "extra preserved");
        });

        // ================================================================
        // JSON READER ROBUSTNESS (2026-05 audit regressions)
        // ================================================================
        // These guard the parse_number/parse_string/parse_json hardening:
        // malformed input must raise jai::serialization_error (NOT a raw
        // std::out_of_range / std::invalid_argument, which derive from a sibling
        // hierarchy and would escape callers catching serialization_error), and
        // must never invoke UB (std::isdigit on a sign-extended char) or crash.

        // Helper: returns 1 if the parse threw serialization_error, 2 if it threw some
        // OTHER exception type (the bug we are guarding against), 0 if it did not throw.
        auto parseOutcome = [](engine* e, const std::string& json) -> int {
            try {
                serialization::json_archive_reader reader(json, e);
                return 0;
            } catch (const serialization_error&) {
                return 1;
            } catch (...) {
                return 2;
            }
        };

        test("json_reader_rejects_trailing_garbage", [this, parseOutcome]() {
            auto eng = engine::make();
            check_eq(parseOutcome(eng.get(), "42 garbage"), 1, "trailing garbage -> serialization_error");
            check_eq(parseOutcome(eng.get(), "{} []"), 1, "two top-level values -> serialization_error");
            check_eq(parseOutcome(eng.get(), "  123  "), 0, "leading/trailing whitespace is fine");
        });

        test("json_reader_bare_minus_is_serialization_error", [this, parseOutcome]() {
            auto eng = engine::make();
            // Previously std::stoll("-") threw std::invalid_argument (outcome 2).
            check_eq(parseOutcome(eng.get(), "-"), 1, "bare '-' -> serialization_error");
            check_eq(parseOutcome(eng.get(), "1.2.3"), 1, "malformed number -> serialization_error");
        });

        test("json_reader_huge_int_degrades_to_double", [this]() {
            auto eng = engine::make();
            // Previously std::stoll threw std::out_of_range and escaped the loader.
            serialization::json_archive_reader reader("99999999999999999999", eng.get());
            double v = reader.read_value().as<double>();
            check(v > 9.9e19 && v < 1.1e20, "out-of-range integer parses as double");
        });

        test("json_reader_non_ascii_value_byte_no_ub", [this, parseOutcome]() {
            auto eng = engine::make();
            // A non-ASCII byte where a value is expected previously reached
            // std::isdigit(signed char) -> UB / debug-CRT abort. Must be a clean throw.
            std::string bad = "\xC3\xA9";   // 'é' bytes, not a valid value start
            check_eq(parseOutcome(eng.get(), bad), 1, "non-ASCII value byte -> serialization_error, no UB");
        });

        test("json_reader_invalid_unicode_escape_is_serialization_error", [this, parseOutcome]() {
            auto eng = engine::make();
            // Previously std::stoul on non-hex threw std::invalid_argument (outcome 2).
            check_eq(parseOutcome(eng.get(), "\"\\uZZZZ\""), 1, "non-hex \\u -> serialization_error");
            check_eq(parseOutcome(eng.get(), "\"\\uD8\""), 1, "truncated \\u -> serialization_error");
        });

        test("json_reader_lone_surrogate_becomes_replacement_char", [this]() {
            auto eng = engine::make();
            // Lone high surrogate must become U+FFFD (valid UTF-8), not WTF-8.
            serialization::json_archive_reader reader("\"\\uD800\"", eng.get());
            std::string s = reader.read_value().as<std::string>();
            check_eq(s, std::string("\xEF\xBF\xBD"), "lone high surrogate -> U+FFFD");
        });

        test("json_reader_valid_surrogate_pair", [this]() {
            auto eng = engine::make();
            // U+1F600 (grinning face) as a UTF-16 surrogate pair -> 4-byte UTF-8.
            serialization::json_archive_reader reader("\"\\uD83D\\uDE00\"", eng.get());
            std::string s = reader.read_value().as<std::string>();
            check_eq(s, std::string("\xF0\x9F\x98\x80"), "surrogate pair -> U+1F600");
        });

        test("json_string_escapes_and_utf8_roundtrip", [this]() {
            auto eng = engine::make();
            // Exercises the bulk-copy parse_string rewrite + escape writer: quotes,
            // backslash, control chars, and a multi-byte UTF-8 character.
            std::string original = "a\"b\\c\nd\te\xC3\xA9z";
            std::string json = to_json(*eng, original);
            std::string back = from_json<std::string>(*eng, json);
            check_eq(back, original, "string with escapes + UTF-8 round-trips");
        });

        test("json_infinity_roundtrips", [this]() {
            auto eng = engine::make();
            // Writer emits the 1e999 sentinel for infinity; the reader's from_chars now
            // treats result_out_of_range as the (inf) value rather than throwing.
            double inf = std::numeric_limits<double>::infinity();
            std::string json = to_json(*eng, inf);
            double back = from_json<double>(*eng, json);
            check(std::isinf(back) && back > 0, "+infinity round-trips via 1e999 sentinel");
        });

        // ================================================================
        // weak_ptr FORWARD REFERENCE (2026-05 audit, high-severity)
        // ================================================================
        // A std::weak_ptr serialized BEFORE the shared_ptr it points to used to write
        // {"$ref": 0} (lookup_shared_id returned 0 for the not-yet-written object) and the
        // link was silently dropped on load. Now the first (forward) reference inlines the
        // object's data under a stable id, so it round-trips. Covers JSON and binary.

        test("weak_ptr_forward_reference_json", [this]() {
            auto eng = engine::make();
            auto shared = std::make_shared<sp_simple_data>();
            shared->value = 77;
            std::weak_ptr<sp_simple_data> weak = shared;

            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object();
            writer.serialize("weak", weak);       // forward: weak BEFORE shared
            writer.serialize("shared", shared);
            writer.end_object();
            std::string json = writer.str();

            std::weak_ptr<sp_simple_data> loaded_weak;
            std::shared_ptr<sp_simple_data> loaded_shared;
            serialization::json_archive_reader reader(json, eng.get());
            reader.begin_object();
            reader.serialize("weak", loaded_weak);
            reader.serialize("shared", loaded_shared);
            reader.end_object();

            auto locked = loaded_weak.lock();
            check(locked != nullptr, "forward-referenced weak_ptr resolves (was dropped before)");
            check(loaded_shared != nullptr, "shared loaded");
            check(locked.get() == loaded_shared.get(), "forward weak and shared are the same object");
            check_eq(loaded_shared->value, 77, "value preserved through forward ref");
        });

        test("weak_ptr_forward_reference_binary", [this]() {
            auto eng = engine::make();
            auto shared = std::make_shared<sp_simple_data>();
            shared->value = 88;
            std::weak_ptr<sp_simple_data> weak = shared;

            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object();
            writer.serialize("weak", weak);       // forward: weak BEFORE shared
            writer.serialize("shared", shared);
            writer.end_object();

            std::weak_ptr<sp_simple_data> loaded_weak;
            std::shared_ptr<sp_simple_data> loaded_shared;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            reader.serialize("weak", loaded_weak);
            reader.serialize("shared", loaded_shared);
            reader.end_object();

            auto locked = loaded_weak.lock();
            check(locked != nullptr, "forward-referenced weak_ptr resolves (binary)");
            check(loaded_shared != nullptr, "shared loaded (binary)");
            check(locked.get() == loaded_shared.get(), "forward weak and shared same object (binary)");
            check_eq(loaded_shared->value, 88, "value preserved through forward ref (binary)");
        });
    }
};

// Register the test suite
FOUNDRY_REGISTER(convenience_function_tests);

} // namespace jai::foundry::tests

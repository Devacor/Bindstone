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

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

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

            // Serialize using ar(obj) - dispatches to save() which calls property_mgr.save + extras
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("BlendedObject", 1);
            writer(*original);
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

            // Serialize using ar(obj)
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.begin_object("BlendedObject", 1);
            writer(*original);
            writer.end_object();

            // Deserialize using ar(obj)
            blended_object loaded;
            serialization::binary_archive_reader reader(buffer, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            reader(loaded);
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

            // Serialize using ar(obj) - dispatches to property_mgr.save (no custom save())
            serialization::json_archive_writer writer(2, eng.get());
            writer.begin_object("ConvPlayer", 1);
            writer(*original);
            writer.end_object();
            std::string json = writer.str();

            // Deserialize using ar(obj) - dispatches to property_mgr.load (no custom load())
            conv_player loaded;
            serialization::json_archive_reader reader(json, eng.get());
            std::string type_name;
            uint32_t version;
            reader.begin_object(type_name, version);
            reader(loaded);
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
    }
};

// Register the test suite
FOUNDRY_REGISTER(convenience_function_tests);

} // namespace jai::foundry::tests

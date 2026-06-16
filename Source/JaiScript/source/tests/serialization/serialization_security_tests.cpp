// Security regression tests for the Phase 2 deserialization findings (June release review, B5).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/serialization/binary_archive.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class serialization_security_tests : public suite {
public:
    serialization_security_tests() : suite("Serialization Security") {}

    void forge_tests() override {

        // A malicious `_type_` value must NOT be parsed+executed as script source. The old
        // path did `execute(type_name + "()")`, so a `_type_` of "g_flag = 99; Dummy" ran the
        // injected statement. The fix resolves the type through the registry (no eval).
        test("from_json_type_name_not_executed", [&]() {
            auto eng = engine::make();
            jai::stdlib::register_all(*eng);
            eng->add_global("g_flag", script_value((script_int)0, eng.get()));
            eng->add_global("payload", std::string(R"({"_type_":"g_flag = 99; Dummy"})"));
            (void)eng->execute("from_json(payload)");
            check_eq((script_int)0, eng->execute("g_flag").as_int()); // injection must NOT fire
        });

        // A malicious property NAME must NOT be parsed+executed. The old path did
        // `execute(obj + "." + propName + " = " + tmp)`, so a key of "x = 1; g_flag = 99; z"
        // ran the injected statement. The fix sets fields through the symbolized setter, only
        // for fields the class actually declares.
        test("from_json_property_name_not_executed", [&]() {
            auto eng = engine::make();
            jai::stdlib::register_all(*eng);
            eng->execute("class Dummy { int x = 0; }");
            eng->add_global("g_flag", script_value((script_int)0, eng.get()));
            eng->add_global("payload",
                std::string(R"({"_type_":"Dummy","x = 1; g_flag = 99; z":5})"));
            (void)eng->execute("from_json(payload)");
            check_eq((script_int)0, eng->execute("g_flag").as_int()); // injection must NOT fire
        });

        // Same injection through the binary path (from_binary reconstructs `_type_` maps too).
        // The blob is produced from a map, so we don't hand-craft binary bytes.
        test("from_binary_type_name_not_executed", [&]() {
            auto eng = engine::make();
            jai::stdlib::register_all(*eng);
            eng->add_global("g_flag", script_value((script_int)0, eng.get()));
            eng->execute(R"( auto blob = to_binary({"_type_": "g_flag = 99; Dummy"}); )");
            (void)eng->execute("from_binary(blob)");
            check_eq((script_int)0, eng->execute("g_flag").as_int());
        });

        test("from_base64_type_name_not_executed", [&]() {
            auto eng = engine::make();
            jai::stdlib::register_all(*eng);
            eng->add_global("g_flag", script_value((script_int)0, eng.get()));
            eng->execute(R"( auto blob = to_base64({"_type_": "g_flag = 99; Dummy"}); )");
            (void)eng->execute("from_base64(blob)");
            check_eq((script_int)0, eng->execute("g_flag").as_int());
        });

        // Legitimate object reconstruction must still work end-to-end (guards the fix against
        // regressing the real round-trip path).
        test("from_json_object_roundtrip_intact", [&]() {
            auto eng = engine::make();
            jai::stdlib::register_all(*eng);
            eng->execute(R"(
                class Pt { int x = 0; int y = 0; }
                auto p = Pt(); p.x = 11; p.y = 22;
                auto j = to_json(p);
                auto q = from_json(j);
            )");
            check_eq((script_int)11, eng->execute("q.x").as_int());
            check_eq((script_int)22, eng->execute("q.y").as_int());
        });

        // A length prefix bounded only by uint32 lets a ~5-byte blob claim 4 billion elements;
        // the reader must reject it against the remaining buffer, not reserve/loop into a
        // multi-GB allocation on the live scene-load path. B5.
        test("binary_oversized_array_length_rejected", [&]() {
            auto eng = engine::make();
            std::vector<uint8_t> blob = { 0x03, 0xFF, 0xFF, 0xFF, 0xFF }; // array marker + 4B count
            jai::serialization::binary_archive_reader r(blob.data(), blob.size(), eng.get());
            check_throws([&]() { (void)r.begin_array(); }, "oversized array length must be rejected");
        });

        test("binary_oversized_map_length_rejected", [&]() {
            auto eng = engine::make();
            std::vector<uint8_t> blob = { 0x05, 0xFF, 0xFF, 0xFF, 0xFF }; // map marker + 4B count
            jai::serialization::binary_archive_reader r(blob.data(), blob.size(), eng.get());
            check_throws([&]() { (void)r.begin_map(); }, "oversized map length must be rejected");
        });
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::serialization_security_tests)

} // namespace jai::foundry::tests

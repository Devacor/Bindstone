#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/reflection.hpp>
#include <jaiscript/detail/parallel_transform.hpp>
#include <sstream>

namespace jai::foundry::tests {

enum class app_privilege : std::uint8_t { edit = jai::privilege_count, publish, view, unnamed, far_slot = 200 };
void jai_privilege_enum(app_privilege);

enum class oversized_privilege : int { past_the_end = 256, negative = -1 };
void jai_privilege_enum(oversized_privilege);

struct custom_privilege_probe {
    script_int read() { return 17; }
    script_int query() const { return 23; }
    static script_int make() { return 31; }
};

class custom_privilege_tests : public suite {
public:
    custom_privilege_tests() : suite("Custom Privilege") {}

    template<class F>
    void custom_test(const char* name, F body) {
        test(name, [this, body]() {
            auto eng = make_engine();
            eng->register_privilege(app_privilege::edit, "app.edit");
            eng->register_privilege(app_privilege::publish, "app.publish");
            eng->register_privilege(app_privilege::view, "app.view");
            body(*eng);
        });
    }

    template<class F>
    static std::string failure(F&& operation) {
        try { operation(); }
        catch (const std::exception& error) { return error.what(); }
        return {};
    }

    static std::string denied(const char* name = "app.edit") {
        return std::string("Insufficient privilege: ") + name + ".";
    }

    void forge_tests() override {
        custom_test("host_privileges_start_disabled_and_grants_are_engine_local", [this](engine& eng) {
            check_false(eng.is_enabled(app_privilege::edit));
            check_true(eng.is_enabled(jai::filesystem, jai::scoped_template));
            check_false(eng.is_enabled(jai::network));
            auto other = make_engine();
            other->enable(app_privilege::edit);
            eng.add_function("edit", []() { return script_int(42); }, app_privilege::edit);
            other->add_function("edit", []() { return script_int(42); }, app_privilege::edit);
            check_eq(denied(), failure([&]() { eng.execute("edit()"); }));
            check_eq(script_int(42), other->execute("edit()").as_int());
            other->disable(app_privilege::edit);
            check_eq(std::string("Insufficient privilege: 3."), failure([&]() { other->execute("edit()"); }));
            eng.enable(app_privilege::edit);
            check_eq(script_int(42), eng.execute("edit()").as_int());
            check_true(eng.is_enabled(app_privilege::edit, jai::filesystem));
        });

        custom_test("mixed_requirements_are_all_required_and_the_lowest_missing_slot_is_named", [](engine& eng) {
            int calls = 0;
            eng.add_function("publish", [&]() { return script_int(++calls); }, app_privilege::publish, jai::filesystem, app_privilege::edit, app_privilege::publish);
            check_eq(denied(), failure([&]() { eng.execute("publish()"); }));
            eng.enable(app_privilege::edit);
            check_eq(denied("app.publish"), failure([&]() { eng.execute("publish()"); }));
            eng.enable(app_privilege::publish);
            check_eq(script_int(1), eng.execute("publish()").as_int());
            eng.disable(jai::filesystem, app_privilege::edit);
            check_eq(denied("filesystem"), failure([&]() { eng.execute("publish()"); }));
            eng.enable(jai::filesystem, app_privilege::edit, app_privilege::publish);
            check_eq(script_int(2), eng.execute("publish()").as_int());
            eng.disable(app_privilege::edit, app_privilege::publish);
            check_false(eng.is_enabled(app_privilege::edit));
            check_false(eng.is_enabled(app_privilege::publish));
            check_true(eng.is_enabled(jai::filesystem));
            check_eq(2, calls);
        });

        custom_test("options_are_a_list_in_any_order", [](engine& eng) {
            eng.add_function("try_edit", []() { return true; }, jai::nodiscard, app_privilege::edit, jai::serial);
            eng.add_variadic_function("count_edit", [](jai::engine&, std::span<const jai::script_value> args) { return static_cast<script_int>(args.size()); },
                jai::serial, app_privilege::edit, jai::nodiscard);
            check_true(eng.is_nodiscard_function("try_edit") && eng.is_nodiscard_function("count_edit"));
            check_true(eng.check("try_edit();\nprint(1);").format().find("the result of 'try_edit' is discarded") != std::string::npos);
            check_eq(denied(), failure([&]() { eng.execute("bool b = try_edit(); b;"); }));
            eng.enable(app_privilege::edit);
            check_true(eng.execute("bool b = try_edit(); b;").as_bool());
            check_eq(script_int(2), eng.execute("int n = count_edit(1, 2); n;").as_int());
        });

        custom_test("saved_alias_and_reflection_recheck_grants", [](engine& eng) {
            stdlib::register_reflection_functions(eng.shared_from_this());
            int calls = 0;
            eng.add_function("edit", [&]() { return script_int(++calls); }, app_privilege::edit);
            eng.execute("var saved_edit = edit;");
            eng.enable(app_privilege::edit);
            check_eq(script_int(1), eng.execute("saved_edit()").as_int());
            eng.disable(app_privilege::edit);
            for (const char* code : {"edit()", "saved_edit()", "reflect::call(\"edit\")"}) {
                check_eq(denied(), failure([&]() { eng.execute(code); }));
            }
            check_eq(1, calls);
        });

        custom_test("methods_constructors_static_methods_and_variadics_share_the_guard", [](engine& eng) {
            stdlib::register_reflection_functions(eng.shared_from_this());
            dynamic_binder<custom_privilege_probe> binder(eng, "CustomProbe");
            binder.template constructor<>()
                .method("read", &custom_privilege_probe::read, jai::filesystem, app_privilege::edit)
                .method("query", &custom_privilege_probe::query, app_privilege::edit)
                .method("lambda", [](custom_privilege_probe& self) { return self.read(); }, app_privilege::edit, jai::serial)
                .static_method("make", &custom_privilege_probe::make, app_privilege::edit, jai::nodiscard)
                .build();
            eng.add_variadic_function("custom_count", [&eng](const std::vector<script_value>& args) -> checked_result<script_value> {
                return script_value(script_int(args.size()), &eng);
            }, app_privilege::edit, jai::filesystem);
            eng.execute("var probe = CustomProbe(); var saved_read = reflect::bind(probe, \"read\");");
            for (const char* code : {"probe.read()", "probe.query()", "probe.lambda()", "saved_read()", "custom_count(1, 2)", "int m = CustomProbe::make(); m;"}) {
                check_eq(denied(), failure([&]() { eng.execute(code); }));
            }
            eng.enable(app_privilege::edit);
            check_eq(script_int(17), eng.execute("saved_read()").as_int());
            check_eq(script_int(23), eng.execute("probe.query()").as_int());
            check_eq(script_int(17), eng.execute("probe.lambda()").as_int());
            check_eq(script_int(2), eng.execute("custom_count(1, 2)").as_int());
            check_eq(script_int(31), eng.execute("int m = CustomProbe::make(); m;").as_int());
        });

        custom_test("overloads_keep_individual_requirements", [](engine& eng) {
            eng.add_function("action", [](script_int) { return script_int(7); }, app_privilege::edit);
            eng.add_function("action", [](const std::string&) { return script_int(9); }, app_privilege::view);
            eng.enable(app_privilege::view);
            check_eq(denied(), failure([&]() { eng.execute("action(1)"); }));
            check_eq(script_int(9), eng.execute("action(\"x\")").as_int());
            eng.enable(app_privilege::edit);
            eng.disable(app_privilege::view);
            check_eq(script_int(7), eng.execute("action(1)").as_int());
            check_eq(denied("app.view"), failure([&]() { eng.execute("action(\"x\")"); }));
        });

        custom_test("reentrant_revoke_allows_started_call_only", [](engine& eng) {
            int calls = 0;
            eng.add_function("edit", [&]() {
                ++calls;
                eng.disable(app_privilege::edit);
                return script_int(42);
            }, app_privilege::edit);
            eng.enable(app_privilege::edit);
            check_eq(script_int(42), eng.execute("edit()").as_int());
            check_eq(denied(), failure([&]() { eng.execute("edit()"); }));
            check_eq(1, calls);
        });

        custom_test("denial_is_checked_catchable_and_printable_and_an_unnamed_one_shows_its_slot", [](engine& eng) {
            auto result = eng.require_privilege(app_privilege::edit);
            check_false(bool(result));
            check_eq(std::string("Insufficient privilege: app.edit."), eng.format_error(result));
            check_eq(std::string("Insufficient privilege: 6."), eng.format_error(eng.require_privilege(jai::filesystem, app_privilege::unnamed)));
            eng.add_function("edit", []() {}, app_privilege::edit);
            auto output = std::make_shared<std::ostringstream>();
            eng.set_output_stream(output);
            check_eq(std::string("Insufficient privilege: app.edit."), eng.execute(
                "var caught = \"\"; try { edit(); } catch (e) { caught = e; print(e); } caught;").as_string());
            check_eq(std::string("Insufficient privilege: app.edit.\n"), output->str());
            eng.register_privilege(app_privilege::unnamed, "app.renamed");
            check_eq(std::string("Insufficient privilege: app.renamed."), eng.format_error(eng.require_privilege(app_privilege::unnamed)));
        });

        custom_test("reflection_takes_slot_numbers", [](engine& eng) {
            eng.enable(app_privilege::edit);
            check_true(eng.execute("reflect::is_enabled(jai::filesystem, 3, jai::scoped_template)").as_bool());
            check_false(eng.execute("reflect::is_enabled(jai::filesystem, 4)").as_bool());
            check_false(eng.execute("reflect::is_enabled(jai::network)").as_bool());
            check_eq(std::string("reflect::is_enabled expects valid privilege flags"), failure([&]() { eng.execute("reflect::is_enabled(\"app.edit\")"); }));
        });

        custom_test("slots_past_sixty_four_are_privileges_too_and_past_the_type_are_refused", [](engine& eng) {
            eng.add_function("far", []() { return script_int(42); }, app_privilege::far_slot);
            check_eq(std::string("Insufficient privilege: 200."), failure([&]() { eng.execute("far()"); }));
            eng.enable(app_privilege::far_slot);
            check_true(eng.is_enabled(app_privilege::far_slot));
            check_true(eng.execute("reflect::is_enabled(200)").as_bool());
            check_eq(script_int(42), eng.execute("far()").as_int());
            const std::string out_of_range = "Privilege slot out of range";
            check_eq(out_of_range, failure([&]() { eng.enable(oversized_privilege::past_the_end); }));
            check_eq(out_of_range, failure([&]() { eng.add_function("never", []() {}, oversized_privilege::negative); }));
            check_eq(out_of_range, failure([&]() { eng.register_privilege(jai::filesystem, "files"); }));
            check_eq(std::string("Privilege name must not be empty"), failure([&]() { eng.register_privilege(app_privilege::edit, ""); }));
        });

        custom_test("active_region_rejects_all_mutation", [](engine& eng) {
            auto& state = detail::engine_access::parallel_state(eng);
            detail::parallel_region_table region;
            struct region_guard {
                detail::parallel_engine_state& state;
                string_symbolizer* symbols;
                ~region_guard() { state.active_region = nullptr; symbols->set_frozen(false); }
            } restore{state, detail::engine_access::get_symbolizer(eng)};
            state.active_region = &region;
            detail::engine_access::get_symbolizer(eng)->set_frozen(true);
            const std::string expected = "Cannot change privileges during a parallel region";
            check_eq(expected, failure([&]() { eng.enable(app_privilege::edit); }));
            check_eq(expected, failure([&]() { eng.disable(app_privilege::edit, jai::filesystem); }));
            check_eq(expected, failure([&]() { eng.register_privilege(app_privilege::unnamed, "app.new_in_region"); }));
            check_false(detail::engine_access::get_symbolizer(eng)->find("app.new_in_region").has_value());
            check_false(eng.is_enabled(app_privilege::edit));
            check_true(eng.is_enabled(jai::filesystem));
        });

        custom_test("worker_callable_copy_retains_live_gate_and_names_an_unnamed_slot", [](engine& eng) {
            eng.add_function("edit", []() { return script_int(42); }, app_privilege::edit);
            eng.add_function("plain", []() { return script_int(43); }, app_privilege::unnamed);
            auto copy = detail::engine_access::make_parallel_host_function_copy(eng, "edit");
            auto unnamed_copy = detail::engine_access::make_parallel_host_function_copy(eng, "plain");
            eng.enable(app_privilege::edit);
            auto allowed = copy.as_function()({});
            check_true(bool(allowed));
            if (allowed) { check_eq(script_int(42), allowed.value().as_int()); }
            eng.disable(app_privilege::edit);
            auto refused = copy.as_function()({});
            check_false(bool(refused));
            check_eq(std::string("Insufficient privilege: app.edit."), eng.format_error(refused));
            auto* symbols = detail::engine_access::get_symbolizer(eng);
            struct thaw_guard {
                string_symbolizer* symbols;
                ~thaw_guard() { symbols->set_frozen(false); }
            } thaw{symbols};
            symbols->set_frozen(true);
            check_eq(std::string("Insufficient privilege: 6."), eng.format_error(unnamed_copy.as_function()({})));
        });

        custom_test("nested_execute_sees_reentrant_revocation", [](engine& eng) {
            int calls = 0;
            eng.add_function("edit", [&]() { ++calls; }, app_privilege::edit);
            std::string nested_error;
            eng.add_function("outer", [&]() {
                eng.disable(app_privilege::edit);
                nested_error = failure([&]() { eng.execute("edit()"); });
                return script_int(42);
            }, app_privilege::edit);
            eng.enable(app_privilege::edit);
            check_eq(script_int(42), eng.execute("outer()").as_int());
            check_eq(denied(), nested_error);
            check_eq(0, calls);
        });
    }
};

} // namespace jai::foundry::tests

using custom_privilege_tests = jai::foundry::tests::custom_privilege_tests;
FOUNDRY_REGISTER(custom_privilege_tests)

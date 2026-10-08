// Signal auto-binding: declaring a JAI_SIGNAL_PROPERTY (or exposing a plain
// signal member via .property) is the whole registration — script connects
// through the generic "Signal" view with no bind_signal_type ceremony, and
// typed per-signature bindings keep precedence when present.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal_property.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include <jaiscript/properties/property_serialization.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/serialization/json_archive.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class scoreboard : public jai::property_owner<scoreboard> {
public:
	typedef void ScoreSignature(int);

	JAI_SIGNAL_PROPERTY(ScoreSignature, on_scored);

	jai::signal_emitter<void(int)> onTickedSignal;   // plain pair, the Clickable onEnabled shape
	jai::signal<void(int)> onTicked{ onTickedSignal };

	void score(int v) { on_scoredSignal(v); }
	void tick(int v) { onTickedSignal(v); }
};

// The MV Button shape: the signal lives on the BASE (Clickable declares onAccept) and
// script connects through a DERIVED registration (base_class + auto_bind).
class base_widget : public jai::property_owner<base_widget> {
public:
	typedef void PressSignature(int);
	JAI_SIGNAL_PROPERTY(PressSignature, on_press);
	void press(int v) { on_pressSignal(v); }
};

class derived_widget : public jai::property_owner<derived_widget, base_widget> {
public:
	derived_widget() = default;
};

// A host whose naming convention is not nameSignal: the emitter is named explicitly.
class door : public jai::property_owner<door> {
public:
	typedef void OpenSignature(int);
	JAI_SIGNAL_PROPERTY_AS(OpenSignature, on_open, open_emitter);
	void open(int v) { open_emitter(v); }
	void add_text_receiver(const std::string& id, const std::string& script) { open_emitter.connect(id, script); }
	bool has_receiver(const std::string& id) { return open_emitter.connected(id); }
};

class signal_auto_binding_tests : public suite {
public:
	signal_auto_binding_tests() : suite("Signal Auto Binding") {}

	static std::shared_ptr<engine> make_bound_engine() {
		auto eng = jai::foundry::make_engine();
		dynamic_binder<scoreboard>(*eng, "Scoreboard")
			.constructor<>()
			.method("score", &scoreboard::score)
			.method("tick", &scoreboard::tick)
			.property("onTicked", &scoreboard::onTicked)
			.auto_bind()
			.build();
		return eng;
	}

	void forge_tests() override {
		test("signal_property_as_names_its_emitter", [this]() {
			auto eng = jai::foundry::make_engine();
			dynamic_binder<door>(*eng, "Door")
				.constructor<>()
				.method("open", &door::open)
				.auto_bind()
				.build();
			eng->execute("var got = -1; var d = new Door();");
			eng->execute("d.on_open.connect(\"t\", [](var v) { got = v; });");
			eng->execute("d.open(12);");
			check_eq((int64_t)12, eng->execute("got").as_int());
		});

		test("signal_property_as_serializes_under_its_name", [this]() {
			auto eng = jai::foundry::make_engine();
			int64_t fired = 0;
			eng->add_function("mark", [counter = &fired](int v) { *counter += v; });
			auto original = std::make_shared<door>();
			original->add_text_receiver("bump", "mark(arg_0);");
			std::string json = jai::to_json(*eng, original);
			check_true(json.find("\"on_open\"") != std::string::npos, "the property key is the signal's name: " + json);
			auto restored = jai::from_json<std::shared_ptr<door>>(*eng, json);
			check(restored != nullptr, "loaded not null");
			check_true(restored->has_receiver("bump"), "text receiver persisted");
			restored->open(4);
			check_eq((int64_t)4, fired, "persisted text receiver runs through the named emitter");
		});

		test("signal_property_connects_via_auto_bind_only", [this]() {
			auto eng = make_bound_engine();
			eng->execute("var got = -1; var s = new Scoreboard();");
			eng->execute("s.on_scored.connect(\"t\", [](var v) { got = v; });");
			eng->execute("s.score(41);");
			check_eq((int64_t)41, eng->execute("got").as_int());
		});

		test("signal_view_reports_and_drops_connections", [this]() {
			auto eng = make_bound_engine();
			eng->execute("var got = 0; var s = new Scoreboard();");
			eng->execute("s.on_scored.connect(\"t\", [](var v) { got = got + v; });");
			check_eq(true, eng->execute("s.on_scored.connected(\"t\")").as<bool>());
			eng->execute("s.score(5);");
			eng->execute("s.on_scored.disconnect(\"t\");");
			check_eq(false, eng->execute("s.on_scored.connected(\"t\")").as<bool>());
			eng->execute("s.score(5);");
			check_eq((int64_t)5, eng->execute("got").as_int());
		});

		test("plain_signal_member_binds_without_ceremony", [this]() {
			auto eng = make_bound_engine();
			eng->execute("var got = -1; var s = new Scoreboard();");
			eng->execute("s.onTicked.connect(\"t\", [](var v) { got = v; });");
			eng->execute("s.tick(7);");
			check_eq((int64_t)7, eng->execute("got").as_int());
		});

		test("typed_bind_signal_type_keeps_precedence", [this]() {
			auto eng = jai::foundry::make_engine();
			bind_signal_type<void(int)>(*eng, "SignalInt");
			dynamic_binder<scoreboard>(*eng, "Scoreboard")
				.constructor<>()
				.method("tick", &scoreboard::tick)
				.property("onTicked", &scoreboard::onTicked)
				.build();
			eng->execute("var got = -1; var s = new Scoreboard();");
			eng->execute("s.onTicked.connect(\"t\", [](var v) { got = v; });");
			eng->execute("s.tick(9);");
			check_eq((int64_t)9, eng->execute("got").as_int());
		});

		test("view_reads_share_one_emitter", [this]() {
			auto eng = make_bound_engine();
			eng->execute("var count = 0; var s = new Scoreboard();");
			eng->execute("var a = s.on_scored; var b = s.on_scored;");
			eng->execute("a.connect(\"one\", [](var v) { count = count + 1; });");
			eng->execute("b.connect(\"two\", [](var v) { count = count + 1; });");
			eng->execute("s.score(1);");
			check_eq((int64_t)2, eng->execute("count").as_int());
		});

		test("text_receiver_fires_through_generic_view", [this]() {
			auto eng = make_bound_engine();
			eng->execute("var txtgot = 0; var s = new Scoreboard();");
			eng->execute("s.on_scored.connect(\"txt\", \"txtgot = 5;\");");
			eng->execute("s.score(1);");
			check_eq((int64_t)5, eng->execute("txtgot").as_int());
		});

		// Red-locked from the Workbench File panel: loadButton.onAccept threw "Object has
		// no member 'onAccept'" — onAccept is Clickable's, Button registers with
		// base_class + auto_bind. Both registration orders (static registrar order is
		// arbitrary in MV).
		test("inherited_signal_property_through_derived_registration", [this]() {
			auto bindBase = [](engine& a_engine) {
				dynamic_binder<base_widget>(a_engine, "BaseWidget")
					.constructor<>()
					.method("press", &base_widget::press)
					.auto_bind()
					.build();
			};
			auto bindDerived = [](engine& a_engine) {
				dynamic_binder<derived_widget>(a_engine, "DerivedWidget")
					.constructor<>()
					.base_class<base_widget>()
					.auto_bind()
					.build();
			};
			auto run = [&](bool a_baseFirst) {
				auto eng = jai::foundry::make_engine();
				if (a_baseFirst) { bindBase(*eng); bindDerived(*eng); }
				else { bindDerived(*eng); bindBase(*eng); }
				const char* order = a_baseFirst ? "base-first" : "derived-first";
				try {
					eng->execute("var got = -1; var w = new DerivedWidget();");
					eng->execute("w.press(3);");
					check_eq((int64_t)-1, eng->execute("got").as_int());
					eng->execute("w.on_press.connect(\"t\", [](var v) { got = v; });");
					eng->execute("w.press(4);");
					check_eq((int64_t)4, eng->execute("got").as_int());
				} catch (const std::exception& a_error) {
					throw runtime_error(std::string(order) + ": " + a_error.what());
				}
			};
			run(true);
			run(false);
		});
	}
};

} // namespace jai::foundry::tests

using signal_auto_binding_tests = jai::foundry::tests::signal_auto_binding_tests;
FOUNDRY_REGISTER(signal_auto_binding_tests)

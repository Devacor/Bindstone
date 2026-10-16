#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/core/engine_impl.hpp>

namespace jai::detail {

namespace {

template<typename ValueT>
void ensure_observable_ref_type_registered(engine& a_engine) {
	using RefType = jai::observable_property_ref<ValueT>;

	std::string type_name = "observable_property_ref<" + std::string(typeid(ValueT).name()) + ">";
	if (a_engine.is_type_registered(type_name)) {
		return;
	}

	dynamic_binder<RefType>(a_engine, type_name)
		.transparent_wrapper([](RefType& self) -> ValueT {
			return self.get();
		})
		.method("on_change", [](RefType& self, const script_value& callback) -> script_value {
			if (!callback.is_function()) {
				throw runtime_error("on_change requires a function argument");
			}

			auto* obs_prop = self.property();
			auto* prop_mgr = self.manager();
			if (!obs_prop || !prop_mgr) {
				throw runtime_error("Invalid observable property reference");
			}

			engine* eng = callback.get_engine();
			const script_function& script_func = callback.as_function();

			auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
				engine::external_call_guard scope(eng);
				script_value old_sv = script_value_from_cpp(old_val, eng);
				script_value new_sv = script_value_from_cpp(new_val, eng);
				throw_script_callback_failure(eng, script_func({old_sv, new_sv}));
			};

			auto recv = obs_prop->on_change.connect(std::move(cpp_callback));
			prop_mgr->template track_receiver<void(const ValueT&, const ValueT&)>(recv);

			return script_value(std::monostate{}, eng);
		})
		.build();
}

template<typename Getter, typename Setter>
void register_schema_property(engine& a_engine, class_definition& class_def, serialization::class_metadata& metadata,
		const std::string& name, const schema_property_access& access, Getter getter, Setter setter) {
	class_def.add_field(name, script_value(std::monostate{}, &a_engine));

	serialization::property_metadata prop_meta;
	prop_meta.name = name;
	prop_meta.read_only = false;
	prop_meta.type = nullptr;
	metadata.properties.push_back(prop_meta);

	class_def.add_method("_get_" + name, [getter = std::move(getter), access, engine_ptr = &a_engine, native_type_id = class_def.get_type_id()](const std::vector<script_value>& args) -> script_value {
		if (args.empty()) {
			throw runtime_error("Property getter called without 'this' object");
		}
		void* cpp_obj = access.extract(args[0], native_type_id);
		if (auto eng = engine_ptr) {
			return getter(cpp_obj);
		}
		throw runtime_error("Engine no longer exists");
	});
	class_def.register_property_getter(a_engine.symbolize(name), a_engine.symbolize("_get_" + name));

	class_def.add_method("_set_" + name, [setter = std::move(setter), access, engine_ptr = &a_engine, native_type_id = class_def.get_type_id()](const std::vector<script_value>& args) -> script_value {
		if (args.size() < 2) {
			throw runtime_error("Property setter requires 'this' and value");
		}
		void* cpp_obj = access.extract(args[0], native_type_id);
		setter(cpp_obj, args[1]);
		return script_value(std::monostate{}, engine_ptr);
	});

	uint64_t field_id = a_engine.symbolize(name);
	uint64_t setter_id = a_engine.symbolize("_set_" + name);
	class_def.register_property_setter(field_id, setter_id);
}

template<typename ValueT>
void bind_observable_change_method(engine& a_engine, class_definition& class_def, const std::string& prop_name, const schema_property_access& access) {
	engine* eng = &a_engine;
	std::string observe_method_name = "on_" + prop_name + "_change";

	auto method_func = [prop_name, eng, access, native_type_id = class_def.get_type_id()](const std::vector<script_value>& args) -> script_value {
		if (args.empty()) {
			throw runtime_error("Method called without 'this' object");
		}
		if (args.size() != 2) {
			throw_binder_arity("Method expects ", 1, args.size() - 1);
		}

		void* self = access.extract(args[0], native_type_id);

		if (eng) {
			parameter_storage storage;
			parameter_storage::scope_guard guard(eng, &storage);

			const script_value& callback = args[1];
			if (!callback.is_function()) {
				throw runtime_error("on_" + prop_name + "_change requires a function argument");
			}

			jai::property_manager& mgr = *access.manager(self);
			auto* base_prop = mgr.get(prop_name);
			if (!base_prop) {
				throw runtime_error("Property '" + prop_name + "' not found");
			}

			auto* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
			if (!obs_prop) {
				throw runtime_error("Property '" + prop_name + "' is not observable");
			}

			const script_function& script_func = callback.as_function();
			auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
				engine::external_call_guard scope(eng);
				script_value old_sv = script_value_from_cpp(old_val, eng);
				script_value new_sv = script_value_from_cpp(new_val, eng);
				throw_script_callback_failure(eng, script_func({old_sv, new_sv}));
			};

			auto recv = obs_prop->on_change.connect(std::move(cpp_callback));
			mgr.template track_receiver<void(const ValueT&, const ValueT&)>(recv);

			return script_value(std::monostate{}, eng);
		}
		throw runtime_error("Engine no longer exists");
	};

	class_def.add_method(observe_method_name, a_engine.guard_function(std::move(method_func), privilege::none), 1,
		std::vector<param_type_info>{ engine::map_cpp_type_to_param_info<script_value>() }, engine::map_cpp_type_to_param_info<script_value>());
}

template<typename ValueT>
bool bind_schema_typed(engine& a_engine, class_definition& class_def, serialization::class_metadata& metadata,
		const std::string& prop_name, std::type_index value_type, bool is_observable, const schema_property_access& access) {
	if (value_type != std::type_index(typeid(ValueT))) {
		return false;
	}

	engine* eng = &a_engine;

	if (is_observable) {
		ensure_observable_ref_type_registered<ValueT>(a_engine);

		auto getter = [prop_name, eng, access](void* self) -> script_value {
			auto* base_prop = access.find(self, prop_name);
			if (!base_prop) {
				return script_value(std::monostate{}, eng);
			}
			auto* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
			if (!obs_prop) {
				return script_value(std::monostate{}, eng);
			}
			auto ref = std::make_shared<jai::observable_property_ref<ValueT>>(obs_prop, access.manager(self));
			return eng->make_object(ref);
		};

		auto setter = [prop_name, access](void* self, const script_value& arg) {
			auto val = arg.as<ValueT>();
			jai::property_base* base_prop = access.find(self, prop_name);
			if (!base_prop) return;
			auto* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
			if (obs_prop) {
				*obs_prop = std::move(val);
			}
		};

		register_schema_property(a_engine, class_def, metadata, prop_name, access, std::move(getter), std::move(setter));
		bind_observable_change_method<ValueT>(a_engine, class_def, prop_name, access);
	} else {
		auto getter = [prop_name, eng, access](void* self) -> script_value {
			if (auto* prop = dynamic_cast<jai::property<ValueT>*>(access.find(self, prop_name))) {
				return script_value_from_cpp(prop->get(), eng);
			}
			return script_value(std::monostate{}, eng);
		};

		auto setter = [prop_name, access](void* self, const script_value& arg) {
			auto val = arg.as<ValueT>();
			jai::property_base* base_prop = access.find(self, prop_name);
			if (!base_prop) return;
			auto* regular_prop = dynamic_cast<jai::property<ValueT>*>(base_prop);
			if (regular_prop) {
				regular_prop->get() = std::move(val);
			}
		};

		register_schema_property(a_engine, class_def, metadata, prop_name, access, std::move(getter), std::move(setter));
	}

	return true;
}

} // namespace

void bind_schema_math_property(engine& a_engine, class_definition& class_def, serialization::class_metadata& metadata,
		const std::string& prop_name, const math_property_bridge& bridge, const schema_property_access& access) {
	engine* eng = &a_engine;
	auto getter = [prop_name, bridge, eng, access](void* self) -> script_value {
		const jai::property_base* prop = access.find(self, prop_name);
		const void* host = prop ? prop->erased_value_ptr() : nullptr;
		if (!host) {
			return script_value(std::monostate{}, eng);
		}
		uint32_t words[4] = {0, 0, 0, 0};
		bridge.read(host, words);
		return script_value::make_vec(bridge.kind, words, eng);
	};
	auto setter = [prop_name, bridge, access](void* self, const script_value& arg) {
		uint32_t words[4] = {0, 0, 0, 0};
		if (!arg.math_words_as(bridge.kind, words)) {
			throw_numeric_error(script_value::math_conversion_text(bridge.kind), arg.deref().type_id(), 0);
		}
		if (jai::property_base* prop = access.find(self, prop_name)) {
			bridge.assign(*prop, words);
		}
	};
	register_schema_property(a_engine, class_def, metadata, prop_name, access, std::move(getter), std::move(setter));
}

bool bind_schema_primitive_property(engine& a_engine, class_definition& class_def, serialization::class_metadata& metadata,
		const std::string& prop_name, std::type_index value_type, bool is_observable, const schema_property_access& access) {
	return bind_schema_typed<int>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<float>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<double>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<bool>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<std::string>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<int64_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<uint64_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<int32_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<uint32_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<int16_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<uint16_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<int8_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access) ||
		bind_schema_typed<uint8_t>(a_engine, class_def, metadata, prop_name, value_type, is_observable, access);
}

} // namespace jai::detail

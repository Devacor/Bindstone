#include <jaiscript/stdlib/json.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <optional>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <atomic>

namespace jai {
namespace stdlib {

    inline std::string escape_json_string(const std::string& str) {
        std::ostringstream oss;
        for (size_t i = 0; i < str.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c >= 0x80) {
                        // UTF-8 multi-byte: pass through unescaped (valid JSON per RFC 8259)
                        oss << str[i];
                    } else if (c >= 0x20) {
                        oss << str[i];
                    } else {
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(c);
                    }
            }
        }
        return oss.str();
    }

    std::string to_json_impl(const script_value& val, int indent, int current_depth) {
        std::ostringstream oss;
        const std::string indent_str(indent > 0 ? indent : 0, ' ');
        const std::string current_indent(indent > 0 ? current_depth * indent : 0, ' ');
        const std::string next_indent(indent > 0 ? (current_depth + 1) * indent : 0, ' ');
        const bool pretty = (indent >= 0);

        switch (val.type()) {
        case script_value_type::jai_null_type:
            oss << "null";
            break;
            
        case script_value_type::jai_bool_type:
            oss << (val.as_bool() ? "true" : "false");
            break;
            
        case script_value_type::jai_int_type:
            oss << val.as_int();
            break;
            
        case script_value_type::jai_float_type: {
            double d = val.as_float();
            if (std::isfinite(d)) {
                oss << std::setprecision(15) << d;
                // Ensure decimal point for whole numbers
                std::string str = oss.str();
                if (str.find('.') == std::string::npos && str.find('e') == std::string::npos) {
                    oss << ".0";
                }
            } else if (std::isnan(d)) {
                oss << "null"; // JSON doesn't support NaN
            } else {
                oss << "null"; // JSON doesn't support Infinity
            }
            break;
        }
            
        case script_value_type::jai_string_type:
            oss << '"' << escape_json_string(val.as_string()) << '"';
            break;

        case script_value_type::jai_utf8_string_type:
            oss << '"' << escape_json_string(val.unchecked_as_utf8().bytes) << '"';
            break;

        case script_value_type::jai_vec2_type:
        case script_value_type::jai_vec3_type:
        case script_value_type::jai_vec4_type:
        case script_value_type::jai_color_type:
        case script_value_type::jai_ivec2_type:
        case script_value_type::jai_ivec3_type: {
            // the archive's shape (json_archive.hpp): {"$type": "math::K", "$val": {canonical components}}
            const script_value_type kind = val.unchecked_vec_kind();
            std::string body = std::string("{\"$type\":\"") + detail::math::kind_name(kind) + "\",\"$val\":{";
            for (size_t k = 0; k < detail::math::dims(kind); ++k) {
                if (k) body += ',';
                body += '"'; body += detail::math::component_name(kind, k); body += "\":";
                if (detail::math::integral(kind)) {
                    body += std::to_string(detail::math::i32_at(val, k));
                } else {
                    const float c = detail::math::f32_at(val, k);
                    if (std::isfinite(c)) { detail::math::append_f32(body, c); } else { body += "null"; }
                }
            }
            body += "}}";
            oss << body;
            break;
        }

        case script_value_type::jai_array_type: {
            const script_array* node = val.unchecked_array_node();
            if (!node) { throw runtime_error("script_value is not an array"); }
            const bool empty = node->size() == 0;
            oss << '[';
            if (pretty && !empty) oss << '\n';

            size_t i = 0;
            node->for_each(val.get_engine(), [&](const script_value& elem) {
                if (i++ > 0) {
                    oss << ',';
                    if (pretty) oss << '\n';
                }
                if (pretty) oss << next_indent;
                oss << to_json_impl(elem, indent, current_depth + 1);
            });

            if (pretty && !empty) {
                oss << '\n' << current_indent;
            }
            oss << ']';
            break;
        }
            
        case script_value_type::jai_map_type: {
            const auto& map = val.as_map();
            oss << '{';
            if (pretty && !map.empty()) oss << '\n';
            
            bool first = true;
            for (const auto& [k, v] : map) {
                if (!first) {
                    oss << ',';
                    if (pretty) oss << '\n';
                }
                first = false;
                
                if (pretty) oss << next_indent;
                
                // Keys must be strings in JSON
                if (k.is_text()) {
                    oss << '"' << escape_json_string(k.deref().unchecked_text()) << '"';
                } else {
                    // Convert other types to string representation
                    oss << '"' << escape_json_string(k.to_string()) << '"';
                }
                
                oss << ':';
                if (pretty) oss << ' ';
                oss << to_json_impl(v, indent, current_depth + 1);
            }
            
            if (pretty && !map.empty()) {
                oss << '\n' << current_indent;
            }
            oss << '}';
            break;
        }
            
        case script_value_type::jai_object_type: {
            // Try to extract as class_instance for C++ bound objects
            try {
                auto instance = val.as<std::shared_ptr<class_instance>>();
                if (instance) {
                    // Get the class definition to access property names
                    auto classDef = instance->get_class_definition();
                    if (!classDef) {
                        // No class definition, fall back to basic type info
                        oss << "{\"_type_\": \"" << instance->get_class_name() << "\"}";
                        break;
                    }
                    
                    oss << '{';
                    bool first = true;
                    
                    // First add _type_ field
                    if (pretty) oss << '\n' << next_indent;
                    oss << "\"_type_\": \"" << instance->get_class_name() << "\"";
                    first = false;
                    
                    // Serialize all registered properties
                    auto eng = classDef->get_engine();
                    if (eng) {
                        for (const auto& propName : classDef->get_property_names()) {
                            // Call the getter method to get property value
                            uint64_t getter_id = eng->get_symbolizer()->get_getter_id_with_view(eng->symbolize(propName)).first;
                            auto getter = classDef->get_method(getter_id);
                            if (getter.type() == script_value_type::jai_function_type) {
                                try {
                                    // Call getter with instance as 'this'
                                    std::vector<script_value> args = { val }; // Pass the instance itself
                                    auto result = getter.as_function()(args);
                                    if (result) {
                                        script_value propscript_value = std::move(result.value());

                                        if (!first) {
                                            oss << ',';
                                            if (pretty) oss << '\n' << next_indent;
                                        }
                                        first = false;
                                        oss << "\"" << propName << "\": ";
                                        oss << to_json_impl(propscript_value, indent, current_depth + 1);
                                    }
                                    // Skip properties that fail (result has error)
                                } catch (...) {
                                    // Skip properties that fail to serialize
                                }
                            }
                        }
                    }
                    
                    if (pretty && !first) {
                        oss << '\n' << current_indent;
                    }
                    oss << '}';
                    break;
                }
            } catch (...) {
                // Not a class_instance
            }
            
            // Fallback for other object types (unbound C++ types)
            oss << "{\"_type_\": \"UNBOUND_TYPE_[" << val.get_type_info()->type_name << "]\"";
            
            // If object has to_string, include it
            try {
                std::string str = val.to_string();
                oss << ", \"_str_\": \"" << escape_json_string(str) << "\"";
            } catch (...) {
                // Ignore if to_string fails
            }
            
            oss << "}";
            break;
        }
            
        case script_value_type::jai_char_type:
            // Treat char as single-character string
            oss << '"' << escape_json_string(std::string(1, val.as_char())) << '"';
            break;
            
        
        default:
            // Functions, References, shared_ptr, weak_ptr -> not JSON serializable
            oss << "null";
            break;
        }
        
        return oss.str();
    }

    // Reconstruct a class instance from a {"_type_": Name, ...} map WITHOUT eval'ing
    // attacker-controlled keys (the from_json/from_binary/from_base64 key-injection fix, B5).
    // Both the type name and every property name are treated as DATA, not code:
    //   * the type is resolved through the registry first — a malicious _type_ like
    //     "touch(); Dummy" matches no class, so we return null and the caller falls back to the
    //     raw map; the attacker string is never parsed or executed. A resolved name is a bare
    //     class identifier, so default-constructing it is injection-safe and seeds the full
    //     field set (incl. inherited and C++-base members) exactly as the language's `Name()`.
    //   * each property name is symbolized and assigned only if it names a declared member, via
    //     the same setter/field dispatch the interpreter uses — never built into a script string.
    // This also drops the old per-property parse+execute (an O(n) reparse) for a direct write.
    // Returns null if `type_name` is unregistered (caller returns the map); a constructor's error fails the load.
    inline script_value reconstruct_typed_object(
            engine* eng,
            const script_map& map,
            const std::string& type_name,
            const std::function<script_value(const script_value&)>& transform,
            const std::function<void(const script_value&)>& on_constructed = {}) {
        if (!eng->get_class_definition(type_name)) return script_value(std::monostate{}, eng);

        // the class's constructor, called as Name() would call it; its error (or throw) is the load's
        script_value ctor = eng->get_variable(type_name);
        if (!ctor.is_function()) return script_value(std::monostate{}, eng);
        auto constructed = ctor.as_function()({});
        if (!constructed) {
            throw runtime_error(eng->format_error(constructed));
        }
        if (eng->backend_unwinding()) {
            throw runtime_error(eng->backend_exception_text());
        }
        script_value instance = std::move(constructed.value());
        std::shared_ptr<class_instance> inst = instance.is_object() ? instance.get_class_instance() : nullptr;
        if (!inst) return script_value(std::monostate{}, eng);
        if (on_constructed) { on_constructed(instance); }
        {
            for (const auto& [key, value] : map) {
                if (!key.is_string()) continue;
                const std::string prop_name = key.as_string();
                if (prop_name == "_type_" || prop_name == "_version_") continue;

                // A property NAME is data, never code: resolve it to a member symbol and assign
                // only if it names a declared member. A malicious key ("x=1; touch(); y") has no
                // setter and is not a field, so it is silently skipped. This mirrors the
                // interpreter's own `obj.member = value` resolution (the symbolizer's
                // "_set_<member>" setter convention), so a C++-bound property — pure or inherited
                // — routes through its setter (coercion lives in the binding) and a script field
                // is written directly.
                uint64_t member_id = eng->symbolize(prop_name);
                uint64_t setter_id = eng->symbolize("_set_" + prop_name);
                script_value setter = inst->get_method(setter_id, false);
                script_value v = transform ? transform(value) : value.clone();
                if (setter.is_function()) {
                    // a store the member refuses leaves its constructed value; a setter the
                    // script declared fails the load with its own error
                    auto stored = setter.as_function()({ instance, v });
                    if (!stored && ((inst->get_class_definition() && inst->get_class_definition()->script_declares_user_accessor(setter_id)) ||
                                    stored.error() == make_error_code(runtime_error_code::cpp_exception))) {
                        throw runtime_error(eng->format_error(stored));
                    }
                    if (eng->backend_unwinding()) { return instance; }
                } else if (inst->has_field(member_id)) {
                    inst->set_field(member_id, v);
                }
            }

            // post_load(self, version) migration hook, if the class declares one.
            if (auto* def = inst->get_class_definition()) {
                if (auto class_eng = def->get_engine()) {
                    uint64_t post_load_id = class_eng->symbolize("post_load");
                    script_value post_load = def->get_method(post_load_id, false);
                    if (post_load.type() == script_value_type::jai_function_type) {
                        script_int version = 1;
                        auto version_it = map.find(script_value("_version_", eng));
                        if (version_it != map.end() && version_it->second.is_int()) {
                            version = version_it->second.as<script_int>();
                        }
                        std::vector<script_value> args = { instance, script_value(version, eng) };
                        auto loaded = post_load.as_function()(args);
                        if (!loaded) {
                            throw runtime_error(eng->format_error(loaded));
                        }
                    }
                }
            }
            return instance;
        }
    }

	// Every archive format restores nested script classes through the same walk.
	// Serialized names remain data; reconstruction uses only registered classes/members.
	// A handle to a loaded instance (what new T() yields): a load that aliases one object hands out the same handle
	inline script_value script_handle_of(engine* eng, script_value object) {
		const auto type = object.get_type_info();
		if (object.is_object() && type && type->base_type == script_value_type::jai_object_type) {
			object.set_type_info(eng->get_type_info_shared_ptr(type.get()));
		}
		return object;
	}


	inline bool archive_handle_parts(engine* eng, const script_map& map, script_int& id, const script_value*& data, bool& weak) {
		weak = false;
		auto id_it = map.find(script_value(std::string("$shared_ptr_id"), eng));
		if (id_it == map.end()) {
			id_it = map.find(script_value(std::string("$weak_ptr_id"), eng));
			weak = true;
		}
		if (id_it == map.end() || !id_it->second.is_int()) { return false; }
		auto data_it = map.find(script_value(std::string(weak ? "$weak_ptr_data" : "$shared_ptr_data"), eng));
		if (map.size() == 2 && data_it == map.end()) { return false; }
		id = id_it->second.as_int();
		data = data_it == map.end() ? nullptr : &data_it->second;
		return true;
	}

	inline void index_archive_handles(engine* eng, const script_value& value, archive_handles& handles) {
		if (value.is_map()) {
			const auto& map = value.as_map();
			script_int id = 0;
			const script_value* data = nullptr;
			bool weak = false;
			if (map.size() <= 2 && archive_handle_parts(eng, map, id, data, weak) && data && id != 0) {
				handles.payloads.try_emplace(id, *data);
			}
			for (const auto& [key, child] : map) { index_archive_handles(eng, child, handles); }
		} else if (value.is_array()) {
			value.unchecked_array_node()->for_each(eng, [&](const script_value& child) { index_archive_handles(eng, child, handles); });
		}
	}

	inline std::optional<script_value> json_tagged_value(engine* eng, const script_map& map, bool json_string_keys, archive_handles& handles);
	inline std::optional<script_value> archive_handle_value(engine* eng, const script_map& map, bool json_string_keys, archive_handles& handles);

	// handle_id: the archive id a handle's object is loading under, registered as soon as it is constructed so a cycle back to it resolves
	script_value reconstruct_archive_value(engine* eng, const script_value& value, bool json_string_keys, archive_handles& handles,
	                                       script_int handle_id) {
		if (value.is_map()) {
			const auto& map = value.as_map();
			if (map.size() <= 2) {
				if (auto handle = archive_handle_value(eng, map, json_string_keys, handles)) { return std::move(*handle); }
			}
			if (json_string_keys && map.size() == 1) {
				if (auto tagged = json_tagged_value(eng, map, json_string_keys, handles)) { return std::move(*tagged); }
			}
			auto type_it = map.find(script_value("_type_", eng));
			if (type_it != map.end() && type_it->second.is_string()) {
				auto object = reconstruct_typed_object(eng, map, type_it->second.as_string(),
					[eng, json_string_keys, &handles](const script_value& child) { return reconstruct_archive_value(eng, child, json_string_keys, handles); },
					[eng, &handles, handle_id](const script_value& instance) {
						if (handle_id != 0) { handles.loaded.try_emplace(handle_id, script_handle_of(eng, instance)); }
					});
				if (!object.is_null()) {
					return object;
				}
			}
			auto result = script_value::make_map(json_string_keys ? eng->get_type_info_string() : nullptr, nullptr, eng);
			auto& restored = const_cast<script_map&>(result.as_map());
			for (const auto& [key, child] : map) {
				restored.emplace(key, reconstruct_archive_value(eng, child, json_string_keys, handles));
			}
			return result;
		}
		if (value.is_array()) {
			auto result = script_value::make_array(nullptr, eng);
			auto& restored = const_cast<std::vector<script_value>&>(result.as_array());
			restored.reserve(value.unchecked_array_node()->size());
			value.unchecked_array_node()->for_each(eng, [&](const script_value& child) {
				restored.push_back(reconstruct_archive_value(eng, child, json_string_keys, handles));
			});
			return result;
		}
		return value;
	}

	// One archive's script value back to live values: classes by name, handles by id
	script_value load_archive_value(engine* eng, const script_value& value, bool json_string_keys) {
		archive_handles handles;
		handles.root = &value;
		return reconstruct_archive_value(eng, value, json_string_keys, handles);
	}

	[[noreturn]] JAI_NOINLINE inline void throw_unwritten_handle(script_int id) {
		throw serialization_error("Invalid shared_ptr ID: " + std::to_string(id));
	}

	inline std::optional<script_value> archive_handle_value(engine* eng, const script_map& map, bool json_string_keys, archive_handles& handles) {
		script_int id = 0;
		const script_value* data = nullptr;
		bool weak = false;
		if (!archive_handle_parts(eng, map, id, data, weak)) { return std::nullopt; }
		script_value target(std::monostate{}, eng);
		if (id != 0) {
			if (auto known = handles.loaded.find(id); known != handles.loaded.end()) {
				target = known->second;
			} else {
				if (!data) {
					if (!handles.indexed && handles.root) {
						handles.indexed = true;
						index_archive_handles(eng, *handles.root, handles);
					}
					if (auto found = handles.payloads.find(id); found != handles.payloads.end()) { data = &found->second; }
				}
				if (data) {
					if (!handles.loading.insert(id).second) { throw_unwritten_handle(id); }
					script_value rebuilt = reconstruct_archive_value(eng, *data, json_string_keys, handles, id);
					handles.loading.erase(id);
					target = handles.loaded.try_emplace(id, script_handle_of(eng, std::move(rebuilt))).first->second;
				} else if (!weak) {
					throw_unwritten_handle(id);
				}
			}
		}
		if (!weak) {
			if (target.is_null()) { target.set_type_info(eng->get_type_info_shared_ptr(nullptr)); }
			return target;
		}
		if (target.is_object()) {
			if (auto observed = script_value::make_weak_ptr(target, eng)) { return std::move(observed.value()); }
		}
		return script_value::make_empty_weak_ptr(eng->get_type_info_weak_ptr(nullptr), eng);
	}

	// JSON's tagged values: {"$char": n} is the char n (0..255), {"$map": [[k, v], ...]} a map whose keys keep their kind. Any other
	// shape stays the map it is
	inline std::optional<script_value> json_tagged_value(engine* eng, const script_map& map, bool json_string_keys, archive_handles& handles) {
		const auto& [tag, payload] = *map.begin();
		if (!tag.is_string()) { return std::nullopt; }
		if (tag.as_string() == "$char") {
			if (payload.is_int() && payload.as_int() >= 0 && payload.as_int() <= 255) {
				return script_value(static_cast<script_char>(payload.as_int()), eng);
			}
			return std::nullopt;
		}
		if (tag.as_string() != "$map" || !payload.is_array()) { return std::nullopt; }
		const script_array* pairs = payload.unchecked_array_node();
		for (size_t i = 0; i < pairs->size(); ++i) {
			script_value pair = pairs->get(i, eng);
			if (!pair.is_array() || pair.unchecked_array_node()->size() != 2) { return std::nullopt; }
		}
		auto result = script_value::make_map(nullptr, nullptr, eng);
		auto& restored = const_cast<script_map&>(result.as_map());
		for (size_t i = 0; i < pairs->size(); ++i) {
			script_value pair = pairs->get(i, eng);
			const script_array* kv = pair.unchecked_array_node();
			restored.emplace(reconstruct_archive_value(eng, kv->get(0, eng), json_string_keys, handles),
			                          reconstruct_archive_value(eng, kv->get(1, eng), json_string_keys, handles));
		}
		return result;
	}

    // Register JSON and binary serialization functions with an engine
    void register_json_functions(engine& eng_ref) {
        engine* eng = &eng_ref;

        eng_ref.add_variadic_function("to_json", [eng](const std::vector<script_value>& args) -> script_value {
            if (args.size() == 1) {
                serialization::json_archive_writer writer(-1, eng); // No indentation, with engine ref
                writer.write_value(args[0]);
                return script_value(writer.str(), eng);
            } else if (args.size() == 2) {
                int indent = static_cast<int>(args[1].as_int());
                serialization::json_archive_writer writer(indent, eng);
                writer.charge_indentation();
                writer.write_value(args[0]);
                return script_value(writer.str(), eng);
            } else {
                throw runtime_error("to_json expects 1 or 2 arguments, got " + std::to_string(args.size()));
            }
        });
        
		eng_ref.add_function("from_json", [eng](const std::string& json) -> script_value {
			serialization::json_archive_reader reader(json, eng);
			return load_archive_value(eng, reader.read_value(), true);
		});
        
        eng_ref.add_variadic_function("to_binary", [eng](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("to_binary expects exactly 1 argument, got " + std::to_string(args.size()));
            }

            serialization::binary_archive_writer writer(eng);
            writer.write_value(args[0]);

            const auto& data = writer.data();
            script_string binary_str(data.begin(), data.end());
            return script_value(binary_str, eng);
        });

        eng_ref.add_variadic_function("from_binary", [eng](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("from_binary expects exactly 1 argument, got " + std::to_string(args.size()));
            }

            if (!args[0].is_string()) {
                throw runtime_error("from_binary expects a string argument");
            }

            script_string binary_data = args[0].as<script_string>();
            std::vector<uint8_t> data(binary_data.begin(), binary_data.end());

            serialization::binary_archive_reader reader(data, eng);

			return load_archive_value(eng, reader.read_value(), false);
        });

        eng_ref.add_variadic_function("to_base64", [eng](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("to_base64 expects exactly 1 argument, got " + std::to_string(args.size()));
            }

            serialization::binary_archive_writer writer(eng);
            writer.write_value(args[0]);

            const auto& data = writer.data();
            std::string binary_str(data.begin(), data.end());
            return script_value(base64_encode(binary_str), eng);
        });

        eng_ref.add_function("from_base64", [eng](const std::string& base64_str) -> script_value {
            std::string binary_str = base64_decode(base64_str);
            std::vector<uint8_t> data(binary_str.begin(), binary_str.end());

            serialization::binary_archive_reader reader(data, eng);
			return load_archive_value(eng, reader.read_value(), false);
        });

        eng_ref.add_function("base64_encode", [eng](const std::string& input) -> script_value {
            return script_value(base64_encode(input), eng);
        });

        eng_ref.add_function("base64_decode", [eng](const std::string& input) -> script_value {
            return script_value(base64_decode(input), eng);
        });
    }

} // namespace stdlib
} // namespace jai

#pragma once

#include "archive_impl.hpp"
#include <jaiscript/core/class_definition.hpp>
#include <stack>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <charconv>     // std::to_chars for shortest round-trippable float output
#include <string_view>

// Maximum nesting depth for JSON TEXT parsing (from_json). Each level recurses a
// couple of native stack frames, so this is intentionally well below
// JAI_MAX_SERIALIZATION_DEPTH (used for in-memory traversal) to guarantee the
// guard fires before deeply-nested / adversarial input exhausts the call stack.
#ifndef JAI_MAX_JSON_PARSE_DEPTH
#define JAI_MAX_JSON_PARSE_DEPTH 128
#endif

namespace jai {
namespace serialization {

// JSON archive writer (CRTP, no virtual dispatch)
class json_archive_writer : public archive_writer_impl<json_archive_writer> {
public:
    json_archive_writer(int indent = 2) : archive_writer_impl<json_archive_writer>(), indent_(indent), json_depth_(0) {}
    json_archive_writer(int indent, engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(indent), json_depth_(0) {}
    explicit json_archive_writer(engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(2), json_depth_(0) {}

    // JSON format properties (compile-time constants, no virtual dispatch)
    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

    // Basic type serialization - non-virtual
    void write_int8(int8_t value) { write_json_value(value); }
    void write_int16(int16_t value) { write_json_value(value); }
    void write_int32(int32_t value) { write_json_value(value); }
    void write_int64(int64_t value) { write_json_value(value); }
    void write_uint8(uint8_t value) { write_json_value(value); }
    void write_uint16(uint16_t value) { write_json_value(value); }
    void write_uint32(uint32_t value) { write_json_value(value); }
    void write_uint64(uint64_t value) { write_json_value(value); }
    void write_float32(float value) { write_json_value(value); }
    void write_float64(double value) { write_json_value(value); }
    void write_bool(bool value) { write_json_value(value); }

    void write_null() { write_json_value("null"); }

    void write_string(const std::string& value) {
        write_json_value('"' + escape_json_string_local(value) + '"');
    }

    void write_binary(const void* data, size_t size) {
        // Encode binary as a hex string. Build directly instead of streaming each byte
        // through std::ostringstream with std::hex/setw/setfill (re-imbues stream state
        // per byte).
        static const char hexd[] = "0123456789abcdef";
        std::string s;
        s.reserve(size * 2 + 2);
        s += '"';
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            s += hexd[(bytes[i] >> 4) & 0xF];
            s += hexd[bytes[i] & 0xF];
        }
        s += '"';
        write_json_value(s);
    }

    // Object/array structure - non-virtual
    // No-argument version for inline objects (no type metadata)
    void begin_object() {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);
    }

    void begin_object(const std::string& type_name, uint32_t version) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);

        // Write _type_ and _version_ metadata
        write_property_name("_type_");
        write_string(type_name);
        if (version > 1) {
            write_property_name("_version_");
            write_uint32(version);
        }
    }

    void end_object() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void begin_array(size_t size) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '[';
        json_depth_++;
        container_stack_.push(ContainerType::Array);
        first_in_container_.push(true);
    }

    void end_array() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << ']';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void write_property_name(const std::string& name) {
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(name) << "\": ";
        first_in_container_.top() = false;
        // Context is managed by base class serialize() which pushes PropertyValue after this
    }

    // Map serialization - JSON uses native object format
    void begin_map(size_t size) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);
    }

    void end_map() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void write_map_key(const std::string& key) {
        // Same as write_property_name
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(key) << "\": ";
        first_in_container_.top() = false;
        // Context is managed by base class which pushes MapValue after this
    }

    void write_value(const script_value& value) {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        // Handle shared_ptr types with proper tracking, delegate others to stdlib
        switch (value.type()) {
            
            case script_value_type::jai_weak_ptr_type: {
                // Serialize weak_ptr by tracking the shared_ptr it references
                auto weak = value.get_weak_ptr();
                auto shared = weak.lock();  // Try to lock to get shared_ptr<object_holder>

                if (!shared) {
                    // Expired or null weak_ptr
                    oss_ << "{\"$weak_ptr_id\": 0}";
                } else {
                    // Track the shared_ptr and write its ID
                    auto [id, is_new] = track_shared_ptr(shared.get());
                    oss_ << "{\"$weak_ptr_id\": " << id;

                    // If this is the first time seeing this object, serialize it inline
                    if (is_new) {
                        oss_ << ", \"$weak_ptr_data\": ";
                        // Create a script_value from the object_holder using factory method
                        script_value shared_val = script_value::make_object(
                            shared->type_name,
                            shared->type_id,
                            shared->data,
                            engine_ref_,
                            shared->is_class_instance_wrapper
                        );
                        write_value(shared_val);
                    }

                    oss_ << "}";
                }
                break;
            }
            
            case script_value_type::jai_array_type: {
                const auto& arr = value.as_array();
                oss_ << '[';
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss_ << ',';
                    write_value(arr[i]);  // Recursive call handles shared_ptrs
                }
                oss_ << ']';
                break;
            }
            
            case script_value_type::jai_map_type: {
                const auto& map = value.as_map();
                oss_ << '{';
                bool first = true;
                for (const auto& [k, v] : map) {
                    if (!first) oss_ << ',';
                    first = false;
                    
                    if (k.is_string()) {
                        oss_ << '"' << escape_json_string_local(k.as_string()) << '"';
                    } else {
                        oss_ << '"' << escape_json_string_local(k.to_string()) << '"';
                    }
                    
                    oss_ << ':';
                    write_value(v);  // Recursive call handles shared_ptrs
                }
                oss_ << '}';
                break;
            }
            
            case script_value_type::jai_object_type: {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";

                auto eng = engine_ref_;
                if (!eng) {
                    oss_ << "null";
                    break;
                }

                // Try to serialize as class_instance
                try {
                    auto instance = value.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();

                        // For script classes, type_info->type_name may be "any" - use the actual class name
                        if (class_def) {
                            type_name = class_def->get_name();
                        }

                        oss_ << '{';
                        bool first = true;

                        // Add _type_ field
                        oss_ << "\"_type_\":\"" << escape_json_string_local(type_name) << "\"";
                        first = false;

                        // Check if this type is registered in the serialization registry
                        const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);

                        if (metadata && !metadata->properties.empty() && class_def) {
                            // C++ bound class with registered properties - use getters
                            for (const auto& prop_name : class_def->get_property_names()) {
                                uint64_t getter_id = eng->symbolize("_get_" + prop_name);
                                auto getter = class_def->get_method(getter_id);
                                if (getter.type() == script_value_type::jai_function_type) {
                                    try {
                                        std::vector<script_value> args = {value};
                                        auto result = getter.as_function()(args);
                                        if (result) {
                                            if (!first) oss_ << ',';
                                            first = false;
                                            oss_ << "\"" << escape_json_string_local(prop_name) << "\":";
                                            write_value(std::move(result.value()));
                                        }
                                    } catch (...) {
                                        // Skip properties that fail to get
                                    }
                                }
                            }
                        } else {
                            // Script-defined class or unregistered - serialize fields directly
                            const auto& fields = instance->get_fields();
                            for (const auto& [field_id, field_val] : fields) {
                                // Skip the __cpp_object__ field - it's not serializable
                                std::string field_name = std::string(eng->get_symbolizer()->get_string(field_id));
                                if (field_name == "__cpp_object__") continue;

                                if (!first) oss_ << ',';
                                first = false;
                                oss_ << "\"" << escape_json_string_local(field_name) << "\":";
                                write_value(field_val);
                            }
                        }

                        oss_ << '}';
                        break;
                    }
                } catch (...) {
                    // Not a class_instance - fall through to default
                }

                // Fallback for other object types
                oss_ << to_json_impl_fallback(value, 0, 0);
                break;
            }

            default: {
                // Use the existing to_json implementation for other types
                oss_ << to_json_impl_fallback(value, 0, 0);  // No indentation, we handle it ourselves
                break;
            }
        }
    }
    
    // Get the resulting JSON string
    std::string str() const { return oss_.str(); }
    
private:
    enum class ContainerType { Object, Array };
    
    // Local helper functions to avoid circular includes
    std::string escape_json_string_local(const std::string& str) {
        // Build into a std::string instead of constructing a fresh std::ostringstream
        // (heavy: stringbuf + locale imbue) for every key and every string value in the
        // document. Also avoids the std::hex/setw/setfill stream-state churn for control
        // characters. Fast path: a run with no escapes is bulk-appended.
        std::string out;
        out.reserve(str.size() + 2);
        static const char hexd[] = "0123456789abcdef";
        size_t runStart = 0;
        for (size_t i = 0; i < str.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            const char* esc = nullptr;
            char escbuf[7];
            switch (c) {
                case '"':  esc = "\\\""; break;
                case '\\': esc = "\\\\"; break;
                case '\b': esc = "\\b";  break;
                case '\f': esc = "\\f";  break;
                case '\n': esc = "\\n";  break;
                case '\r': esc = "\\r";  break;
                case '\t': esc = "\\t";  break;
                default:
                    if (c < 0x20) {
                        // Control characters below 0x20 (UTF-8 >= 0x80 passes through
                        // unescaped, which is valid JSON per RFC 8259).
                        escbuf[0] = '\\'; escbuf[1] = 'u'; escbuf[2] = '0'; escbuf[3] = '0';
                        escbuf[4] = hexd[(c >> 4) & 0xF]; escbuf[5] = hexd[c & 0xF]; escbuf[6] = '\0';
                        esc = escbuf;
                    }
                    break;
            }
            if (esc) {
                if (i > runStart) out.append(str, runStart, i - runStart);
                out += esc;
                runStart = i + 1;
            }
        }
        if (runStart < str.size()) out.append(str, runStart, str.size() - runStart);
        return out;
    }
    
    std::string to_json_impl_fallback(const script_value& val, int indent, int current_depth) {
        // Simple fallback for non-shared_ptr types
        std::ostringstream oss;
        
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
                    // Shortest exact round-trip representation (setprecision(15) loses
                    // the trailing bits of a double, which has up to 17 sig digits).
                    char buf[64];
                    auto res = std::to_chars(buf, buf + sizeof(buf), d);
                    std::string_view sv(buf, static_cast<size_t>(res.ptr - buf));
                    oss << sv;
                    if (sv.find('.') == std::string_view::npos &&
                        sv.find('e') == std::string_view::npos &&
                        sv.find('E') == std::string_view::npos) {
                        oss << ".0";
                    }
                } else {
                    oss << "null";
                }
                break;
            }
                
            case script_value_type::jai_string_type:
                oss << '"' << escape_json_string_local(val.as_string()) << '"';
                break;
                
            case script_value_type::jai_char_type:
                oss << '"' << escape_json_string_local(std::string(1, val.as_char())) << '"';
                break;
                
            case script_value_type::jai_array_type: {
                const auto& arr = val.as_array();
                oss << '[';
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss << ',';
                    oss << to_json_impl_fallback(arr[i], indent, current_depth + 1);
                }
                oss << ']';
                break;
            }
            
            case script_value_type::jai_map_type: {
                const auto& map = val.as_map();
                oss << '{';
                bool first = true;
                for (const auto& [k, v] : map) {
                    if (!first) oss << ',';
                    first = false;
                    
                    if (k.is_string()) {
                        oss << '"' << escape_json_string_local(k.as_string()) << '"';
                    } else {
                        oss << '"' << escape_json_string_local(k.to_string()) << '"';
                    }
                    
                    oss << ':';
                    oss << to_json_impl_fallback(v, indent, current_depth + 1);
                }
                oss << '}';
                break;
            }
            
            default:
                oss << "null";
                break;
        }
        
        return oss.str();
    }
    
    int indent_;
    int json_depth_;  // JSON nesting depth (separate from serialization depth tracking in base)
    std::ostringstream oss_;
    std::stack<ContainerType> container_stack_;
    std::stack<bool> first_in_container_;
    // Context tracking is now handled by base class (SerializationContext)

    bool in_container() const { return !container_stack_.empty(); }

    void write_indent() {
        if (indent_ > 0) {
            oss_ << std::string(json_depth_ * indent_, ' ');
        }
    }
    
    void write_newline() {
        if (indent_ > 0) {
            oss_ << '\n';
        }
    }
    
    template<typename T>
    void write_json_value(T value) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            // Only add comma in array context for non-first elements
            oss_ << ',';
            write_newline();
            write_indent();
        }

        if constexpr (std::is_same_v<T, bool>) {
            oss_ << (value ? "true" : "false");
        } else if constexpr (std::is_floating_point_v<T>) {
            // Handle special float values
            if (std::isnan(value)) {
                oss_ << "null";
            } else if (std::isinf(value)) {
                oss_ << (value > 0 ? "1e999" : "-1e999");
            } else {
                // Shortest representation that round-trips exactly (default ostream
                // precision is only 6 significant digits, which silently loses bits).
                char buf[64];
                auto res = std::to_chars(buf, buf + sizeof(buf), value);
                std::string_view sv(buf, static_cast<size_t>(res.ptr - buf));
                oss_ << sv;
                // Keep it a JSON "float" on round-trip (e.g. 3 -> 3.0, not int 3).
                if (sv.find('.') == std::string_view::npos &&
                    sv.find('e') == std::string_view::npos &&
                    sv.find('E') == std::string_view::npos) {
                    oss_ << ".0";
                }
            }
        } else {
            oss_ << value;
        }

        if (in_container() && container_stack_.top() == ContainerType::Array) {
            first_in_container_.top() = false;
        }
    }
};

// JSON archive reader (CRTP, no virtual dispatch)
class json_archive_reader : public archive_reader_impl<json_archive_reader> {
public:
    // Engine is REQUIRED for JSON reading since we need to create script_values
    json_archive_reader(const std::string& json_string, engine* eng)
        : archive_reader_impl<json_archive_reader>(eng), json_(json_string), pos_(0) {
        if (!eng) {
            throw serialization_error("json_archive_reader requires a valid engine reference");
        }
        root_value_ = parse_json();
        current_value_ = &root_value_;
        path_stack_.push_back("");
    }

    ~json_archive_reader() = default;

    // JSON format properties (compile-time constants, no virtual dispatch)
    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

    // Basic type deserialization - non-virtual
    int8_t read_int8() { return static_cast<int8_t>(read_value().as<script_int>()); }
    int16_t read_int16() { return static_cast<int16_t>(read_value().as<script_int>()); }
    int32_t read_int32() { return static_cast<int32_t>(read_value().as<script_int>()); }
    int64_t read_int64() { return read_value().as<script_int>(); }
    uint8_t read_uint8() { return static_cast<uint8_t>(read_value().as<script_int>()); }
    uint16_t read_uint16() { return static_cast<uint16_t>(read_value().as<script_int>()); }
    uint32_t read_uint32() { return static_cast<uint32_t>(read_value().as<script_int>()); }
    uint64_t read_uint64() { return static_cast<uint64_t>(read_value().as<script_int>()); }
    float read_float32() { return static_cast<float>(read_value().as<script_float>()); }
    double read_float64() { return read_value().as<script_float>(); }
    bool read_bool() { return read_value().as<script_bool>(); }
    std::string read_string() { return read_value().as<script_string>(); }

    bool peek_null() {
        const script_value* val = next_value(/*peek_only=*/true);
        return val && val->is_null();
    }

    void read_null() {
        auto val = read_value();
        if (!val.is_null()) {
            throw serialization_error("Expected null value");
        }
    }

    std::vector<uint8_t> read_binary(size_t /*size*/) {
        std::string hex_str = read_value().as<script_string>();
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::vector<uint8_t> result;
        result.reserve(hex_str.length() / 2);
        // Decode complete byte-pairs only. std::stoi on a 2-char substr threw on bad hex
        // and silently mis-parsed an odd trailing nibble; this validates and skips it.
        for (size_t i = 0; i + 1 < hex_str.length(); i += 2) {
            int hi = nibble(hex_str[i]);
            int lo = nibble(hex_str[i + 1]);
            if (hi < 0 || lo < 0) {
                throw serialization_error("Invalid hex digit in binary JSON field");
            }
            result.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return result;
    }

    // Object/array structure - non-virtual
    // No-argument version for inline objects (discard type metadata)
    bool begin_object() {
        std::string type_name;
        uint32_t version = 0;
        return begin_object(type_name, version);
    }

    bool begin_object(std::string& type_name, uint32_t& version) {
        const script_value* value_to_check = next_value();
        if (!value_to_check || !value_to_check->is_map()) {
            return false;
        }

        const auto& map = value_to_check->as_map();

        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        auto type_it = map.find(script_value(script_value::ast_literal_tag{}, std::string("_type_")));
        if (type_it != map.end() && type_it->second.is_string()) {
            type_name = type_it->second.as_string();
        } else {
            type_name = "";
        }

        auto version_it = map.find(script_value(script_value::ast_literal_tag{}, std::string("_version_")));
        if (version_it != map.end() && version_it->second.is_int()) {
            version = static_cast<uint32_t>(version_it->second.as_int());
        } else {
            version = 1;
        }

        version_ = version;
        object_stack_.push(ObjectState(map, map.begin()));
        return true;
    }

    void end_object() {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    size_t begin_array() {
        const script_value* value_to_check = next_value();
        if (!value_to_check || !value_to_check->is_array()) {
            return 0;
        }

        const auto& arr = value_to_check->as_array();
        array_stack_.push(ArrayState{arr, 0});
        return arr.size();
    }

    void end_array() {
        if (!array_stack_.empty()) {
            array_stack_.pop();
        }
    }

    bool read_property_name(std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }

        auto& obj_state = object_stack_.top();

        // Skip metadata fields
        while (obj_state.current != obj_state.map.end()) {
            const std::string& prop_name = obj_state.current->first.as_string();
            if (prop_name != "_type_" && prop_name != "_version_") {
                name = prop_name;
                current_property_value_ = &obj_state.current->second;
                cpv_depth_ = object_stack_.size();
                ++obj_state.current;
                return true;
            }
            ++obj_state.current;
        }

        return false;
    }

    // Map deserialization - JSON reads from native object format
    size_t begin_map() {
        const script_value* value_to_check = next_value();
        if (!value_to_check || !value_to_check->is_map()) {
            return 0;
        }

        const auto& map = value_to_check->as_map();
        object_stack_.push(ObjectState(map, map.begin()));
        return map.size();
    }

    void end_map() {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    bool read_map_key(std::string& key) {
        if (object_stack_.empty()) {
            return false;
        }

        auto& obj_state = object_stack_.top();

        if (obj_state.current != obj_state.map.end()) {
            key = obj_state.current->first.as_string();
            current_property_value_ = &obj_state.current->second;
            cpv_depth_ = object_stack_.size();
            ++obj_state.current;
            return true;
        }

        return false;
    }

    void clear_property_value() { current_property_value_ = nullptr; }
    bool has_current_property_value() const { return current_property_value_ != nullptr; }
    bool in_array() const { return !array_stack_.empty(); }

    bool has_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }
        return object_stack_.top().index.count(name) > 0;
    }

    // Seek to a specific property by name
    // Fast path: if properties are in sorted order, sequential access works
    // Slow path: fall back to map lookup for out-of-order access
    bool seek_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }

        auto& obj_state = object_stack_.top();

        // Fast path: O(1) lookup via the pre-built string index.
        // The old path called map.find(script_value(name, eng)) which allocated
        // and interned a script_value key on every property access — O(log N) with
        // heavy constant cost. The index uses plain std::string keys: O(1) average.
        auto it = obj_state.index.find(name);
        if (it == obj_state.index.end()) {
            return false;
        }

        current_property_value_ = it->second;
        cpv_depth_ = object_stack_.size();
        return true;
    }

    script_value read_value() {
        depth_guard guard(current_depth_);

        const script_value* val = next_value();
        if (!val) val = current_value_;
        script_value result = val ? *val : script_value();

        // Check for shared_ptr reconstruction
        if (result.is_map()) {
            const auto& map = result.as_map();

            // Get engine reference for creating script_values
            auto eng = engine_ref_;
            if (!eng) {
                throw serialization_error("Engine reference expired during JSON deserialization");
            }

            // Check for weak_ptr serialization format: {"$weak_ptr_id": id, "$weak_ptr_data": <optional>}
            auto weak_id_it = map.find(script_value("$weak_ptr_id", eng));
            if (weak_id_it != map.end() && weak_id_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(weak_id_it->second.as<script_int>());

                if (id == 0) {
                    // Null or expired weak_ptr
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }

                // Check if we've already reconstructed this object
                script_value existing_obj = get_shared_ptr(id);

                if (!existing_obj.is_invalid()) {
                    // Object was already reconstructed, get its object_holder and create weak_ptr
                    auto obj_holder = existing_obj.get_object_holder();
                    if (obj_holder) {
                        script_value weak_val(std::monostate{}, eng);
                        weak_val.set_type_info(eng->get_type_info_weak_ptr(existing_obj.get_type_info()));
                        weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                        return weak_val;
                    }
                }

                // First time seeing this ID - check for inline data
                auto weak_data_it = map.find(script_value("$weak_ptr_data", eng));
                if (weak_data_it != map.end()) {
                    script_value obj_val = weak_data_it->second;  // Already properly wrapped

                    // Register the object for future references
                    register_shared_ptr(id, obj_val);

                    // Get the object_holder from the deserialized object
                    auto obj_holder = obj_val.get_object_holder();
                    if (!obj_holder) {
                        // Object doesn't have an object_holder (shouldn't happen for valid objects)
                        throw std::runtime_error("Deserialized weak_ptr object has no object_holder");
                    }

                    // Create weak_ptr to the object_holder
                    script_value weak_val(std::monostate{}, eng);
                    weak_val.set_type_info(eng->get_type_info_weak_ptr(obj_val.get_type_info()));
                    weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                    return weak_val;
                } else {
                    // No data found - return null weak_ptr (object was never serialized)
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }
            }
        }
        
        return result;
    }

private:
    std::string json_;
    size_t pos_;
    script_value root_value_;
    const script_value* current_value_;
    const script_value* current_property_value_ = nullptr;
    size_t cpv_depth_ = 0;
    script_value temp_value_;
    std::vector<std::string> path_stack_;

    // Central value resolution — every value-consuming method delegates here.
    // cpv is only valid at the depth where it was set; stale cpv is discarded.
    // peek_only=true examines without consuming (for peek_null).
    const script_value* next_value(bool peek_only = false) {
        bool cpv_valid = current_property_value_ && (cpv_depth_ == object_stack_.size());

        if (cpv_valid) {
            const script_value* val = current_property_value_;
            if (!peek_only) current_property_value_ = nullptr;
            return val;
        }

        current_property_value_ = nullptr;

        if (!array_stack_.empty()) {
            auto& arr = array_stack_.top();
            if (arr.index < arr.array.size()) {
                if (peek_only) return &arr.array[arr.index];
                temp_value_ = arr.array[arr.index++];
                return &temp_value_;
            }
            return nullptr;
        }

        return current_value_;
    }

    // JSON parsing methods
    script_value parse_json() {
        pos_ = 0;
        script_value result = parse_value();
        skip_whitespace();
        if (pos_ != json_.length()) {
            // The root value parsed but bytes remain — malformed input (two top-level
            // values, truncated-then-garbage, etc.). Surface it instead of silently
            // accepting a prefix.
            throw serialization_error(
                "Unexpected trailing characters after JSON value at position " + std::to_string(pos_));
        }
        return result;
    }
    
    script_value parse_value() {
        // Bound recursion: nested arrays/objects ([[[...]]] or {"a":{"a":...}})
        // recurse per nesting level, and without a limit deeply-nested (e.g.
        // adversarial) input overflows the native stack — an uncatchable crash.
        depth_guard guard(current_depth_);  // also enforces JAI_MAX_SERIALIZATION_DEPTH
        if (current_depth_ > JAI_MAX_JSON_PARSE_DEPTH) {
            throw serialization_error(
                "Maximum JSON nesting depth (" + std::to_string(JAI_MAX_JSON_PARSE_DEPTH) + ") exceeded");
        }

        skip_whitespace();

        if (pos_ >= json_.length()) {
            throw serialization_error("Unexpected end of JSON");
        }

        // Get engine reference for creating script_values
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        char c = json_[pos_];

        if (c == '"') {
            return script_value(parse_string(), eng);
        } else if (c == '{') {
            return parse_object(eng);
        } else if (c == '[') {
            return parse_array(eng);
        } else if (c == 't' || c == 'f') {
            return parse_bool(eng);
        } else if (c == 'n') {
            return parse_null(eng);
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(eng);
        } else {
            // Build context for error message
            size_t start = (pos_ > 20) ? pos_ - 20 : 0;
            size_t len = std::min(size_t(40), json_.length() - start);
            std::string context = json_.substr(start, len);
            throw serialization_error("Unexpected character '" + std::string(1, c) +
                "' in JSON at position " + std::to_string(pos_) +
                ", near: ..." + context + "...");
        }
    }
    
    void skip_whitespace() {
        // JSON whitespace is exactly these four bytes (RFC 8259). Comparing directly
        // avoids std::isspace, which is a locale-aware function call AND is undefined
        // behavior when handed a raw (sign-extended) char >= 0x80.
        while (pos_ < json_.length()) {
            char c = json_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos_++;
            else break;
        }
    }
    
    char peek() {
        skip_whitespace();
        return pos_ < json_.length() ? json_[pos_] : '\0';
    }
    
    char advance() {
        skip_whitespace();
        return pos_ < json_.length() ? json_[pos_++] : '\0';
    }
    
    void expect(char c) {
        if (advance() != c) {
            throw serialization_error("Expected '" + std::string(1, c) + "' in JSON");
        }
    }
    
    // Appends the UTF-8 encoding of a Unicode code point.
    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // Reads the 4 hex digits at json_[pos_+1 .. pos_+4] and advances pos_ by 4 (to the
    // last digit; the caller's trailing pos_++ steps past it). Returns false on missing or
    // non-hex input — replaces std::stoul, which threw std::invalid_argument (outside the
    // serialization_error hierarchy) on malformed \u escapes.
    bool read_hex4(uint32_t& out) {
        if (pos_ + 4 >= json_.length()) return false;
        uint32_t v = 0;
        for (size_t k = 1; k <= 4; ++k) {
            char c = json_[pos_ + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        pos_ += 4;
        out = v;
        return true;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        const size_t n = json_.length();

        while (pos_ < n) {
            // Bulk-copy the run of ordinary characters up to the next quote/backslash
            // rather than appending one char at a time.
            const size_t runStart = pos_;
            while (pos_ < n) {
                char c = json_[pos_];
                if (c == '"' || c == '\\') break;
                pos_++;
            }
            if (pos_ > runStart) result.append(json_, runStart, pos_ - runStart);

            if (pos_ >= n) break;                 // unterminated -> expect('"') below throws
            if (json_[pos_] == '"') { pos_++; return result; }

            // Escape sequence.
            pos_++;                               // consume backslash
            if (pos_ >= n) throw serialization_error("Unexpected end of JSON string");
            switch (json_[pos_]) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!read_hex4(cp)) throw serialization_error("Invalid \\u escape in JSON string");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: consume a following \uXXXX low surrogate if valid.
                        if (pos_ + 2 < n && json_[pos_ + 1] == '\\' && json_[pos_ + 2] == 'u') {
                            const size_t save = pos_;
                            pos_ += 2;                                 // move onto the low 'u'
                            uint32_t low;
                            if (read_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                pos_ = save;                           // not a valid pair
                                cp = 0xFFFD;                           // U+FFFD replacement
                            }
                        } else {
                            cp = 0xFFFD;                               // lone high surrogate
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;                                   // lone low surrogate
                    }
                    append_utf8(result, cp);
                    break;
                }
                default:
                    throw serialization_error("Invalid escape sequence");
            }
            pos_++;                               // step past the escape's final char
        }

        expect('"');                              // unterminated string -> throws
        return result;
    }
    
    script_value parse_object(engine* eng) {
        expect('{');

        script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
        auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());

        if (peek() == '}') {
            advance();
            return map_val;
        }

        while (true) {
            std::string key = parse_string();
            expect(':');
            script_value value = parse_value();

            // Use ast_literal_tag: creates a string script_value WITHOUT interning
            // into the symbolizer. JSON keys are only used for DOM lookup (via the
            // unordered_map index in ObjectState) and string content comparison in
            // std::map<> — both work correctly without interning. This eliminates
            // O(N) symbolizer hash-map operations, the dominant parse cost.
            map.emplace(script_value(script_value::ast_literal_tag{}, key), std::move(value));

            char c = peek();
            if (c == '}') {
                advance();
                break;
            } else if (c == ',') {
                advance();
            } else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or '}' in JSON object at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }

        return map_val;
    }

    script_value parse_array(engine* eng) {
        expect('[');

        script_value array_val = script_value::make_array(nullptr, eng);
        auto& arr = const_cast<std::vector<script_value>&>(array_val.as_array());

        if (peek() == ']') {
            advance();
            return array_val;
        }

        while (true) {
            arr.push_back(parse_value());

            char c = peek();
            if (c == ']') {
                advance();
                break;
            } else if (c == ',') {
                advance();
            } else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or ']' in JSON array at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }

        return array_val;
    }

    script_value parse_bool(engine* eng) {
        // compare(pos, len, lit) clamps len to size()-pos, so a truncated tail never
        // matches and we avoid allocating a substring per literal.
        if (json_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return script_value(true, eng);
        } else if (json_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return script_value(false, eng);
        } else {
            throw serialization_error("Invalid boolean value");
        }
    }

    script_value parse_null(engine* eng) {
        if (json_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return script_value(std::monostate{}, eng);
        } else {
            throw serialization_error("Invalid null value");
        }
    }

    script_value parse_number(engine* eng) {
        const size_t start = pos_;
        bool has_decimal = false;
        bool has_exponent = false;

        if (pos_ < json_.length() && json_[pos_] == '-') pos_++;

        while (pos_ < json_.length()) {
            char c = json_[pos_];
            if (c >= '0' && c <= '9') { pos_++; }
            else if (c == '.') { has_decimal = true; pos_++; }
            else if (c == 'e' || c == 'E') { has_exponent = true; pos_++; }
            else if (c == '+' || c == '-') { pos_++; }   // exponent sign
            else break;
        }

        // std::from_chars: no exceptions (the old std::stod/std::stoll could throw
        // std::out_of_range / std::invalid_argument — which derive from std::logic_error,
        // a sibling of serialization_error's hierarchy, so callers catching
        // serialization_error would miss them and a single bad number crashed loadJai),
        // no temporary substring, locale-independent, and faster.
        const char* first = json_.data() + start;
        const char* last = json_.data() + pos_;

        if (has_decimal || has_exponent) {
            double d = 0.0;
            auto r = std::from_chars(first, last, d);
            // result_out_of_range still yields a usable (clamped/inf) magnitude, which lets
            // the writer's "1e999" infinity sentinel round-trip back to infinity rather
            // than throwing. Any OTHER error, or not consuming the whole token, is malformed.
            if (r.ptr != last || (r.ec != std::errc() && r.ec != std::errc::result_out_of_range)) {
                throw serialization_error(
                    "Invalid numeric literal in JSON at position " + std::to_string(start));
            }
            return script_value(d, eng);
        }

        script_int iv = 0;
        auto r = std::from_chars(first, last, iv);
        if (r.ptr == last && r.ec == std::errc()) {
            return script_value(iv, eng);
        }
        // Integer that overflows int64 (e.g. a huge id/hash) — degrade to double rather
        // than throw, matching lenient JSON consumers. (from_chars leaves iv untouched on
        // failure, so re-parse the same token as a double.)
        double d = 0.0;
        auto r2 = std::from_chars(first, last, d);
        if (r2.ptr != last || (r2.ec != std::errc() && r2.ec != std::errc::result_out_of_range)) {
            throw serialization_error(
                "Invalid integer literal in JSON at position " + std::to_string(start));
        }
        return script_value(d, eng);
    }
    
    struct ObjectState {
        const std::map<script_value, script_value>& map;
        std::map<script_value, script_value>::const_iterator current;
        // O(1) property lookup index built once when the object is opened.
        // Keys are plain std::string — no engine, no script_value comparison.
        std::unordered_map<std::string, const script_value*> index;

        ObjectState(const std::map<script_value, script_value>& m,
                    std::map<script_value, script_value>::const_iterator it)
            : map(m), current(it) {
            index.reserve(m.size());
            for (const auto& [k, v] : m) {
                if (k.is_string()) {
                    index.emplace(k.unchecked_as_string(), &v);
                }
            }
        }
    };
    
    struct ArrayState {
        const std::vector<script_value>& array;
        size_t index;
    };
    
    std::stack<ObjectState> object_stack_;
    std::stack<ArrayState> array_stack_;
};

// Archive ID trait specializations for dispatch pattern
template<> struct writer_archive_id_trait<json_archive_writer> {
    static constexpr writer_archive_id value = writer_archive_id::json;
};
template<> struct reader_archive_id_trait<json_archive_reader> {
    static constexpr reader_archive_id value = reader_archive_id::json;
};

} // namespace serialization
} // namespace jai
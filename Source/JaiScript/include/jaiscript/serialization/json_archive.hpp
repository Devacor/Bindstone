#pragma once

#include "archive_impl.hpp"
#include <jaiscript/core/class_definition.hpp>
#include <stack>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <charconv>     // std::to_chars for shortest round-trippable float output
#include <string_view>
#include <limits>
#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <cerrno>       // strtod fallback: libc++ ships integer-only std::from_chars
#include <clocale>
#include <cstdlib>
#endif

// Maximum nesting depth for JSON TEXT parsing (from_json). Each level recurses a
// couple of native stack frames, so this is intentionally well below
// JAI_MAX_SERIALIZATION_DEPTH (used for in-memory traversal) to guarantee the
// guard fires before deeply-nested / adversarial input exhausts the call stack.
#ifndef JAI_MAX_JSON_PARSE_DEPTH
#define JAI_MAX_JSON_PARSE_DEPTH 128
#endif

namespace jai {
namespace serialization {

class json_archive_writer : public archive_writer_impl<json_archive_writer> {
public:
    json_archive_writer(int indent = 2) : archive_writer_impl<json_archive_writer>(), indent_(indent), json_depth_(0) {}
    json_archive_writer(int indent, engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(indent), json_depth_(0) {}
    explicit json_archive_writer(engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(2), json_depth_(0) {}

    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

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

    void begin_object() {
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
                            // (slot order: deterministic, ≈ declaration order — the old map
                            // walk was hash-order)
                            instance->for_each_field([&](uint64_t field_id, const script_value& field_val) {
                                // Skip the __cpp_object__ field - it's not serializable
                                std::string field_name = std::string(eng->get_symbolizer()->get_string(field_id));
                                if (field_name == "__cpp_object__") return;

                                if (!first) oss_ << ',';
                                first = false;
                                oss_ << "\"" << escape_json_string_local(field_name) << "\":";
                                write_value(field_val);
                            });
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
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
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

class json_archive_reader : public archive_reader_impl<json_archive_reader> {
public:
    // Engine is REQUIRED for JSON reading since we need to create script_values
    json_archive_reader(const std::string& json_string, engine* eng)
        : archive_reader_impl<json_archive_reader>(eng) {
        if (!eng) {
            throw serialization_error("json_archive_reader requires a valid engine reference");
        }
        json_ = json_string;
        parse_into_arena();   // builds the flat arena DOM (nodes_/chars_); root_ is the last node
        json_.clear();        // source no longer needed — the flat DOM is self-contained
        json_.shrink_to_fit();
    }

    ~json_archive_reader() = default;

    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

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
        uint32_t n;
        if (!next_node(n, /*peek_only=*/true)) return false;
        return nodes_[n].tag == JTag::Null;
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
        // Decode complete byte-pairs only; validates each nibble and skips an odd trailing nibble.
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

    bool begin_object() {
        std::string type_name;
        uint32_t version = 0;
        return begin_object(type_name, version);
    }

    bool begin_object(std::string& type_name, uint32_t& version) {
        uint32_t n;
        if (!next_node(n) || nodes_[n].tag != JTag::Obj) {
            return false;
        }
        const JNode& obj = nodes_[n];
        const uint32_t first = obj.u.agg.first;
        const uint32_t count = obj.u.agg.count;
        type_name.clear();
        version = 1;
        // _type_/_version_ are read directly off the flat members — no script_value keys.
        for (uint32_t m = 0; m < count; ++m) {
            const JNode& k = nodes_[first + 2 * m];
            std::string_view kv(chars_.data() + k.u.str.off, k.u.str.len);
            const JNode& v = nodes_[first + 2 * m + 1];
            if (kv == "_type_" && v.tag == JTag::Str) {
                type_name.assign(chars_.data() + v.u.str.off, v.u.str.len);
            } else if (kv == "_version_" && v.tag == JTag::Int) {
                version = static_cast<uint32_t>(v.u.i);
            }
        }
        version_ = version;
        object_stack_.push(ObjState{ first, count, 0, nullptr });
        return true;
    }

    void end_object() {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    size_t begin_array() {
        uint32_t n;
        if (!next_node(n) || nodes_[n].tag != JTag::Arr) {
            return 0;
        }
        const JNode& a = nodes_[n];
        array_stack_.push(ArrState{ a.u.agg.first, a.u.agg.count, 0 });
        return a.u.agg.count;
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
        auto& s = object_stack_.top();
        // Sequential cursor over the flat members; skip the _type_/_version_ metadata.
        while (s.cursor < s.count) {
            const JNode& k = nodes_[s.first + 2 * s.cursor];
            std::string_view kv(chars_.data() + k.u.str.off, k.u.str.len);
            const uint32_t valNode = s.first + 2 * s.cursor + 1;
            ++s.cursor;
            if (kv != "_type_" && kv != "_version_") {
                name.assign(kv);
                cpv_node_ = valNode;
                cpv_valid_ = true;
                cpv_depth_ = object_stack_.size();
                return true;
            }
        }
        return false;
    }

    // Map deserialization - JSON reads from native object format
    size_t begin_map() {
        uint32_t n;
        if (!next_node(n) || nodes_[n].tag != JTag::Obj) {
            return 0;
        }
        const JNode& o = nodes_[n];
        object_stack_.push(ObjState{ o.u.agg.first, o.u.agg.count, 0, nullptr });
        return o.u.agg.count;
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
        auto& s = object_stack_.top();
        if (s.cursor < s.count) {
            const JNode& k = nodes_[s.first + 2 * s.cursor];
            key.assign(chars_.data() + k.u.str.off, k.u.str.len);
            cpv_node_ = s.first + 2 * s.cursor + 1;
            cpv_valid_ = true;
            cpv_depth_ = object_stack_.size();
            ++s.cursor;
            return true;
        }
        return false;
    }

    void clear_property_value() { cpv_valid_ = false; }
    bool has_current_property_value() const { return cpv_valid_; }
    bool in_array() const { return !array_stack_.empty(); }

    bool has_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }
        return object_index(object_stack_.top()).count(std::string_view(name)) > 0;
    }

    // Seek to a property by name. Builds a lazy name->value-node index on first random
    // access (the common sequential read path never pays for it); keys are string_views
    // into the char arena, so lookups never construct a script_value.
    bool seek_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }
        auto& s = object_stack_.top();
        auto& idx = object_index(s);
        auto it = idx.find(std::string_view(name));
        if (it == idx.end()) {
            return false;
        }
        cpv_node_ = it->second;
        cpv_valid_ = true;
        cpv_depth_ = object_stack_.size();
        return true;
    }

    script_value read_value() {
        depth_guard guard(current_depth_);
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        uint32_t n;
        if (!next_node(n)) n = root_;   // matches the old "fall back to the root value"
        const JNode& node = nodes_[n];

        if (node.tag == JTag::Obj) {
            // Detect the SCRIPT weak_ptr format {"$weak_ptr_id": id, "$weak_ptr_data"?: ...}
            // by scanning the flat members directly — no script_value is materialized unless
            // this genuinely is a weak_ptr (then only its $weak_ptr_data subtree) or unless a
            // raw composite is requested (the final materialize(n) below, e.g. stdlib from_json).
            uint32_t widNode = 0, wdataNode = 0;
            bool hasWid = false, hasWdata = false;
            const uint32_t first = node.u.agg.first, count = node.u.agg.count;
            for (uint32_t m = 0; m < count; ++m) {
                const JNode& k = nodes_[first + 2 * m];
                std::string_view kv(chars_.data() + k.u.str.off, k.u.str.len);
                if (kv == "$weak_ptr_id") { widNode = first + 2 * m + 1; hasWid = true; }
                else if (kv == "$weak_ptr_data") { wdataNode = first + 2 * m + 1; hasWdata = true; }
            }
            if (hasWid && nodes_[widNode].tag == JTag::Int) {
                uint32_t id = static_cast<uint32_t>(nodes_[widNode].u.i);
                if (id == 0) {
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }
                script_value existing_obj = get_shared_ptr(id);
                if (!existing_obj.is_invalid()) {
                    auto obj_holder = existing_obj.get_object_holder();
                    if (obj_holder) {
                        script_value weak_val(std::monostate{}, eng);
                        weak_val.set_type_info(eng->get_type_info_weak_ptr(existing_obj.get_type_info()));
                        weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                        return weak_val;
                    }
                }
                if (hasWdata) {
                    script_value obj_val = materialize(wdataNode);
                    register_shared_ptr(id, obj_val);
                    auto obj_holder = obj_val.get_object_holder();
                    if (!obj_holder) {
                        throw std::runtime_error("Deserialized weak_ptr object has no object_holder");
                    }
                    script_value weak_val(std::monostate{}, eng);
                    weak_val.set_type_info(eng->get_type_info_weak_ptr(obj_val.get_type_info()));
                    weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                    return weak_val;
                } else {
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }
            }
            return materialize(n);
        }
        if (node.tag == JTag::Arr) {
            return materialize(n);
        }
        return materialize_primitive(node, eng);
    }

private:
    // ---- Flat arena DOM ----------------------------------------------------------------
    // Objects/arrays are 16-byte nodes in one contiguous arena; string bytes live in one
    // char arena. Composites become script_values only on demand (read_value/materialize).
    enum class JTag : uint8_t { Null, Bool, Int, Double, Str, Arr, Obj };
    struct JNode {
        JTag tag = JTag::Null;
        union UVal {
            int64_t  i;                              // Int, Bool (0/1)
            double   d;                              // Double
            struct { uint32_t off, len; } str;       // Str: [off,len) into chars_
            struct { uint32_t first, count; } agg;   // Arr: count elems; Obj: count members ([k,v]*)
        } u{};
    };

    std::string json_;        // parse source; cleared once the arena is built
    size_t pos_ = 0;          // parse cursor
    std::vector<JNode> nodes_;   // value arena (root_ is the last element)
    std::string chars_;          // unescaped string-byte arena
    uint32_t root_ = 0;
    std::vector<JNode> scratch_; // parser stack (children bulk-moved into nodes_ on container close)

    uint32_t cpv_node_ = 0;   // current property value: a flat node index
    bool cpv_valid_ = false;
    size_t cpv_depth_ = 0;

    // Resolves the next value to consume as a flat node index. cpv is only valid at the
    // object depth where it was set; arrays advance an element cursor. Returns false only
    // when an open array is exhausted (callers either skip or fall back to the root).
    bool next_node(uint32_t& out, bool peek_only = false) {
        if (cpv_valid_ && cpv_depth_ == object_stack_.size()) {
            out = cpv_node_;
            if (!peek_only) cpv_valid_ = false;
            return true;
        }
        cpv_valid_ = false;
        if (!array_stack_.empty()) {
            auto& a = array_stack_.top();
            if (a.index < a.count) {
                out = a.first + (peek_only ? a.index : a.index++);
                return true;
            }
            return false;
        }
        out = root_;
        return true;
    }

    // Builds an engine-bound script_value for a primitive flat node (the hot path; no recursion).
    script_value materialize_primitive(const JNode& node, engine* eng) {
        switch (node.tag) {
            case JTag::Null:   return script_value(std::monostate{}, eng);
            case JTag::Bool:   return script_value(static_cast<script_bool>(node.u.i != 0), eng);
            case JTag::Int:    return script_value(static_cast<script_int>(node.u.i), eng);
            case JTag::Double: return script_value(node.u.d, eng);
            case JTag::Str:    return script_value(std::string(chars_.data() + node.u.str.off, node.u.str.len), eng);
            default:           return script_value(std::monostate{}, eng);
        }
    }

    // Recursively builds a full script_value subtree from a flat node — only used when a raw
    // composite is actually requested (stdlib from_json / a weak_ptr's $weak_ptr_data).
    // Keys are ENGINE-CARRYING string values: ordering and map.find(script_value("_type_"/...))
    // rank by type-then-content (type_info never participates), and every value in the result
    // must clone/detach — the old ast_literal_tag keys had no engine pointer, so copying any
    // deserialized map died with "Cannot clone script_value: missing engine pointer".
    script_value materialize(uint32_t idx) {
        depth_guard guard(current_depth_);
        engine* eng = engine_ref_;
        const JNode& node = nodes_[idx];
        if (node.tag == JTag::Obj) {
            script_value mv = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
            auto& map = const_cast<script_map&>(mv.as_map());
            for (uint32_t m = 0; m < node.u.agg.count; ++m) {
                const JNode& k = nodes_[node.u.agg.first + 2 * m];
                map.emplace(script_value(script_string(chars_.data() + k.u.str.off, k.u.str.len), eng),
                            materialize(node.u.agg.first + 2 * m + 1));
            }
            return mv;
        }
        if (node.tag == JTag::Arr) {
            script_value av = script_value::make_array(nullptr, eng);
            auto& arr = const_cast<std::vector<script_value>&>(av.as_array());
            arr.reserve(node.u.agg.count);
            for (uint32_t e = 0; e < node.u.agg.count; ++e) {
                arr.push_back(materialize(node.u.agg.first + e));
            }
            return av;
        }
        return materialize_primitive(node, eng);
    }

    // ---- Flat JSON parser (builds nodes_/chars_; no script_value DOM) ----
    void parse_into_arena() {
        pos_ = 0;
        nodes_.clear();
        chars_.clear();
        scratch_.clear();
        nodes_.reserve(json_.size() / 6 + 16);   // ~1 value / 6 bytes for minified JSON
        chars_.reserve(json_.size() / 2 + 16);
        parse_value_flat();
        skip_whitespace();
        if (pos_ != json_.length()) {
            // Trailing bytes after the root value -> malformed (two top-level values, etc.).
            throw serialization_error(
                "Unexpected trailing characters after JSON value at position " + std::to_string(pos_));
        }
        if (scratch_.size() != 1) {
            throw serialization_error("Invalid JSON document");
        }
        nodes_.push_back(scratch_[0]);   // the root value becomes the last arena element
        root_ = static_cast<uint32_t>(nodes_.size() - 1);
        scratch_.clear();
    }

    // Bulk-moves the [mark, end) children off the scratch stack into the arena and pushes one
    // container node referencing them. Objects store members interleaved [key,value,...].
    void push_container(JTag tag, size_t mark) {
        const uint32_t first = static_cast<uint32_t>(nodes_.size());
        const size_t cnt = scratch_.size() - mark;
        nodes_.insert(nodes_.end(), scratch_.begin() + mark, scratch_.end());
        scratch_.resize(mark);
        JNode v;
        v.tag = tag;
        v.u.agg.first = first;
        v.u.agg.count = (tag == JTag::Obj) ? static_cast<uint32_t>(cnt / 2) : static_cast<uint32_t>(cnt);
        scratch_.push_back(v);
    }

    void parse_value_flat() {
        // Bound recursion so adversarial deep nesting can't overflow the native stack.
        depth_guard guard(current_depth_);
        if (current_depth_ > JAI_MAX_JSON_PARSE_DEPTH) {
            throw serialization_error(
                "Maximum JSON nesting depth (" + std::to_string(JAI_MAX_JSON_PARSE_DEPTH) + ") exceeded");
        }
        skip_whitespace();
        if (pos_ >= json_.length()) {
            throw serialization_error("Unexpected end of JSON");
        }
        char c = json_[pos_];
        if (c == '"') {
            JNode v; v.tag = JTag::Str;
            parse_string_into(v.u.str.off, v.u.str.len);
            scratch_.push_back(v);
        } else if (c == '{') {
            parse_object_flat();
        } else if (c == '[') {
            parse_array_flat();
        } else if (c == 't' || c == 'f') {
            parse_bool_flat();
        } else if (c == 'n') {
            parse_null_flat();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            parse_number_flat();
        } else {
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
    // non-hex input (keeps errors in the serialization_error hierarchy on malformed \u escapes).
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

    // Parses a string body into the char arena, returning its [off,len).
    void parse_string_into(uint32_t& off, uint32_t& len) {
        expect('"');
        const uint32_t start = static_cast<uint32_t>(chars_.size());
        const size_t n = json_.length();

        while (pos_ < n) {
            const size_t runStart = pos_;
            while (pos_ < n) {
                char c = json_[pos_];
                if (c == '"' || c == '\\') break;
                pos_++;
            }
            if (pos_ > runStart) chars_.append(json_, runStart, pos_ - runStart);

            if (pos_ >= n) break;                 // unterminated -> expect('"') below throws
            if (json_[pos_] == '"') {
                pos_++;
                off = start;
                len = static_cast<uint32_t>(chars_.size() - start);
                return;
            }

            pos_++;                               // consume backslash
            if (pos_ >= n) throw serialization_error("Unexpected end of JSON string");
            switch (json_[pos_]) {
                case '"':  chars_.push_back('"');  break;
                case '\\': chars_.push_back('\\'); break;
                case '/':  chars_.push_back('/');  break;
                case 'b':  chars_.push_back('\b'); break;
                case 'f':  chars_.push_back('\f'); break;
                case 'n':  chars_.push_back('\n'); break;
                case 'r':  chars_.push_back('\r'); break;
                case 't':  chars_.push_back('\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!read_hex4(cp)) throw serialization_error("Invalid \\u escape in JSON string");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 < n && json_[pos_ + 1] == '\\' && json_[pos_ + 2] == 'u') {
                            const size_t save = pos_;
                            pos_ += 2;
                            uint32_t low;
                            if (read_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                pos_ = save;
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    append_utf8(chars_, cp);
                    break;
                }
                default:
                    throw serialization_error("Invalid escape sequence");
            }
            pos_++;                               // step past the escape's final char
        }

        expect('"');                              // unterminated string -> throws
    }

    void parse_object_flat() {
        expect('{');
        const size_t mark = scratch_.size();
        if (peek() == '}') { advance(); push_container(JTag::Obj, mark); return; }
        while (true) {
            // key (parse_string_into's expect('"') skips any leading whitespace)
            JNode k; k.tag = JTag::Str;
            parse_string_into(k.u.str.off, k.u.str.len);
            scratch_.push_back(k);
            expect(':');
            parse_value_flat();   // pushes the value node
            char c = peek();
            if (c == ',') { advance(); }
            else if (c == '}') { advance(); break; }
            else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or '}' in JSON object at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }
        push_container(JTag::Obj, mark);
    }

    void parse_array_flat() {
        expect('[');
        const size_t mark = scratch_.size();
        if (peek() == ']') { advance(); push_container(JTag::Arr, mark); return; }
        while (true) {
            parse_value_flat();   // pushes the element node
            char c = peek();
            if (c == ',') { advance(); }
            else if (c == ']') { advance(); break; }
            else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or ']' in JSON array at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }
        push_container(JTag::Arr, mark);
    }

    void parse_bool_flat() {
        JNode v; v.tag = JTag::Bool;
        if (json_.compare(pos_, 4, "true") == 0) { pos_ += 4; v.u.i = 1; }
        else if (json_.compare(pos_, 5, "false") == 0) { pos_ += 5; v.u.i = 0; }
        else throw serialization_error("Invalid boolean value");
        scratch_.push_back(v);
    }

    void parse_null_flat() {
        if (json_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            JNode v; v.tag = JTag::Null; v.u.i = 0;
            scratch_.push_back(v);
        } else {
            throw serialization_error("Invalid null value");
        }
    }

    // libc++ still ships integer-only std::from_chars; the floating-point overloads are
    // absent, and the incomplete implementation deliberately leaves __cpp_lib_to_chars
    // undefined — exactly the feature-test to key on. The fallback preserves the two
    // from_chars behaviors parse_number_flat relies on (full-consumption ptr,
    // errc::result_out_of_range): the caller pre-scans a strict JSON number charset, so
    // strtod never sees whitespace/hex/inf/nan forms, ERANGE maps to result_out_of_range,
    // and '.' is swapped for the host locale's decimal point so a comma-decimal locale
    // cannot misparse.
    struct double_parse_result { const char* ptr; std::errc ec; };
    static double_parse_result from_chars_double(const char* first, const char* last, double& d) {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        auto r = std::from_chars(first, last, d);
        return { r.ptr, r.ec };
#else
        std::string text(first, last);
        const char locale_decimal = std::localeconv()->decimal_point[0];
        if (locale_decimal != '.') {
            for (auto& c : text) { if (c == '.') c = locale_decimal; }
        }
        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end == text.c_str()) { return { first, std::errc::invalid_argument }; }
        d = parsed;
        return { first + (end - text.c_str()), errno == ERANGE ? std::errc::result_out_of_range : std::errc() };
#endif
    }

    void parse_number_flat() {
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
        const char* first = json_.data() + start;
        const char* last = json_.data() + pos_;
        // from_chars leaves the out-of-range result implementation-defined (MS STL: +-inf,
        // libstdc++: untouched). Pin the writer's 1e999 infinity sentinel on every platform:
        // a negative exponent underflows to signed zero, anything else overflows to signed inf.
        auto out_of_range_double = [](const char* f, const char* l) {
            bool negative = (f != l && *f == '-');
            bool negative_exponent = false;
            for (const char* p = f; p != l; ++p) {
                if (*p == 'e' || *p == 'E') { negative_exponent = (p + 1 != l && *(p + 1) == '-'); break; }
            }
            double magnitude = negative_exponent ? 0.0 : std::numeric_limits<double>::infinity();
            return negative ? -magnitude : magnitude;
        };
        JNode v;
        if (has_decimal || has_exponent) {
            double d = 0.0;
            auto r = from_chars_double(first, last, d);
            if (r.ptr != last || (r.ec != std::errc() && r.ec != std::errc::result_out_of_range)) {
                throw serialization_error("Invalid numeric literal in JSON at position " + std::to_string(start));
            }
            if (r.ec == std::errc::result_out_of_range) { d = out_of_range_double(first, last); }
            v.tag = JTag::Double; v.u.d = d;
        } else {
            script_int iv = 0;
            auto r = std::from_chars(first, last, iv);
            if (r.ptr == last && r.ec == std::errc()) {
                v.tag = JTag::Int; v.u.i = iv;
            } else {
                double d = 0.0;
                auto r2 = from_chars_double(first, last, d);
                if (r2.ptr != last || (r2.ec != std::errc() && r2.ec != std::errc::result_out_of_range)) {
                    throw serialization_error("Invalid integer literal in JSON at position " + std::to_string(start));
                }
                if (r2.ec == std::errc::result_out_of_range) { d = out_of_range_double(first, last); }
                v.tag = JTag::Double; v.u.d = d;
            }
        }
        scratch_.push_back(v);
    }

    // Open-object reader state over the flat arena.
    struct ObjState {
        uint32_t first;    // index of the first member node (a key) in nodes_
        uint32_t count;    // member count
        uint32_t cursor;   // sequential read position, 0..count
        // Lazy name -> value-node index, built on first random (seek/has) access. Keys are
        // string_views into chars_ (stable for the reader's lifetime).
        std::unique_ptr<std::unordered_map<std::string_view, uint32_t>> index;
    };

    struct ArrState {
        uint32_t first;
        uint32_t count;
        uint32_t index;
    };

    std::unordered_map<std::string_view, uint32_t>& object_index(ObjState& s) {
        if (!s.index) {
            s.index = std::make_unique<std::unordered_map<std::string_view, uint32_t>>();
            s.index->reserve(s.count);
            for (uint32_t m = 0; m < s.count; ++m) {
                const JNode& k = nodes_[s.first + 2 * m];
                s.index->emplace(std::string_view(chars_.data() + k.u.str.off, k.u.str.len),
                                 s.first + 2 * m + 1);
            }
        }
        return *s.index;
    }

    std::stack<ObjState> object_stack_;
    std::stack<ArrState> array_stack_;
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
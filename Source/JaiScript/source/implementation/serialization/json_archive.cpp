#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/detail/class_runtime.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/math_values.hpp>
#include <charconv>     // std::to_chars for shortest round-trippable float output
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <jaiscript/detail/float_text.hpp>   // strtod fallback: libc++ ships integer-only std::from_chars
#include <clocale>
#include <cstdlib>
#endif

namespace jai::serialization {

json_archive_writer::json_archive_writer(int indent) :
    archive_writer_impl<json_archive_writer>(), indent_(indent), json_depth_(0), stream_(std::make_unique<std::ostringstream>()) {}

json_archive_writer::json_archive_writer(int indent, engine* eng) :
    archive_writer_impl<json_archive_writer>(eng), indent_(indent), json_depth_(0), stream_(std::make_unique<std::ostringstream>()) {}

json_archive_writer::json_archive_writer(engine* eng) :
    archive_writer_impl<json_archive_writer>(eng), indent_(2), json_depth_(0), stream_(std::make_unique<std::ostringstream>()) {}

json_archive_writer::~json_archive_writer() = default;

json_archive_reader::~json_archive_reader() = default;

template<typename T>
void json_archive_writer::write_json_value(T value) {
    auto ctx = this->current_context();
    bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

    if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
        (*stream_) << ',';
        write_newline();
        write_indent();
    }

    if constexpr (std::is_same_v<T, bool>) {
        (*stream_) << (value ? "true" : "false");
    } else if constexpr (std::is_floating_point_v<T>) {
        // Handle special float values
        if (std::isnan(value)) {
            (*stream_) << "null";
        } else if (std::isinf(value)) {
            (*stream_) << (value > 0 ? "1e999" : "-1e999");
        } else {
            // Shortest representation that round-trips exactly (default ostream
            // precision is only 6 significant digits, which silently loses bits).
            char buf[64];
            auto res = std::to_chars(buf, buf + sizeof(buf), value);
            std::string_view sv(buf, static_cast<size_t>(res.ptr - buf));
            (*stream_) << sv;
            // Keep it a JSON "float" on round-trip (e.g. 3 -> 3.0, not int 3).
            if (sv.find('.') == std::string_view::npos &&
                sv.find('e') == std::string_view::npos &&
                sv.find('E') == std::string_view::npos) {
                (*stream_) << ".0";
            }
        }
    } else {
        (*stream_) << value;
    }

    if (in_container() && container_stack_.top() == ContainerType::Array) {
        first_in_container_.top() = false;
    }
}

void json_archive_writer::write_int8(int8_t value) { write_json_value(value); }

void json_archive_writer::write_int16(int16_t value) { write_json_value(value); }

void json_archive_writer::write_int32(int32_t value) { write_json_value(value); }

void json_archive_writer::write_int64(int64_t value) { write_json_value(value); }

void json_archive_writer::write_uint8(uint8_t value) { write_json_value(value); }

void json_archive_writer::write_uint16(uint16_t value) { write_json_value(value); }

void json_archive_writer::write_uint32(uint32_t value) { write_json_value(value); }

void json_archive_writer::write_uint64(uint64_t value) { write_json_value(value); }

void json_archive_writer::write_float32(float value) { write_json_value(value); }

void json_archive_writer::write_float64(double value) { write_json_value(value); }

void json_archive_writer::write_bool(bool value) { write_json_value(value); }

void json_archive_writer::write_null() { write_json_value("null"); }

void json_archive_writer::write_string(const std::string& value) {
    write_json_value('"' + escape_json_string_local(value) + '"');
}

void json_archive_writer::write_binary(const void* data, size_t size) {
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

void json_archive_writer::begin_object() {
    enter_host_record(open_records_);
    ++open_records_;
    auto ctx = this->current_context();
    bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

    if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '{';
    json_depth_++;
    container_stack_.push(ContainerType::Object);
    first_in_container_.push(true);
}

void json_archive_writer::begin_object(const std::string& type_name, uint32_t version) {
    enter_host_record(open_records_);
    ++open_records_;
    auto ctx = this->current_context();
    bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

    if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '{';
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

void json_archive_writer::end_object() {
    --open_records_;
    json_depth_--;
    container_stack_.pop();
    first_in_container_.pop();
    write_newline();
    write_indent();
    (*stream_) << '}';
    if (in_container()) {
        first_in_container_.top() = false;
    }
}

void json_archive_writer::begin_array(size_t size) {
    auto ctx = this->current_context();
    bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

    if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '[';
    json_depth_++;
    container_stack_.push(ContainerType::Array);
    first_in_container_.push(true);
}

void json_archive_writer::end_array() {
    json_depth_--;
    container_stack_.pop();
    first_in_container_.pop();
    write_newline();
    write_indent();
    (*stream_) << ']';
    if (in_container()) {
        first_in_container_.top() = false;
    }
}

void json_archive_writer::write_property_name(const std::string& name) {
    if (!first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '"' << escape_json_string_local(name) << "\": ";
    first_in_container_.top() = false;
    // Context is managed by base class serialize() which pushes PropertyValue after this
}

void json_archive_writer::begin_map(size_t size) {
    auto ctx = this->current_context();
    bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

    if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '{';
    json_depth_++;
    container_stack_.push(ContainerType::Object);
    first_in_container_.push(true);
}

void json_archive_writer::end_map() {
    json_depth_--;
    container_stack_.pop();
    first_in_container_.pop();
    write_newline();
    write_indent();
    (*stream_) << '}';
    if (in_container()) {
        first_in_container_.top() = false;
    }
}

void json_archive_writer::write_map_key(const std::string& key) {
    // Same as write_property_name
    if (!first_in_container_.top()) {
        (*stream_) << ',';
    }
    write_newline();
    write_indent();
    (*stream_) << '"' << escape_json_string_local(key) << "\": ";
    first_in_container_.top() = false;
    // Context is managed by base class which pushes MapValue after this
}

void json_archive_writer::write_value(const script_value& root) {
    std::vector<write_frame> frames;
    const int depth_at_entry = json_depth_;
    begin_value(root, frames);
    script_value scratch(std::monostate{}, engine_ref_);
    while (!frames.empty()) {
        const script_value* child = next_child(frames.back(), scratch);
        if (aborted_) [[unlikely]] {
            json_depth_ = depth_at_entry;   // a getter threw into the backend: the in-flight exception leaves the archive
            aborted_ = false;
            return;
        }
        if (child) {
            begin_value(*child, frames);
        } else {
            end_frame(frames.back());
            frames.pop_back();
        }
    }
}

std::string json_archive_writer::str() const { return stream_->str(); }

void json_archive_writer::begin_value(const script_value& given, std::vector<write_frame>& frames) {
    const script_value& value = given.is_reference() ? given.deref() : given;
    switch (value.type()) {
        case script_value_type::jai_weak_ptr_type: {
            // a weak_ptr writes its id; its object rides along the first time the archive meets it
            auto weak = value.get_weak_ptr();
            auto shared = weak.lock();
            if (indent_ > 0) {
                open_value('{');
                auto [id, is_new] = shared ? track_shared_ptr(shared.get()) : std::pair<uint32_t, bool>{0, false};
                write_value_key("$weak_ptr_id", false);
                (*stream_) << id;
                if (is_new) {
                    write_value_key("$weak_ptr_data", true);
                    frames.emplace_back(write_shape::payload, weak_target_value(shared));
                    return;
                }
                close_value('}', false);
            } else if (!shared) {
                (*stream_) << "{\"$weak_ptr_id\": 0}";
            } else {
                auto [id, is_new] = track_shared_ptr(shared.get());
                (*stream_) << "{\"$weak_ptr_id\": " << id;
                if (is_new) {
                    (*stream_) << ", \"$weak_ptr_data\": ";
                    write_frame& f = frames.emplace_back(write_shape::payload, weak_target_value(shared));
                    f.compact_weak = true;
                    return;
                }
                (*stream_) << "}";
            }
            return;
        }

        case script_value_type::jai_shared_ptr_type: {
            const auto holder = value.get_object_holder();
            if (!holder) {
                (*stream_) << "null";
                return;
            }
            auto [id, is_new] = track_shared_ptr(holder.get());
            open_value('{');
            write_value_key("$shared_ptr_id", false);
            (*stream_) << id;
            if (is_new) {
                write_value_key("$shared_ptr_data", true);
                frames.emplace_back(write_shape::payload, weak_target_value(holder));
                return;
            }
            close_value('}', false);
            return;
        }

        case script_value_type::jai_array_type: {
            const script_array* node = value.unchecked_array_node();
            if (!node) { throw runtime_error("script_value is not an array"); }
            open_value('[');
            write_frame& f = frames.emplace_back(write_shape::array, value);
            f.count = node->size();
            return;
        }

        case script_value_type::jai_map_type: {
            const auto& map = value.as_map();
            if (map_needs_tag(value)) {
                open_value('{');
                write_value_key("$map", false);
                open_value('[');
                write_frame& f = frames.emplace_back(write_shape::tagged_map, value);
                f.next = map.begin();
                return;
            }
            open_value('{');
            write_frame& f = frames.emplace_back(write_shape::map, value);
            f.next = map.begin();
            return;
        }

        case script_value_type::jai_object_type: {
            auto type_info = value.get_type_info();
            std::string type_name = type_info ? type_info->type_name : "unknown";

            auto eng = engine_ref_;
            if (!eng) {
                (*stream_) << "null";
                return;
            }

            std::shared_ptr<class_instance> instance;
            try {
                instance = value.as<std::shared_ptr<class_instance>>();
            } catch (...) {
                // not a class_instance: the fallback below
            }
            if (instance) {
                auto class_def = instance->get_class_definition();

                // For script classes, type_info->type_name may be "any" - use the actual class name
                if (class_def) {
                    type_name = class_def->get_name();
                }

                open_value('{');
                write_value_key("_type_", false);
                (*stream_) << '"' << escape_json_string_local(type_name) << '"';

                if (class_def) {
                    write_frame& f = frames.emplace_back(write_shape::instance, value);
                    f.names = class_def->get_property_names();
                    f.class_def = class_def;
                    f.instance = instance;
                } else {
                    // No class definition - serialize fields directly (slot order: deterministic, ≈ declaration order)
                    write_frame& f = frames.emplace_back(write_shape::fields, value);
                    instance->for_each_field([&](uint64_t field_id, const script_value& field_val) {
                        f.fields.emplace_back(std::string(eng->get_symbolizer()->get_string(field_id)), field_val);
                    });
                }
                return;
            }

            // Fallback for other object types
            (*stream_) << to_json_impl_fallback(value);
            return;
        }

        default:
            (*stream_) << to_json_impl_fallback(value);
            return;
    }
}

const script_value* json_archive_writer::next_child(write_frame& f, script_value& scratch) {
    switch (f.shape) {
        case write_shape::payload:
            if (f.index++ > 0) { return nullptr; }
            scratch = f.source;
            return &scratch;
        case write_shape::array: {
            if (f.index >= f.count) { return nullptr; }
            write_value_separator(f.index > 0);
            const script_array* node = f.source.unchecked_array_node();
            const size_t i = f.index++;
            if (node->kind() == script_array::kind_t::hetero) { return &node->values()[i]; }
            scratch = node->get(i, f.source.get_engine());
            return &scratch;
        }
        case write_shape::map: {
            if (f.next == f.source.as_map().end()) { return nullptr; }
            const script_value& k = f.next->first;
            if (k.is_string()) {
                write_value_key(k.as_string(), !f.first);
            } else {
                write_value_key(k.to_string(), !f.first);
            }
            f.first = false;
            const script_value* child = &f.next->second;
            ++f.next;
            return child;
        }
        case write_shape::tagged_map:
            for (;;) {
                if (f.phase == 2) {
                    close_value(']', false);
                    ++f.next;
                    f.phase = 0;
                }
                if (f.phase == 0) {
                    if (f.next == f.source.as_map().end()) { return nullptr; }
                    write_value_separator(!f.first);
                    f.first = false;
                    open_value('[');
                    write_value_separator(false);
                    f.phase = 1;
                    return &f.next->first;
                }
                write_value_separator(true);
                f.phase = 2;
                return &f.next->second;
            }
        case write_shape::instance: {
            if (!next_serialized_property(f.source, f.instance.get(), f.class_def, engine_ref_, f.names, f.index, scratch, aborted_)) { return nullptr; }
            write_value_key(f.names[f.index - 1], true);
            return &scratch;
        }
        case write_shape::fields:
            if (f.index >= f.fields.size()) { return nullptr; }
            write_value_key(f.fields[f.index].first, true);
            return &f.fields[f.index++].second;
    }
    return nullptr;
}

void json_archive_writer::end_frame(write_frame& f) {
    switch (f.shape) {
        case write_shape::payload:
            if (f.compact_weak) { (*stream_) << "}"; } else { close_value('}', false); }
            break;
        case write_shape::array:
            close_value(']', f.count == 0);
            break;
        case write_shape::map:
            close_value('}', f.first);
            break;
        case write_shape::tagged_map:
            close_value(']', f.first);
            close_value('}', false);
            break;
        case write_shape::instance:
        case write_shape::fields:
            close_value('}', false);
            break;
    }
}

void json_archive_writer::open_value(char bracket) {
    (*stream_) << bracket;
    ++json_depth_;
}

void json_archive_writer::close_value(char bracket, bool empty) {
    --json_depth_;
    if (indent_ > 0 && !empty) {
        (*stream_) << '\n' << indentation();
    }
    (*stream_) << bracket;
}

void json_archive_writer::write_value_separator(bool comma) {
    if (comma) (*stream_) << ',';
    if (indent_ > 0) {
        (*stream_) << '\n' << indentation();
    }
}

std::string json_archive_writer::indentation() {
    const size_t width = detail::saturating_text_size(static_cast<size_t>(json_depth_), static_cast<size_t>(indent_));
    if (charges_indentation_ && engine_ref_ && !engine_ref_->execution_limits().memory_charge_items(1, width)) [[unlikely]] {
        detail::throw_memory_cap(engine_ref_->execution_limits());
    }
    return std::string(width, ' ');
}

void json_archive_writer::write_value_key(std::string_view key, bool comma) {
    write_value_separator(comma);
    (*stream_) << '"' << escape_json_string_local(key) << (indent_ > 0 ? "\": " : "\":");
}

std::string json_archive_writer::escape_json_string_local(std::string_view str) {
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

bool json_archive_writer::map_needs_tag(const script_value& value) {
    const auto& map = value.as_map();
    auto string_like = [](script_value_type t) { return t == script_value_type::jai_string_type || t == script_value_type::jai_utf8_string_type; };
    for (const auto& [k, v] : map) {
        if (!string_like(k.type())) return true;
    }
    if (!map.empty()) return false;
    const auto ti = value.get_type_info();
    const auto kt = ti ? ti->key_type() : nullptr;
    return kt && !string_like(kt->base_type) && kt->base_type != script_value_type::jai_any_type &&
           kt->base_type != script_value_type::jai_null_type;
}

std::string json_archive_writer::to_json_impl_fallback(const script_value& val) {
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

        case script_value_type::jai_utf8_string_type:
            oss << '"' << escape_json_string_local(val.deref().unchecked_as_utf8().bytes) << '"';
            break;

        case script_value_type::jai_vec2_type:
        case script_value_type::jai_vec3_type:
        case script_value_type::jai_vec4_type:
        case script_value_type::jai_color_type:
        case script_value_type::jai_ivec2_type:
        case script_value_type::jai_ivec3_type: {
            // {"$type": "math::K", "$val": {canonical components}}: binary32 in the shortest round-tripping text
            const script_value& m = val.deref();
            const script_value_type kind = m.unchecked_vec_kind();
            std::string body = std::string("{\"$type\":\"") + detail::math::kind_name(kind) + "\",\"$val\":{";
            for (size_t k = 0; k < detail::math::dims(kind); ++k) {
                if (k) body += ',';
                body += '"'; body += detail::math::component_name(kind, k); body += "\":";
                if (detail::math::integral(kind)) {
                    body += std::to_string(detail::math::i32_at(m, k));
                } else {
                    const float c = detail::math::f32_at(m, k);
                    if (std::isfinite(c)) { detail::math::append_f32(body, c); } else { body += "null"; }
                }
            }
            body += "}}";
            oss << body;
            break;
        }

        case script_value_type::jai_char_type:
            oss << "{\"$char\":" << static_cast<int>(static_cast<unsigned char>(val.as_char())) << '}';
            break;
            
        default:
            oss << "null";
            break;
    }
    
    return oss.str();
}

bool json_archive_writer::in_container() const { return !container_stack_.empty(); }

void json_archive_writer::write_indent() {
    if (indent_ > 0) {
        (*stream_) << indentation();
    }
}

void json_archive_writer::write_newline() {
    if (indent_ > 0) {
        (*stream_) << '\n';
    }
}
json_archive_reader::json_archive_reader(const std::string& json_string, engine* eng) : archive_reader_impl<json_archive_reader>(eng) {
    if (!eng) {
        throw serialization_error("json_archive_reader requires a valid engine reference");
    }
    json_ = json_string;
    parse_into_arena();   // builds the flat arena DOM (nodes_/chars_); root_ is the last node
    json_.clear();        // source no longer needed — the flat DOM is self-contained
    json_.shrink_to_fit();
}

int8_t json_archive_reader::read_int8() { return static_cast<int8_t>(read_value().as<script_int>()); }

int16_t json_archive_reader::read_int16() { return static_cast<int16_t>(read_value().as<script_int>()); }

int32_t json_archive_reader::read_int32() { return static_cast<int32_t>(read_value().as<script_int>()); }

int64_t json_archive_reader::read_int64() { return read_value().as<script_int>(); }

uint8_t json_archive_reader::read_uint8() { return static_cast<uint8_t>(read_value().as<script_int>()); }

uint16_t json_archive_reader::read_uint16() { return static_cast<uint16_t>(read_value().as<script_int>()); }

uint32_t json_archive_reader::read_uint32() { return static_cast<uint32_t>(read_value().as<script_int>()); }

uint64_t json_archive_reader::read_uint64() { return static_cast<uint64_t>(read_value().as<script_int>()); }

float json_archive_reader::read_float32() { return static_cast<float>(read_value().as<script_float>()); }

double json_archive_reader::read_float64() { return read_value().as<script_float>(); }

bool json_archive_reader::read_bool() { return read_value().as<script_bool>(); }

std::string json_archive_reader::read_string() { return read_value().as<script_string>(); }

bool json_archive_reader::peek_null() {
    uint32_t n;
    if (!next_node(n, /*peek_only=*/true)) return false;
    return nodes_[n].tag == JTag::Null;
}

void json_archive_reader::read_null() {
    auto val = read_value();
    if (!val.is_null()) {
        throw serialization_error("Expected null value");
    }
}

std::vector<uint8_t> json_archive_reader::read_binary(size_t /*size*/) {
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

bool json_archive_reader::begin_object() {
    std::string type_name;
    uint32_t version = 0;
    return begin_object(type_name, version);
}

bool json_archive_reader::begin_object(std::string& type_name, uint32_t& version) {
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
    enter_host_record(object_stack_.size());
    object_stack_.push(ObjState{ first, count, 0, nullptr });
    return true;
}

void json_archive_reader::end_object() {
    if (!object_stack_.empty()) {
        object_stack_.pop();
    }
}

size_t json_archive_reader::begin_array() {
    uint32_t n;
    if (!next_node(n) || nodes_[n].tag != JTag::Arr) {
        return 0;
    }
    const JNode& a = nodes_[n];
    array_stack_.push(ArrState{ a.u.agg.first, a.u.agg.count, 0 });
    return a.u.agg.count;
}

void json_archive_reader::end_array() {
    if (!array_stack_.empty()) {
        array_stack_.pop();
    }
}

bool json_archive_reader::read_property_name(std::string& name) {
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

size_t json_archive_reader::begin_map() {
    uint32_t n;
    if (!next_node(n) || nodes_[n].tag != JTag::Obj) {
        return 0;
    }
    const JNode& o = nodes_[n];
    object_stack_.push(ObjState{ o.u.agg.first, o.u.agg.count, 0, nullptr });
    return o.u.agg.count;
}

void json_archive_reader::end_map() {
    if (!object_stack_.empty()) {
        object_stack_.pop();
    }
}

bool json_archive_reader::read_map_key(std::string& key) {
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

bool json_archive_reader::in_array() const { return !array_stack_.empty(); }

bool json_archive_reader::has_property(const std::string& name) {
    if (object_stack_.empty()) {
        return false;
    }
    return object_index(object_stack_.top()).count(std::string_view(name)) > 0;
}

bool json_archive_reader::seek_property(const std::string& name) {
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

script_value json_archive_reader::read_value() {
    auto eng = engine_ref_;
    if (!eng) {
        throw serialization_error("Engine reference expired during JSON deserialization");
    }

    uint32_t n;
    if (!next_node(n)) n = root_;   // matches the old "fall back to the root value"
    const JNode& node = nodes_[n];

    if (node.tag == JTag::Obj || node.tag == JTag::Arr) {
        return materialize(n);
    }
    return materialize_primitive(node, eng);
}

bool json_archive_reader::next_node(uint32_t& out, bool peek_only) {
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

script_value json_archive_reader::materialize_primitive(const JNode& node, engine* eng) {
    switch (node.tag) {
        case JTag::Null:   return script_value(std::monostate{}, eng);
        case JTag::Bool:   return script_value(static_cast<script_bool>(node.u.i != 0), eng);
        case JTag::Int:    return script_value(static_cast<script_int>(node.u.i), eng);
        case JTag::Double: return script_value(node.u.d, eng);
        case JTag::Str:    return script_value(std::string(chars_.data() + node.u.str.off, node.u.str.len), eng);
        default:           return script_value(std::monostate{}, eng);
    }
}

bool json_archive_reader::materialize_math(const JNode& node, engine* eng, script_value& out) {
    if (node.u.agg.count != 2) { return false; }
    uint32_t type_node = 0, val_node = 0;
    bool has_type = false, has_val = false;
    for (uint32_t m = 0; m < 2; ++m) {
        const JNode& k = nodes_[node.u.agg.first + 2 * m];
        std::string_view key(chars_.data() + k.u.str.off, k.u.str.len);
        if (key == "$type") { type_node = node.u.agg.first + 2 * m + 1; has_type = true; }
        else if (key == "$val") { val_node = node.u.agg.first + 2 * m + 1; has_val = true; }
    }
    if (!has_type || !has_val || nodes_[type_node].tag != JTag::Str) { return false; }
    const JNode& t = nodes_[type_node];
    const script_value_type kind = math_type_from_name(std::string_view(chars_.data() + t.u.str.off, t.u.str.len));
    if (kind == script_value_type::jai_null_type) { return false; }
    const JNode& val = nodes_[val_node];
    const size_t dims = detail::math::dims(kind);
    if (val.tag != JTag::Obj || val.u.agg.count != dims) {
        throw serialization_error(std::string("Malformed ") + math_type_name(kind) + " in JSON: expected " + std::to_string(dims) + " components");
    }
    float f[4] = {0, 0, 0, 0};
    int32_t i[4] = {0, 0, 0, 0};
    bool seen[4] = {false, false, false, false};
    for (uint32_t m = 0; m < val.u.agg.count; ++m) {
        const JNode& k = nodes_[val.u.agg.first + 2 * m];
        const JNode& c = nodes_[val.u.agg.first + 2 * m + 1];
        const int comp = detail::math::component_of(kind, std::string_view(chars_.data() + k.u.str.off, k.u.str.len));
        if (comp < 0 || seen[comp] || (c.tag != JTag::Int && c.tag != JTag::Double)) {
            throw serialization_error(std::string("Malformed ") + math_type_name(kind) + " component in JSON");
        }
        seen[comp] = true;
        if (detail::math::integral(kind)) {
            auto narrowed = c.tag == JTag::Int ? detail::math::to_i32(static_cast<script_int>(c.u.i)) : detail::math::to_i32(c.u.d);
            if (!narrowed) { throw serialization_error(std::string("Out-of-range ") + math_type_name(kind) + " component in JSON"); }
            i[comp] = narrowed.value();
        } else {
            f[comp] = c.tag == JTag::Int ? detail::math::to_f32(static_cast<script_int>(c.u.i)) : detail::math::to_f32(c.u.d);
        }
    }
    out = detail::math::make(kind, f, i, eng);
    return true;
}

script_value json_archive_reader::materialize(uint32_t idx) {
    engine* eng = engine_ref_;
    struct frame { uint32_t node; uint32_t next; script_value container; };
    std::vector<frame> frames;
    // a leaf's value, or nothing when n opened a container frame
    auto open = [&](uint32_t n, script_value& leaf) -> bool {
        const JNode& node = nodes_[n];
        if (node.tag == JTag::Obj) {
            script_value math_value(std::monostate{}, eng);
            if (materialize_math(node, eng, math_value)) {
                leaf = std::move(math_value);
                return true;
            }
            frames.push_back({n, 0, script_value::make_map(eng->get_type_info_string(), nullptr, eng)});
            return false;
        }
        if (node.tag == JTag::Arr) {
            script_value av = script_value::make_array(nullptr, eng);
            const_cast<std::vector<script_value>&>(av.as_array()).reserve(node.u.agg.count);
            frames.push_back({n, 0, std::move(av)});
            return false;
        }
        leaf = materialize_primitive(node, eng);
        return true;
    };
    // the child just made goes into the container whose member index - 1 it is
    auto attach = [&](frame& parent, script_value&& child) {
        const JNode& node = nodes_[parent.node];
        if (node.tag == JTag::Obj) {
            const JNode& k = nodes_[node.u.agg.first + 2 * (parent.next - 1)];
            const_cast<script_map&>(parent.container.as_map()).emplace(script_value(script_string(chars_.data() + k.u.str.off, k.u.str.len), eng), std::move(child));
        } else {
            const_cast<std::vector<script_value>&>(parent.container.as_array()).push_back(std::move(child));
        }
    };
    script_value leaf(std::monostate{}, eng);
    if (open(idx, leaf)) { return leaf; }
    for (;;) {
        frame& top = frames.back();
        const JNode& node = nodes_[top.node];
        if (top.next < node.u.agg.count) {
            const uint32_t child = node.tag == JTag::Obj ? node.u.agg.first + 2 * top.next + 1 : node.u.agg.first + top.next;
            ++top.next;
            if (open(child, leaf)) { attach(frames.back(), std::move(leaf)); }
            continue;
        }
        script_value done = std::move(top.container);
        frames.pop_back();
        if (frames.empty()) { return done; }
        attach(frames.back(), std::move(done));
    }
}

void json_archive_reader::parse_into_arena() {
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

void json_archive_reader::push_container(JTag tag, size_t mark) {
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

void json_archive_reader::parse_value_flat() {
    struct open_container { JTag tag; size_t mark; };
    std::vector<open_container> open;
    for (;;) {
        skip_whitespace();
        if (pos_ >= json_.length()) {
            throw serialization_error("Unexpected end of JSON");
        }
        char c = json_[pos_];
        bool completed = true;
        if (c == '"') {
            JNode v; v.tag = JTag::Str;
            parse_string_into(v.u.str.off, v.u.str.len);
            scratch_.push_back(v);
        } else if (c == '{' || c == '[') {
            const JTag tag = c == '{' ? JTag::Obj : JTag::Arr;
            pos_++;
            const size_t mark = scratch_.size();
            if (peek() == (c == '{' ? '}' : ']')) {
                advance();
                push_container(tag, mark);
            } else {
                open.push_back({tag, mark});
                if (tag == JTag::Obj) { parse_member_key(); }
                completed = false;
            }
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
        if (!completed) { continue; }
        // a value finished: close every container it finished, until one wants its next member
        for (;;) {
            if (open.empty()) { return; }
            const open_container top = open.back();
            const char next = peek();
            if (next == ',') {
                advance();
                if (top.tag == JTag::Obj) { parse_member_key(); }
                break;
            }
            if (next == (top.tag == JTag::Obj ? '}' : ']')) {
                advance();
                open.pop_back();
                push_container(top.tag, top.mark);
                continue;
            }
            size_t start = (pos_ > 20) ? pos_ - 20 : 0;
            size_t len = std::min(size_t(40), json_.length() - start);
            std::string context = json_.substr(start, len);
            throw serialization_error(std::string(top.tag == JTag::Obj ? "Expected ',' or '}' in JSON object at position " : "Expected ',' or ']' in JSON array at position ") +
                std::to_string(pos_) + ", got '" + std::string(1, next) + "', near: ..." + context + "...");
        }
    }
}

void json_archive_reader::parse_member_key() {
    JNode k; k.tag = JTag::Str;
    parse_string_into(k.u.str.off, k.u.str.len);
    scratch_.push_back(k);
    expect(':');
}

void json_archive_reader::skip_whitespace() {
    // JSON whitespace is exactly these four bytes (RFC 8259). Comparing directly
    // avoids std::isspace, which is a locale-aware function call AND is undefined
    // behavior when handed a raw (sign-extended) char >= 0x80.
    while (pos_ < json_.length()) {
        char c = json_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos_++;
        else break;
    }
}

char json_archive_reader::peek() {
    skip_whitespace();
    return pos_ < json_.length() ? json_[pos_] : '\0';
}

char json_archive_reader::advance() {
    skip_whitespace();
    return pos_ < json_.length() ? json_[pos_++] : '\0';
}

void json_archive_reader::expect(char c) {
    if (advance() != c) {
        throw serialization_error("Expected '" + std::string(1, c) + "' in JSON");
    }
}

void json_archive_reader::append_utf8(std::string& out, uint32_t cp) {
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

bool json_archive_reader::read_hex4(uint32_t& out) {
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

void json_archive_reader::parse_string_into(uint32_t& off, uint32_t& len) {
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

void json_archive_reader::parse_bool_flat() {
    JNode v; v.tag = JTag::Bool;
    if (json_.compare(pos_, 4, "true") == 0) { pos_ += 4; v.u.i = 1; }
    else if (json_.compare(pos_, 5, "false") == 0) { pos_ += 5; v.u.i = 0; }
    else throw serialization_error("Invalid boolean value");
    scratch_.push_back(v);
}

void json_archive_reader::parse_null_flat() {
    if (json_.compare(pos_, 4, "null") == 0) {
        pos_ += 4;
        JNode v; v.tag = JTag::Null; v.u.i = 0;
        scratch_.push_back(v);
    } else {
        throw serialization_error("Invalid null value");
    }
}

auto json_archive_reader::from_chars_double(const char* first, const char* last, double& d) -> double_parse_result {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    auto r = std::from_chars(first, last, d);
    return { r.ptr, r.ec };
#else
    std::string text(first, last);
    const char locale_decimal = std::localeconv()->decimal_point[0];
    if (locale_decimal != '.') {
        for (auto& c : text) { if (c == '.') c = locale_decimal; }
    }
    const auto parsed = detail::parse_float_text(text.c_str());
    if (parsed.end == text.c_str()) { return { first, std::errc::invalid_argument }; }
    d = parsed.value;
    return { first + (parsed.end - text.c_str()), parsed.out_of_range ? std::errc::result_out_of_range : std::errc() };
#endif
}

void json_archive_reader::parse_number_flat() {
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

std::unordered_map<std::string_view, uint32_t>& json_archive_reader::object_index(ObjState& s) {
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

} // namespace jai::serialization

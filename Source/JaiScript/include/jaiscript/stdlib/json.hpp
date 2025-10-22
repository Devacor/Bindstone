#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace jai {
namespace stdlib {

    // JSON escape helper
    inline std::string escape_json_string(const std::string& str) {
        std::ostringstream oss;
        for (char c : str) {
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c >= 0x20 && c <= 0x7E) {
                        oss << c;
                    } else {
                        // Unicode escape for non-printable
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
                            << static_cast<int>(static_cast<unsigned char>(c));
                    }
            }
        }
        return oss.str();
    }

    // Convert script_value to JSON string
    inline std::string to_json_impl(const script_value& val, int indent, int current_depth) {
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
            
        case script_value_type::jai_array_type: {
            const auto& arr = val.as_array();
            oss << '[';
            if (pretty && !arr.empty()) oss << '\n';
            
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) {
                    oss << ',';
                    if (pretty) oss << '\n';
                }
                if (pretty) oss << next_indent;
                oss << to_json_impl(arr[i], indent, current_depth + 1);
            }
            
            if (pretty && !arr.empty()) {
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
                if (k.is_string()) {
                    oss << '"' << escape_json_string(k.as_string()) << '"';
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
                    for (const auto& propName : classDef->get_property_names()) {
                        // Call the getter method to get property value
                        auto getter = classDef->get_method("_get_" + propName);
                        if (getter.type() == script_value_type::jai_function_type) {
                            try {
                                // Call getter with instance as 'this'
                                std::vector<script_value> args = { val }; // Pass the instance itself
                                script_value propscript_value = getter.as_function()(args);
                                
                                if (!first) {
                                    oss << ',';
                                    if (pretty) oss << '\n' << next_indent;
                                }
                                first = false;
                                oss << "\"" << propName << "\": ";
                                oss << to_json_impl(propscript_value, indent, current_depth + 1);
                            } catch (...) {
                                // Skip properties that fail to serialize
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

    // Simple JSON parser for from_json
    // NOTE: This parser is deprecated in favor of json_archive_reader
    // which properly handles engine references and shared_ptr reconstruction
    /*
    class JsonParser {
    private:
        const std::string& json;
        size_t pos;
        
        void skipWhitespace() {
            while (pos < json.length() && std::isspace(json[pos])) {
                pos++;
            }
        }
        
        char peek() {
            skipWhitespace();
            if (pos >= json.length()) return '\0';
            return json[pos];
        }
        
        char advance() {
            skipWhitespace();
            if (pos >= json.length()) return '\0';
            return json[pos++];
        }
        
        void expect(char c) {
            if (advance() != c) {
                throw runtime_error("Expected '" + std::string(1, c) + "' in JSON at position " + std::to_string(pos));
            }
        }
        
        std::string parseString() {
            expect('"');
            std::string result;
            
            while (pos < json.length() && json[pos] != '"') {
                if (json[pos] == '\\') {
                    pos++;
                    if (pos >= json.length()) {
                        throw runtime_error("Unexpected end of JSON string");
                    }
                    switch (json[pos]) {
                        case '"':  result += '"'; break;
                        case '\\': result += '\\'; break;
                        case '/':  result += '/'; break;
                        case 'b':  result += '\b'; break;
                        case 'f':  result += '\f'; break;
                        case 'n':  result += '\n'; break;
                        case 'r':  result += '\r'; break;
                        case 't':  result += '\t'; break;
                        case 'u': {
                            // Unicode escape sequence
                            if (pos + 4 >= json.length()) {
                                throw runtime_error("Invalid unicode escape in JSON");
                            }
                            std::string hex = json.substr(pos + 1, 4);
                            pos += 4;
                            int codepoint = std::stoi(hex, nullptr, 16);
                            // Simple ASCII handling for now
                            if (codepoint < 128) {
                                result += static_cast<char>(codepoint);
                            } else {
                                result += '?'; // Placeholder for non-ASCII
                            }
                            break;
                        }
                        default:
                            throw runtime_error("Invalid escape sequence in JSON string");
                    }
                    pos++;
                } else {
                    result += json[pos++];
                }
            }
            
            expect('"');
            return result;
        }
        
        script_value parseNumber() {
            size_t start = pos;
            bool hasDecimal = false;
            bool hasExponent = false;
            
            if (json[pos] == '-') pos++;
            
            while (pos < json.length() && (std::isdigit(json[pos]) || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+' || json[pos] == '-')) {
                if (json[pos] == '.') hasDecimal = true;
                if (json[pos] == 'e' || json[pos] == 'E') hasExponent = true;
                pos++;
            }
            
            std::string numStr = json.substr(start, pos - start);
            
            if (hasDecimal || hasExponent) {
                return script_value(std::stod(numStr));
            } else {
                return script_value(static_cast<script_int>(std::stoll(numStr)));
            }
        }
        
        script_value parseArray() {
            expect('[');
            
            // Create an empty array value
            script_value arrayValue = script_value::make_array(nullptr); // nullptr for heterogeneous array
            auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
            
            if (peek() == ']') {
                advance();
                return arrayValue;
            }
            
            while (true) {
                array.push_back(parsescript_value());
                
                char c = peek();
                if (c == ']') {
                    advance();
                    break;
                } else if (c == ',') {
                    advance();
                } else {
                    throw runtime_error("Expected ',' or ']' in JSON array");
                }
            }
            
            return arrayValue;
        }
        
        script_value parseObject() {
            expect('{');
            
            // Create an empty map value
            script_value mapValue = script_value::make_map(type_info::make_string(), nullptr);
            auto& map = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());
            
            if (peek() == '}') {
                advance();
                return mapValue;
            }
            
            while (true) {
                // Parse key (must be string)
                std::string key = parseString();
                expect(':');
                script_value value = parsescript_value();
                
                map[script_value(key)] = value;
                
                char c = peek();
                if (c == '}') {
                    advance();
                    break;
                } else if (c == ',') {
                    advance();
                } else {
                    throw runtime_error("Expected ',' or '}' in JSON object");
                }
            }
            
            return mapValue;
        }
        
        script_value parsescript_value() {
            skipWhitespace();
            
            char c = peek();
            
            if (c == '"') {
                return script_value(parseString());
            } else if (c == '{') {
                return parseObject();
            } else if (c == '[') {
                return parseArray();
            } else if (c == 't') {
                // true
                if (json.substr(pos, 4) == "true") {
                    pos += 4;
                    return script_value(true);
                }
                throw runtime_error("Invalid JSON value");
            } else if (c == 'f') {
                // false
                if (json.substr(pos, 5) == "false") {
                    pos += 5;
                    return script_value(false);
                }
                throw runtime_error("Invalid JSON value");
            } else if (c == 'n') {
                // null
                if (json.substr(pos, 4) == "null") {
                    pos += 4;
                    return script_value();
                }
                throw runtime_error("Invalid JSON value");
            } else if (c == '-' || std::isdigit(c)) {
                return parseNumber();
            } else {
                throw runtime_error("Unexpected character in JSON: '" + std::string(1, c) + "'");
            }
        }
        
    public:
        JsonParser(const std::string& jsonStr) : json(jsonStr), pos(0) {}
        
        script_value parse() {
            script_value result = parsescript_value();
            skipWhitespace();
            if (pos < json.length()) {
                throw runtime_error("Unexpected characters after JSON value");
            }
            return result;
        }
    };
    */

    // Register JSON and binary serialization functions with an engine
    inline void register_json_functions(engine& engine) {
        auto engine_weak = engine.weak_from_this();
        
        // to_json with optional pretty printing - now with shared_ptr support
        engine.add_variadic_function("to_json", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.size() == 1) {
                // Compact mode - use archive for shared_ptr support
                serialization::json_archive_writer writer(-1); // No indentation
                writer.write_value(args[0]);
                return script_value(writer.str(), engine_weak);
            } else if (args.size() == 2) {
                // Pretty mode with indentation
                int indent = static_cast<int>(args[1].as_int());
                serialization::json_archive_writer writer(indent);
                writer.write_value(args[0]);
                return script_value(writer.str(), engine_weak);
            } else {
                throw runtime_error("to_json expects 1 or 2 arguments, got " + std::to_string(args.size()));
            }
        });
        
        // from_json implementation - now with automatic object reconstruction and shared_ptr support
        engine.add_function("from_json", [engine_weak](const std::string& json) -> script_value {
            // Use JSON archive for shared_ptr reconstruction
            serialization::json_archive_reader reader(json);
            script_value data = reader.read_value();
            
            // Helper function to automatically reconstruct objects with _type_
            std::function<script_value(const script_value&)> processValue;
            processValue = [engine_weak, &processValue](const script_value& val) -> script_value {
                if (val.is_map()) {
                    const auto& map = val.as_map();
                    
                    // Check if this map has a _type_ field
                    auto typeIt = map.find(script_value("_type_", engine_weak));
                    if (typeIt != map.end() && typeIt->second.is_string()) {
                        std::string type_name = typeIt->second.as_string();
                        
                        // Try to reconstruct the bound object
                        try {
                            // Get strong reference to engine
                            auto eng = engine_weak.lock();
                            if (!eng) {
                                throw runtime_error("Engine no longer exists");
                            }
                            
                            script_value instance(std::monostate{}, engine_weak);
                            
                            // Try to get the class definition to check for custom serialization constructor
                            bool has_custom_constructor = false;
                            try {
                                // Create a temporary instance to get the class definition
                                script_value temp_instance = eng->execute(type_name + "()");
                                auto temp_class_instance = temp_instance.as<std::shared_ptr<class_instance>>();
                                if (temp_class_instance) {
                                    auto class_def = temp_class_instance->get_class_definition();
                                    if (class_def) {
                                        script_value custom_constructor = class_def->get_method("_serialize_construct");
                                        if (custom_constructor.type() == script_value_type::jai_function_type) {
                                            // Use custom serialization constructor
                                            std::vector<script_value> args = { val };
                                            instance = custom_constructor.as_function()(args);
                                            has_custom_constructor = true;
                                        }
                                    }
                                }
                            } catch (...) {
                                // Default constructor failed, continue with fallback
                            }
                            
                            if (!has_custom_constructor) {
                                // Fallback: Create default instance and set properties
                                instance = eng->execute(type_name + "()");
                                
                                // Set all properties (except _type_)
                                for (const auto& [key, value] : map) {
                                    if (key.is_string() && key.as_string() != "_type_") {
                                        std::string propName = key.as_string();
                                        
                                        // Process nested values recursively
                                        script_value propValue = processValue(value);
                                        
                                        // Set the property using a clean temporary approach
                                        std::string tempVar = "_json_temp_" + propName;
                                        eng->add_global(tempVar, propValue);
                                        eng->add_global("_json_obj", instance);
                                        
                                        eng->execute("_json_obj." + propName + " = " + tempVar);
                                        instance = eng->get_variable("_json_obj");
                                        
                                        // Clean up
                                        eng->execute(tempVar + " = null");
                                    }
                                }
                                
                                eng->execute("_json_obj = null");
                            }
                            return instance;
                            
                        } catch (...) {
                            // If object reconstruction fails, return as processed map
                        }
                    }
                    
                    // Process map values recursively
                    script_value mapValue = script_value::make_map(type_info::make_string(), nullptr);
                    auto& resultMap = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());
                    for (const auto& [k, v] : map) {
                        resultMap[k] = processValue(v);
                    }
                    return mapValue;
                    
                } else if (val.is_array()) {
                    // Process arrays recursively
                    script_value arrayValue = script_value::make_array(nullptr);
                    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
                    
                    for (const auto& elem : val.as_array()) {
                        array.push_back(processValue(elem));
                    }
                    
                    return arrayValue;
                } else {
                    // Primitive values pass through unchanged
                    return val;
                }
            };
            
            return processValue(data);
        });
        
        // Binary serialization functions
        engine.add_variadic_function("to_binary", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("to_binary expects exactly 1 argument, got " + std::to_string(args.size()));
            }
            
            // Create binary archive and serialize
            serialization::binary_archive_writer writer;
            
            // For simple values, use write_value directly
            writer.write_value(args[0]);
            
            // Convert binary data to string for script access
            const auto& data = writer.data();
            script_string binary_str(data.begin(), data.end());
            return script_value(binary_str, engine_weak);
        });
        
        engine.add_variadic_function("from_binary", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("from_binary expects exactly 1 argument, got " + std::to_string(args.size()));
            }
            
            if (!args[0].is_string()) {
                throw runtime_error("from_binary expects a string argument");
            }
            
            script_string binary_data = args[0].as<script_string>();
            // Convert string back to binary data
            std::vector<uint8_t> data(binary_data.begin(), binary_data.end());
            
            // Create binary archive reader and deserialize
            serialization::binary_archive_reader reader(data);
            
            script_value result = reader.read_value();
            
            // For objects with _type_ field, try to reconstruct like JSON
            if (result.is_map()) {
                // Get strong reference to engine
                auto eng = engine_weak.lock();
                if (!eng) {
                    throw runtime_error("Engine no longer exists");
                }
                
                const auto& map = result.as_map();
                auto type_it = map.find(script_value("_type_", engine_weak));
                if (type_it != map.end() && type_it->second.is_string()) {
                    std::string type_name = type_it->second.as_string();
                    
                    try {
                        script_value instance = eng->execute(type_name + "()");
                        
                        // Set properties
                        for (const auto& [key, value] : map) {
                            if (key.is_string() && key.as_string() != "_type_" && key.as_string() != "_version_") {
                                std::string prop_name = key.as_string();
                                std::string temp_var = "_binary_temp_" + prop_name;
                                eng->add_global(temp_var, value);
                                eng->add_global("_binary_obj", instance);
                                
                                eng->execute("_binary_obj." + prop_name + " = " + temp_var);
                                instance = eng->get_variable("_binary_obj");
                                
                                eng->execute(temp_var + " = null");
                            }
                        }
                        
                        eng->execute("_binary_obj = null");
                        return instance;
                    } catch (...) {
                        // If object reconstruction fails, return as map
                    }
                }
            }
            
            return result;
        });
    }

} // namespace stdlib
} // namespace jai
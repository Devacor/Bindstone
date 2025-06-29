#pragma once

/**
 * JaiScript Class System - Complete Integration Header
 * 
 * This header provides the complete class system implementation for JaiScript,
 * including native script classes, C++ integration, inheritance, and method dispatch.
 * 
 * Features:
 * - Native script class declarations with constructors/destructors
 * - Single inheritance between script classes
 * - C++ to script class inheritance (hybrid classes)
 * - Centralized virtual call dispatch for consistency
 * - Constructor delegation (same-class and base-class)
 * - RAII-style automatic destructors with shared_ptr
 * - Override enforcement with mandatory 'override' keyword
 * - Dynamic virtual promotion (methods start direct, become virtual when overridden)
 * - Script-focused access control (public/private/protected)
 * - Method and field access with proper permission checking
 * 
 * Usage:
 * 
 *   // Initialize class system
 *   jaiscript::engine js;
 *   jaiscript::class_system_integration integration(js);
 *   integration.initialize();
 * 
 *   // Define classes in script
 *   js.eval(R"(
 *     class Point {
 *       float x = 0.0;
 *       float y = 0.0;
 *       
 *       Point(float px, float py) {
 *         x = px; y = py;
 *       }
 *       
 *       Point() : Point(0.0, 0.0) {}  // Constructor delegation
 *       
 *       float distance() {
 *         return sqrt(x*x + y*y);
 *       }
 *       
 *       ~Point() {
 *         print("Point destroyed");
 *       }
 *     }
 *     
 *     class ColoredPoint : Point {
 *       string color = "red";
 *       
 *       ColoredPoint(float x, float y, string c) : super(x, y) {
 *         color = c;
 *       }
 *       
 *       float distance() override {
 *         auto base_dist = super::distance();
 *         return base_dist * 1.1;  // Colored points are slightly further
 *       }
 *     }
 *   )");
 * 
 *   // Use classes
 *   js.eval(R"(
 *     auto p1 = make_shared<Point>(3.0, 4.0);
 *     auto p2 = make_shared<ColoredPoint>(1.0, 1.0, "blue");
 *     
 *     print("Distance: " + p1->distance());
 *     print("Colored distance: " + p2->distance());
 *   )");
 */

// Core class system headers
#include "jaiscript/core/script_class.hpp"
#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/core/value_extensions.hpp"
#include "jaiscript/core/class_integration.hpp"

// Parser and interpreter extensions
#include "jaiscript/detail/class_parser.hpp"
#include "jaiscript/detail/class_interpreter.hpp"

// Main JaiScript integration
#include "jaiscript/jaiscript.hpp"

namespace jai {

/**
 * Easy initialization function for the complete class system
 * 
 * @param js The JaiScript engine to add class support to
 * @return Integration object for further customization
 */
inline std::unique_ptr<class_system_integration> enable_class_system(engine& js) {
    auto integration = std::make_unique<class_system_integration>(js);
    integration->initialize();
    return integration;
}

/**
 * Macro for easy class registration in C++
 * 
 * Example:
 *   JAISCRIPT_REGISTER_CLASS(Point)
 *     .field("x", &Point::x)
 *     .field("y", &Point::y)
 *     .method("distance", &Point::distance)
 *     .allow_script_inheritance();
 */
#define JAISCRIPT_REGISTER_CLASS(ClassName) \
    jaiscript::enhanced_class_builder<ClassName>(js, #ClassName)

/**
 * Convenience macros for common class operations
 */

// Create script class instance
#define JAISCRIPT_NEW_CLASS(ClassName, ...) \
    jaiscript::create_script_instance(#ClassName, {__VA_ARGS__})

// Create shared_ptr to script class instance  
#define JAISCRIPT_MAKE_SHARED_CLASS(ClassName, ...) \
    jaiscript::make_shared_script_instance(#ClassName, {__VA_ARGS__})

// Type checking
#define JAISCRIPT_INSTANCEOF(instance, ClassName) \
    jaiscript::instanceof_check(instance, #ClassName)

#define JAISCRIPT_TYPEOF(instance) \
    jaiscript::typeof_class(instance)

// Method calling
#define JAISCRIPT_CALL_METHOD(instance, method_name, ...) \
    jaiscript::script_value_class_extensions::call_method(instance, method_name, {__VA_ARGS__})

// Field access
#define JAISCRIPT_GET_FIELD(instance, field_name) \
    jaiscript::script_value_class_extensions::get_field(instance, field_name)

#define JAISCRIPT_SET_FIELD(instance, field_name, value) \
    jaiscript::script_value_class_extensions::set_field(instance, field_name, value)

} // namespace jai

/**
 * Example usage in C++:
 * 
 * #include "jaiscript/class_system.hpp"
 * 
 * int main() {
 *     jaiscript::engine js;
 *     auto class_system = jaiscript::enable_class_system(js);
 * 
 *     // Register C++ class for script inheritance
 *     JAISCRIPT_REGISTER_CLASS(MyBaseClass)
 *         .method("virtual_method", &MyBaseClass::virtual_method)
 *         .allow_script_inheritance();
 * 
 *     // Define script class that inherits from C++ class
 *     js.eval(R"(
 *         class ScriptDerived : MyBaseClass {
 *             void virtual_method() override {
 *                 super::virtual_method();
 *                 print("Script override called");
 *             }
 *         }
 *     )");
 * 
 *     // Create and use instances
 *     auto instance = JAISCRIPT_MAKE_SHARED_CLASS(ScriptDerived);
 *     JAISCRIPT_CALL_METHOD(instance, "virtual_method");
 * 
 *     return 0;
 * }
 */
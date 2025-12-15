#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <cmath>

namespace jai {
namespace stdlib {

    // Vec2 - 2D vector type
    struct Vec2 {
        double x, y;

        Vec2() : x(0), y(0) {}
        Vec2(double x, double y) : x(x), y(y) {}

        // Basic arithmetic
        Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
        Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
        Vec2 operator*(double scalar) const { return Vec2(x * scalar, y * scalar); }
        Vec2 operator/(double scalar) const { return Vec2(x / scalar, y / scalar); }
        Vec2 operator-() const { return Vec2(-x, -y); }

        // Compound assignment
        Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
        Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
        Vec2& operator*=(double scalar) { x *= scalar; y *= scalar; return *this; }
        Vec2& operator/=(double scalar) { x /= scalar; y /= scalar; return *this; }

        // Vector operations
        double dot(const Vec2& other) const { return x * other.x + y * other.y; }
        double cross(const Vec2& other) const { return x * other.y - y * other.x; }  // Returns scalar (z-component of 3D cross)
        double length_squared() const { return x * x + y * y; }
        double length() const { return std::sqrt(length_squared()); }
        double magnitude() const { return length(); }  // Alias (Unity naming)
        double magnitude_squared() const { return length_squared(); }  // Alias

        Vec2 normalized() const {
            double len = length();
            return (len > 0) ? Vec2(x / len, y / len) : Vec2(0, 0);
        }

        void normalize() {
            double len = length();
            if (len > 0) { x /= len; y /= len; }
        }

        double distance_to(const Vec2& other) const {
            return (*this - other).length();
        }

        double distance_to_squared(const Vec2& other) const {
            return (*this - other).length_squared();
        }

        // Perpendicular vector (rotated 90 degrees counter-clockwise)
        Vec2 perpendicular() const { return Vec2(-y, x); }

        // Component-wise operations
        Vec2 abs() const { return Vec2(std::abs(x), std::abs(y)); }
        Vec2 scale(const Vec2& other) const { return Vec2(x * other.x, y * other.y); }  // Hadamard product
        Vec2 min(const Vec2& other) const { return Vec2(std::fmin(x, other.x), std::fmin(y, other.y)); }
        Vec2 max(const Vec2& other) const { return Vec2(std::fmax(x, other.x), std::fmax(y, other.y)); }

        // Projection: project this vector onto another
        Vec2 project(const Vec2& onto) const {
            double denom = onto.length_squared();
            return (denom > 0) ? onto * (dot(onto) / denom) : Vec2(0, 0);
        }

        // Rejection: component of this vector perpendicular to another
        Vec2 reject(const Vec2& from) const {
            return *this - project(from);
        }

        // Angle operations (radians)
        double angle() const { return std::atan2(y, x); }  // Angle from positive x-axis
        double angle_to(const Vec2& other) const {         // Unsigned angle between vectors
            double d = dot(other);
            double len_product = length() * other.length();
            return (len_product > 0) ? std::acos(std::fmax(-1.0, std::fmin(1.0, d / len_product))) : 0.0;
        }
        double signed_angle_to(const Vec2& other) const {  // Signed angle (positive = counter-clockwise)
            return std::atan2(cross(other), dot(other));
        }

        // Rotate by angle (radians, counter-clockwise)
        Vec2 rotate(double angle) const {
            double c = std::cos(angle);
            double s = std::sin(angle);
            return Vec2(x * c - y * s, x * s + y * c);
        }

        // Comparison helpers
        bool is_zero() const { return x == 0.0 && y == 0.0; }
        bool is_nearly_zero(double epsilon = 1e-6) const {
            return std::abs(x) < epsilon && std::abs(y) < epsilon;
        }
        bool nearly_equals(const Vec2& other, double epsilon = 1e-6) const {
            return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
        }

        // Reflect vector around normal
        Vec2 reflect(const Vec2& normal) const {
            return *this - normal * (2.0 * dot(normal));
        }

        // Linear interpolation (alias: mix)
        Vec2 lerp(const Vec2& target, double t) const {
            return *this + (target - *this) * t;
        }

        // mix - same as lerp (Bindstone naming)
        Vec2 mix(const Vec2& target, double percent) const {
            return lerp(target, percent);
        }

        // mix_in - ease-in interpolation (accelerating)
        Vec2 mix_in(const Vec2& target, double percent, double strength) const {
            return lerp(target, std::pow(percent, strength));
        }

        // mix_out - ease-out interpolation (decelerating)
        Vec2 mix_out(const Vec2& target, double percent, double strength) const {
            return lerp(target, 1.0 - std::pow(1.0 - percent, strength));
        }

        // mix_in_out - ease-in-out interpolation
        Vec2 mix_in_out(const Vec2& target, double percent, double strength) const {
            if (percent < 0.5) {
                return lerp(target, std::pow(percent * 2.0, strength) * 0.5);
            }
            return lerp(target, 1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength) * 0.5);
        }

        // mix_out_in - ease-out-in interpolation (decelerate then accelerate)
        Vec2 mix_out_in(const Vec2& target, double percent, double strength) const {
            if (percent < 0.5) {
                return lerp(target, (1.0 - std::pow(1.0 - percent * 2.0, strength)) * 0.5);
            }
            return lerp(target, std::pow((percent - 0.5) * 2.0, strength) * 0.5 + 0.5);
        }

        // unmix - inverse lerp: given a result value, returns the percent
        // Projects 'value' onto the line from 'this' to 'target' and returns how far along it is
        double unmix(const Vec2& target, const Vec2& value) const {
            Vec2 dir = target - *this;
            double len_sq = dir.length_squared();
            if (len_sq == 0.0) return 0.0;
            return (value - *this).dot(dir) / len_sq;
        }

        // unmix_in - inverse of mix_in
        double unmix_in(const Vec2& target, const Vec2& value, double strength) const {
            double t = unmix(target, value);
            return std::pow(t, 1.0 / strength);
        }

        // unmix_out - inverse of mix_out
        double unmix_out(const Vec2& target, const Vec2& value, double strength) const {
            double t = unmix(target, value);
            return 1.0 - std::pow(1.0 - t, 1.0 / strength);
        }

        // unmix_in_out - inverse of mix_in_out
        double unmix_in_out(const Vec2& target, const Vec2& value, double strength) const {
            double t = unmix(target, value);
            if (t < 0.5) {
                return std::pow(t * 2.0, 1.0 / strength) / 2.0;
            }
            return (1.0 - std::pow(1.0 - (t - 0.5) * 2.0, 1.0 / strength)) / 2.0 + 0.5;
        }

        // unmix_out_in - inverse of mix_out_in
        double unmix_out_in(const Vec2& target, const Vec2& value, double strength) const {
            double t = unmix(target, value);
            if (t < 0.5) {
                return (1.0 - std::pow(1.0 - t * 2.0, 1.0 / strength)) / 2.0;
            }
            return std::pow((t - 0.5) * 2.0, 1.0 / strength) / 2.0 + 0.5;
        }

        // Move towards target by max_delta distance
        Vec2 move_towards(const Vec2& target, double max_delta) const {
            Vec2 diff = target - *this;
            double dist = diff.length();
            if (dist <= max_delta || dist == 0.0) {
                return target;
            }
            return *this + diff * (max_delta / dist);
        }
    };

    // Vec3 - 3D vector type
    struct Vec3 {
        double x, y, z;

        Vec3() : x(0), y(0), z(0) {}
        Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

        // Basic arithmetic
        Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
        Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
        Vec3 operator*(double scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
        Vec3 operator/(double scalar) const { return Vec3(x / scalar, y / scalar, z / scalar); }
        Vec3 operator-() const { return Vec3(-x, -y, -z); }

        // Compound assignment
        Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
        Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        Vec3& operator*=(double scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
        Vec3& operator/=(double scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

        // Vector operations
        double dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

        Vec3 cross(const Vec3& other) const {
            return Vec3(
                y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x
            );
        }

        double length_squared() const { return x * x + y * y + z * z; }
        double length() const { return std::sqrt(length_squared()); }
        double magnitude() const { return length(); }  // Alias (Unity naming)
        double magnitude_squared() const { return length_squared(); }  // Alias

        Vec3 normalized() const {
            double len = length();
            return (len > 0) ? Vec3(x / len, y / len, z / len) : Vec3(0, 0, 0);
        }

        void normalize() {
            double len = length();
            if (len > 0) { x /= len; y /= len; z /= len; }
        }

        double distance_to(const Vec3& other) const {
            return (*this - other).length();
        }

        double distance_to_squared(const Vec3& other) const {
            return (*this - other).length_squared();
        }

        // Component-wise operations
        Vec3 abs() const { return Vec3(std::abs(x), std::abs(y), std::abs(z)); }
        Vec3 scale(const Vec3& other) const { return Vec3(x * other.x, y * other.y, z * other.z); }  // Hadamard product
        Vec3 min(const Vec3& other) const { return Vec3(std::fmin(x, other.x), std::fmin(y, other.y), std::fmin(z, other.z)); }
        Vec3 max(const Vec3& other) const { return Vec3(std::fmax(x, other.x), std::fmax(y, other.y), std::fmax(z, other.z)); }

        // Projection: project this vector onto another
        Vec3 project(const Vec3& onto) const {
            double denom = onto.length_squared();
            return (denom > 0) ? onto * (dot(onto) / denom) : Vec3(0, 0, 0);
        }

        // Rejection: component of this vector perpendicular to another
        Vec3 reject(const Vec3& from) const {
            return *this - project(from);
        }

        // Angle between vectors (radians, unsigned)
        double angle_to(const Vec3& other) const {
            double d = dot(other);
            double len_product = length() * other.length();
            return (len_product > 0) ? std::acos(std::fmax(-1.0, std::fmin(1.0, d / len_product))) : 0.0;
        }

        // Comparison helpers
        bool is_zero() const { return x == 0.0 && y == 0.0 && z == 0.0; }
        bool is_nearly_zero(double epsilon = 1e-6) const {
            return std::abs(x) < epsilon && std::abs(y) < epsilon && std::abs(z) < epsilon;
        }
        bool nearly_equals(const Vec3& other, double epsilon = 1e-6) const {
            return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon && std::abs(z - other.z) < epsilon;
        }

        // Reflect vector around normal
        Vec3 reflect(const Vec3& normal) const {
            return *this - normal * (2.0 * dot(normal));
        }

        // Linear interpolation (alias: mix)
        Vec3 lerp(const Vec3& target, double t) const {
            return *this + (target - *this) * t;
        }

        // mix - same as lerp (Bindstone naming)
        Vec3 mix(const Vec3& target, double percent) const {
            return lerp(target, percent);
        }

        // mix_in - ease-in interpolation (accelerating)
        Vec3 mix_in(const Vec3& target, double percent, double strength) const {
            return lerp(target, std::pow(percent, strength));
        }

        // mix_out - ease-out interpolation (decelerating)
        Vec3 mix_out(const Vec3& target, double percent, double strength) const {
            return lerp(target, 1.0 - std::pow(1.0 - percent, strength));
        }

        // mix_in_out - ease-in-out interpolation
        Vec3 mix_in_out(const Vec3& target, double percent, double strength) const {
            if (percent < 0.5) {
                return lerp(target, std::pow(percent * 2.0, strength) * 0.5);
            }
            return lerp(target, 1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength) * 0.5);
        }

        // mix_out_in - ease-out-in interpolation (decelerate then accelerate)
        Vec3 mix_out_in(const Vec3& target, double percent, double strength) const {
            if (percent < 0.5) {
                return lerp(target, (1.0 - std::pow(1.0 - percent * 2.0, strength)) * 0.5);
            }
            return lerp(target, std::pow((percent - 0.5) * 2.0, strength) * 0.5 + 0.5);
        }

        // unmix - inverse lerp: given a result value, returns the percent
        // Projects 'value' onto the line from 'this' to 'target' and returns how far along it is
        double unmix(const Vec3& target, const Vec3& value) const {
            Vec3 dir = target - *this;
            double len_sq = dir.length_squared();
            if (len_sq == 0.0) return 0.0;
            return (value - *this).dot(dir) / len_sq;
        }

        // unmix_in - inverse of mix_in
        double unmix_in(const Vec3& target, const Vec3& value, double strength) const {
            double t = unmix(target, value);
            return std::pow(t, 1.0 / strength);
        }

        // unmix_out - inverse of mix_out
        double unmix_out(const Vec3& target, const Vec3& value, double strength) const {
            double t = unmix(target, value);
            return 1.0 - std::pow(1.0 - t, 1.0 / strength);
        }

        // unmix_in_out - inverse of mix_in_out
        double unmix_in_out(const Vec3& target, const Vec3& value, double strength) const {
            double t = unmix(target, value);
            if (t < 0.5) {
                return std::pow(t * 2.0, 1.0 / strength) / 2.0;
            }
            return (1.0 - std::pow(1.0 - (t - 0.5) * 2.0, 1.0 / strength)) / 2.0 + 0.5;
        }

        // unmix_out_in - inverse of mix_out_in
        double unmix_out_in(const Vec3& target, const Vec3& value, double strength) const {
            double t = unmix(target, value);
            if (t < 0.5) {
                return (1.0 - std::pow(1.0 - t * 2.0, 1.0 / strength)) / 2.0;
            }
            return std::pow((t - 0.5) * 2.0, 1.0 / strength) / 2.0 + 0.5;
        }

        // Move towards target by max_delta distance
        Vec3 move_towards(const Vec3& target, double max_delta) const {
            Vec3 diff = target - *this;
            double dist = diff.length();
            if (dist <= max_delta || dist == 0.0) {
                return target;
            }
            return *this + diff * (max_delta / dist);
        }
    };

    // Register vector types with an engine
    inline void register_vector_types(engine& engine) {
        // Vec2
        class_builder<Vec2>(engine, "Vec2")
            .constructor<>()
            .constructor<double, double>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            // Operators
            .method("+", &Vec2::operator+)
            .method("-", static_cast<Vec2(Vec2::*)(const Vec2&) const>(&Vec2::operator-))
            .method("*", &Vec2::operator*)
            .method("/", &Vec2::operator/)
            // Core methods
            .method("dot", &Vec2::dot)
            .method("cross", &Vec2::cross)
            .method("length", &Vec2::length)
            .method("magnitude", &Vec2::length)  // Alias (Unity naming)
            .method("length_squared", &Vec2::length_squared)
            .method("magnitude_squared", &Vec2::magnitude_squared)  // Alias
            .method("normalized", &Vec2::normalized)
            .method("normalize", &Vec2::normalize)
            .method("distance_to", &Vec2::distance_to)
            .method("distance_to_squared", &Vec2::distance_to_squared)
            .method("perpendicular", &Vec2::perpendicular)
            // Component-wise operations
            .method("abs", &Vec2::abs)
            .method("scale", &Vec2::scale)
            .method("min", &Vec2::min)
            .method("max", &Vec2::max)
            // Projection
            .method("project", &Vec2::project)
            .method("reject", &Vec2::reject)
            // Angles (radians)
            .method("angle", &Vec2::angle)
            .method("angle_to", &Vec2::angle_to)
            .method("signed_angle_to", &Vec2::signed_angle_to)
            .method("rotate", &Vec2::rotate)
            // Comparison
            .method("is_zero", &Vec2::is_zero)
            .method("is_nearly_zero", &Vec2::is_nearly_zero)
            .method("nearly_equals", &Vec2::nearly_equals)
            // Misc
            .method("reflect", &Vec2::reflect)
            .method("lerp", &Vec2::lerp)
            .method("mix", &Vec2::mix)
            .method("mix_in", &Vec2::mix_in)
            .method("mix_out", &Vec2::mix_out)
            .method("mix_in_out", &Vec2::mix_in_out)
            .method("mix_out_in", &Vec2::mix_out_in)
            .method("unmix", &Vec2::unmix)
            .method("unmix_in", &Vec2::unmix_in)
            .method("unmix_out", &Vec2::unmix_out)
            .method("unmix_in_out", &Vec2::unmix_in_out)
            .method("unmix_out_in", &Vec2::unmix_out_in)
            .method("move_towards", &Vec2::move_towards)
            .build();

        // Vec3
        class_builder<Vec3>(engine, "Vec3")
            .constructor<>()
            .constructor<double, double, double>()
            .property("x", &Vec3::x)
            .property("y", &Vec3::y)
            .property("z", &Vec3::z)
            // Operators
            .method("+", &Vec3::operator+)
            .method("-", static_cast<Vec3(Vec3::*)(const Vec3&) const>(&Vec3::operator-))
            .method("*", &Vec3::operator*)
            .method("/", &Vec3::operator/)
            // Core methods
            .method("dot", &Vec3::dot)
            .method("cross", &Vec3::cross)
            .method("length", &Vec3::length)
            .method("magnitude", &Vec3::length)  // Alias
            .method("length_squared", &Vec3::length_squared)
            .method("magnitude_squared", &Vec3::magnitude_squared)  // Alias
            .method("normalized", &Vec3::normalized)
            .method("normalize", &Vec3::normalize)
            .method("distance_to", &Vec3::distance_to)
            .method("distance_to_squared", &Vec3::distance_to_squared)
            // Component-wise operations
            .method("abs", &Vec3::abs)
            .method("scale", &Vec3::scale)
            .method("min", &Vec3::min)
            .method("max", &Vec3::max)
            // Projection
            .method("project", &Vec3::project)
            .method("reject", &Vec3::reject)
            // Angles (radians)
            .method("angle_to", &Vec3::angle_to)
            // Comparison
            .method("is_zero", &Vec3::is_zero)
            .method("is_nearly_zero", &Vec3::is_nearly_zero)
            .method("nearly_equals", &Vec3::nearly_equals)
            // Misc
            .method("reflect", &Vec3::reflect)
            .method("lerp", &Vec3::lerp)
            .method("mix", &Vec3::mix)
            .method("mix_in", &Vec3::mix_in)
            .method("mix_out", &Vec3::mix_out)
            .method("mix_in_out", &Vec3::mix_in_out)
            .method("mix_out_in", &Vec3::mix_out_in)
            .method("unmix", &Vec3::unmix)
            .method("unmix_in", &Vec3::unmix_in)
            .method("unmix_out", &Vec3::unmix_out)
            .method("unmix_in_out", &Vec3::unmix_in_out)
            .method("unmix_out_in", &Vec3::unmix_out_in)
            .method("move_towards", &Vec3::move_towards)
            .build();

        // Global helper functions
        auto engine_weak = engine.weak_from_this();

        // vec2(x, y) - shorthand constructor
        engine.add_function("vec2", [](double x, double y) -> Vec2 {
            return Vec2(x, y);
        });

        // vec3(x, y, z) - shorthand constructor
        engine.add_function("vec3", [](double x, double y, double z) -> Vec3 {
            return Vec3(x, y, z);
        });

        // Global move_towards that works with Vec2, Vec3, or scalars
        engine.add_variadic_function("move_towards", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.size() == 3) {
                // Check if first arg is Vec2
                if (auto* v2 = args[0].get_if<std::shared_ptr<Vec2>>()) {
                    auto* target = args[1].get_if<std::shared_ptr<Vec2>>();
                    if (!target) throw runtime_error("move_towards: target must be Vec2 when start is Vec2");
                    double max_delta = args[2].as<double>();
                    return script_value(std::make_shared<Vec2>((*v2)->move_towards(**target, max_delta)), engine_weak);
                }
                // Check if first arg is Vec3
                if (auto* v3 = args[0].get_if<std::shared_ptr<Vec3>>()) {
                    auto* target = args[1].get_if<std::shared_ptr<Vec3>>();
                    if (!target) throw runtime_error("move_towards: target must be Vec3 when start is Vec3");
                    double max_delta = args[2].as<double>();
                    return script_value(std::make_shared<Vec3>((*v3)->move_towards(**target, max_delta)), engine_weak);
                }
                // Scalar fallback
                double current = args[0].as<double>();
                double target = args[1].as<double>();
                double max_delta = args[2].as<double>();
                if (std::abs(target - current) <= max_delta) {
                    return script_value(target, engine_weak);
                }
                return script_value(current + std::copysign(max_delta, target - current), engine_weak);
            }
            throw runtime_error("move_towards expects 3 arguments (start, target, max_delta), got " + std::to_string(args.size()));
        });
    }

} // namespace stdlib
} // namespace jai

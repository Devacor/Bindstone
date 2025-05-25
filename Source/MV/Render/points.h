#ifndef _MV_POINTS_H_
#define _MV_POINTS_H_

#include <ostream>
#include <istream>
#include <type_traits>
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstdint>
#include <algorithm>

#include "MV/Utility/generalUtility.h"
#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

namespace MV {
	using PointPrecision = float;

	// Modern C++17 type trait
	template<typename T>
	inline constexpr bool is_arithmetic_v = std::is_arithmetic_v<T>;

	// Forward declarations
	class DrawPoint;
	class Scale;
	template<typename T> class Point;
	template<typename T> class Size;

	class TexturePoint {
	public:
		TexturePoint(PointPrecision a_textureX, PointPrecision a_textureY)
			: textureX(a_textureX), textureY(a_textureY) {
		}
		TexturePoint() : textureX(0.0f), textureY(0.0f) {}
		~TexturePoint() = default;

		TexturePoint& operator=(const TexturePoint& a_other) = default;
		TexturePoint& operator=(const DrawPoint& a_other);

		bool operator==(const TexturePoint& a_other) const {
			return equals(textureX, a_other.textureX) && equals(textureY, a_other.textureY);
		}
		bool operator!=(const TexturePoint& a_other) const {
			return !(*this == a_other);
		}

		template <class Archive>
		void serialize(Archive& archive) {
			archive(CEREAL_NVP(textureX), CEREAL_NVP(textureY));
		}

		PointPrecision textureX, textureY;
	};

	class Color {
	public:
		Color() : R(1.0f), G(1.0f), B(1.0f), A(1.0f) {}
		Color(uint32_t a_hex, bool a_allowFullAlpha = false);
		Color(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0f);
		Color(int a_Red, int a_Green, int a_Blue, int a_Alpha = 255);
		Color(int64_t a_Red, int64_t a_Green, int64_t a_Blue, int64_t a_Alpha = 255);
		~Color() = default;

		Color& operator=(const Color& a_other) = default;
		Color& operator=(const DrawPoint& a_other);

		bool operator==(const Color& a_other) const;
		bool operator!=(const Color& a_other) const;

		Color& operator+=(const Color& a_other);
		Color& operator-=(const Color& a_other);
		Color& operator*=(const Color& a_other);
		Color& operator/=(const Color& a_other);

		template<typename T>
		auto operator*=(T a_other) -> std::enable_if_t<is_arithmetic_v<T>, Color&>;

		template<typename T>
		auto operator/=(T a_other)->std::enable_if_t<is_arithmetic_v<T>, Color&>;

		template <class Archive>
		void serialize(Archive& archive) {
			normalize();
			archive(CEREAL_NVP(R), CEREAL_NVP(G), CEREAL_NVP(B), CEREAL_NVP(A));
			normalize();
		}

		[[nodiscard]] uint32_t hex() const;
		Color& hex(uint32_t a_hex, bool a_allowFullAlpha = false);

		struct HSV {
			HSV(float a_Hue = 0.0f, float a_Saturation = 1.0f, float a_Value = 1.0f, float a_Alpha = 1.0f)
				: H(a_Hue), S(a_Saturation), V(a_Value), A(a_Alpha) {
			}

			[[nodiscard]] float percentHue() const { return H / 360.0f; }
			[[nodiscard]] float invertedPercentHue() const { return (360.0f - H) / 360.0f; }

			float percentHue(float a_percent) {
				H = a_percent * 360.0f;
				return H;
			}
			float invertedPercentHue(float a_percent) {
				H = (1.0f - a_percent) * 360.0f;
				return H;
			}

			float H, S, V, A;
		};

		[[nodiscard]] HSV hsv() const;
		[[nodiscard]] HSV getHsv(HSV a_resultInput) const;
		Color& hsv(HSV a_hsv);

		Color& set(uint32_t a_hex, bool a_allowFullAlpha = false);
		Color& set(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0f);
		Color& set(int a_Red, int a_Green, int a_Blue, int a_Alpha = 255);
		Color& set(int64_t a_Red, int64_t a_Green, int64_t a_Blue, int64_t a_Alpha = 255);
		Color& set(HSV a_hsv);

		float R, G, B, A;

		void normalize();

	private:
		[[nodiscard]] float validColorRange(float a_color) {
			return std::clamp(a_color, 0.0f, 1.0f);
		}
	};

	template <typename T = PointPrecision>
	class Size {
	public:
		Size() : width(0), height(0), depth(0) {}
		Size(T a_width, T a_height, T a_depth = 0)
			: width(a_width), height(a_height), depth(a_depth) {
		}

		Size& set(T a_width, T a_height) {
			width = a_width;
			height = a_height;
			return *this;
		}

		Size& set(T a_width, T a_height, T a_depth) {
			set(a_width, a_height);
			depth = a_depth;
			return *this;
		}

		[[nodiscard]] bool contains(const Size& a_other, bool a_useDepth = false) const {
			return width >= a_other.width && height >= a_other.height &&
				(!a_useDepth || depth >= a_other.depth);
		}

		[[nodiscard]] T area(bool a_useDepth = false) const {
			return width * height * (a_useDepth ? depth : 1);
		}

		Size& operator+=(const Size& a_other);
		Size& operator-=(const Size& a_other);
		Size& operator*=(const Size& a_other);
		Size& operator/=(const Size& a_other);

		template<typename U = T>
		auto operator+=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&>;
		template<typename U = T>
		auto operator-=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&>;
		template<typename U = T>
		auto operator*=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&>;
		template<typename U = T>
		auto operator/=(const T& a_other)->std::enable_if_t<is_arithmetic_v<U>, Size&>;

		Size& operator*=(const Scale& a_other);
		Size& operator/=(const Scale& a_other);

		bool operator<(const Size& a_other) const {
			return (width + height) < (a_other.width + a_other.height);
		}
		bool operator>(const Size& a_other) const {
			return (width + height) > (a_other.width + a_other.height);
		}

		template <class Archive>
		void serialize(Archive& archive) {
			archive(CEREAL_NVP(width), CEREAL_NVP(height), CEREAL_NVP(depth));
		}

		T width, height, depth;
	};

	template <typename T = PointPrecision>
	class Point {
	public:
		using value_type = T;

		Point() : x(0), y(0), z(0) {}
		Point(T a_xPos, T a_yPos, T a_zPos = 0) : x(a_xPos), y(a_yPos), z(a_zPos) {}

		[[nodiscard]] static constexpr Point One() {
			return { 1, 1, 1 };
		}

		void clear();

		Point& set(T a_xPos, T a_yPos) {
			x = a_xPos;
			y = a_yPos;
			return *this;
		}

		Point& set(T a_xPos, T a_yPos, T a_zPos) {
			x = a_xPos;
			y = a_yPos;
			z = a_zPos;
			return *this;
		}

		Point& scale(T a_amount);
		Point& locate(T a_xPos, T a_yPos, T a_zPos);
		Point& locate(T a_xPos, T a_yPos);
		Point& translate(T a_xAmount, T a_yAmount, T a_zAmount);
		Point& translate(T a_xAmount, T a_yAmount);

		[[nodiscard]] bool atOrigin() const {
			return equals<T>(x, 0) && equals<T>(y, 0) && equals<T>(z, 0);
		}

		[[nodiscard]] Point ignoreZ() const {
			return Point(x, y);
		}

		Point& operator+=(const Point& a_other);
		Point& operator-=(const Point& a_other);
		Point& operator*=(const Point& a_other);
		Point& operator/=(const Point& a_other);

		template<typename U = T>
		auto operator*=(const typename Point<T>::value_type& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Point&>;
		template<typename U = T>
		auto operator/=(const typename Point<T>::value_type& a_other)->std::enable_if_t<is_arithmetic_v<U>, Point&>;

		Point& operator*=(const Scale& a_other);
		Point& operator/=(const Scale& a_other);

		bool operator==(const Point& a_other) const {
			return equals(x, a_other.x) && equals(y, a_other.y) && equals(z, a_other.z);
		}
		bool operator==(const T& a_other) const {
			return equals(x, a_other) && equals(y, a_other) && equals(z, a_other);
		}

		bool operator!=(const Point& a_other) const {
			return !(*this == a_other);
		}
		bool operator!=(const T& a_other) const {
			return !(*this == a_other);
		}

		bool operator<(const Point& a_other) const {
			return (x + y) < (a_other.x + a_other.y);
		}
		bool operator>(const Point& a_other) const {
			return (x + y) > (a_other.x + a_other.y);
		}

		T& operator()(size_t a_index) {
			return *(&x + a_index);
		}
		[[nodiscard]] T operator()(size_t a_index) const {
			return *(&x + a_index);
		}

		template <class Archive>
		void serialize(Archive& archive) {
			archive(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(z));
		}

		[[nodiscard]] Point<PointPrecision> normalized() const {
			PointPrecision length = magnitude();
			if (length == 0) { length = 1; }
			return Point<PointPrecision>(
				static_cast<PointPrecision>(x) / length,
				static_cast<PointPrecision>(y) / length,
				static_cast<PointPrecision>(z) / length
			);
		}

		[[nodiscard]] PointPrecision preSquareMagnitude() const {
			return static_cast<PointPrecision>((x * x) + (y * y) + (z * z));
		}

		[[nodiscard]] PointPrecision magnitude() const {
			return std::sqrt(preSquareMagnitude());
		}

		T x, y, z;
	};

	using AxisAngles = Point<>;
	using AxisMagnitude = Point<>;

	class Scale {
	public:
		Scale() : x(1.0f), y(1.0f), z(1.0f) {}
		Scale(PointPrecision a_scale) : x(a_scale), y(a_scale), z(a_scale) {}
		Scale(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 1)
			: x(a_x), y(a_y), z(a_z) {
		}

		Scale& operator=(PointPrecision a_value);

		Scale& operator+=(const Scale& a_other);
		Scale& operator-=(const Scale& a_other);
		Scale& operator*=(const Scale& a_other);
		Scale& operator/=(const Scale& a_other);
		Scale& operator+=(PointPrecision a_other);
		Scale& operator-=(PointPrecision a_other);
		Scale& operator*=(PointPrecision a_other);
		Scale& operator/=(PointPrecision a_other);

		bool operator==(const Scale& a_other) const;
		bool operator==(PointPrecision a_other) const;

		bool operator!=(const Scale& a_other) const;
		bool operator!=(PointPrecision a_other) const;

		template <class Archive>
		void serialize(Archive& archive) {
			archive(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(z));
		}

		PointPrecision x, y, z;
	};

	[[nodiscard]] Point<PointPrecision> toPoint(const Scale& a_scale);
	[[nodiscard]] Size<PointPrecision> toSize(const Scale& a_scale);

	template <typename T>
	[[nodiscard]] Scale toScale(const Point<T>& a_point) {
		return {
			static_cast<PointPrecision>(a_point.x),
			static_cast<PointPrecision>(a_point.y),
			static_cast<PointPrecision>(a_point.z)
		};
	}

	template <typename T>
	[[nodiscard]] Scale toScale(const Size<T>& a_size) {
		return {
			static_cast<PointPrecision>(a_size.width),
			static_cast<PointPrecision>(a_size.height),
			static_cast<PointPrecision>(a_size.depth)
		};
	}

	// Scale operators
	[[nodiscard]] Scale operator+(Scale a_lhs, const Scale& a_rhs);
	[[nodiscard]] Scale operator+(Scale a_lhs, PointPrecision a_rhs);
	[[nodiscard]] Scale operator+(PointPrecision a_lhs, Scale a_rhs);
	[[nodiscard]] Scale operator-(Scale a_lhs, const Scale& a_rhs);
	[[nodiscard]] Scale operator-(Scale a_lhs, PointPrecision a_rhs);
	[[nodiscard]] Scale operator-(PointPrecision a_lhs, Scale a_rhs);
	[[nodiscard]] Scale operator*(Scale a_lhs, const Scale& a_rhs);
	[[nodiscard]] Scale operator*(Scale a_lhs, PointPrecision a_rhs);
	[[nodiscard]] Scale operator*(PointPrecision a_lhs, Scale a_rhs);
	[[nodiscard]] Scale operator/(Scale a_lhs, const Scale& a_rhs);
	[[nodiscard]] Scale operator/(Scale a_lhs, PointPrecision a_rhs);
	[[nodiscard]] Scale operator/(PointPrecision a_lhs, Scale a_rhs);

	inline std::ostream& operator<<(std::ostream& os, const Scale& a_scale) {
		os << "(x: " << a_scale.x << ", y: " << a_scale.y << ", z: " << a_scale.z << ")";
		return os;
	}

	inline std::istream& operator>>(std::istream& is, Scale& a_scale) {
		is >> a_scale.x >> a_scale.y >> a_scale.z;
		return is;
	}

	// Scale interpolation functions
	[[nodiscard]] inline Scale mix(Scale a_start, Scale a_end, float a_percent) {
		return {
			mix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent),
			mix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent),
			mix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent)
		};
	}

	[[nodiscard]] inline Scale unmix(Scale a_start, Scale a_end, float a_percent) {
		return {
			unmix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent),
			unmix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent),
			unmix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent)
		};
	}

	[[nodiscard]] inline Scale mixIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale mix(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	[[nodiscard]] inline Scale mixOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale mixInOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale mixOutIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale unmixIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale unmix(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	[[nodiscard]] inline Scale unmixOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale unmixInOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Scale unmixOutIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	class DrawPoint : public Point<>, public Color, public TexturePoint {
	public:
		DrawPoint() = default;
		DrawPoint(PointPrecision a_xPos, PointPrecision a_yPos, PointPrecision a_zPos = 0.0f) {
			locate(a_xPos, a_yPos, a_zPos);
		}
		DrawPoint(const Point<>& a_position, const Color& a_color = Color(),
			const TexturePoint& a_texture = TexturePoint())
			: Point<>(a_position), Color(a_color), TexturePoint(a_texture) {}
		DrawPoint(const Point<>& a_position, const TexturePoint& a_texture)
			: Point<>(a_position), TexturePoint(a_texture) {}
		~DrawPoint() = default;

		void clear();

		DrawPoint& operator=(const DrawPoint& a_other);
		DrawPoint& operator=(const Point<>& a_other);
		DrawPoint& operator=(const Color& a_other);
		DrawPoint& operator=(const TexturePoint& a_other);

		DrawPoint& operator+=(const DrawPoint& a_other);
		DrawPoint& operator-=(const DrawPoint& a_other);
		DrawPoint& operator+=(const Point<>& a_other);
		DrawPoint& operator-=(const Point<>& a_other);
		DrawPoint& operator+=(const Color& a_other);
		DrawPoint& operator-=(const Color& a_other);
		DrawPoint& operator+=(const TexturePoint& a_other);
		DrawPoint& operator-=(const TexturePoint& a_other);

		void copyPosition(const DrawPoint& a_other);
		void copyTexture(const DrawPoint& a_other);
		void copyColor(const DrawPoint& a_other);

		[[nodiscard]] Point<> point() const {
			return Point<>(x, y, z);
		}

		[[nodiscard]] Color color() const {
			return Color(R, G, B, A);
		}

		[[nodiscard]] TexturePoint texturePoint() const {
			return TexturePoint(textureX, textureY);
		}

		template <class Archive>
		void serialize(Archive& archive) {
			Point::serialize(archive);
			Color::serialize(archive);
			TexturePoint::serialize(archive);
		}
	};

	// DrawPoint operators
	[[nodiscard]] DrawPoint operator+(const DrawPoint& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator-(const DrawPoint& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator+(const DrawPoint& a_left, const Point<>& a_right);
	[[nodiscard]] DrawPoint operator-(const DrawPoint& a_left, const Point<>& a_right);
	[[nodiscard]] DrawPoint operator+(const DrawPoint& a_left, const Color& a_right);
	[[nodiscard]] DrawPoint operator-(const DrawPoint& a_left, const Color& a_right);
	[[nodiscard]] DrawPoint operator+(const DrawPoint& a_left, const TexturePoint& a_right);
	[[nodiscard]] DrawPoint operator-(const DrawPoint& a_left, const TexturePoint& a_right);

	[[nodiscard]] DrawPoint operator+(const Point<>& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator-(const Point<>& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator+(const Color& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator-(const Color& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator+(const TexturePoint& a_left, const DrawPoint& a_right);
	[[nodiscard]] DrawPoint operator-(const TexturePoint& a_left, const DrawPoint& a_right);

	// Conversion functions
	template <typename T>
	[[nodiscard]] Size<T> toSize(const Point<T>& a_point) {
		return Size<T>{a_point.x, a_point.y, a_point.z};
	}

	template <typename T>
	[[nodiscard]] Point<T> toPoint(const Size<T>& a_size) {
		return Point<T>{a_size.width, a_size.height, a_size.depth};
	}

	// Color operators and functions
	std::ostream& operator<<(std::ostream& os, const Color& a_color);

	[[nodiscard]] Color operator+(const Color& a_lhs, const Color& a_rhs);
	[[nodiscard]] Color operator-(const Color& a_lhs, const Color& a_rhs);
	[[nodiscard]] Color operator/(const Color& a_lhs, const Color& a_rhs);
	[[nodiscard]] Color operator*(const Color& a_lhs, const Color& a_rhs);

	template<typename T>
	[[nodiscard]] auto operator*(const Color& a_lhs, T a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		Color result = a_lhs;
		return result *= a_rhs;
	}

	template<typename T>
	[[nodiscard]] auto operator/(const Color& a_lhs, T a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		Color result = a_lhs;
		return result /= a_rhs;
	}

	template<typename T>
	[[nodiscard]] auto operator+(const Color& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		Color tmpPoint = a_left;
		auto value = static_cast<float>(a_right);
		return tmpPoint += Color(value, value, value, value);
	}

	template<typename T>
	[[nodiscard]] auto operator-(const Color& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		Color tmpPoint = a_left;
		auto value = static_cast<float>(a_right);
		return tmpPoint -= Color(value, value, value, value);
	}

	template<typename T>
	[[nodiscard]] auto operator+(const T& a_left, const Color& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint += a_right;
	}

	template<typename T>
	[[nodiscard]] auto operator-(const T& a_left, const Color& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint -= a_right;
	}

	template<typename T>
	[[nodiscard]] auto operator*(const T& a_left, const Color& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint *= a_right;
	}

	template<typename T>
	[[nodiscard]] auto operator/(const T& a_left, const Color& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Color> {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint /= a_right;
	}

	// Color interpolation functions
	[[nodiscard]] inline Color mix(Color a_start, Color a_end, float a_percent) {
		return {
			mix(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent),
			mix(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent),
			mix(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent),
			mix(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent)
		};
	}

	[[nodiscard]] inline Color unmix(Color a_start, Color a_end, float a_percent) {
		return {
			unmix(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent),
			unmix(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent),
			unmix(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent),
			unmix(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent)
		};
	}

	[[nodiscard]] inline Color mixIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color mix(Color a_start, Color a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	[[nodiscard]] inline Color mixOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color mixInOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixInOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color mixOutIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixOutIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color unmixIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color unmix(Color a_start, Color a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	[[nodiscard]] inline Color unmixOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color unmixInOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixInOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	[[nodiscard]] inline Color unmixOutIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixOutIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	// Size implementation
	template <typename T>
	[[nodiscard]] auto fitAspect(Size<T> a_toConstrain, Size<T> a_maximum)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		auto ratio = std::min(
			static_cast<double>(a_maximum.width) / static_cast<double>(a_toConstrain.width),
			static_cast<double>(a_maximum.height) / static_cast<double>(a_toConstrain.height)
		);
		return {
			static_cast<T>(static_cast<double>(a_toConstrain.width) * ratio),
			static_cast<T>(static_cast<double>(a_toConstrain.height) * ratio)
		};
	}

	template <typename T>
	[[nodiscard]] auto size(T a_width, T a_height)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		return Size<T>{a_width, a_height};
	}

	template <typename T>
	[[nodiscard]] auto size(T a_width, T a_height, T a_depth)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		return Size<T>{a_width, a_height, a_depth};
	}

	template <typename Target, typename Origin>
	[[nodiscard]] inline Size<Target> cast(const Size<Origin>& a_size) {
		return Size<Target>{
			static_cast<Target>(a_size.width),
				static_cast<Target>(a_size.height),
				static_cast<Target>(a_size.depth)
		};
	}

	template <typename Target, typename Origin>
	[[nodiscard]] inline Size<Target> round(const Size<Origin>& a_size) {
		return Size<Target>(
			static_cast<Target>(std::round(a_size.width)),
			static_cast<Target>(std::round(a_size.height)),
			static_cast<Target>(std::round(a_size.depth))
		);
	}

	// Size member function implementations
	template <typename T>
	Size<T>& Size<T>::operator+=(const Size& a_other) {
		width += a_other.width;
		height += a_other.height;
		depth += a_other.depth;
		return *this;
	}

	template <typename T>
	Size<T>& Size<T>::operator-=(const Size& a_other) {
		width -= a_other.width;
		height -= a_other.height;
		depth -= a_other.depth;
		return *this;
	}

	template <typename T>
	Size<T>& Size<T>::operator*=(const Size& a_other) {
		width *= a_other.width;
		height *= a_other.height;
		depth *= a_other.depth;
		return *this;
	}

	template <typename T>
	Size<T>& Size<T>::operator*=(const Scale& a_other) {
		width = static_cast<T>(static_cast<PointPrecision>(width) * a_other.x);
		height = static_cast<T>(static_cast<PointPrecision>(height) * a_other.y);
		depth = static_cast<T>(static_cast<PointPrecision>(depth) * a_other.z);
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Size<T>::operator+=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&> {
		width += a_other;
		height += a_other;
		depth += a_other;
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Size<T>::operator-=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&> {
		width -= a_other;
		height -= a_other;
		depth -= a_other;
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Size<T>::operator*=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&> {
		width *= a_other;
		height *= a_other;
		depth *= a_other;
		return *this;
	}

	template <typename T>
	Size<T>& Size<T>::operator/=(const Size& a_other) {
		width /= (a_other.width == 0) ? 1 : a_other.width;
		height /= (a_other.height == 0) ? 1 : a_other.height;
		depth /= (a_other.depth == 0) ? 1 : a_other.depth;
		return *this;
	}

	template <typename T>
	Size<T>& Size<T>::operator/=(const Scale& a_other) {
		width = static_cast<T>(static_cast<PointPrecision>(width) / (a_other.x == 0 ? 1.0f : a_other.x));
		height = static_cast<T>(static_cast<PointPrecision>(height) / (a_other.y == 0 ? 1.0f : a_other.y));
		depth = static_cast<T>(static_cast<PointPrecision>(depth) / (a_other.z == 0 ? 1.0f : a_other.z));
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Size<T>::operator/=(const T& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Size&> {
		if (a_other != 0) {
			width /= a_other;
			height /= a_other;
			depth /= a_other;
		}
		else {
			std::cerr << "ERROR: Size<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	// Size operators
	template <typename T>
	[[nodiscard]] bool operator==(const Size<T>& a_left, const Size<T>& a_right) {
		return equals(a_left.width, a_right.width) &&
			equals(a_left.height, a_right.height) &&
			equals(a_left.depth, a_right.depth);
	}

	template <typename T>
	[[nodiscard]] bool operator!=(const Size<T>& a_left, const Size<T>& a_right) {
		return !(a_left == a_right);
	}

	template <typename T>
	[[nodiscard]] Size<T> operator+(const Size<T>& a_left, const Size<T>& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <typename T>
	[[nodiscard]] Size<T> operator-(const Size<T>& a_left, const Size<T>& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <typename T>
	[[nodiscard]] Size<T> operator*(const Size<T>& a_left, const Size<T>& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] Size<T> operator/(const Size<T>& a_left, const Size<T>& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint /= a_right;
	}

	template <typename T>
	[[nodiscard]] Size<T> operator*(const Size<T>& a_left, const Scale& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] Size<T> operator/(const Size<T>& a_left, const Scale& a_right) {
		Size<T> tmpPoint = a_left;
		return tmpPoint /= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator+(const Size<T>& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator-(const Size<T>& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator*(const Size<T>& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator/(const Size<T>& a_left, const T& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = a_left;
		return tmpPoint /= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator+(const T& a_left, const Size<T>& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator-(const T& a_left, const Size<T>& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator*(const T& a_left, const Size<T>& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator/(const T& a_left, const Size<T>& a_right)
		-> std::enable_if_t<is_arithmetic_v<T>, Size<T>> {
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint /= a_right;
	}

	template <typename T>
	std::ostream& operator<<(std::ostream& os, const Size<T>& a_size) {
		os << "(w: " << a_size.width << ", h: " << a_size.height << ", d: " << a_size.depth << ")";
		return os;
	}

	template <typename T>
	std::istream& operator>>(std::istream& is, Size<T>& a_size) {
		is >> a_size.width >> a_size.height >> a_size.depth;
		return is;
	}

	// Size interpolation functions
	template <typename T>
	[[nodiscard]] Size<T> mix(Size<T> a_start, Size<T> a_end, float a_percent) {
		return {
			static_cast<T>(mix(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> unmix(Size<T> a_start, Size<T> a_end, float a_percent) {
		return {
			static_cast<T>(unmix(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> mixIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> mix(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	template <typename T>
	[[nodiscard]] Size<T> mixOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> mixInOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixInOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> mixOutIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOutIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> unmixIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> unmix(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	template <typename T>
	[[nodiscard]] Size<T> unmixOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> unmixInOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixInOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Size<T> unmixOutIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	// Point implementation
	template <typename T>
	[[nodiscard]] auto point(T a_xPos, T a_yPos)
		-> std::enable_if_t<is_arithmetic_v<T>, Point<T>> {
		return Point<T>{a_xPos, a_yPos};
	}

	template <typename T>
	[[nodiscard]] auto point(T a_xPos, T a_yPos, T a_zPos)
		-> std::enable_if_t<is_arithmetic_v<T>, Point<T>> {
		return Point<T>{a_xPos, a_yPos, a_zPos};
	}

	template <typename Target, typename Origin>
	[[nodiscard]] inline Point<Target> round(const Point<Origin>& a_point) {
		return Point<Target>{
			static_cast<Target>(std::round(a_point.x)),
				static_cast<Target>(std::round(a_point.y)),
				static_cast<Target>(std::round(a_point.z))
		};
	}

	template <typename Target, typename Origin>
	[[nodiscard]] inline Point<Target> cast(const Point<Origin>& a_point) {
		return Point<Target>{
			static_cast<Target>(a_point.x),
				static_cast<Target>(a_point.y),
				static_cast<Target>(a_point.z)
		};
	}

	// Point member functions
	template <typename T>
	void Point<T>::clear() {
		x = 0;
		y = 0;
		z = 0;
	}

	template <typename T>
	Point<T>& Point<T>::translate(T a_xAmount, T a_yAmount, T a_zAmount) {
		x += a_xAmount;
		y += a_yAmount;
		z += a_zAmount;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::translate(T a_xAmount, T a_yAmount) {
		x += a_xAmount;
		y += a_yAmount;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::locate(T a_xPos, T a_yPos, T a_zPos) {
		x = a_xPos;
		y = a_yPos;
		z = a_zPos;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::locate(T a_xPos, T a_yPos) {
		x = a_xPos;
		y = a_yPos;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::scale(T a_amount) {
		x *= a_amount;
		y *= a_amount;
		z *= a_amount;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator+=(const Point& a_other) {
		x += a_other.x;
		y += a_other.y;
		z += a_other.z;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator-=(const Point& a_other) {
		x -= a_other.x;
		y -= a_other.y;
		z -= a_other.z;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator*=(const Point& a_other) {
		x *= a_other.x;
		y *= a_other.y;
		z *= a_other.z;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator*=(const Scale& a_other) {
		x = static_cast<T>(static_cast<PointPrecision>(x) * a_other.x);
		y = static_cast<T>(static_cast<PointPrecision>(y) * a_other.y);
		z = static_cast<T>(static_cast<PointPrecision>(z) * a_other.z);
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Point<T>::operator*=(const typename Point<T>::value_type& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Point&> {
		x *= a_other;
		y *= a_other;
		z *= a_other;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator/=(const Point& a_other) {
		x /= (a_other.x != 0) ? a_other.x : 1;
		y /= (a_other.y != 0) ? a_other.y : 1;
		z /= (a_other.z != 0) ? a_other.z : 1;
		return *this;
	}

	template <typename T>
	Point<T>& Point<T>::operator/=(const Scale& a_other) {
		x = static_cast<T>(static_cast<PointPrecision>(x) / (!equals(a_other.x, 0.0f) ? a_other.x : 1));
		y = static_cast<T>(static_cast<PointPrecision>(y) / (!equals(a_other.y, 0.0f) ? a_other.y : 1));
		z = static_cast<T>(static_cast<PointPrecision>(z) / (!equals(a_other.z, 0.0f) ? a_other.z : 1));
		return *this;
	}

	template <typename T>
	template <typename U>
	auto Point<T>::operator/=(const typename Point<T>::value_type& a_other) -> std::enable_if_t<is_arithmetic_v<U>, Point&> {
		if (a_other != 0) {
			x /= a_other;
			y /= a_other;
			z /= a_other;
		}
		else {
			std::cerr << "ERROR: Point<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	// Point interpolation functions
	template <typename T>
	[[nodiscard]] Point<T> mix(Point<T> a_start, Point<T> a_end, float a_percent) {
		return {
			static_cast<T>(mix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> unmix(Point<T> a_start, Point<T> a_end, float a_percent) {
		return {
			static_cast<T>(unmix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> mixIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> mix(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	template <typename T>
	[[nodiscard]] Point<T> mixOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> mixInOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> mixOutIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> unmixIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> unmix(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	template <typename T>
	[[nodiscard]] Point<T> unmixOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> unmixInOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <typename T>
	[[nodiscard]] Point<T> unmixOutIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	// Angle conversion functions
	[[nodiscard]] inline Point<> anglesToDirectionRad(PointPrecision spin, PointPrecision tilt) {
		auto cTilt = std::cos(tilt);
		return { -std::sin(tilt), (std::sin(spin) * cTilt), (std::cos(spin) * cTilt) };
	}

	[[nodiscard]] inline Point<> anglesToDirection(PointPrecision spin, PointPrecision tilt) {
		return anglesToDirectionRad(toRadians(spin), toRadians(tilt));
	}

	template <typename T>
	[[nodiscard]] Point<T> toDegrees(const Point<T>& a_in) {
		return a_in * static_cast<T>(180.0 / PIE);
	}

	template <typename T>
	[[nodiscard]] Point<T> toRadians(const Point<T>& a_in) {
		return a_in * static_cast<T>(PIE / 180.0);
	}

	template <typename T>
	void toDegreesInPlace(Point<T>& a_in) {
		T amount = static_cast<T>(180.0 / PIE);
		a_in.x *= amount;
		a_in.y *= amount;
		a_in.z *= amount;
	}

	template <typename T>
	void toRadiansInPlace(Point<T>& a_in) {
		T amount = static_cast<T>(PIE / 180.0);
		a_in.x *= amount;
		a_in.y *= amount;
		a_in.z *= amount;
	}

	// Point operators
	template <typename T>
	[[nodiscard]] bool operator!=(const Point<T>& a_left, const Point<T>& a_right) {
		return !(a_left == a_right);
	}

	template <typename T>
	[[nodiscard]] Point<T> operator+(const Point<T>& a_left, const Point<T>& a_right) {
		Point<T> tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <typename T>
	[[nodiscard]] Point<T> operator-(const Point<T>& a_left, const Point<T>& a_right) {
		Point<T> tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <typename T>
	[[nodiscard]] Point<T> operator*(const Point<T>& a_left, const Point<T>& a_right) {
		Point<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] Point<T> operator*(const Point<T>& a_left, const Scale& a_right) {
		Point<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] Point<T> operator/(const Point<T>& a_left, const Point<T>& a_right) {
		auto result = a_left;
		return result /= a_right;
	}

	template <typename T>
	[[nodiscard]] Point<T> operator/(const Point<T>& a_left, const Scale& a_right) {
		auto result = a_left;
		return result /= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator+(const Point<T>& a_left, const typename Point<T>::value_type& a_right)
		-> Point<T> {
		auto result = a_left;
		return result += Point<T>(a_right, a_right, a_right);
	}

	template <typename T>
	[[nodiscard]] auto operator-(const Point<T>& a_left, const typename Point<T>::value_type& a_right)
		-> Point<T> {
		auto result = a_left;
		return result -= Point<T>(a_right, a_right, a_right);
	}

	template <typename T>
	[[nodiscard]] auto operator*(const Point<T>& a_left, const typename Point<T>::value_type& a_right)
		-> Point<T> {
		auto result = a_left;
		return result *= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator/(const Point<T>& a_left, const typename Point<T>::value_type& a_right)
		-> Point<T> {
		auto result = a_left;
		return result /= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator+(const typename Point<T>::value_type& a_left, const Point<T>& a_right)
		-> Point<T> {
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator-(const typename Point<T>::value_type& a_left, const Point<T>& a_right)
		-> Point<T> {
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator*(const typename Point<T>::value_type& a_left, const Point<T>& a_right)
		-> Point<T> {
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

	template <typename T>
	[[nodiscard]] auto operator/(const typename Point<T>::value_type& a_left, const Point<T>& a_right)
		-> Point<T> {
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint /= a_right;
	}

	template <typename T>
	std::ostream& operator<<(std::ostream& os, const Point<T>& a_point) {
		os << "(" << a_point.x << ", " << a_point.y << ", " << a_point.z << ")";
		return os;
	}

	template <typename T>
	[[nodiscard]] std::string to_string(const Point<T>& a_point) {
		std::stringstream out;
		out << a_point;
		return out.str();
	}

	template <typename T>
	std::istream& operator>>(std::istream& is, Point<T>& a_point) {
		is >> a_point.x >> a_point.y >> a_point.z;
		return is;
	}

	// Distance functions
	template<typename T>
	[[nodiscard]] auto distance(const Point<T>& a_lhs, const Point<T>& a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, PointPrecision> {
		return (a_lhs - a_rhs).magnitude();
	}

	template<typename T>
	[[nodiscard]] auto preSquareDistance(const Point<T>& a_lhs, const Point<T>& a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, PointPrecision> {
		return (a_lhs - a_rhs).preSquareMagnitude();
	}

	template <typename T>
	[[nodiscard]] auto angle2D(const Point<T>& a_lhs, const Point<T>& a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, PointPrecision> {
		return static_cast<PointPrecision>(angle2D(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y));
	}

	template <typename T>
	[[nodiscard]] auto angle2DRad(const Point<T>& a_lhs, const Point<T>& a_rhs)
		-> std::enable_if_t<is_arithmetic_v<T>, PointPrecision> {
		return static_cast<PointPrecision>(angle2DRad(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y));
	}

	[[nodiscard]] inline Point<> moveToward(const Point<>& a_start, const Point<>& a_goal, PointPrecision magnitude) {
		auto delta = a_goal - a_start;
		if (magnitude >= delta.magnitude()) {
			return a_goal;
		}
		return a_start + (delta.normalized() * magnitude);
	}

	// Color member function implementations
	template<typename T>
	auto Color::operator*=(T a_other) -> std::enable_if_t<is_arithmetic_v<T>, Color&> {
		float value = static_cast<float>(a_other);
		R *= value;
		G *= value;
		B *= value;
		A *= value;
		return *this;
	}

	template<typename T>
	auto Color::operator/=(T a_other) -> std::enable_if_t<is_arithmetic_v<T>, Color&> {
		if (a_other != 0) {
			float value = static_cast<float>(a_other);
			R /= value;
			G /= value;
			B /= value;
			A /= value;
		}
		return *this;
	}

} // namespace MV

#endif // _MV_POINTS_H_
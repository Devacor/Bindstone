#ifndef _MV_POINTS_H_
#define _MV_POINTS_H_

#include <ostream>
#include <istream>
#include <type_traits>

#include "MV/Utility/generalUtility.h"
#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

namespace MV {
	typedef float PointPrecision;

	template<typename T>
	using enable_if_arithmetic_t = std::enable_if_t<std::is_arithmetic_v<T>>;

	class DrawPoint;

	class TexturePoint{
	public:
		TexturePoint(PointPrecision a_textureX, PointPrecision a_textureY):textureX(a_textureX), textureY(a_textureY){}
		TexturePoint():textureX(0.0),textureY(0.0){}
		~TexturePoint(){}

		TexturePoint& operator=(const TexturePoint& a_other);
		TexturePoint& operator=(const DrawPoint& a_other);

		bool operator==(const TexturePoint& a_other) const{
			return equals(textureX, a_other.textureX) && equals(textureY, a_other.textureY);
		}
		bool operator!=(const TexturePoint& a_other) const{
			return !(*this == a_other);
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(textureX), CEREAL_NVP(textureY));
		}

		PointPrecision textureX, textureY;
	};

	class Color{
	public:
		Color():R(1.0f), G(1.0f), B(1.0f), A(1.0f){}
		//Due to being unable to distinguish between 0x000000 and 0x00000000 we default the 00 bits in the alpha channel to 1.0.
		//allowFullAlpha overrides this behavior so that if the alpha channel is 00 it is read as 00 instead as FF.
		Color(uint32_t a_hex, bool a_allowFullAlpha = false);

		Color(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0f);
		Color(int a_Red, int a_Green, int a_Blue, int a_Alpha = 255);
		Color(int64_t a_Red, int64_t a_Green, int64_t a_Blue, int64_t a_Alpha = 255);

		~Color(){}

		Color& operator=(const Color& a_other);
		Color& operator=(const DrawPoint& a_other);

		bool operator==(const Color& a_other) const;
		bool operator!=(const Color& a_other) const;

		Color& operator+=(const Color& a_other);
		Color& operator-=(const Color& a_other);
		Color& operator*=(const Color& a_other);
		Color& operator/=(const Color& a_other);

		template<typename T>
		std::enable_if_t<std::is_arithmetic_v<T>, Color&>
		operator*=(T a_other);

		template<typename T>
		std::enable_if_t<std::is_arithmetic_v<T>, Color&>
		operator/=(T a_other);

		template <class Archive>
		void serialize(Archive & archive){
			normalize();
			archive(CEREAL_NVP(R), CEREAL_NVP(G), CEREAL_NVP(B), CEREAL_NVP(A));
			normalize();
		}

		uint32_t hex() const;
		Color& hex(uint32_t a_hex, bool a_allowFullAlpha = false); //to maintain parity with the hex get.

		struct HSV {
			HSV(float a_Hue = 0.0f, float a_Saturation = 1.0f, float a_Value = 1.0f, float a_Alpha = 1.0f) :
				H(a_Hue), S(a_Saturation), V(a_Value), A(a_Alpha) {
			}
			float percentHue() const {
				return H / 360.0f;
			}
			float invertedPercentHue() const {
				return (360.0f - H) / 360.0f;
			}
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

		HSV hsv() const;
		HSV getHsv(HSV a_resultInput) const;
		Color& hsv(HSV a_hsv);

		Color& set(uint32_t a_hex, bool a_allowFullAlpha = false); //option to get back a Color&
		Color& set(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0f);
		Color& set(int a_Red, int a_Green, int a_Blue, int a_Alpha = 255);
		Color& set(int64_t a_Red, int64_t a_Green, int64_t a_Blue, int64_t a_Alpha = 255);
		Color& set(HSV a_hsv);

		float R, G, B, A;

		void normalize();
	private:
		float validColorRange(float a_color){
			if(a_color > 1.0f){a_color = 1.0f;}
			if(a_color < 0.0f){a_color = 0.0f;}
			return a_color;
		}
	};

	class Scale;

	template <class T = PointPrecision>
	class Size{
	public:
		Size():width(0), height(0), depth(0){}
		Size(T a_width, T a_height, T a_depth = 0):width(a_width),height(a_height),depth(a_depth){}

		Size<T>& set(T a_width, T a_height){
			width = a_width;
			height = a_height;
			return *this;
		}

		Size<T>& set(T a_width, T a_height, T a_depth){
			set(a_width, a_height);
			depth = a_depth;
			return *this;
		}

		bool contains(const Size<T>& a_other, bool a_useDepth = false) const{
			return width >= a_other.width && height >= a_other.height && (!a_useDepth || depth >= a_other.depth);
		}

		T area(bool a_useDepth = false) const{
			return width * height * (a_useDepth ? depth : 1);
		}

		Size<T>& operator+=(const Size<T>& a_other);
		Size<T>& operator-=(const Size<T>& a_other);
		Size<T>& operator*=(const Size<T>& a_other);
		Size<T>& operator/=(const Size<T>& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               Size<T>& operator+=(const T& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               Size<T>& operator-=(const T& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               Size<T>& operator*=(const T& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               Size<T>& operator/=(const T& a_other);
		Size<T>& operator*=(const Scale& a_other);
		Size<T>& operator/=(const Scale& a_other);

		bool operator<(const Size<T>& a_other){
			return (width + height) < (a_other.width + a_other.height);
		}
		bool operator>(const Size<T>& a_other){
			return (width + height) > (a_other.width + a_other.height);
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(width), CEREAL_NVP(height), CEREAL_NVP(depth));
		}

		T width, height;
		T depth; //may be ignored for most 2d concepts
	};

	template <class T = PointPrecision>
	class Point{
	public:
		Point():x(0),y(0),z(0){}
		Point(T a_xPos, T a_yPos, T a_zPos = 0):x(a_xPos),y(a_yPos),z(a_zPos){}

		static constexpr Point<T> One() {
			return { 1.0, 1.0, 1.0 };
		}

		inline void clear();

		inline Point<T>& set(T a_xPos, T a_yPos) {
			x = a_xPos;
			y = a_yPos;
			return *this;
		}
		inline Point<T>& set(T a_xPos, T a_yPos, T a_zPos) {
			x = a_xPos;
			y = a_yPos;
			z = a_zPos;
			return *this;
		}

		inline Point<T>& scale(T a_amount);
		inline Point<T>& locate(T a_xPos, T a_yPos, T a_zPos);
		inline Point<T>& locate(T a_xPos, T a_yPos);
		inline Point<T>& translate(T a_xAmount, T a_yAmount, T a_zAmount);
		inline Point<T>& translate(T a_xAmount, T a_yAmount);

		inline bool atOrigin() const { return equals<T>(x, 0) && equals<T>(y, 0) && equals<T>(z, 0); }

		inline Point<T> ignoreZ() const {
			return Point<T>(x, y);
		}

		inline Point<T>& operator+=(const Point<T>& a_other);
		inline Point<T>& operator-=(const Point<T>& a_other);
		inline Point<T>& operator*=(const Point<T>& a_other);
		inline Point<T>& operator/=(const Point<T>& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               inline Point<T>& operator*=(const T& a_other);
               template<typename U = T, typename = enable_if_arithmetic_t<U>>
               inline Point<T>& operator/=(const T& a_other);
		inline Point<T>& operator*=(const Scale& a_other);
		inline Point<T>& operator/=(const Scale& a_other);

		inline bool operator==(const Point<T>& a_other) const{
			return equals(x, a_other.x) && equals(y, a_other.y) && equals(z, a_other.z);
		}
		inline bool operator==(const T &a_other) const{
			return equals(x, a_other) && equals(y, a_other) && equals(z, a_other);
		}

		inline bool operator!=(const Point<T>& a_other) const{
			return !(*this == a_other);
		}
		inline bool operator!=(const T &a_other) const{
			return !(*this == a_other);
		}

		inline bool operator<(const Point<T>& a_other) const{
			return (x + y) < (a_other.x + a_other.y);
		}
		inline bool operator>(const Point<T>& a_other) const{
			return (x + y) > (a_other.x + a_other.y);
		}

		inline float& operator()(size_t a_index) {
			return *(&x + a_index);
		}
		inline float operator()(size_t a_index) const {
			return *(&x + a_index);
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(z));
		}

		inline Point<PointPrecision> normalized() const {
			PointPrecision length = magnitude();
			if (length == 0) { length = 1; }
			return Point<PointPrecision>(static_cast<PointPrecision>(x) / length, static_cast<PointPrecision>(y) / length, static_cast<PointPrecision>(z) / length);
		}

		//useful for sorting where you don't care exactly about the distance, just the relative distance.
		inline PointPrecision preSquareMagnitude() const {
			return static_cast<PointPrecision>((x * x) + (y * y) + (z * z));
		}

		inline PointPrecision magnitude() const {
			return sqrt(preSquareMagnitude());
		}

		T x, y, z;
	};

	typedef Point<> AxisAngles;
	typedef Point<> AxisMagnitude;

	class Scale{
	public:
		Scale():x(1.0f), y(1.0f), z(1.0f){}
		Scale(PointPrecision a_scale):x(a_scale), y(a_scale), z(a_scale){}
		Scale(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 1):x(a_x), y(a_y), z(a_z){}

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
		void serialize(Archive & archive){
			archive(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(z));
		}
		PointPrecision x, y, z;
	};

	Point<PointPrecision> toPoint(const Scale &a_scale);
	Size<PointPrecision> toSize(const Scale &a_scale);

	template <typename T>
	Scale toScale(const Point<T> &a_point){
		return{static_cast<PointPrecision>(a_point.x), static_cast<PointPrecision>(a_point.y), static_cast<PointPrecision>(a_point.z)};
	}
	template <typename T>
	Scale toScale(const Size<T> &a_point){
		return {static_cast<PointPrecision>(a_point.width), static_cast<PointPrecision>(a_point.height), static_cast<PointPrecision>(a_point.depth)};
	}

	Scale operator+(Scale a_lhs, const Scale &a_rhs);
	Scale operator+(Scale a_lhs, PointPrecision a_rhs);
	Scale operator+(PointPrecision a_lhs, Scale a_rhs);
	Scale operator-(Scale a_lhs, const Scale &a_rhs);
	Scale operator-(Scale a_lhs, PointPrecision a_rhs);
	Scale operator-(PointPrecision a_lhs, Scale a_rhs);
	Scale operator*(Scale a_lhs, const Scale &a_rhs);
	Scale operator*(Scale a_lhs, PointPrecision a_rhs);
	Scale operator*(PointPrecision a_lhs, Scale a_rhs);
	Scale operator/(Scale a_lhs, const Scale &a_rhs);
	Scale operator/(Scale a_lhs, PointPrecision a_rhs);
	Scale operator/(PointPrecision a_lhs, Scale a_rhs);

	inline std::ostream& operator<<(std::ostream& os, const Scale& a_scale) {
		os << "(x: " << a_scale.x << ", y: " << a_scale.y << ", z: " << a_scale.z << ")";
		return os;
	}

	inline std::istream& operator>>(std::istream& is, Scale& a_scale) {
		is >> a_scale.x >> a_scale.y >> a_scale.z;
		return is;
	}

	inline Scale mix(Scale a_start, Scale a_end, float a_percent) {
		return {
			mix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent),
			mix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent),
			mix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent)
		};
	}

	inline Scale unmix(Scale a_start, Scale a_end, float a_percent) {
		return {
			unmix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent),
			unmix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent),
			unmix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent)
		};
	}

	inline Scale mixIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale mix(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	inline Scale mixOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale mixInOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale mixOutIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			mixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale unmixIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}
	inline Scale unmix(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	inline Scale unmixOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale unmixInOut(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	inline Scale unmixOutIn(Scale a_start, Scale a_end, float a_percent, float a_strength) {
		return {
			unmixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength)
		};
	}

	class DrawPoint : public Point<>, public Color, public TexturePoint{
	public:
		DrawPoint(){}
		DrawPoint(PointPrecision a_xPos, PointPrecision a_yPos, PointPrecision a_zPos = 0.0){ locate(a_xPos, a_yPos, a_zPos); }
		DrawPoint(const Point<>& a_position, const Color& a_color = Color(), const TexturePoint &a_texture = TexturePoint()):
			Point<>(a_position),
			Color(a_color),
			TexturePoint(a_texture){	
		}
		DrawPoint(const Point<>& a_position, const TexturePoint &a_texture) :
			Point<>(a_position),
			TexturePoint(a_texture) {
		}
		~DrawPoint(){}

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

		void copyPosition(DrawPoint& a_other);
		void copyTexture(DrawPoint& a_other);
		void copyColor(DrawPoint& a_other);

		Point<> point() const{
			return Point<>(x, y, z);
		}

		Color color() const{
			return Color(R, G, B, A);
		}

		TexturePoint texturePoint() const{
			return TexturePoint(textureX, textureY);
		}
		template <class Archive>
		void serialize(Archive & archive){
			Point::serialize(archive);
			Color::serialize(archive);
			TexturePoint::serialize(archive);
		}
	};

	const DrawPoint operator+(const DrawPoint& a_left, const DrawPoint & a_right);
	const DrawPoint operator-(const DrawPoint& a_left, const DrawPoint & a_right);
	const DrawPoint operator+(const DrawPoint& a_left, const Point<> & a_right);
	const DrawPoint operator-(const DrawPoint& a_left, const Point<> & a_right);
	const DrawPoint operator+(const DrawPoint& a_left, const Color & a_right);
	const DrawPoint operator-(const DrawPoint& a_left, const Color & a_right);
	const DrawPoint operator+(const DrawPoint& a_left, const TexturePoint & a_right);
	const DrawPoint operator-(const DrawPoint& a_left, const TexturePoint & a_right);

	const DrawPoint operator+(const Point<>& a_left, const DrawPoint & a_right);
	const DrawPoint operator-(const Point<>& a_left, const DrawPoint & a_right);
	const DrawPoint operator+(const Color& a_left, const DrawPoint & a_right);
	const DrawPoint operator-(const Color& a_left, const DrawPoint & a_right);
	const DrawPoint operator+(const TexturePoint& a_left, const DrawPoint & a_right);
	const DrawPoint operator-(const TexturePoint& a_left, const DrawPoint & a_right);

	/**************************\
	| -------Conversion------- |
	\**************************/

	template <class T>
	Size<T> toSize(const Point<T>& a_point){
		return Size<T>{a_point.x, a_point.y, a_point.z};
	}
	template <class T>
	Point<T> toPoint(const Size<T>& a_size){
		return Point<T>{a_size.width, a_size.height, a_size.depth};
	}


	/************************\
	| ------Color IMP------- |
	\************************/

	std::ostream& operator<<(std::ostream& os, const Color& a_color);

	Color operator+(const Color &a_lhs, const Color &a_rhs);
	Color operator-(const Color &a_lhs, const Color &a_rhs);

	Color operator/(const Color &a_lhs, const Color &a_rhs);
	Color operator*(const Color &a_lhs, const Color &a_rhs);

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator*(const Color& a_lhs, T a_rhs) {
		Color result = a_lhs;
		return result *= a_rhs;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator/(const Color& a_lhs, T a_rhs) {
		Color result = a_lhs;
		return result /= a_rhs;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator+(const Color& a_left, const T& a_right) {
		Color tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator-(const Color& a_left, const T& a_right) {
		Color tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator+(const T& a_left, const Color& a_right) {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint += a_right;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator-(const T& a_left, const Color& a_right) {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint -= a_right;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator*(const T& a_left, const Color& a_right) {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint *= a_right;
	}

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>, Color>
		operator/(const T& a_left, const Color& a_right) {
		auto value = static_cast<float>(a_left);
		Color tmpPoint = Color(value, value, value, value);
		return tmpPoint /= a_right;
	}

	inline Color mix(Color a_start, Color a_end, float a_percent) {
		return {
			mix(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent),
			mix(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent),
			mix(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent),
			mix(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent)
		};
	}

	inline Color unmix(Color a_start, Color a_end, float a_percent) {
		return {
			unmix(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent),
			unmix(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent),
			unmix(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent),
			unmix(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent)
		};
	}

	inline Color mixIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color mix(Color a_start, Color a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	inline Color mixOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color mixInOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixInOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixInOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color mixOutIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			mixOutIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			mixOutIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color unmixIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}
	inline Color unmix(Color a_start, Color a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	inline Color unmixOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color unmixInOut(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixInOut(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixInOut(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	inline Color unmixOutIn(Color a_start, Color a_end, float a_percent, float a_strength) {
		return {
			unmixOutIn(static_cast<float>(a_start.R), static_cast<float>(a_end.R), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.G), static_cast<float>(a_end.G), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.B), static_cast<float>(a_end.B), a_percent, a_strength),
			unmixOutIn(static_cast<float>(a_start.A), static_cast<float>(a_end.A), a_percent, a_strength)
		};
	}

	/************************\
	| -------Size IMP------- |
	\************************/

template <typename T, typename>
Size<T> fitAspect(Size<T> a_toConstrain, Size<T> a_maximum){
		auto ratio = std::min(static_cast<double>(a_maximum.width) / static_cast<double>(a_toConstrain.width), static_cast<double>(a_maximum.height) / static_cast<double>(a_toConstrain.height));
		return {static_cast<T>(static_cast<double>(a_toConstrain.width) * ratio), static_cast<T>(static_cast<double>(a_toConstrain.height) * ratio)};
	}

template <typename T, typename>
Size<T> size(T a_width, T a_height){
		return Size<T>{a_width, a_height};
	}

template <typename T, typename>
Size<T> size(T a_width, T a_height, T a_depth) {
		return Size<T>{a_width, a_height, a_depth};
	}

	template <class Target, class Origin>
	inline Size<Target> cast(const Size<Origin> &a_point) {
		return Size<Target>{static_cast<Target>(a_point.width), static_cast<Target>(a_point.height), static_cast<Target>(a_point.depth)};
	}

	template <class Target, class Origin>
	inline Size<Target> round(const Size<Origin> &a_size){
		return Size<Target>(static_cast<Target>(std::round(a_size.width)), static_cast<Target>(std::round(a_size.height)), static_cast<Target>(std::round(a_size.depth)));
	}

	template <class T>
	Size<T>& MV::Size<T>::operator+=( const Size<T>& a_other ){
		width+=a_other.width; height+=a_other.height; depth+=a_other.depth;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator-=( const Size<T>& a_other ){
		width-=a_other.width; height-=a_other.height; depth-=a_other.depth;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator*=( const Size<T>& a_other ){
		width*=a_other.width; height*=a_other.height; depth*=a_other.depth;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator*=(const Scale& a_other){
		width = static_cast<T>(static_cast<PointPrecision>(width) * a_other.x);
		height = static_cast<T>(static_cast<PointPrecision>(height)* a_other.y);
		depth = static_cast<T>(static_cast<PointPrecision>(depth)* a_other.z);
		return *this;
	}

template <class T>
template <typename U, typename>
Size<T>& MV::Size<T>::operator+=(const T& a_other){
		width += a_other; height += a_other; depth += a_other;
		return *this;
	}

template <class T>
template <typename U, typename>
Size<T>& MV::Size<T>::operator-=(const T& a_other){
		width -= a_other; height -= a_other; depth -= a_other;
		return *this;
	}

template <class T>
template <typename U, typename>
Size<T>& MV::Size<T>::operator*=(const T& a_other){
		width*=a_other; height*=a_other; depth*=a_other;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator/=(const Size<T>& a_other){
		width /= (a_other.width == 0) ? 1 : a_other.width;
		height /= (a_other.height == 0) ? 1 : a_other.height;
		depth /= (a_other.depth == 0) ? 1 : a_other.depth;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator/=(const Scale& a_other){
		width = static_cast<T>(static_cast<PointPrecision>(width) / (a_other.x == 0 ? 1.0f : a_other.x));
		height = static_cast<T>(static_cast<PointPrecision>(height) / (a_other.y == 0 ? 1.0f : a_other.y));
		depth = static_cast<T>(static_cast<PointPrecision>(depth) / (a_other.z == 0 ? 1.0f : a_other.z));
		return *this;
	}

template <class T>
template <typename U, typename>
Size<T>& MV::Size<T>::operator/=(const T& a_other){
		if(a_other != 0){
			width/=a_other; height/=a_other; depth/=a_other;
		}else{
			std::cerr << "ERROR: Size<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	template <class T>
	bool operator==(const Size<T>& a_left, const Size<T>& a_right){
		return equals(a_left.width, a_right.width) && equals(a_left.height, a_right.height) && equals(a_left.depth, a_right.depth);
	}

	template <class T>
	bool operator!=(const Size<T>& a_left, const Size<T>& a_right){
		return !(a_left == a_right);
	}

	template <class T>
	Size<T> operator+(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <class T>
	Size<T> operator-(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <class T>
	Size<T> operator*(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <class T>
	Size<T> operator/(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint /= a_right;
	}

	template <class T>
	Size<T> operator*(const Size<T>& a_left, const Scale& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <class T>
	Size<T> operator/(const Size<T>& a_left, const Scale& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint /= a_right;
	}

template <class T, typename>
Size<T> operator+(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint+=a_right;
	}

template <class T, typename>
Size<T> operator-(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint-=a_right;
	}

template <class T, typename>
Size<T> operator*(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

template <class T, typename>
Size<T> operator/(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
	}

template <class T, typename>
Size<T> operator+(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

template <class T, typename>
Size<T> operator-(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

template <class T, typename>
Size<T> operator*(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

template <class T, typename>
Size<T> operator/(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint /= a_right;
	}

	template <class T>
	std::ostream& operator<<(std::ostream& os, const Size<T>& a_size){
		os << "(w: " << a_size.width << ", h: " << a_size.height << ", d: " << a_size.depth << ")";
		return os;
	}

	template <class T>
	std::istream& operator>>(std::istream& is, Size<T>& a_size){
		is >> a_size.width >> a_size.height >> a_size.depth;
		return is;
	}

	template <class T>
	Size<T> mix(Size<T> a_start, Size<T> a_end, float a_percent) {
		return {
			static_cast<T>(mix(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent))
		};
	}

	template <class T>
	Size<T> unmix(Size<T> a_start, Size<T> a_end, float a_percent) {
		return {
			static_cast<T>(unmix(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent))
		};
	}

	template <class T>
	Size<T> mixIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> mix(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	template <class T>
	Size<T> mixOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> mixInOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixInOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> mixOutIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOutIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> unmixIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}
	template <class T>
	Size<T> unmix(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	template <class T>
	Size<T> unmixOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> unmixInOut(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixInOut(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	template <class T>
	Size<T> unmixOutIn(Size<T> a_start, Size<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.width), static_cast<float>(a_end.width), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.height), static_cast<float>(a_end.height), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.depth), static_cast<float>(a_end.depth), a_percent, a_strength))
		};
	}

	/*************************\
	| -------Point IMP------- |
	\*************************/

template <typename T, typename>
Point<T> point(T a_xPos, T a_yPos){
		return Point<T>{a_xPos, a_yPos};
	}

template <typename T, typename>
Point<T> point(T a_xPos, T a_yPos, T a_zPos) {
		return Point<T>{a_xPos, a_yPos, a_zPos};
	}

	template <class Target, class Origin>
	inline Point<Target> round(const Point<Origin> &a_point){
		return Point<Target>{static_cast<Target>(std::round(a_point.x)), static_cast<Target>(std::round(a_point.y)), static_cast<Target>(std::round(a_point.z))};
	}

	template <class Target, class Origin>
	inline Point<Target> cast(const Point<Origin> &a_point) {
		return Point<Target>{static_cast<Target>(a_point.x), static_cast<Target>(a_point.y), static_cast<Target>(a_point.z)};
	}

	template <class T>
	void MV::Point<T>::clear(){
		x = 0; y = 0; z = 0;
	}

	template <class T>
	Point<T>& MV::Point<T>::translate( T a_xAmount, T a_yAmount, T a_zAmount ){
		x+=a_xAmount; y+=a_yAmount; z+=a_zAmount;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::translate( T a_xAmount, T a_yAmount ){
		x+=a_xAmount; y+=a_yAmount;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::locate( T a_xPos, T a_yPos, T a_zPos ){
		x = a_xPos; y = a_yPos; z = a_zPos;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::locate( T a_xPos, T a_yPos ){
		x = a_xPos; y = a_yPos;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::scale( T a_amount ){
		x*=a_amount; y*=a_amount; z*=a_amount;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator+=( const Point<T>& a_other ){
		x+=a_other.x; y+=a_other.y; z+=a_other.z;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator-=( const Point<T>& a_other ){
		x-=a_other.x; y-=a_other.y; z-=a_other.z;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator*=( const Point<T>& a_other ){
		x*=a_other.x; y*=a_other.y; z*=a_other.z;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator*=(const Scale& a_other){
		x = static_cast<T>(static_cast<PointPrecision>(x)* a_other.x);
		y = static_cast<T>(static_cast<PointPrecision>(y)* a_other.y);
		z = static_cast<T>(static_cast<PointPrecision>(z)* a_other.z);
		return *this;
	}

template <class T>
template <typename U, typename>
Point<T>& MV::Point<T>::operator*=(const T& a_other){
		x*=a_other; y*=a_other; z*=a_other;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator/=(const Point<T>& a_other){
		x /= (a_other.x != 0) ? a_other.x : 1;
		y /= (a_other.y != 0) ? a_other.y : 1;
		z /= (a_other.z != 0) ? a_other.z : 1;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator/=(const Scale& a_other){
		x = static_cast<T>(static_cast<PointPrecision>(x) / (!equals(a_other.x, 0.0f) ? a_other.x : 1));
		y = static_cast<T>(static_cast<PointPrecision>(y) / (!equals(a_other.y, 0.0f) ? a_other.y : 1));
		z = static_cast<T>(static_cast<PointPrecision>(z) / (!equals(a_other.z, 0.0f) ? a_other.z : 1));
		return *this;
	}

template <class T>
template <typename U, typename>
Point<T>& MV::Point<T>::operator/=(const T& a_other){
		if(a_other != 0){
			x/=a_other; y/=a_other; z/=a_other;
		}else{
			std::cerr << "ERROR: Point<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	template <class T>
	Point<T> mix(Point<T> a_start, Point<T> a_end, float a_percent) {
		return {
			static_cast<T>(mix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent)),
			static_cast<T>(mix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent))
		};
	}

	template <class T>
	Point<T> unmix(Point<T> a_start, Point<T> a_end, float a_percent) {
		return {
			static_cast<T>(unmix(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent)),
			static_cast<T>(unmix(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent))
		};
	}

	template <class T>
	Point<T> mixIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> mix(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	template <class T>
	Point<T> mixOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> mixInOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> mixOutIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(mixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(mixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> unmixIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}
	template <class T>
	Point<T> unmix(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return unmixIn(a_start, a_end, a_percent, a_strength);
	}

	template <class T>
	Point<T> unmixOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> unmixInOut(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixInOut(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixInOut(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	template <class T>
	Point<T> unmixOutIn(Point<T> a_start, Point<T> a_end, float a_percent, float a_strength) {
		return {
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.x), static_cast<float>(a_end.x), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.y), static_cast<float>(a_end.y), a_percent, a_strength)),
			static_cast<T>(unmixOutIn(static_cast<float>(a_start.z), static_cast<float>(a_end.z), a_percent, a_strength))
		};
	}

	inline MV::Point<> anglesToDirectionRad(MV::PointPrecision spin, MV::PointPrecision tilt) {
		auto cTilt = cos(tilt);
		return { -sin(tilt), (sin(spin)*cTilt), (cos(spin)*cTilt) };
	}

	inline MV::Point<> anglesToDirection(MV::PointPrecision spin, MV::PointPrecision tilt) {
		return anglesToDirectionRad(toRadians(spin), toRadians(tilt));
	}

	template <class T>
	Point<T> toDegrees(const Point<T> &a_in) {
		return a_in * static_cast<T>(180.0 / PIE);
	}

	template <class T>
	Point<T> toRadians(const Point<T> &a_in) {
		return a_in * static_cast<T>(PIE / 180.0);
	}

	template <class T>
	void toDegreesInPlace(Point<T> &a_in) {
		T amount = static_cast<T>(180.0 / PIE);
		a_in.x *= amount;
		a_in.y *= amount;
		a_in.z *= amount;
	}

	template <class T>
	void toRadiansInPlace(Point<T> &a_in) {
		T amount = static_cast<T>(PIE / 180.0);
		a_in.x *= amount;
		a_in.y *= amount;
		a_in.z *= amount;
	}

	template <class T>
	bool operator!=(const Point<T>& a_left, const Point<T>& a_right){
		return !(a_left == a_right);
	}

	template <class T>
	Point<T> operator+(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <class T>
	Point<T> operator-(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <class T>
	Point<T> operator*(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <class T>
	Point<T> operator*(const Point<T>& a_left, const Scale& a_right) {
		Point<T> tmpPoint = a_left;
		return tmpPoint *= a_right;
	}

	template <class T>
	Point<T> operator/(const Point<T> &a_left, const Point<T>& a_right){
		auto result = a_left;
		return result /= a_right;
	}

	template <class T>
	Point<T> operator/(const Point<T> &a_left, const Scale& a_right) {
		auto result = a_left;
		return result /= a_right;
	}

template <class T, typename>
Point<T> operator+(const Point<T> &a_left, const T& a_right){
		auto result = a_left;
		return result +=Point<T>(a_right, a_right, a_right);
	}

template <class T, typename>
Point<T> operator-(const Point<T> &a_left, const T& a_right){
		auto result = a_left;
		return result-=Point<T>(a_right, a_right, a_right);
	}

template <class T, typename>
Point<T> operator*(const Point<T> &a_left, const T& a_right){
		auto result = a_left;
		return result *=Point<T>(a_right, a_right, a_right);
	}

template <class T, typename>
Point<T> operator/(const Point<T> &a_left, const T& a_right){
		auto result = a_left;
		return result/=Point<T>(a_right, a_right, a_right);
	}

template <class T, typename>
Point<T> operator+(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

template <class T, typename>
Point<T> operator-(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

template <class T, typename>
Point<T> operator*(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

template <class T, typename>
const Point<T> operator/(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint /= a_right;
	}

	template <class T>
	std::ostream& operator<<(std::ostream& os, const Point<T>& a_point){
		os << "(" << a_point.x << ", " << a_point.y << ", " << a_point.z << ")";
		return os;
	}

	template <class T>
	std::string to_string(const MV::Point<T> &a_point) {
		std::stringstream out;
		out << a_point;
		return out.str();
	}

	template <class T>
	std::istream& operator>>(std::istream& is, Point<T> &a_point){
		is >> a_point.x >> a_point.y >> a_point.z;
		return is;
	}

template<typename T, typename>
PointPrecision distance(const Point<T> &a_lhs, const Point<T> &a_rhs) {
		return (a_lhs - a_rhs).magnitude();
	}

template<typename T, typename>
PointPrecision preSquareDistance(const Point<T> &a_lhs, const Point<T> &a_rhs) {
		return (a_lhs - a_rhs).preSquareMagnitude();
	}

template <typename T, typename>
PointPrecision angle2D(const Point<T> &a_lhs, const Point<T> &a_rhs) {
		return static_cast<PointPrecision>(angle2D(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y));
	}

template <typename T, typename>
PointPrecision angle2DRad(const Point<T> &a_lhs, const Point<T> &a_rhs) {
		return static_cast<PointPrecision>(angle2DRad(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y));
	}

	static inline Point<> moveToward(const Point<> &a_start, const Point<> &a_goal, PointPrecision magnitude) {
		auto delta = a_goal - a_start;
		if (magnitude >= delta.magnitude()) {
			return a_goal;
		}
		return a_start + (delta.normalized() * magnitude);
	}
}
#endif

#ifndef _MV_POINTS_H_
#define _MV_POINTS_H_

#include <ostream>
#include <istream>

#include "Utility/generalUtility.h"
#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

namespace MV {

	typedef float PointPrecision;

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
		Color& operator*=(T a_other);
		template<typename T>
		Color& operator/=(T a_other);

		template <class Archive>
		void serialize(Archive & archive){
			normalize();
			archive(CEREAL_NVP(R), CEREAL_NVP(G), CEREAL_NVP(B), CEREAL_NVP(A));
			normalize();
		}

		uint32_t hex() const;
		Color& hex(uint32_t a_hex, bool a_allowFullAlpha = false); //to maintain parity with the hex get.
		
		Color& set(uint32_t a_hex, bool a_allowFullAlpha = false); //option to get back a Color&
		Color& set(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0f);

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

		template <class T2>
		Size(const Size<T2> &a_size):width(a_size.width), height(a_size.height), depth(a_size.depth){}

		void set(T a_width, T a_height){
			width = a_width;
			height = a_height;
		}

		void set(T a_width, T a_height, T a_depth){
			set(a_width, a_height);
			depth = a_depth;
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
		Size<T>& operator+=(const T& a_other);
		Size<T>& operator-=(const T& a_other);
		Size<T>& operator*=(const T& a_other);
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

		void clear();

		Point<T>& scale(T a_amount);
		Point<T>& locate(T a_xPos, T a_yPos, T a_zPos);
		Point<T>& locate(T a_xPos, T a_yPos);
		Point<T>& translate(T a_xAmount, T a_yAmount, T a_zAmount);
		Point<T>& translate(T a_xAmount, T a_yAmount);

		bool atOrigin() const{ return equals<T>(x, 0) && equals<T>(y, 0) && equals<T>(z, 0); }

		Point<T>& operator+=(const Point<T>& a_other);
		Point<T>& operator-=(const Point<T>& a_other);
		Point<T>& operator*=(const Point<T>& a_other);
		Point<T>& operator/=(const Point<T>& a_other);
		Point<T>& operator*=(const T& a_other);
		Point<T>& operator/=(const T& a_other);
		Point<T>& operator*=(const Scale& a_other);
		Point<T>& operator/=(const Scale& a_other);

		bool operator==(const Point<T>& a_other){
			return equals(x, a_other.x) && equals(y, a_other.y) && equals(z, a_other.z);
		}
		bool operator==(T a_other){
			return equals(x, a_other) && equals(y, a_other) && equals(z, a_other);
		}

		bool operator!=(const Point<T>& a_other){
			return !(*this == a_other);
		}
		bool operator!=(T a_other){
			return !(*this == a_other);
		}

		bool operator<(const Size<T>& a_other){
			return (x + y) < (a_other.x + a_other.y);
		}
		bool operator>(const Size<T>& a_other){
			return (x + y) > (a_other.x + a_other.y);
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(z));
		}

		T x, y, z;
	};

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

		bool operator==(const Scale& a_other);
		bool operator==(PointPrecision a_other);

		bool operator!=(const Scale& a_other);
		bool operator!=(PointPrecision a_other);

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
		return{a_point.x, a_point.y, a_point.z};
	}
	template <typename T>
	Scale toScale(const Size<T> &a_point){
		return{a_point.width, a_point.height, a_point.depth};
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

	class DrawPoint : public Point<>, public Color, public TexturePoint{
	public:
		DrawPoint(){}
		DrawPoint(PointPrecision a_xPos, PointPrecision a_yPos, PointPrecision a_zPos = 0.0){ locate(a_xPos, a_yPos, a_zPos); }
		DrawPoint(const Point<>& a_position, const Color& a_color = Color(), const TexturePoint &a_texture = TexturePoint()):
			Point<>(a_position),
			Color(a_color),
			TexturePoint(a_texture){	
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

	template <typename T>
	Color& Color::operator*=(T a_other){
		R *= static_cast<float>(a_other);
		G *= static_cast<float>(a_other);
		B *= static_cast<float>(a_other);
		A *= static_cast<float>(a_other);
		return *this;
	}

	template <typename T>
	Color& Color::operator/=(T a_other){
		R /= static_cast<float>(a_other != 0 ? a_other : 1);
		G /= static_cast<float>(a_other != 0 ? a_other : 1);
		B /= static_cast<float>(a_other != 0 ? a_other : 1);
		A /= static_cast<float>(a_other != 0 ? a_other : 1);
		return *this;
	}

	template<typename T>
	Color& operator*(const Color &a_lhs, T a_rhs){
		Color result = a_lhs;
		return result *= a_rhs;
	}
	template<typename T>
	Color& operator/(const Color &a_lhs, T a_rhs){
		Color result = a_lhs;
		return result *= a_rhs;
	}

	template <class T>
	Color operator+(const Color& a_left, const T& a_right){
		Color tmpPoint = a_left;
		return tmpPoint += a_right;
	}

	template <class T>
	Color operator-(const Color& a_left, const T& a_right){
		Color tmpPoint = a_left;
		return tmpPoint -= a_right;
	}

	template <class T>
	Color operator+(const T& a_left, const Color& a_right){
		Color tmpPoint = Color(a_left, a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

	template <class T>
	Color operator-(const T& a_left, const Color& a_right){
		Color tmpPoint = Color(a_left, a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

	template <class T>
	Color operator*(const T& a_left, const Color& a_right){
		Color tmpPoint = Color(a_left, a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

	template <class T>
	Color operator/(const T& a_left, const Color& a_right){
		Color tmpPoint = Color(a_left, a_left, a_left, a_left);
		return tmpPoint /= a_right;
	}

	/************************\
	| -------Size IMP------- |
	\************************/

	template <typename T>
	Size<T> size(T a_width, T a_height, T a_depth = 0){
		return Size<T>{a_width, a_height, a_depth};
	}

	template <class Target, class Origin>
	Size<Target> cast(const Size<Origin> &a_size){
		return Size<Target>(static_cast<Target>(a_size.width), static_cast<Target>(a_size.height), static_cast<Target>(a_size.depth));
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
	Size<T>& MV::Size<T>::operator+=(const T& a_other){
		width += a_other; height += a_other; depth += a_other;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator-=(const T& a_other){
		width -= a_other.width; height -= a_other.height; depth -= a_other.depth;
		return *this;
	}

	template <class T>
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
		width = static_cast<T>(static_cast<PointPrecision>(width) / (a_other.x == 0) ? 1 : a_other.x);
		height = static_cast<T>(static_cast<PointPrecision>(height) / (a_other.y == 0) ? 1 : a_other.y);
		depth = static_cast<T>(static_cast<PointPrecision>(depth) / (a_other.z == 0) ? 1 : a_other.z);
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator/=(const T& a_other){
		if(a_other != 0){
			width/=a_other; height/=a_other; depth/=a_other;
		}else{
			std::cerr << "ERROR: Size<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	template <class T>
	const bool operator==(const Size<T>& a_left, const Size<T>& a_right){
		return equals(a_left.width, a_right.width) && equals(a_left.height, a_right.height) && equals(a_left.depth, a_right.depth);
	}

	template <class T>
	const bool operator!=(const Size<T>& a_left, const Size<T>& a_right){
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

	template <class T>
	Size<T> operator+(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint+=a_right;
	}

	template <class T>
	Size<T> operator-(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint-=a_right;
	}

	template <class T>
	Size<T> operator*(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

	template <class T>
	Size<T> operator/(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
	}

	template <class T>
	Size<T> operator+(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

	template <class T>
	Size<T> operator-(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

	template <class T>
	Size<T> operator*(const T& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = Size<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

	template <class T>
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

	/*************************\
	| -------Point IMP------- |
	\*************************/

	template <typename T>
	Point<T> point(T a_xPos, T a_yPos, T a_zPos = 0){
		return Point<T>{a_xPos, a_yPos, a_zPos};
	}

	template <class Target, class Origin>
	Point<Target> cast(const Point<Origin> &a_point){
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
		x = static_cast<T>(static_cast<PointPrecision>(x) / (a_other.x != 0) ? a_other.x : 1);
		y = static_cast<T>(static_cast<PointPrecision>(y) / (a_other.y != 0) ? a_other.y : 1);
		z = static_cast<T>(static_cast<PointPrecision>(z) / (a_other.z != 0) ? a_other.z : 1);
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator/=(const T& a_other){
		if(a_other != 0){
			x/=a_other; y/=a_other; z/=a_other;
		}else{
			std::cerr << "ERROR: Point<T> operator/=(const T& a_other) divide by 0!" << std::endl;
		}
		return *this;
	}

	template <class T>
	bool operator==(const Point<T>& a_left, const Point<T>& a_right){
		return equals(a_left.x, a_right.x) && equals(a_left.y, a_right.y) && equals(a_left.z, a_right.z);
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
	const Point<T> operator/(Point<T> a_left, const Point<T>& a_right){
		return a_left /= a_right;
	}

	template <class T>
	Point<T> operator+(Point<T> a_left, const T& a_right){
		return a_left+=a_right;
	}

	template <class T>
	Point<T> operator-(Point<T> a_left, const T& a_right){
		return a_left-=a_right;
	}

	template <class T>
	Point<T> operator*(Point<T> a_left, const T& a_right){
		return a_left*=a_right;
	}

	template <class T>
	const Point<T> operator/(Point<T> a_left, const T& a_right){
		return a_left/=a_right;
	}

	template <class T>
	Point<T> operator+(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint += a_right;
	}

	template <class T>
	Point<T> operator-(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint -= a_right;
	}

	template <class T>
	Point<T> operator*(const T& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = Point<T>(a_left, a_left, a_left);
		return tmpPoint *= a_right;
	}

	template <class T>
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
	std::istream& operator>>(std::istream& is, Point<T> &a_point){
		is >> a_point.x >> a_point.y >> a_point.z;
		return is;
	}
}
#endif

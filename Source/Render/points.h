#ifndef __POINTS_H__
#define __POINTS_H__

#include <ostream>
#include <istream>

namespace MV {

	class DrawPoint;

	class TexturePoint{
	public:
		TexturePoint():textureX(0.0),textureY(0.0){}
		~TexturePoint(){}

		TexturePoint& operator=(const TexturePoint& a_other);
		TexturePoint& operator=(const DrawPoint& a_other);

		double textureX, textureY;
	};

	class Color{
	public:
		Color():R(1.0),G(1.0),B(1.0),A(1.0){}
		Color(float a_Red, float a_Green, float a_Blue, float a_Alpha = 1.0){R = validRange(a_Red); G = validRange(a_Green); B = validRange(a_Blue); A = validRange(a_Alpha);}
		~Color(){}
		Color& operator=(const Color& a_other);
		Color& operator=(const DrawPoint& a_other);

		float R, G, B, A;
	private:
		float validRange(float a_color){
			if(a_color > 1.0){a_color = 1.0;}
			if(a_color < 0.0){a_color = 0.0;}
			return a_color;
		}
	};

	template <class T = double>
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

		Size<T>& operator+=(const Size<T>& a_other);
		Size<T>& operator-=(const Size<T>& a_other);
		Size<T>& operator*=(const Size<T>& a_other);
		Size<T>& operator/=(const Size<T>& a_other);
		Size<T>& operator*=(const T& a_other);
		Size<T>& operator/=(const T& a_other);

		T width, height;
		T depth; //may be ignored for most 2d concepts
	};

	template <class T = double>
	class Point{
	public:
		Point(){clear();}
		Point(T a_xPos, T a_yPos, T a_zPos = 0){locate(a_xPos, a_yPos, a_zPos);}
		
		void clear();
		
		Point<T>& scale(T a_amount);
		Point<T>& locate(T a_xPos, T a_yPos, T a_zPos);
		Point<T>& locate(T a_xPos, T a_yPos);
		Point<T>& translate(T a_xAmount, T a_yAmount, T a_zAmount);
		Point<T>& translate(T a_xAmount, T a_yAmount);
		
		bool atOrigin(){return x == 0 && y == 0 && z == 0;}
		
		Point<T>& operator+=(const Point<T>& a_other);
		Point<T>& operator-=(const Point<T>& a_other);
		Point<T>& operator*=(const Point<T>& a_other);
		Point<T>& operator/=(const Point<T>& a_other);
		Point<T>& operator*=(const T& a_other);
		Point<T>& operator/=(const T& a_other);

		T x, y, z;
	};

	class DrawPoint : public Point<>, public Color, public TexturePoint{
	public:
		DrawPoint(){clear();}
		DrawPoint(double a_xPos, double a_yPos, double a_zPos = 0.0){locate(a_xPos, a_yPos, a_zPos);}
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

		void copyPosition(DrawPoint& a_other);
		void copyTexture(DrawPoint& a_other);
		void copyColor(DrawPoint& a_other);
	};

	const DrawPoint operator+(const DrawPoint& a_left, const DrawPoint & a_right);
	const DrawPoint operator-(const DrawPoint& a_left, const DrawPoint & a_right);

	/**************************\
	| -------Conversion------- |
	\**************************/


	template <class T>
	Point<T> pointFromSize(const Size<T>& a_size){
		return Point<T>{a_size.width, a_size.height, a_size.depth};
	}

	template <class T>
	Size<T> sizeFromPoint(const Point<T>& a_point){
		return Size<T>{a_point.x, a_point.y, a_point.z};
	}

	/************************\
	| -------Size IMP------- |
	\************************/

	template <typename T>
	Size<T> size(T a_width, T a_height, T a_depth = 0){
		return Size<T>{a_width, a_height, a_depth};
	}

	template <class Target, class Origin>
	Size<Target> castSize(const Size<Origin> &a_size){
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
	Size<T>& MV::Size<T>::operator*=(const T& a_other){
		width*=a_other; height*=a_other; depth*=a_other;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator/=(const Size<T>& a_other){
		width/=a_other.width; height/=a_other.height; depth/=(a_other.depth == 0)?1:a_other.depth;
		return *this;
	}

	template <class T>
	Size<T>& MV::Size<T>::operator/=(const T& a_other){
		if(a_other != 0){
			width/=a_other; height/=a_other; depth/=a_other;
		}else{
			std::cerr << "ERROR: Size<T> operator/=(const T& a_other) divide by 0!" std::endl;
		}
		return *this;
	}

	template <class T>
	const bool operator==(const Size<T>& a_left, const Size<T>& a_right){
		return (a_left.width == a_right.width) && (a_left.height == a_right.height) && (a_left.depth == a_right.depth);
	}

	template <class T>
	const bool operator!=(const Size<T>& a_left, const Size<T>& a_right){
		return !(a_left == a_right);
	}

	template <class T>
	const Size<T> operator+(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint+=a_right;
	}

	template <class T>
	const Size<T> operator-(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint-=a_right;
	}

	template <class T>
	const Size<T> operator*(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

	template <class T>
	const Size<T> operator/(const Size<T>& a_left, const Size<T>& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
	}

	template <class T>
	const Size<T> operator*(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

	template <class T>
	const Size<T> operator/(const Size<T>& a_left, const T& a_right){
		Size<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
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
	Point<Target> castPoint(const Point<Origin> &a_point){
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
	Point<T>& MV::Point<T>::operator*=(const T& a_other){
		x*=a_other; y*=a_other; z*=a_other;
		return *this;
	}

	template <class T>
	Point<T>& MV::Point<T>::operator/=(const Point<T>& a_other){
		x/=a_other.x; y/=a_other.y; z/=(a_other.z == 0)?1:a_other.z;
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
	const bool operator==(const Point<T>& a_left, const Point<T>& a_right){
		return (a_left.x == a_right.x) && (a_left.y == a_right.y) && (a_left.z == a_right.z);
	}

	template <class T>
	const bool operator!=(const Point<T>& a_left, const Point<T>& a_right){
		return !(a_left == a_right);
	}

	template <class T>
	const Point<T> operator+(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint+=a_right;
	}

	template <class T>
	const Point<T> operator-(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint-=a_right;
	}

	template <class T>
	const Point<T> operator*(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

	template <class T>
	const Point<T> operator/(const Point<T>& a_left, const Point<T>& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
	}

	template <class T>
	const Point<T> operator*(const Point<T>& a_left, const T& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint*=a_right;
	}

	template <class T>
	const Point<T> operator/(const Point<T>& a_left, const T& a_right){
		Point<T> tmpPoint = a_left;
		return tmpPoint/=a_right;
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

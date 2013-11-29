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
		Size(T a_width, T a_height, T a_depth = 0.0):width(a_width),height(a_height),depth(a_depth){}

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

		T width, height;
		T depth; //may be ignored for most 2d concepts
	};

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

	class Point{
	public:
		Point(){clear();}
		Point(double a_xPos, double a_yPos, double a_zPos = 0.0){placeAt(a_xPos, a_yPos, a_zPos);}
		
		virtual void clear();
		
		Point scale(double a_amount);
		Point placeAt(double a_xPos, double a_yPos, double a_zPos);
		Point placeAt(double a_xPos, double a_yPos);
		Point translate(double a_xAmount, double a_yAmount, double a_zAmount);
		Point translate(double a_xAmount, double a_yAmount);
		
		bool atOrigin(){return x == 0 && y == 0 && z == 0;}
		
		Point& operator+=(const Point& a_other);
		Point& operator-=(const Point& a_other);
		Point& operator*=(const Point& a_other);
		Point& operator/=(const Point& a_other);
		Point& operator*=(const double& a_other);
		Point& operator/=(const double& a_other);

		double x, y, z;
	};

	const Point operator+(const Point& a_left, const Point& a_right);
	const Point operator-(const Point& a_left, const Point& a_right);
	const Point operator*(const Point& a_left, const Point& a_right);
	const Point operator/(const Point& a_left, const Point& a_right);
	const Point operator*(const Point& a_left, const double& a_right);
	const Point operator/(const Point& a_left, const double& a_right);
	const bool operator==(const Point& a_left, const Point& a_right);
	const bool operator!=(const Point& a_left, const Point& a_right);
	std::ostream& operator<<(std::ostream& os, const Point& a_point);
	std::istream& operator>>(std::istream& is, Point& a_point);

	class DrawPoint : public Point, public Color, public TexturePoint{
	public:
		DrawPoint(){clear();}
		DrawPoint(double a_xPos, double a_yPos, double a_zPos = 0.0){placeAt(a_xPos, a_yPos, a_zPos);}
		DrawPoint(const Point& a_position, const Color& a_color = Color(), const TexturePoint &a_texture = TexturePoint()):
			Point(a_position),
			Color(a_color),
			TexturePoint(a_texture){	
		}
		~DrawPoint(){}

		virtual void clear();

		DrawPoint& operator=(const DrawPoint& a_other);
		DrawPoint& operator=(const Point& a_other);
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

	Point pointFromSize(const Size<double>& a_size);

	Size<double> sizeFromPoint(const Point& a_point);
}
#endif

#include "points.h"
namespace MV {
	/*************************\
	| ------TexturePoint----- |
	\*************************/

	TexturePoint& TexturePoint::operator=( const TexturePoint& a_other ){
		textureX = a_other.textureX; textureY = a_other.textureY;
		return *this;
	}

	TexturePoint& TexturePoint::operator=( const DrawPoint& a_other ){
		textureX = a_other.textureX; textureY = a_other.textureY;
		return *this;
	}

	/*************************\
	| ---------Color--------- |
	\*************************/

	Color& Color::operator=( const Color& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		return *this;
	}

	Color& Color::operator=( const DrawPoint& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		return *this;
	}

	/*************************\
	| -------DrawPoint------- |
	\*************************/

	DrawPoint& DrawPoint::operator=( const DrawPoint& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
		textureX = a_other.textureX; textureY = a_other.textureY;
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		return *this;
	}

	DrawPoint& DrawPoint::operator=( const Point& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
		return *this;
	}

	DrawPoint& DrawPoint::operator=( const Color& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		return *this;
	}

	DrawPoint& DrawPoint::operator=( const TexturePoint& a_other ){
		textureX = a_other.textureX; textureY = a_other.textureY;
		return *this;
	}

	const DrawPoint operator+(const DrawPoint& a_left, const DrawPoint& a_right){
		DrawPoint returnPoint = a_left;
		return returnPoint+=a_right;
	}
	const DrawPoint operator-(const DrawPoint& a_left, const DrawPoint& a_right){
		DrawPoint returnPoint = a_left;
		return returnPoint-=a_right;
	}

	DrawPoint& DrawPoint::operator+=(const DrawPoint& a_other){
		x+=a_other.x; y+=a_other.y; z+=a_other.z;
		textureX+=a_other.textureX; textureY+=a_other.textureY;
		R+=a_other.R; G+=a_other.G; B+=a_other.B; A+=a_other.A;
		return *this;
	}

	DrawPoint& DrawPoint::operator-=(const DrawPoint& a_other){
		x-=a_other.x; y-=a_other.y; z-=a_other.z;
		textureX-=a_other.textureX; textureY-=a_other.textureY;
		R-=a_other.R; G-=a_other.G; B-=a_other.B; A-=a_other.A;
		return *this;
	}

	void DrawPoint::clear(){
		x = 0; y = 0; z = 0;
		textureX = 0; textureY = 0;
		R = 1.0; G = 1.0; B = 1.0; A = 1.0;
	}

	void DrawPoint::copyPosition( DrawPoint& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
	}

	void DrawPoint::copyTexture( DrawPoint& a_other ){
		textureX = a_other.textureX; textureY = a_other.textureY;
	}

	void DrawPoint::copyColor( DrawPoint& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B;
	}

}

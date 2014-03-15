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

	Color::Color(uint32_t a_hex, bool a_readAlphaBits /*= false*/):
		A(a_readAlphaBits ? ((a_hex >> 24) & 0xFF) / 255.0f : 1.0f),
		R(((a_hex >> 16) & 0xFF) / 255.0f),
		G(((a_hex >> 8) & 0xFF) / 255.0f),
		B(((a_hex)& 0xFF) / 255.0f) {
	}

	Color& Color::operator=( const Color& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		normalize();
		return *this;
	}

	Color& Color::operator=( const DrawPoint& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		normalize();
		return *this;
	}

	bool Color::operator==(const Color& a_other) const{
		return equals(R, a_other.R) && equals(G, a_other.G) && equals(B, a_other.B) && equals(A, a_other.A);
	}
	bool Color::operator!=(const Color& a_other) const{
		return !(*this == a_other);
	}

	Color& Color::operator+=(const Color& a_other){
		R += a_other.R;
		G += a_other.G;
		B += a_other.B;
		A += a_other.A;
		normalize();
		return *this;
	}
	Color& Color::operator-=(const Color& a_other){
		R -= a_other.R;
		G -= a_other.G;
		B -= a_other.B;
		A -= a_other.A;
		normalize();
		return *this;
	}
	Color& Color::operator*=(const Color& a_other){
		R *= a_other.R;
		G *= a_other.G;
		B *= a_other.B;
		A *= a_other.A;
		normalize();
		return *this;
	}
	Color& Color::operator/=(const Color& a_other){
		R /= a_other.R;
		G /= a_other.G;
		B /= a_other.B;
		A /= a_other.A;
		normalize();
		return *this;
	}

	uint32_t Color::hex() const {
		return (static_cast<uint32_t>(round(A * static_cast<float>(0xFF))) << 24) |
			(static_cast<uint32_t>(round(R * static_cast<float>(0xFF))) << 16) |
			(static_cast<uint32_t>(round(G * static_cast<float>(0xFF))) << 8) |
			(static_cast<uint32_t>(round(B * static_cast<float>(0xFF))));
	}

	uint32_t Color::hex(uint32_t a_hex, bool a_readAlphaBits /*= false*/) {
		A = (a_readAlphaBits ? ((a_hex >> 24) & 0xFF) / 255.0f : 1.0f);
		R = (((a_hex >> 16) & 0xFF) / 255.0f);
		G = (((a_hex >> 8) & 0xFF) / 255.0f);
		B = (((a_hex)& 0xFF) / 255.0f);
		return hex();
	}

	void Color::normalize() {
		R = validColorRange(R);
		G = validColorRange(G);
		B = validColorRange(B);
		A = validColorRange(A);
	}

	Color operator+(const Color &a_lhs, const Color &a_rhs){
		Color result = a_lhs;
		return result += a_rhs;
	}

	Color operator-(const Color &a_lhs, const Color &a_rhs){
		Color result = a_lhs;
		return result -= a_rhs;
	}

	Color operator/(const Color &a_lhs, const Color &a_rhs){
		Color result = a_lhs;
		return result /= a_rhs;
	}

	Color operator*(const Color &a_lhs, const Color &a_rhs){
		Color result = a_lhs;
		return result *= a_rhs;
	}

	/*************************\
	| -------DrawPoint------- |
	\*************************/

	DrawPoint& DrawPoint::operator=( const DrawPoint& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
		textureX = a_other.textureX; textureY = a_other.textureY;
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		Color::normalize();
		return *this;
	}

	DrawPoint& DrawPoint::operator=( const Point& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
		return *this;
	}

	DrawPoint& DrawPoint::operator=( const Color& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		Color::normalize();
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
		R+=a_other.R; G+=a_other.G; B+=a_other.B; A+=a_other.A;
		Color::normalize();
		return *this;
	}

	DrawPoint& DrawPoint::operator-=(const DrawPoint& a_other){
		x-=a_other.x; y-=a_other.y; z-=a_other.z;
		R-=a_other.R; G-=a_other.G; B-=a_other.B; A-=a_other.A;
		Color::normalize();
		return *this;
	}

	void DrawPoint::clear(){
		x = 0; y = 0; z = 0;
		textureX = 0; textureY = 0;
		R = 1.0f; G = 1.0f; B = 1.0f; A = 1.0f;
	}

	void DrawPoint::copyPosition( DrawPoint& a_other ){
		x = a_other.x; y = a_other.y; z = a_other.z;
	}

	void DrawPoint::copyTexture( DrawPoint& a_other ){
		textureX = a_other.textureX; textureY = a_other.textureY;
	}

	void DrawPoint::copyColor( DrawPoint& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B;
		Color::normalize();
	}

}

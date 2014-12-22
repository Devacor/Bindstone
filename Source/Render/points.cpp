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

	std::ostream& operator<<(std::ostream& os, const Color& a_color){
		os << "(" << a_color.R << ", " << a_color.G << ", " << a_color.B << ", " << a_color.A << ")";
		return os;
	}

	Color::Color(uint32_t a_hex, bool a_allowFullAlpha):
		A((a_allowFullAlpha || (a_hex & 0xff000000)) ? ((a_hex >> 24) & 0xFF) / 255.0f : 1.0f),
		R(((a_hex >> 16) & 0xFF) / 255.0f),
		G(((a_hex >> 8) & 0xFF) / 255.0f),
		B(((a_hex)& 0xFF) / 255.0f) {
	}

	Color::Color(float a_Red, float a_Green, float a_Blue, float a_Alpha /*= 1.0f*/):
		R(a_Red),
		G(a_Green),
		B(a_Blue),
		A(a_Alpha) {
	}

	Color& Color::operator=( const Color& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
		return *this;
	}

	Color& Color::operator=( const DrawPoint& a_other ){
		R = a_other.R; G = a_other.G; B = a_other.B; A = a_other.A;
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
		return *this;
	}
	Color& Color::operator-=(const Color& a_other){
		R -= a_other.R;
		G -= a_other.G;
		B -= a_other.B;
		A -= a_other.A;
		return *this;
	}
	Color& Color::operator*=(const Color& a_other){
		R *= a_other.R;
		G *= a_other.G;
		B *= a_other.B;
		A *= a_other.A;
		return *this;
	}
	Color& Color::operator/=(const Color& a_other){
		R /= (a_other.R != 0) ? a_other.R : 1.0f;
		G /= (a_other.G != 0) ? a_other.G : 1.0f;
		B /= (a_other.B != 0) ? a_other.B : 1.0f;
		A /= (a_other.A != 0) ? a_other.A : 1.0f;
		return *this;
	}

	uint32_t Color::hex() const {
		return (static_cast<uint32_t>(std::round(A * static_cast<float>(0xFF))) << 24) |
			(static_cast<uint32_t>(std::round(R * static_cast<float>(0xFF))) << 16) |
			(static_cast<uint32_t>(std::round(G * static_cast<float>(0xFF))) << 8) |
			(static_cast<uint32_t>(std::round(B * static_cast<float>(0xFF))));
	}

	Color& Color::hex(uint32_t a_hex, bool a_allowFullAlpha) {
		A = ((a_allowFullAlpha || (a_hex & 0xff000000)) ? ((a_hex >> 24) & 0xFF) / 255.0f : 1.0f);
		R = (((a_hex >> 16) & 0xFF) / 255.0f);
		G = (((a_hex >> 8) & 0xFF) / 255.0f);
		B = (((a_hex)& 0xFF) / 255.0f);
		return *this;
	}

	Color& Color::set(uint32_t a_hex, bool a_allowFullAlpha){
		hex(a_hex, a_allowFullAlpha);
		return *this;
	}
	Color& Color::set(float a_Red, float a_Green, float a_Blue, float a_Alpha){
		R = a_Red;
		G = a_Green;
		B = a_Blue;
		A = a_Alpha;
		return *this;
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
	| ---------Scale--------- |
	\*************************/

	Scale operator+(Scale a_lhs, const Scale &a_rhs){
		return a_lhs += a_rhs;
	}

	Scale operator+(Scale a_lhs, PointPrecision a_rhs){
		return a_lhs += a_rhs;
	}

	Scale operator+(PointPrecision a_lhs, Scale a_rhs){
		return a_rhs += a_lhs;
	}

	Scale operator-(Scale a_lhs, const Scale &a_rhs){
		return a_lhs -= a_rhs;
	}

	Scale operator-(Scale a_lhs, PointPrecision a_rhs){
		return a_lhs -= a_rhs;
	}

	Scale operator-(PointPrecision a_lhs, Scale a_rhs){
		return Scale(a_lhs, a_lhs, a_lhs) -= a_rhs;
	}

	Scale operator*(Scale a_lhs, const Scale &a_rhs){
		return a_lhs *= a_rhs;
	}

	Scale operator*(Scale a_lhs, PointPrecision a_rhs){
		return a_lhs *= a_rhs;
	}

	Scale operator*(PointPrecision a_lhs, Scale a_rhs){
		return a_rhs *= a_lhs;
	}

	Scale operator/(Scale a_lhs, const Scale &a_rhs){
		return a_lhs /= a_rhs;
	}

	Scale operator/(Scale a_lhs, PointPrecision a_rhs){
		return a_lhs /= a_rhs;
	}

	Scale operator/(PointPrecision a_lhs, Scale a_rhs){
		return Scale(a_lhs, a_lhs, a_lhs) /= a_rhs;
	}


	Scale& Scale::operator+=(const Scale& a_other){
		x += a_other.x;
		y += a_other.y;
		z += a_other.z;
		return *this;
	}
	Scale& Scale::operator-=(const Scale& a_other){
		x -= a_other.x;
		y -= a_other.y;
		z -= a_other.z;
		return *this;
	}
	Scale& Scale::operator*=(const Scale& a_other){
		x *= a_other.x;
		y *= a_other.y;
		z *= a_other.z;
		return *this;
	}
	Scale& Scale::operator/=(const Scale& a_other){
		x /= (a_other.x != 0.0f) ? a_other.x : 1.0f;
		y /= (a_other.y != 0.0f) ? a_other.y : 1.0f;
		z /= (a_other.z != 0.0f) ? a_other.z : 1.0f;
		return *this;
	}
	Scale& Scale::operator+=(PointPrecision a_other){
		x += a_other;
		y += a_other;
		z += a_other;
		return *this;
	}
	Scale& Scale::operator-=(PointPrecision a_other){
		x -= a_other;
		y -= a_other;
		z -= a_other;
		return *this;
	}
	Scale& Scale::operator*=(PointPrecision a_other){
		x *= a_other;
		y *= a_other;
		z *= a_other;
		return *this;
	}
	Scale& Scale::operator/=(PointPrecision a_other){
		x /= (a_other != 0.0f) ? a_other : 1.0f;
		y /= (a_other != 0.0f) ? a_other : 1.0f;
		z /= (a_other != 0.0f) ? a_other : 1.0f;
		return *this;
	}


	bool Scale::operator==(PointPrecision a_other) const {
		return equals(x, a_other) && equals(y, a_other) && equals(z, a_other);
	}

	bool Scale::operator==(const Scale& a_other) const {
		return equals(x, a_other.x) && equals(y, a_other.y) && equals(z, a_other.z);
	}

	bool Scale::operator!=(PointPrecision a_other) const {
		return !(*this == a_other);
	}

	bool Scale::operator!=(const Scale& a_other) const {
		return !(*this == a_other);
	}

	Point<PointPrecision> toPoint(const Scale &a_scale){
		return{a_scale.x, a_scale.y, a_scale.z};
	}

	Point<PointPrecision> pointFromScale(const Scale &a_scale){
		return toPoint(a_scale);
	}

	Size<PointPrecision> toSize(const Scale &a_scale){
		return{a_scale.x, a_scale.y, a_scale.z};
	}

	Size<PointPrecision> sizeFromScale(const Scale &a_scale){
		return toSize(a_scale);
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

	DrawPoint& DrawPoint::operator+=(const Point<>& a_other){
		x += a_other.x; y += a_other.y; z += a_other.z;
		return *this;
	}
	DrawPoint& DrawPoint::operator-=(const Point<>& a_other){
		x -= a_other.x; y -= a_other.y; z -= a_other.z;
		return *this;
	}
	DrawPoint& DrawPoint::operator+=(const Color& a_other){
		R += a_other.R; G += a_other.G; B += a_other.B; A += a_other.B;
		Color::normalize();
		return *this;
	}
	DrawPoint& DrawPoint::operator-=(const Color& a_other){
		R -= a_other.R; G -= a_other.G; B -= a_other.B; A -= a_other.B;
		Color::normalize();
		return *this;
	}
	DrawPoint& DrawPoint::operator+=(const TexturePoint& a_other){
		textureX += a_other.textureX; textureY += a_other.textureY;
		return *this;
	}
	DrawPoint& DrawPoint::operator-=(const TexturePoint& a_other){
		textureX -= a_other.textureX; textureY -= a_other.textureY;
		return *this;
	}

	const DrawPoint operator+(const DrawPoint& a_left, const Point<> & a_right){
		DrawPoint result = a_left;
		return result += a_right;
	}
	const DrawPoint operator-(const DrawPoint& a_left, const Point<> & a_right){
		DrawPoint result = a_left;
		return result -= a_right;
	}
	const DrawPoint operator+(const DrawPoint& a_left, const Color & a_right){
		DrawPoint result = a_left;
		return result += a_right;
	}
	const DrawPoint operator-(const DrawPoint& a_left, const Color & a_right){
		DrawPoint result = a_left;
		return result -= a_right;
	}
	const DrawPoint operator+(const DrawPoint& a_left, const TexturePoint & a_right){
		DrawPoint result = a_left;
		return result += a_right;
	}
	const DrawPoint operator-(const DrawPoint& a_left, const TexturePoint & a_right){
		DrawPoint result = a_left;
		return result -= a_right;
	}

	const DrawPoint operator+(const Point<>& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result += a_left;
	}
	const DrawPoint operator-(const Point<>& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result -= a_left;
	}
	const DrawPoint operator+(const Color& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result += a_left;
	}
	const DrawPoint operator-(const Color& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result -= a_left;
	}
	const DrawPoint operator+(const TexturePoint& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result += a_left;
	}
	const DrawPoint operator-(const TexturePoint& a_left, const DrawPoint & a_right){
		DrawPoint result = a_right;
		return result -= a_left;
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

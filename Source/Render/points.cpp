#include "points.h"
namespace M2Rend {
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
   | ---------Point--------- |
   \*************************/

   void Point::clear(){
      x = 0; y = 0; z = 0;
   }

   bool Point::operator+=(const Point& a_other){
      x+=a_other.x; y+=a_other.y; z+=a_other.z;
      return true;
   }

   bool Point::operator-=(const Point& a_other){
      x-=a_other.x; y-=a_other.y; z-=a_other.z;
      return true;
   }

   bool Point::operator*=(const Point& a_other){
      x*=a_other.x; y*=a_other.y; z*=a_other.z;
      return true;
   }

   bool Point::operator*=(const double& a_other){
      x*=a_other; y*=a_other; z*=a_other;
      return true;
   }

   bool Point::operator/=(const Point& a_other){
      x/=a_other.x; y/=a_other.y; z/=a_other.z;
      return true;
   }

   bool Point::operator/=(const double& a_other){
      x/=a_other; y/=a_other; z/=a_other;
      return true;
   }

   bool operator==(const Point& a_left, const Point& a_right){
      return (a_left.x == a_right.x) && (a_left.y == a_right.y) && (a_left.z == a_right.z);
   }

   bool operator!=(const Point& a_left, const Point& a_right){
      return !(a_left == a_right);
   }

   Point Point::translate( double a_xAmount, double a_yAmount, double a_zAmount ){
      x+=a_xAmount; y+=a_yAmount; z+=a_zAmount;
      return *this;
   }

   Point Point::translate( double a_xAmount, double a_yAmount ){
      x+=a_xAmount; y+=a_yAmount;
      return *this;
   }

   Point Point::placeAt( double a_xPos, double a_yPos, double a_zPos ){
      x = a_xPos; y = a_yPos; z = a_zPos;
      return *this;
   }

   Point Point::placeAt( double a_xPos, double a_yPos ){
      x = a_xPos; y = a_yPos;
      return *this;
   }

   Point Point::scale( double a_amount ){
      x*=a_amount; y*=a_amount; z*=a_amount;
      return *this;
   }

   Point operator+(const Point& a_left, const Point& a_right){
      Point tmpPoint = a_left;
      tmpPoint+=a_right;
      return tmpPoint;
   }

   Point operator-(const Point& a_left, const Point& a_right){
      Point tmpPoint = a_left;
      tmpPoint-=a_right;
      return tmpPoint;
   }

   Point operator*(const Point& a_left, const Point& a_right){
      Point tmpPoint = a_left;
      tmpPoint*=a_right;
      return tmpPoint;
   }

   Point operator/(const Point& a_left, const Point& a_right){
      Point tmpPoint = a_left;
      tmpPoint/=a_right;
      return tmpPoint;
   }

   Point operator*(const Point& a_left, const double& a_right){
      Point tmpPoint = a_left;
      tmpPoint*=a_right;
      return tmpPoint;
   }

   Point operator/(const Point& a_left, const double& a_right){
      Point tmpPoint = a_left;
      tmpPoint/=a_right;
      return tmpPoint;
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

   DrawPoint DrawPoint::operator+(DrawPoint& a_other){
      DrawPoint returnPoint;
      returnPoint.x = x+a_other.x;   returnPoint.y = x+a_other.y;   returnPoint.z = x+a_other.z;
      returnPoint.textureX = textureX+a_other.textureX;   returnPoint.textureY = textureY+a_other.textureY;
      returnPoint.R = R+a_other.R;   returnPoint.G = G+a_other.G; returnPoint.B = B+a_other.B;
      returnPoint.A = A+a_other.A;
      return returnPoint;
   }
   DrawPoint DrawPoint::operator-(DrawPoint& a_other){
      DrawPoint returnPoint;
      returnPoint.x = x-a_other.x;   returnPoint.y = x-a_other.y;   returnPoint.z = x-a_other.z;
      returnPoint.textureX = textureX-a_other.textureX;   returnPoint.textureY = textureY-a_other.textureY;
      returnPoint.R = R-a_other.R;   returnPoint.G = G-a_other.G; returnPoint.B = B-a_other.B;
      returnPoint.A = A-a_other.A;
      return returnPoint;
   }

   bool DrawPoint::operator+=(DrawPoint& a_other){
      x+=a_other.x; y+=a_other.y; z+=a_other.z;
      textureX+=a_other.textureX; textureY+=a_other.textureY;
      R+=a_other.R; G+=a_other.G; B+=a_other.B; A+=a_other.A;
      return true;
   }

   bool DrawPoint::operator-=(DrawPoint& a_other){
      x-=a_other.x; y-=a_other.y; z-=a_other.z;
      textureX-=a_other.textureX; textureY-=a_other.textureY;
      R-=a_other.R; G-=a_other.G; B-=a_other.B; A-=a_other.A;
      return true;
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

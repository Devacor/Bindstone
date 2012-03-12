/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MATRIX_H_
#define _MATRIX_H_

#include <vector>
#include <algorithm>
#include <memory>
#include "Render/points.h"
#include "Utility/package.h"

namespace M2Rend {

   typedef double MatrixValue;
   typedef std::vector<MatrixValue> MatrixRow;

   class Matrix {
   protected:
         typedef std::vector<MatrixRow> MatrixContainer;
   public:
      Matrix(int a_size, MatrixValue value = 0.0):content(a_size,MatrixRow(a_size, value)),sizeX(a_size),sizeY(a_size){}
      Matrix(int a_x, int a_y, MatrixValue value = 0.0):content(a_x,MatrixRow(a_y, value)),sizeX(a_x),sizeY(a_y){}

      int getSizeX() const{return sizeX;}
      int getSizeY() const{return sizeY;}

      void print() {
         for( int y=0;y<sizeY;++y){
            for( int x=0;x<sizeX;++x){
               std::cout << content[x][y] << " ";
            }
            std::cout << std::endl;
         }
      }

      MatrixRow& operator[] (const int a_index){
         return content[a_index];
      }

      const MatrixRow& operator[] (const int a_index) const{
         return content[a_index];
      }

      bool operator+=(const Matrix& a_other){
         M2Util::require(getSizeX() == a_other.getSizeX() && getSizeY() == a_other.getSizeY(), M2Util::RangeException("Invalid Matrix addition in operator+=, mismatched sizes."));
         for(int x=0; x < getSizeX(); ++x){
            for(int y=0; y < getSizeY(); ++y){
               content[x][y]+=a_other[x][y];
            }
         }
         return true;
      }
      bool operator-=(const Matrix& a_other){
         M2Util::require(getSizeX() == a_other.getSizeX() && getSizeY() == a_other.getSizeY(), M2Util::RangeException("Invalid Matrix subtraction in operator-=, mismatched sizes."));
         for(int x=0; x < getSizeX(); ++x){
            for(int y=0; y < getSizeY(); ++y){
               content[x][y]-=a_other[x][y];
            }
         }
         return true;
      }
      bool operator*=(const Matrix& a_other){
         M2Util::require(getSizeX() == a_other.getSizeY(), M2Util::RangeException("Invalid Matrix multiplication in operator*=, mismatched sizes."));
         int resultX = getSizeY(), resultY = a_other.getSizeX(), commonSize = sizeX;
         Matrix result(resultY, resultX);

         for(int x=0; x < resultX; ++x){
            for(int y=0; y < resultY; ++y){
               for(int common=0; common < commonSize; ++common){
                  result[y][x] += content[common][x] * a_other[y][common];
               }
            }
         }

         *this = result;
         return true;
      }

      bool operator*=(const MatrixValue& a_other){
         std::for_each(content.begin(), content.end(), [&](MatrixRow &row){
            std::for_each(row.begin(), row.end(), [&](MatrixValue &value){
               value*=a_other;
            });
         });
         return true;
      }
      bool operator/=(const MatrixValue& a_other){
         std::for_each(content.begin(), content.end(), [&](MatrixRow &row){
            std::for_each(row.begin(), row.end(), [&](MatrixValue &value){
               value/=a_other;
            });
         });
         return true;
      }

      std::shared_ptr<std::vector<MatrixValue>> getMatrixArray() {
         auto matrixArrayRepresentation = std::make_shared<std::vector<MatrixValue>>(sizeX*sizeY);
         for(int y=0; y < sizeY; ++y){
            for(int x=0; x < sizeX; ++x){
               (*matrixArrayRepresentation)[y*sizeX + x] = content[x][y];
            }
         }

         return matrixArrayRepresentation;
      }
   protected:
      int sizeX, sizeY;
      MatrixContainer content;
      bool sizeIsDirty;
   };


   class TransformMatrix : public Matrix {
   public:
      TransformMatrix(MatrixValue a_value = 0.0):Matrix(4, a_value){
         makeIdentity();
      }
      TransformMatrix(const Point &a_position):Matrix(4, 0.0){
         makeIdentity();
         position(a_position.x, a_position.y, a_position.z);
      }

      MatrixValue getX() const{
         return content[3][0];
      }

      MatrixValue getY() const{
         return content[3][1];
      }

      MatrixValue getZ() const{
         return content[3][2];
      }

      TransformMatrix& makeIdentity(){
         content = MatrixContainer(sizeY, MatrixRow(sizeX, 0.0));
         for(int i=0; i < getSizeX() && i < getSizeY();++i){
            content[i][i] = 1;
         }
         return *this;
      }

      TransformMatrix& makeOrtho(MatrixValue a_left, MatrixValue a_right, MatrixValue a_bottom, MatrixValue a_top, MatrixValue a_near, MatrixValue a_far){
         MatrixValue a = 2.0 / (a_right - a_left);
         MatrixValue b = 2.0 / (a_top - a_bottom);
         MatrixValue c = -2.0 / (a_far - a_near);

         MatrixValue tx = - ((a_right + a_left)/(a_right - a_left));
         MatrixValue ty = - ((a_top + a_bottom)/(a_top - a_bottom));
         MatrixValue tz = - ((a_far + a_near)/(a_far - a_near));

         content = MatrixContainer(sizeY, MatrixRow(sizeX, 0.0));
         content[0][0] = a;
         content[1][1] = b;
         content[2][2] = c;
         content[3][3] = 1.0;
         content[3][0] = tx;
         content[3][1] = ty;
         content[3][2] = tz;
         return *this;
      }

      TransformMatrix& rotateX(MatrixValue a_radian){
         //1   0     0
         //0   cos   -sin
         //0   sin   cos
         TransformMatrix rotation;
         rotation.makeIdentity();
         rotation[1][1] = cos(a_radian);
         rotation[2][1] = -(sin(a_radian));
         rotation[1][2] = sin(a_radian);
         rotation[2][2] = cos(a_radian);
         *this *= rotation;
         return *this;
      }

      TransformMatrix& rotateY(MatrixValue a_radian){
         //cos    0  sin
         //0      1  0
         //-sin   0  cos
         TransformMatrix rotation;
         rotation.makeIdentity();
         rotation[0][0] = cos(a_radian);
         rotation[2][0] = sin(a_radian);
         rotation[0][2] = -(sin(a_radian));
         rotation[2][2] = cos(a_radian);
         *this *= rotation;
         return *this;
      }

      TransformMatrix& rotateZ(MatrixValue a_radian){
         //cos    -sin  0
         //sin    cos   0
         //0      0     1
         TransformMatrix rotation;
         rotation.makeIdentity();
         rotation[0][0] = cos(a_radian);
         rotation[1][0] = -(sin(a_radian));
         rotation[0][1] = sin(a_radian);
         rotation[1][1] = cos(a_radian);
         *this *= rotation;
         return *this;
      }

      TransformMatrix& position(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 0.0){
         TransformMatrix translation;
         translation.makeIdentity();
         translation[3][0] = a_x;
         translation[3][1] = a_y;
         translation[3][2] = a_z;
         *this *= translation;
         return *this;
      }

      TransformMatrix& translate(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 0.0){
         TransformMatrix translation;
         translation.makeIdentity();
         translation[3][0] += a_x;
         translation[3][1] += a_y;
         translation[3][2] += a_z;
         *this *= translation;
         return *this;
      }

      TransformMatrix& scale(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 1.0){
         TransformMatrix scaling;
         scaling.makeIdentity();
         scaling[0][0] = a_x;
         scaling[1][1] = a_y;
         scaling[2][2] = a_z;
         *this *= scaling;
         return *this;
      }
   };

   class MatrixStack {
   public:
      MatrixStack(){
         push(TransformMatrix());
      }

      TransformMatrix& top(){
         return stack.back();
      }

      TransformMatrix& push(){
         stack.push_back(stack.back());
         return stack.back();
      }

      TransformMatrix& push(const TransformMatrix &matrix){
         stack.push_back(matrix);
         return stack.back();
      }

      void pop(){
         stack.pop_back();
         if(stack.empty()){
            push(TransformMatrix());
         }
      }

      void clear(){
         stack.clear();
         push(TransformMatrix());
      }

   private:
      std::vector<TransformMatrix> stack;
   };
}

#endif

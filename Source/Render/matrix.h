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

namespace MV {

#ifdef __APPLE__
	#import "TargetConditionals.h"
	#ifdef TARGET_OS_IPHONE
		typedef float MatrixValue;
	#else
		typedef double MatrixValue;
	#endif
#else
	typedef double MatrixValue;	
#endif

	typedef std::vector<MatrixValue> MatrixRow;
	typedef std::vector<MatrixRow> MatrixContainer;

	class Matrix {
	public:
		Matrix(int a_size, MatrixValue value = 0.0):content(a_size,MatrixRow(a_size, value)),sizeX(a_size),sizeY(a_size){}
		Matrix(int a_x, int a_y, MatrixValue value = 0.0):content(a_x,MatrixRow(a_y, value)),sizeX(a_x),sizeY(a_y){}

		int getSizeX() const{return sizeX;}
		int getSizeY() const{return sizeY;}

		void print();

		MatrixRow& operator[] (const int a_index){
			return content[a_index];
		}

		const MatrixRow& operator[] (const int a_index) const{
			return content[a_index];
		}

		Matrix& operator+=(const Matrix& a_other);
		Matrix& operator-=(const Matrix& a_other);
		Matrix& operator*=(const Matrix& a_other);

		Matrix& operator*=(const MatrixValue& a_other);
		Matrix& operator/=(const MatrixValue& a_other);

		std::shared_ptr<std::vector<MatrixValue>> getMatrixArray();
	protected:
		int sizeX, sizeY;
		MatrixContainer content;
		bool sizeIsDirty;
	};

	const Matrix operator-(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator+(const Matrix &a_left, const Matrix &a_right);

	const Matrix operator*(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator*(const Matrix &a_left, const MatrixValue &a_right);

	const Matrix operator/(const Matrix &a_left, const MatrixValue &a_right);

	class TransformMatrix : public Matrix {
	public:
		TransformMatrix(const TransformMatrix &a_matrix):Matrix(static_cast<Matrix>(a_matrix)){}
		TransformMatrix(const Matrix &a_matrix):Matrix(a_matrix){}
		TransformMatrix(MatrixValue a_value = 0.0);
		TransformMatrix(const Point &a_position);

		MatrixValue getX() const{
			return content[3][0];
		}
		MatrixValue getY() const{
			return content[3][1];
		}
		MatrixValue getZ() const{
			return content[3][2];
		}

		TransformMatrix& makeIdentity();
		TransformMatrix& makeOrtho(MatrixValue a_left, MatrixValue a_right, MatrixValue a_bottom, MatrixValue a_top, MatrixValue a_near, MatrixValue a_far);

		TransformMatrix& rotateX(MatrixValue a_radian);
		TransformMatrix& rotateY(MatrixValue a_radian);
		TransformMatrix& rotateZ(MatrixValue a_radian);

		TransformMatrix& position(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 0.0);
		TransformMatrix& translate(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 0.0);
		TransformMatrix& scale(MatrixValue a_x, MatrixValue a_y, MatrixValue a_z = 1.0);
	};

	class MatrixStack {
	public:
		MatrixStack(){
			push(TransformMatrix());
		}

		TransformMatrix& top();
		TransformMatrix& push();
		TransformMatrix& push(const TransformMatrix &matrix);
		void pop();
		void clear();

	private:
		std::vector<TransformMatrix> stack;
	};
}

#endif

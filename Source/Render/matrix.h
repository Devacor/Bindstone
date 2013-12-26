/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MATRIX_H_
#define _MATRIX_H_

#include <vector>
#include <algorithm>
#include <memory>
#include <utility>
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

	class Matrix {
	private:
		class MatrixRowAccess {
		public:
			MatrixRowAccess(std::shared_ptr<std::vector<MatrixValue>> a_matrixArray, size_t a_x);

			void setCurrentX(size_t a_currentX) const;

			MatrixValue& operator[](size_t a_index);
			const MatrixValue& operator[](size_t a_index) const;
			void resize(size_t a_x);
		private:
			mutable size_t currentX;
			std::shared_ptr<std::vector<MatrixValue>> matrixArray;
			size_t sizeX;
		};

	public:
		Matrix(size_t a_size, MatrixValue a_value = 0.0);
		Matrix(size_t a_x, size_t a_y, MatrixValue a_value = 0.0);
		Matrix(const Matrix& a_other);
		Matrix(Matrix&& a_other);

		size_t getSizeX() const{return sizeX;}
		size_t getSizeY() const{return sizeY;}

		void print();

		MatrixRowAccess& operator[] (size_t a_index);
		const MatrixRowAccess& operator[] (size_t a_index) const;

		Matrix& clear(MatrixValue a_value = 0.0);

		Matrix& operator+=(const Matrix& a_other);
		Matrix& operator-=(const Matrix& a_other);
		Matrix& operator*=(const Matrix& a_other);

		Matrix& operator*=(const MatrixValue& a_other);
		Matrix& operator/=(const MatrixValue& a_other);

		Matrix& operator=(const Matrix& a_other);
		Matrix& operator=(Matrix&& a_other);

		std::shared_ptr<std::vector<MatrixValue>> getMatrixArray() const;
	protected:
		size_t sizeX, sizeY;
		std::shared_ptr<std::vector<MatrixValue>> matrixArray;
	private:
		MatrixRowAccess rowAccessor;
	};

	const Matrix operator-(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator+(const Matrix &a_left, const Matrix &a_right);

	const Matrix operator*(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator*(const Matrix &a_left, const MatrixValue &a_right);

	const Matrix operator/(const Matrix &a_left, const MatrixValue &a_right);

	class TransformMatrix : public Matrix {
	public:
		TransformMatrix(const TransformMatrix &a_matrix):Matrix(a_matrix){}
		TransformMatrix(const Matrix &a_matrix):Matrix(a_matrix){}

		TransformMatrix(const TransformMatrix &&a_matrix):Matrix(std::move(a_matrix)){}
		TransformMatrix(const Matrix &&a_matrix):Matrix(std::move(a_matrix)){}

		TransformMatrix(MatrixValue a_value = 0.0);
		TransformMatrix(const Point<MatrixValue> &a_position);

		MatrixValue getX() const{
			return (*this)[3][0];
		}
		MatrixValue getY() const{
			return (*this)[3][1];
		}
		MatrixValue getZ() const{
			return (*this)[3][2];
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
		MatrixStack(const std::string &a_name = ""):
			name(a_name){
			push(TransformMatrix());
		}

		TransformMatrix& top();
		const TransformMatrix& top() const;
		TransformMatrix& push();
		TransformMatrix& push(const TransformMatrix &matrix);
		void pop();
		void clear();

	private:
		std::vector<TransformMatrix> stack;
		std::string name;
	};
}

#endif

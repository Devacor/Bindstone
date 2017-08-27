/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MV_MATRIX_H_
#define _MV_MATRIX_H_

#include <vector>
#include <algorithm>
#include <memory>
#include <utility>
#include "Render/points.h"
#include "Utility/package.h"

namespace MV2 {
	typedef float PointPrecision;
	class Matrix {
	public:
		Matrix(size_t a_size, PointPrecision a_value = 0.0);
		Matrix(size_t a_x, size_t a_y, PointPrecision a_value = 0.0);
		Matrix(const Matrix& a_other);
		Matrix(Matrix&& a_other);

		size_t getSizeX() const{return sizeX;}
		size_t getSizeY() const{return sizeY;}

		void print();

		PointPrecision& access(size_t a_x, size_t a_y){
			return (matrixArray)[(sizeX * a_x) + (a_y)];
		}
		const PointPrecision& access(size_t a_x, size_t a_y) const{
			return (matrixArray)[(sizeX * a_x) + (a_y)];
		}

		PointPrecision& accessTransposed(size_t a_x, size_t a_y){
			return (matrixArray)[(a_x) + (a_y * sizeY)];
		}
		const PointPrecision& accessTransposed(size_t a_x, size_t a_y) const{
			return (matrixArray)[(a_x) + (a_y * sizeY)];
		}

		Matrix transpose() const;

		PointPrecision& operator() (size_t a_x, size_t a_y){
			return (matrixArray)[(sizeX * a_x) + (a_y)];
		}
		const PointPrecision& operator() (size_t a_x, size_t a_y) const{
			return (matrixArray)[(sizeX * a_x) + (a_y)];
		}

		Matrix& clear(PointPrecision a_value = 0.0);

		Matrix& operator+=(const Matrix& a_other);
		Matrix& operator-=(const Matrix& a_other);
		Matrix& operator*=(const Matrix& a_other);

		Matrix& operator*=(const PointPrecision& a_other);
		Matrix& operator/=(const PointPrecision& a_other);

		Matrix& operator=(const Matrix& a_other);
		Matrix& operator=(Matrix&& a_other);

		const std::vector<PointPrecision>& getMatrixArray() const;
		std::vector<PointPrecision>& getMatrixArray();
	protected:
		size_t sizeX, sizeY;
		std::vector<PointPrecision> matrixArray;
	};

	const Matrix operator-(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator+(const Matrix &a_left, const Matrix &a_right);

	const Matrix operator*(const Matrix &a_left, const Matrix &a_right);
	const Matrix operator*(const Matrix &a_left, const PointPrecision &a_right);

	const Matrix operator/(const Matrix &a_left, const PointPrecision &a_right);

	std::ostream& operator<<(std::ostream& os, const Matrix& a_matrix);

	class TransformMatrix : public Matrix {
	public:
		TransformMatrix(const TransformMatrix &a_matrix):Matrix(a_matrix){}
		TransformMatrix(const Matrix &a_matrix):Matrix(a_matrix){}

		TransformMatrix(const TransformMatrix &&a_matrix):Matrix(std::move(a_matrix)){}
		TransformMatrix(const Matrix &&a_matrix):Matrix(std::move(a_matrix)){}

		TransformMatrix(PointPrecision a_value = 0.0);
		TransformMatrix(const MV::Point<PointPrecision> &a_position);

		TransformMatrix& operator=(const TransformMatrix& a_other);
		TransformMatrix& operator=(TransformMatrix&& a_other);

		PointPrecision getX() const{
			return (*this).access(3, 0);
		}
		PointPrecision getY() const{
			return (*this).access(3, 1);
		}
		PointPrecision getZ() const{
			return (*this).access(3, 2);
		}

		TransformMatrix& makeIdentity();
		TransformMatrix& makeOrtho(PointPrecision a_left, PointPrecision a_right, PointPrecision a_bottom, PointPrecision a_top, PointPrecision a_near, PointPrecision a_far);

		TransformMatrix& rotateXSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
			//1	0	0
			//0	cos	-sin
			//0	sin	cos
			TransformMatrix rotation;
			rotation.access(1, 1) = a_cosRad;
			rotation.access(2, 1) = -a_sinRad;
			rotation.access(1, 2) = a_sinRad;
			rotation.access(2, 2) = a_cosRad;
			*this *= rotation;
			return *this;
		}
		TransformMatrix& rotateYSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
			//cos	0  sin
			//0		1  0
			//-sin	0  cos
			TransformMatrix rotation;
			rotation.access(0, 0) = a_cosRad;
			rotation.access(2, 0) = a_sinRad;
			rotation.access(0, 2) = -a_sinRad;
			rotation.access(2, 2) = a_cosRad;
			*this *= rotation;
			return *this;
		}
		TransformMatrix& rotateZSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
			//cos	 -sin  0
			//sin	 cos	0
			//0		0	  1
			TransformMatrix rotation;
			rotation.access(0, 0) = a_cosRad;
			rotation.access(1, 0) = -a_sinRad;
			rotation.access(0, 1) = a_sinRad;
			rotation.access(1, 1) = a_cosRad;
			*this *= rotation;
			return *this;
		}

		TransformMatrix& rotateX(PointPrecision a_radian) {
			return (a_radian != 0.0f) ? rotateXSupplyCosSin(cos(a_radian), sin(a_radian)) : *this;
		}

		TransformMatrix& rotateY(PointPrecision a_radian) {
			return (a_radian != 0.0f) ? rotateYSupplyCosSin(cos(a_radian), sin(a_radian)) : *this;
		}

		TransformMatrix& rotateZ(PointPrecision a_radian) {
			return (a_radian != 0.0f) ? rotateZSupplyCosSin(cos(a_radian), sin(a_radian)) : *this;
		}

		TransformMatrix& translate(const MV::Point<PointPrecision> &a_point);
		TransformMatrix& translate(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 0.0);
		TransformMatrix& scale(const MV::Scale &a_point);
		TransformMatrix& scale(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 1.0);

		MV::Scale scale() const;

		TransformMatrix& position(const MV::Point<PointPrecision> &a_point);

		TransformMatrix& overrideScale(const MV::Scale &a_point);
		MV::Point<PointPrecision> position() const {
			return MV::Point<PointPrecision>(access(3, 0), access(3, 1), access(3, 2));
		}
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

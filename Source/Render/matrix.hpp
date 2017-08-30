/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MV_MATRIX2_H_
#define _MV_MATRIX2_H_

#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <utility>
#include "Render/points.h"

namespace MV {

	template<size_t SizeX, size_t SizeY = SizeX>
	class Matrix;

	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator-(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right);
	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator+(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right);

	template<size_t SizeX, size_t SizeY, size_t ResultY>
	inline const Matrix<SizeX, ResultY> operator*(const Matrix<SizeX, SizeY> &a_left, const Matrix<ResultY, SizeY> &a_right);
	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator*(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right);

	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator/(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right);

	template<size_t SizeX, size_t SizeY>
	class Matrix {
	public:
		constexpr static size_t sizeX() { return SizeX; }
		constexpr static size_t sizeY() { return SizeY; }

		constexpr static size_t size() { return SizeX * SizeY; }

		Matrix() {
			matrixArray.fill(0.0f);
		}

		Matrix(const Matrix<SizeX, SizeY>& a_other) = default;
		Matrix(Matrix<SizeX, SizeY>&& a_other) = default;
		inline Matrix<SizeX, SizeY>& operator=(const Matrix<SizeX, SizeY>& a_other) = default;
		inline Matrix<SizeX, SizeY>& operator=(Matrix<SizeX, SizeY>&& a_other) = default;

		void print() {
			for (size_t y = 0; y < SizeY; ++y) {
				for (size_t x = 0; x < SizeX; ++x) {
					std::cout << "(" << (x*SizeX) + (y) << ":" << (*this).access(x, y) << ") ";
				}
				std::cout << "\n";
			}
		}

		constexpr PointPrecision& operator() (size_t a_x, size_t a_y) {
			return (matrixArray)[(SizeX * a_x) + (a_y)];
		}
		constexpr const PointPrecision operator() (size_t a_x, size_t a_y) const {
			return (matrixArray)[(SizeX * a_x) + (a_y)];
		}

		constexpr PointPrecision& access(size_t a_x, size_t a_y) {
			return (matrixArray)[(SizeX * a_x) + (a_y)];
		}
		constexpr const PointPrecision access(size_t a_x, size_t a_y) const {
			return (matrixArray)[(SizeX * a_x) + (a_y)];
		}

		constexpr PointPrecision& accessTransposed(size_t a_x, size_t a_y) {
			return (matrixArray)[(a_x)+(a_y * SizeY)];
		}
		constexpr const PointPrecision accessTransposed(size_t a_x, size_t a_y) const {
			return (matrixArray)[(a_x)+(a_y * SizeY)];
		}

		inline Matrix<SizeY, SizeX> transpose() const {
			Matrix<SizeY, SizeX> result(*this);
			for (size_t x = 0; x < SizeX; ++x) {
				for (size_t y = 0; y < SizeY; ++y) {
					result.accessTransposed(x, y) = access(x, y);
				}
			}
			return result;
		}

		inline Matrix<SizeX, SizeY>& clear(PointPrecision a_value = 0.0) {
			matrixArray.fill(a_value);
			return *this;
		}

		inline Matrix<SizeX, SizeY>& operator+=(const Matrix<SizeX, SizeY>& a_other) {
			for (size_t x = 0; x < SizeX; ++x) {
				for (size_t y = 0; y < SizeY; ++y) {
					(*this).access(x, y) += a_other.access(x, y);
				}
			}
			return *this;
		}

		inline Matrix<SizeX, SizeY>& operator-=(const Matrix<SizeX, SizeY>& a_other) {
			for (size_t x = 0; x < SizeX; ++x) {
				for (size_t y = 0; y < SizeY; ++y) {
					(*this).access(x, y) -= a_other.access(x, y);
				}
			}
			return *this;
		}

		template <size_t ResultY>
		inline Matrix<SizeX, SizeY>& operator*=(const Matrix<ResultY, SizeY>& a_other) {
			static_assert(SizeX == SizeY && SizeX == ResultY, "Failed to operator*= two differently sized matrices.");
			*this = *this * a_other;
			return *this;
		}

		inline Matrix<SizeX, SizeY>& operator*=(const PointPrecision& a_other) {
			for (auto&& value : matrixArray) {
				value *= a_other;
			}
			return *this;
		}
		inline Matrix<SizeX, SizeY>& operator/=(const PointPrecision& a_other) {
			for (auto&& value : matrixArray) {
				value /= a_other;
			}
			return *this;
		}

		inline const std::array<PointPrecision, SizeX * SizeX>& getMatrixArray() const {
			return matrixArray;
		}
		inline std::array<PointPrecision, SizeX * SizeX>& getMatrixArray() {
			return return matrixArray;;
		}
	protected:
		std::array<PointPrecision, SizeX * SizeY> matrixArray;
	};

	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator-(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result -= a_right;
	}
	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator+(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result += a_right;
	}

	template<size_t SizeX, size_t SizeY, size_t ResultY>
	inline const Matrix<SizeX, ResultY> operator*(const Matrix<SizeX, SizeY> &a_left, const Matrix<ResultY, SizeY> &a_right) {
		Matrix<SizeX, ResultY> result;

		for (size_t x = 0; x < SizeX; ++x) {
			for (size_t common = 0; common < SizeX; ++common) {
				for (size_t y = 0; y < ResultY; ++y) {
					result.access(x, y) += a_left(common, y) * a_right(x, common);
				}
			}
		}

		return result;
	}
	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator*(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result *= a_right;
	}

	template<size_t SizeX, size_t SizeY>
	inline const Matrix<SizeX, SizeY> operator/(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result /= a_right;
	}

	template<size_t SizeX, size_t SizeY>
	std::ostream& operator<<(std::ostream& os, const Matrix<SizeX, SizeY>& a_matrix) {
		for (size_t y = 0; y < a_matrix.sizeY(); ++y) {
			os << "[";
			for (size_t x = 0; x < a_matrix.sizeX(); ++x) {
				os << a_matrix(x, y) << (x != a_matrix.sizeX() - 1 ? ", " : "]");
			}
			os << "\n";
		}
		os << std::endl;
		return os;
	}

	class TransformMatrix : public Matrix<4, 4> {
	public:
		TransformMatrix(const TransformMatrix &a_matrix) :Matrix<4, 4>(a_matrix) {}
		TransformMatrix(const Matrix<4, 4> &a_matrix) :Matrix<4, 4>(a_matrix) {}

		TransformMatrix(const TransformMatrix &&a_matrix) :Matrix<4, 4>(std::move(a_matrix)) {}
		TransformMatrix(const Matrix<4, 4> &&a_matrix) :Matrix<4, 4>(std::move(a_matrix)) {}

		TransformMatrix() : Matrix<4, 4>() {
			makeIdentity();
		}

		TransformMatrix& operator=(const TransformMatrix& a_other) = default;
		TransformMatrix& operator=(TransformMatrix&& a_other) = default;

		constexpr PointPrecision getX() const {
			return (*this).access(3, 0);
		}
		constexpr PointPrecision getY() const {
			return (*this).access(3, 1);
		}
		constexpr PointPrecision getZ() const {
			return (*this).access(3, 2);
		}

		//avoiding branching or needing to set 4 coordinates twice.
		TransformMatrix& makeIdentity() {
			(*this).access(0, 0) = 1.0f;
			(*this).access(1, 0) = 0.0f;
			(*this).access(2, 0) = 0.0f;
			(*this).access(3, 0) = 0.0f;

			(*this).access(0, 1) = 0.0f;
			(*this).access(1, 1) = 1.0f;
			(*this).access(2, 1) = 0.0f;
			(*this).access(3, 1) = 0.0f;

			(*this).access(0, 2) = 0.0f;
			(*this).access(1, 2) = 0.0f;
			(*this).access(2, 2) = 1.0f;
			(*this).access(3, 2) = 0.0f;

			(*this).access(0, 3) = 0.0f;
			(*this).access(1, 3) = 0.0f;
			(*this).access(2, 3) = 0.0f;
			(*this).access(3, 3) = 1.0f;

			return *this;
		}

		TransformMatrix& makeOrtho(PointPrecision a_left, PointPrecision a_right, PointPrecision a_bottom, PointPrecision a_top, PointPrecision a_near, PointPrecision a_far) {
			PointPrecision a = 2.0f / (a_right - a_left);
			PointPrecision b = 2.0f / (a_top - a_bottom);
			PointPrecision c = -2.0f / (a_far - a_near);

			PointPrecision tx = -((a_right + a_left) / (a_right - a_left));
			PointPrecision ty = -((a_top + a_bottom) / (a_top - a_bottom));
			PointPrecision tz = -((a_far + a_near) / (a_far - a_near));

			clear();
			(*this).access(0, 0) = a;
			(*this).access(1, 1) = b;
			(*this).access(2, 2) = c;
			(*this).access(3, 3) = 1.0f;
			(*this).access(3, 0) = tx;
			(*this).access(3, 1) = ty;
			(*this).access(3, 2) = tz;

			return *this;
		}

		inline TransformMatrix& rotateXSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
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
		inline TransformMatrix& rotateYSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
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
		inline TransformMatrix& rotateZSupplyCosSin(PointPrecision a_cosRad, PointPrecision a_sinRad) {
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

		inline TransformMatrix& rotateX(PointPrecision a_radian) {
			return rotateXSupplyCosSin(cos(a_radian), sin(a_radian));
		}

		inline TransformMatrix& rotateY(PointPrecision a_radian) {
			return rotateYSupplyCosSin(cos(a_radian), sin(a_radian));
		}

		inline TransformMatrix& rotateZ(PointPrecision a_radian) {
			return rotateZSupplyCosSin(cos(a_radian), sin(a_radian));
		}

		inline TransformMatrix& translate(const MV::Point<PointPrecision> &a_point) {
			TransformMatrix translation;
			translation.access(3, 0) = a_point.x;
			translation.access(3, 1) = a_point.y;
			translation.access(3, 2) = a_point.z;
			*this *= translation;
			return *this;
		}

		inline TransformMatrix& translate(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 0.0) {
			TransformMatrix translation;
			translation.access(3, 0) = a_x;
			translation.access(3, 1) = a_y;
			translation.access(3, 2) = a_z;
			*this *= translation;
			return *this;
		}

		inline TransformMatrix& scale(const MV::Scale &a_scale) {
			TransformMatrix scaling;
			scaling.access(0, 0) = a_scale.x;
			scaling.access(1, 1) = a_scale.y;
			scaling.access(2, 2) = a_scale.z;
			*this *= scaling;
			return *this;
		}
		inline TransformMatrix& scale(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 1.0f) {
			TransformMatrix scaling;
			scaling.access(0, 0) = a_x;
			scaling.access(1, 1) = a_y;
			scaling.access(2, 2) = a_z;
			*this *= scaling;
			return *this;
		}

		inline MV::Scale scale() const {
			return MV::Scale{ access(0, 0), access(1, 1), access(2, 2) };
		}

		inline TransformMatrix& position(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 0.0f) {
			access(3, 0) = a_x;
			access(3, 1) = a_y;
			access(3, 2) = a_z;
			return *this;
		}
		inline TransformMatrix& position(const MV::Point<PointPrecision> &a_point) {
			access(3, 0) = a_point.x;
			access(3, 1) = a_point.y;
			access(3, 2) = a_point.z;
			return *this;
		}

		inline TransformMatrix& overrideScale(const MV::Scale &a_scale) {
			access(0, 0) = a_scale.x;
			access(1, 1) = a_scale.y;
			access(2, 2) = a_scale.z;
			return *this;
		}
		inline MV::Point<PointPrecision> position() const {
			return MV::Point<PointPrecision>{access(3, 0), access(3, 1), access(3, 2)};
		}
	};

	class MatrixStack {
	public:
		MatrixStack(const std::string &a_name = "") :
			name(a_name),
			stack(1, TransformMatrix()) {
		}

		TransformMatrix& top() {
			return stack.back();
		}
		const TransformMatrix& top() const {
			return stack.back();
		}
		TransformMatrix& push() {
			stack.push_back(stack.back());
			return stack.back();
		}
		TransformMatrix& push(const TransformMatrix &matrix) {
			stack.push_back(matrix);
			return stack.back();
		}
		void pop() {
			stack.pop_back();
			if (stack.empty()) {
				push(TransformMatrix());
			}
		}
		void clear() {
			stack.clear();
			push(TransformMatrix());
		}

	private:
		std::vector<TransformMatrix> stack;
		std::string name;
	};

}

#endif

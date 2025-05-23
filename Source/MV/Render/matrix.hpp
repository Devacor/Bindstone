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
#include "MV/Render/points.h"

namespace MV {

	template<size_t SizeX, size_t SizeY = SizeX>
	class Matrix;

	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator-(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right);
	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator+(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right);

	template<size_t N, size_t M, size_t P>
	inline Matrix<P, N> operator*(const Matrix<M, N>& a_lhs, const Matrix<P, M>& a_rhs);
	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator*(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right);

	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator/(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right);

	enum class MatrixInitialize {NoFill};

	template<size_t SizeX, size_t SizeY>
	class Matrix {
	public:
		constexpr static size_t sizeX() { return SizeX; }
		constexpr static size_t sizeY() { return SizeY; }

		constexpr static size_t size() { return SizeX * SizeY; }

		Matrix() {
			matrixArray.fill(0.0f);
		}

		//nofill
		Matrix(MatrixInitialize) : matrixArray() {
		}

		Matrix(const Matrix<SizeX, SizeY>& a_other) = default;
		Matrix(Matrix<SizeX, SizeY>&& a_other) = default;
		inline Matrix<SizeX, SizeY>& operator=(const Matrix<SizeX, SizeY>& a_other) = default;
		inline Matrix<SizeX, SizeY>& operator=(Matrix<SizeX, SizeY>&& a_other) = default;

		constexpr size_t index(size_t a_x, size_t a_y) const {
			return (SizeY * a_x) + (a_y);
		}

		constexpr PointPrecision& operator[](size_t a_index) {
			return (matrixArray)[a_index];
		}

		constexpr const PointPrecision& operator[](size_t a_index) const {
			return (matrixArray)[a_index];
		}

		constexpr PointPrecision& operator() (size_t a_x, size_t a_y) {
			return (matrixArray)[(SizeY * a_x) + (a_y)];
		}
		constexpr const PointPrecision& operator() (size_t a_x, size_t a_y) const {
			return (matrixArray)[(SizeY * a_x) + (a_y)];
		}

		constexpr PointPrecision& access(size_t a_x, size_t a_y) {
			return (matrixArray)[(SizeY * a_x) + (a_y)];
		}
		constexpr const PointPrecision& access(size_t a_x, size_t a_y) const {
			return (matrixArray)[(SizeY * a_x) + (a_y)];
		}

		constexpr PointPrecision& accessTransposed(size_t a_x, size_t a_y) {
			return (matrixArray)[(a_x)+(a_y * SizeX)];
		}
		constexpr const PointPrecision& accessTransposed(size_t a_x, size_t a_y) const {
			return (matrixArray)[(a_x)+(a_y * SizeX)];
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
			return matrixArray;
		}
	protected:
		std::array<PointPrecision, SizeX * SizeY> matrixArray;
	};

	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator-(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result -= a_right;
	}
	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator+(const Matrix<SizeX, SizeY> &a_left, const Matrix<SizeX, SizeY> &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result += a_right;
	}

	template<size_t N, size_t M, size_t P>
	inline Matrix<P, N> operator*(const Matrix<M, N>& a_lhs, const Matrix<P, M>& a_rhs) {
		Matrix<P, N> result;
		for (size_t n = 0; n < N; ++n) {
			for (size_t p = 0; p < P; ++p) {
				for (size_t m = 0; m < M; ++m) {
					result(p, n) += a_lhs(m, n) * a_rhs(p, m);
				}
			}
		}
		return result;
	}

	template <>
	inline Matrix<4, 4> operator*(const Matrix<4, 4>& a_lhs, const Matrix<4, 4>& a_rhs) {
		Matrix<4, 4> dest(MatrixInitialize::NoFill);
		dest(0, 0) = a_rhs(0, 0) * a_lhs(0, 0) + a_rhs(0, 1) * a_lhs(1, 0) + a_rhs(0, 2) * a_lhs(2, 0) + a_rhs(0, 3) * a_lhs(3, 0);
		dest(0, 1) = a_rhs(0, 0) * a_lhs(0, 1) + a_rhs(0, 1) * a_lhs(1, 1) + a_rhs(0, 2) * a_lhs(2, 1) + a_rhs(0, 3) * a_lhs(3, 1);
		dest(0, 2) = a_rhs(0, 0) * a_lhs(0, 2) + a_rhs(0, 1) * a_lhs(1, 2) + a_rhs(0, 2) * a_lhs(2, 2) + a_rhs(0, 3) * a_lhs(3, 2);
		dest(0, 3) = a_rhs(0, 0) * a_lhs(0, 3) + a_rhs(0, 1) * a_lhs(1, 3) + a_rhs(0, 2) * a_lhs(2, 3) + a_rhs(0, 3) * a_lhs(3, 3);
		dest(1, 0) = a_rhs(1, 0) * a_lhs(0, 0) + a_rhs(1, 1) * a_lhs(1, 0) + a_rhs(1, 2) * a_lhs(2, 0) + a_rhs(1, 3) * a_lhs(3, 0);
		dest(1, 1) = a_rhs(1, 0) * a_lhs(0, 1) + a_rhs(1, 1) * a_lhs(1, 1) + a_rhs(1, 2) * a_lhs(2, 1) + a_rhs(1, 3) * a_lhs(3, 1);
		dest(1, 2) = a_rhs(1, 0) * a_lhs(0, 2) + a_rhs(1, 1) * a_lhs(1, 2) + a_rhs(1, 2) * a_lhs(2, 2) + a_rhs(1, 3) * a_lhs(3, 2);
		dest(1, 3) = a_rhs(1, 0) * a_lhs(0, 3) + a_rhs(1, 1) * a_lhs(1, 3) + a_rhs(1, 2) * a_lhs(2, 3) + a_rhs(1, 3) * a_lhs(3, 3);
		dest(2, 0) = a_rhs(2, 0) * a_lhs(0, 0) + a_rhs(2, 1) * a_lhs(1, 0) + a_rhs(2, 2) * a_lhs(2, 0) + a_rhs(2, 3) * a_lhs(3, 0);
		dest(2, 1) = a_rhs(2, 0) * a_lhs(0, 1) + a_rhs(2, 1) * a_lhs(1, 1) + a_rhs(2, 2) * a_lhs(2, 1) + a_rhs(2, 3) * a_lhs(3, 1);
		dest(2, 2) = a_rhs(2, 0) * a_lhs(0, 2) + a_rhs(2, 1) * a_lhs(1, 2) + a_rhs(2, 2) * a_lhs(2, 2) + a_rhs(2, 3) * a_lhs(3, 2);
		dest(2, 3) = a_rhs(2, 0) * a_lhs(0, 3) + a_rhs(2, 1) * a_lhs(1, 3) + a_rhs(2, 2) * a_lhs(2, 3) + a_rhs(2, 3) * a_lhs(3, 3);
		dest(3, 0) = a_rhs(3, 0) * a_lhs(0, 0) + a_rhs(3, 1) * a_lhs(1, 0) + a_rhs(3, 2) * a_lhs(2, 0) + a_rhs(3, 3) * a_lhs(3, 0);
		dest(3, 1) = a_rhs(3, 0) * a_lhs(0, 1) + a_rhs(3, 1) * a_lhs(1, 1) + a_rhs(3, 2) * a_lhs(2, 1) + a_rhs(3, 3) * a_lhs(3, 1);
		dest(3, 2) = a_rhs(3, 0) * a_lhs(0, 2) + a_rhs(3, 1) * a_lhs(1, 2) + a_rhs(3, 2) * a_lhs(2, 2) + a_rhs(3, 3) * a_lhs(3, 2);
		dest(3, 3) = a_rhs(3, 0) * a_lhs(0, 3) + a_rhs(3, 1) * a_lhs(1, 3) + a_rhs(3, 2) * a_lhs(2, 3) + a_rhs(3, 3) * a_lhs(3, 3);
		return dest;
	}

	inline MV::Point<> operator*(const MV::Matrix<4, 4>& a_lhs, const MV::Point<>& a_rhs) {
		return {
			a_lhs(0, 0) * a_rhs(0) + a_lhs(1, 0) * a_rhs(1) + a_lhs(2, 0) * a_rhs(2) + a_lhs(3, 0),
			a_lhs(0, 1) * a_rhs(0) + a_lhs(1, 1) * a_rhs(1) + a_lhs(2, 1) * a_rhs(2) + a_lhs(3, 1),
			a_lhs(0, 2) * a_rhs(0) + a_lhs(1, 2) * a_rhs(1) + a_lhs(2, 2) * a_rhs(2) + a_lhs(3, 2)
		};
	}

	inline std::array<PointPrecision, 4> fullMatrixPointMultiply(const MV::Matrix<4, 4>& a_lhs, const MV::Point<>& a_rhs) {
		return {
			a_lhs(0, 0) * a_rhs(0) + a_lhs(1, 0) * a_rhs(1) + a_lhs(2, 0) * a_rhs(2) + a_lhs(3, 0),
			a_lhs(0, 1) * a_rhs(0) + a_lhs(1, 1) * a_rhs(1) + a_lhs(2, 1) * a_rhs(2) + a_lhs(3, 1),
			a_lhs(0, 2) * a_rhs(0) + a_lhs(1, 2) * a_rhs(1) + a_lhs(2, 2) * a_rhs(2) + a_lhs(3, 2),
			a_lhs(0, 3) * a_rhs(0) + a_lhs(1, 3) * a_rhs(1) + a_lhs(2, 3) * a_rhs(2) + a_lhs(3, 3)
		};
	}

	inline MV::Point<> fullMatrixPointMultiply(const MV::Matrix<4, 4>& a_lhs, const MV::Point<>& a_rhs, PointPrecision &w) {
		w = a_lhs(0, 3) * a_rhs(0) + a_lhs(1, 3) * a_rhs(1) + a_lhs(2, 3) * a_rhs(2) + a_lhs(3, 3);
		return {
			a_lhs(0, 0) * a_rhs(0) + a_lhs(1, 0) * a_rhs(1) + a_lhs(2, 0) * a_rhs(2) + a_lhs(3, 0),
			a_lhs(0, 1) * a_rhs(0) + a_lhs(1, 1) * a_rhs(1) + a_lhs(2, 1) * a_rhs(2) + a_lhs(3, 1),
			a_lhs(0, 2) * a_rhs(0) + a_lhs(1, 2) * a_rhs(1) + a_lhs(2, 2) * a_rhs(2) + a_lhs(3, 2)
		};
	}

	inline std::array<PointPrecision, 4> operator*(const MV::Matrix<4, 4>& a_lhs, const std::array<PointPrecision, 4>& a_rhs) {
		return {
			a_lhs(0, 0) * a_rhs[0] + a_lhs(1, 0) * a_rhs[1] + a_lhs(2, 0) * a_rhs[2] + a_lhs(3, 0) * a_rhs[3],
			a_lhs(0, 1) * a_rhs[0] + a_lhs(1, 1) * a_rhs[1] + a_lhs(2, 1) * a_rhs[2] + a_lhs(3, 1) * a_rhs[3],
			a_lhs(0, 2) * a_rhs[0] + a_lhs(1, 2) * a_rhs[1] + a_lhs(2, 2) * a_rhs[2] + a_lhs(3, 2) * a_rhs[3],
			a_lhs(0, 3) * a_rhs[0] + a_lhs(1, 3) * a_rhs[1] + a_lhs(2, 3) * a_rhs[2] + a_lhs(3, 3) * a_rhs[3]
		};
	}

	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator*(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right) {
		Matrix<SizeX, SizeY> result{ a_left };
		return result *= a_right;
	}

	template<size_t SizeX, size_t SizeY>
	inline Matrix<SizeX, SizeY> operator/(const Matrix<SizeX, SizeY> &a_left, const PointPrecision &a_right) {
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

		TransformMatrix(TransformMatrix &&a_matrix) noexcept :Matrix<4, 4>(std::move(a_matrix)) {}
		TransformMatrix(Matrix<4, 4> &&a_matrix) noexcept :Matrix<4, 4>(std::move(a_matrix)) {}

		TransformMatrix() : Matrix<4, 4>() {
			makeIdentity();
		}

		TransformMatrix(MatrixInitialize noFill) noexcept : Matrix<4, 4>(noFill) {
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

		inline TransformMatrix& rotateXYZ(AxisAngles a_angles) {
			return rotateXYZ(a_angles.x, a_angles.y, a_angles.z);
		}

		inline TransformMatrix& rotateXYZ(PointPrecision a_rX, PointPrecision a_rY, PointPrecision a_rZ) {
			TransformMatrix rotation;
			rotation.setRotationXYZ(a_rX, a_rY, a_rZ);
			*this *= rotation;
			return *this;
		}

		inline TransformMatrix& setRotationXYZ(PointPrecision a_rX, PointPrecision a_rY, PointPrecision a_rZ) {
			auto cosX = std::cos(a_rX);
			auto sinX = std::sin(a_rX);

			auto cosY = std::cos(a_rY);
			auto sinY = std::sin(a_rY);

			auto cosZ = std::cos(a_rZ);
			auto sinZ = std::sin(a_rZ);

			auto cosXsinY = cosX*sinY;
			auto sinXsinY = sinX*sinY;

			access(0, 0) = cosY * cosZ;
			access(1, 0) = -cosY * sinZ;
			access(2, 0) = sinY;

			access(0, 1) = (cosX*sinZ) + (sinXsinY*cosZ);
			access(1, 1) = (cosX*cosZ) - (sinXsinY*sinZ);
			access(2, 1) = -sinX*cosY;

			access(0, 2) = (sinX*sinZ) - (cosXsinY*cosZ);
			access(1, 2) = (sinX*cosZ) + (cosXsinY*sinZ);
			access(2, 2) = cosX*cosY;
			return *this;
		}

		inline TransformMatrix& setRotationXYZ(AxisAngles a_angles) {
			return setRotationXYZ(a_angles.x, a_angles.y, a_angles.z);
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
			access(3, 0) += a_point.x;
			access(3, 1) += a_point.y;
			access(3, 2) += a_point.z;
			return *this;
		}

		inline TransformMatrix& translate(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 0.0) {
			access(3, 0) += a_x;
			access(3, 1) += a_y;
			access(3, 2) += a_z;
			return *this;
		}

		inline TransformMatrix& scale(const MV::Scale &a_scale) {
			access(0, 0) *= a_scale.x;
			access(1, 1) *= a_scale.y;
			access(2, 2) *= a_scale.z;
			return *this;
		}
		inline TransformMatrix& scale(PointPrecision a_x, PointPrecision a_y, PointPrecision a_z = 1.0f) {
			access(0, 0) *= a_x;
			access(1, 1) *= a_y;
			access(2, 2) *= a_z;
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

	inline TransformMatrix inverse(const Matrix<4, 4> &a_in, float& det) {
		float A2323 = a_in(2, 2) * a_in(3, 3) - a_in(2, 3) * a_in(3, 2);
		float A1323 = a_in(2, 1) * a_in(3, 3) - a_in(2, 3) * a_in(3, 1);
		float A1223 = a_in(2, 1) * a_in(3, 2) - a_in(2, 2) * a_in(3, 1);
		float A0323 = a_in(2, 0) * a_in(3, 3) - a_in(2, 3) * a_in(3, 0);
		float A0223 = a_in(2, 0) * a_in(3, 2) - a_in(2, 2) * a_in(3, 0);
		float A0123 = a_in(2, 0) * a_in(3, 1) - a_in(2, 1) * a_in(3, 0);
		float A2313 = a_in(1, 2) * a_in(3, 3) - a_in(1, 3) * a_in(3, 2);
		float A1313 = a_in(1, 1) * a_in(3, 3) - a_in(1, 3) * a_in(3, 1);
		float A1213 = a_in(1, 1) * a_in(3, 2) - a_in(1, 2) * a_in(3, 1);
		float A2312 = a_in(1, 2) * a_in(2, 3) - a_in(1, 3) * a_in(2, 2);
		float A1312 = a_in(1, 1) * a_in(2, 3) - a_in(1, 3) * a_in(2, 1);
		float A1212 = a_in(1, 1) * a_in(2, 2) - a_in(1, 2) * a_in(2, 1);
		float A0313 = a_in(1, 0) * a_in(3, 3) - a_in(1, 3) * a_in(3, 0);
		float A0213 = a_in(1, 0) * a_in(3, 2) - a_in(1, 2) * a_in(3, 0);
		float A0312 = a_in(1, 0) * a_in(2, 3) - a_in(1, 3) * a_in(2, 0);
		float A0212 = a_in(1, 0) * a_in(2, 2) - a_in(1, 2) * a_in(2, 0);
		float A0113 = a_in(1, 0) * a_in(3, 1) - a_in(1, 1) * a_in(3, 0);
		float A0112 = a_in(1, 0) * a_in(2, 1) - a_in(1, 1) * a_in(2, 0);
		det = a_in(0, 0) * (a_in(1, 1) * A2323 - a_in(1, 2) * A1323 + a_in(1, 3) * A1223)
			- a_in(0, 1) * (a_in(1, 0) * A2323 - a_in(1, 2) * A0323 + a_in(1, 3) * A0223)
			+ a_in(0, 2) * (a_in(1, 0) * A1323 - a_in(1, 1) * A0323 + a_in(1, 3) * A0123)
			- a_in(0, 3) * (a_in(1, 0) * A1223 - a_in(1, 1) * A0223 + a_in(1, 2) * A0123);
		TransformMatrix out(MatrixInitialize::NoFill);
		if (det != 0) {
			det = 1 / det;
			out(0, 0) = det * (a_in(1, 1) * A2323 - a_in(1, 2) * A1323 + a_in(1, 3) * A1223);
			out(0, 1) = det * -(a_in(0, 1) * A2323 - a_in(0, 2) * A1323 + a_in(0, 3) * A1223);
			out(0, 2) = det * (a_in(0, 1) * A2313 - a_in(0, 2) * A1313 + a_in(0, 3) * A1213);
			out(0, 3) = det * -(a_in(0, 1) * A2312 - a_in(0, 2) * A1312 + a_in(0, 3) * A1212);
			out(1, 0) = det * -(a_in(1, 0) * A2323 - a_in(1, 2) * A0323 + a_in(1, 3) * A0223);
			out(1, 1) = det * (a_in(0, 0) * A2323 - a_in(0, 2) * A0323 + a_in(0, 3) * A0223);
			out(1, 2) = det * -(a_in(0, 0) * A2313 - a_in(0, 2) * A0313 + a_in(0, 3) * A0213);
			out(1, 3) = det * (a_in(0, 0) * A2312 - a_in(0, 2) * A0312 + a_in(0, 3) * A0212);
			out(2, 0) = det * (a_in(1, 0) * A1323 - a_in(1, 1) * A0323 + a_in(1, 3) * A0123);
			out(2, 1) = det * -(a_in(0, 0) * A1323 - a_in(0, 1) * A0323 + a_in(0, 3) * A0123);
			out(2, 2) = det * (a_in(0, 0) * A1313 - a_in(0, 1) * A0313 + a_in(0, 3) * A0113);
			out(2, 3) = det * -(a_in(0, 0) * A1312 - a_in(0, 1) * A0312 + a_in(0, 3) * A0112);
			out(3, 0) = det * -(a_in(1, 0) * A1223 - a_in(1, 1) * A0223 + a_in(1, 2) * A0123);
			out(3, 1) = det * (a_in(0, 0) * A1223 - a_in(0, 1) * A0223 + a_in(0, 2) * A0123);
			out(3, 2) = det * -(a_in(0, 0) * A1213 - a_in(0, 1) * A0213 + a_in(0, 2) * A0113);
			out(3, 3) = det * (a_in(0, 0) * A1212 - a_in(0, 1) * A0212 + a_in(0, 2) * A0112);
		}
		return out;
	}

	inline TransformMatrix inverse(const Matrix<4, 4>& a_in) {
		float det;
		auto result = inverse(a_in, det);
		MV::require<MV::LogicException>(det != 0.0f, "Failed to invert matrix due to 0 determinant (in line with the clipping pane).");
		return result;
	}

	class MatrixStack {
	public:
		MatrixStack(const std::string &a_name = "") :
			stack(1, TransformMatrix()),
			name(a_name){
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

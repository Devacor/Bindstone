#include "matrix.h"

#define ALLOW_MATRIX_DEBUG 0

namespace MV{
	const Matrix operator-(const Matrix &a_left, const Matrix &a_right){
		Matrix result = a_left;
		return result-=a_right;
	}

	const Matrix operator+(const Matrix &a_left, const Matrix &a_right){
		Matrix result = a_left;
		return result+=a_right;
	}

	const Matrix operator*(const Matrix &a_left, const Matrix &a_right){
		Matrix result = a_left;
		return result*=a_right;
	}

	const Matrix operator*(const Matrix &a_left, const PointPrecision &a_right){
		Matrix result = a_left;
		return result*=a_right;
	}

	const Matrix operator/(const Matrix &a_left, const PointPrecision &a_right){
		Matrix result = a_left;
		return result/=a_right;
	}

	std::ostream& operator<<(std::ostream& os, const Matrix& a_matrix) {
		for (size_t y = 0; y < a_matrix.getSizeY(); ++y) {
			os << "[";
			for (size_t x = 0; x < a_matrix.getSizeX(); ++x) {
				os << a_matrix(x, y) << (x != a_matrix.getSizeX() - 1 ? ", " : "]");
			}
			os << "\n";
		}
		os << std::endl;
		return os;
	}

	Matrix::Matrix( size_t a_size, PointPrecision a_value ) :
		sizeX(a_size),
		sizeY(a_size),
		matrixArray(a_size*a_size, a_value){
	}

	Matrix::Matrix( size_t a_x, size_t a_y, PointPrecision a_value ) :
		sizeX(a_x),
		sizeY(a_y),
		matrixArray(a_x*a_y, a_value){
	}

	Matrix::Matrix( const Matrix& a_other ):
		sizeX(a_other.sizeX),
		sizeY(a_other.sizeY),
		matrixArray(a_other.matrixArray.begin(), a_other.matrixArray.end()){
	}

	Matrix::Matrix( Matrix&& a_other ):
		sizeX(std::move(a_other.sizeX)),
		sizeY(std::move(a_other.sizeY)),
		matrixArray(std::move(a_other.matrixArray)){
	}

	void Matrix::print(){
		for( size_t y=0;y<sizeY;++y){
			for( size_t x=0;x<sizeX;++x){
				std::cout << "(" << (x*sizeX)+(y) << ":" << (*this).access(x, y) << ") ";
			}
			std::cout << std::endl;
		}
	}

	Matrix& Matrix::operator+=( const Matrix& a_other ){
		require<RangeException>(sizeX == a_other.sizeX && sizeY == a_other.sizeY, "Invalid Matrix addition in operator+=, mismatched sizes.");
		for(size_t x=0; x < getSizeX(); ++x){
			for(size_t y=0; y < getSizeY(); ++y){
				(*this).access(x, y)+=a_other.access(x, y);
			}
		}
		return *this;
	}

	Matrix& Matrix::operator-=( const Matrix& a_other ){
		require<RangeException>(sizeX == a_other.sizeX && sizeY == a_other.sizeY, "Invalid Matrix subtraction in operator-=, mismatched sizes.");
		for(size_t x=0; x < getSizeX(); ++x){
			for(size_t y=0; y < getSizeY(); ++y){
				(*this).access(x, y)-=a_other.access(x, y);
			}
		}
		return *this;
	}

	Matrix& Matrix::operator*=( const Matrix& a_other ){
		require<RangeException>(sizeX == a_other.sizeY, "Invalid Matrix multiplication in operator*=, mismatched sizes.");
		size_t resultX = getSizeY(), resultY = a_other.getSizeX(), commonSize = sizeX;
		Matrix result(resultY, resultX);

		for(size_t x=0; x < resultX; ++x){
			for(size_t y=0; y < resultY; ++y){
				for(size_t common=0; common < commonSize; ++common){
					result.access(y, x) += (*this).access(common, x) * a_other.access(y, common);
				}
			}
		}

		*this = result;
		return *this;
	}

	Matrix& Matrix::operator*=( const PointPrecision& a_other ){
		std::for_each(matrixArray.begin(), matrixArray.end(), [&](PointPrecision &value){
			value*=a_other;
		});
		return *this;
	}

	Matrix& Matrix::operator/=( const PointPrecision& a_other ){
		std::for_each(matrixArray.begin(), matrixArray.end(), [&](PointPrecision &value){
			value/=a_other;
		});
		return *this;
	}

	const std::vector<PointPrecision>& Matrix::getMatrixArray() const{
		return matrixArray;
	}

	std::vector<PointPrecision>& Matrix::getMatrixArray() {
		return matrixArray;
	}

	Matrix& Matrix::clear( PointPrecision a_value ) {
		std::for_each(matrixArray.begin(), matrixArray.end(), [&](PointPrecision &value){
			value=a_value;
		});
		return *this;
	}

	Matrix& Matrix::operator=(const Matrix& a_other){
		matrixArray = a_other.matrixArray;
		sizeX = a_other.sizeX;
		sizeY = a_other.sizeY;
		return *this;
	}

	Matrix& Matrix::operator=(Matrix&& a_other){
		matrixArray = std::move(a_other.matrixArray);
		sizeX = std::move(a_other.sizeX);
		sizeY = std::move(a_other.sizeY);
		return *this;
	}

	TransformMatrix& TransformMatrix::operator=(const TransformMatrix& a_other) {
		Matrix::operator=(a_other);
		return *this;
	}

	TransformMatrix& TransformMatrix::operator=(TransformMatrix&& a_other) {
		Matrix::operator=(a_other);
		return *this;
	}

	MV::Matrix Matrix::transpose() const {
		Matrix result(*this);
		for(size_t x = 0; x < sizeX; ++x){
			for(size_t y = 0; y < sizeY; ++y){
				result.access(x, y) = accessTransposed(x, y);
			}
		}
		return result;
	}


	TransformMatrix::TransformMatrix( PointPrecision a_value ) :Matrix(4, a_value){
		makeIdentity();
	}

	TransformMatrix::TransformMatrix( const Point<PointPrecision> &a_position ) :Matrix(4){
		makeIdentity();
		translate(a_position.x, a_position.y, a_position.z);
	}

	TransformMatrix& TransformMatrix::makeIdentity(){
		clear();
		for(size_t i=0; i < getSizeX() && i < getSizeY();++i){
			(*this).access(i, i) = 1.0;
		}
		return *this;
	}

	TransformMatrix& TransformMatrix::makeOrtho( PointPrecision a_left, PointPrecision a_right, PointPrecision a_bottom, PointPrecision a_top, PointPrecision a_near, PointPrecision a_far ){
		PointPrecision a = 2.0f / (a_right - a_left);
		PointPrecision b = 2.0f / (a_top - a_bottom);
		PointPrecision c = -2.0f / (a_far - a_near);

		PointPrecision tx = - ((a_right + a_left)/(a_right - a_left));
		PointPrecision ty = - ((a_top + a_bottom)/(a_top - a_bottom));
		PointPrecision tz = - ((a_far + a_near)/(a_far - a_near));

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

	TransformMatrix& TransformMatrix::translate(const MV::Point<PointPrecision> &a_point) {
		TransformMatrix translation;
		translation.access(3, 0) = a_point.x;
		translation.access(3, 1) = a_point.y;
		translation.access(3, 2) = a_point.z;
		*this *= translation;
		return *this;
	}

	TransformMatrix& TransformMatrix::translate( PointPrecision a_x, PointPrecision a_y, PointPrecision a_z /*= 0.0*/ ){
		TransformMatrix translation;
		translation.access(3, 0) = a_x;
		translation.access(3, 1) = a_y;
		translation.access(3, 2) = a_z;
		*this *= translation;
		return *this;
	}

	TransformMatrix& TransformMatrix::scale( PointPrecision a_x, PointPrecision a_y, PointPrecision a_z /*= 1.0*/ ){
		TransformMatrix scaling;
		scaling.access(0, 0) = a_x;
		scaling.access(1, 1) = a_y;
		scaling.access(2, 2) = a_z;
		*this *= scaling;
		return *this;
	}

	TransformMatrix& TransformMatrix::scale(const MV::Scale &a_scale) {
		TransformMatrix scaling;
		scaling.access(0, 0) = a_scale.x;
		scaling.access(1, 1) = a_scale.y;
		scaling.access(2, 2) = a_scale.z;
		*this *= scaling;
		return *this;
	}

	TransformMatrix& TransformMatrix::overrideScale(const MV::Scale &a_scale) {
		access(0, 0) = a_scale.x;
		access(1, 1) = a_scale.y;
		access(2, 2) = a_scale.z;
		return *this;
	}

	MV::Scale TransformMatrix::scale() const {
		return MV::Scale(access(0, 0), access(1, 1), access(2, 2));
	}

	MV::TransformMatrix& TransformMatrix::position(const MV::Point<PointPrecision> &a_point) {
		access(3, 0) = a_point.x;
		access(3, 1) = a_point.y;
		access(3, 2) = a_point.y;
		return *this;
	}

	TransformMatrix& MatrixStack::top() {
		MV::require<ResourceException>(!stack.empty(), "MatrixStack::top() failed, stack is empty!");
		return stack.back();
	}

	const TransformMatrix& MatrixStack::top() const{
		MV::require<ResourceException>(!stack.empty(), "MatrixStack::top() failed, stack is empty!");
		return stack.back();
	}

	TransformMatrix& MatrixStack::push(){
#if ALLOW_MATRIX_DEBUG != 0
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".push()" << std::endl;
		}
#endif
		stack.push_back(stack.back());
		onChangedSignal();
		return stack.back();
	}

	TransformMatrix& MatrixStack::push( const TransformMatrix &matrix ){
#if ALLOW_MATRIX_DEBUG != 0
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".push(m)" << std::endl;
		}
#endif
		stack.push_back(matrix);
		onChangedSignal();
		return stack.back();
	}

	void MatrixStack::pop(){
#if ALLOW_MATRIX_DEBUG != 0
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".pop()" << std::endl;
		}
#endif
		stack.pop_back();
		if(stack.empty()){
			push(TransformMatrix());
		} else{
			onChangedSignal();
		}
	}

	void MatrixStack::clear(){
#if ALLOW_MATRIX_DEBUG != 0
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".clear()" << std::endl;
		}
#endif
		stack.clear();
		push(TransformMatrix());
	}

}

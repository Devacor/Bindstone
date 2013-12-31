#include "matrix.h"
#include "boost/lexical_cast.hpp"

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

	const Matrix operator*(const Matrix &a_left, const MatrixValue &a_right){
		Matrix result = a_left;
		return result*=a_right;
	}

	const Matrix operator/(const Matrix &a_left, const MatrixValue &a_right){
		Matrix result = a_left;
		return result/=a_right;
	}

	Matrix::MatrixRowAccess::MatrixRowAccess( std::shared_ptr<std::vector<MatrixValue>> a_matrixArray, size_t a_x ) :
		matrixArray(a_matrixArray),
		sizeX(a_x),
		currentX(0){
	}

	MatrixValue& Matrix::MatrixRowAccess::operator[]( size_t a_index ) {
		return (*matrixArray)[(sizeX * currentX) + (a_index)];
	}

	const MatrixValue& Matrix::MatrixRowAccess::operator[]( size_t a_index ) const {
		return (*matrixArray)[(sizeX * currentX) + (a_index)];
	}

	void Matrix::MatrixRowAccess::setCurrentX( size_t a_currentX ) const {
		currentX = a_currentX;
	}

	void Matrix::MatrixRowAccess::resize(size_t a_x){
		sizeX = a_x;
	}

	Matrix::Matrix( size_t a_size, MatrixValue a_value ) :
		sizeX(a_size),
		sizeY(a_size),
		matrixArray(std::make_shared<std::vector<MatrixValue>>(a_size*a_size, a_value)),
		rowAccessor(matrixArray, a_size){
	}

	Matrix::Matrix( size_t a_x, size_t a_y, MatrixValue a_value ) :
		sizeX(a_x),
		sizeY(a_y),
		matrixArray(std::make_shared<std::vector<MatrixValue>>(a_x*a_y, a_value)),
		rowAccessor(matrixArray, a_x){
	}

	Matrix::Matrix( const Matrix& a_other ):
		sizeX(a_other.sizeX),
		sizeY(a_other.sizeY),
		matrixArray(std::make_shared<std::vector<MatrixValue>>(a_other.matrixArray->begin(), a_other.matrixArray->end())),
		rowAccessor(matrixArray, a_other.sizeX){
	}

	Matrix::Matrix( Matrix&& a_other ):
		sizeX(std::move(a_other.sizeX)),
		sizeY(std::move(a_other.sizeY)),
		matrixArray(std::move(a_other.matrixArray)),
		rowAccessor(matrixArray, sizeX){
		a_other.matrixArray.reset();
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
		require(sizeX == a_other.sizeX && sizeY == a_other.sizeY, RangeException("Invalid Matrix addition in operator+=, mismatched sizes."));
		for(size_t x=0; x < getSizeX(); ++x){
			for(size_t y=0; y < getSizeY(); ++y){
				(*this).access(x, y)+=a_other.access(x, y);
			}
		}
		return *this;
	}

	Matrix& Matrix::operator-=( const Matrix& a_other ){
		require(sizeX == a_other.sizeX && sizeY == a_other.sizeY, RangeException("Invalid Matrix subtraction in operator-=, mismatched sizes."));
		for(size_t x=0; x < getSizeX(); ++x){
			for(size_t y=0; y < getSizeY(); ++y){
				(*this).access(x, y)-=a_other.access(x, y);
			}
		}
		return *this;
	}

	Matrix& Matrix::operator*=( const Matrix& a_other ){
		require(sizeX == a_other.sizeY, RangeException("Invalid Matrix multiplication in operator*=, mismatched sizes."));
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

	Matrix& Matrix::operator*=( const MatrixValue& a_other ){
		std::for_each(matrixArray->begin(), matrixArray->end(), [&](MatrixValue &value){
			value*=a_other;
		});
		return *this;
	}

	Matrix& Matrix::operator/=( const MatrixValue& a_other ){
		std::for_each(matrixArray->begin(), matrixArray->end(), [&](MatrixValue &value){
			value/=a_other;
		});
		return *this;
	}

	std::shared_ptr<std::vector<MatrixValue>> Matrix::getMatrixArray() const{
		return matrixArray;
	}

	Matrix& Matrix::clear( MatrixValue a_value ) {
		std::for_each(matrixArray->begin(), matrixArray->end(), [&](MatrixValue &value){
			value=a_value;
		});
		return *this;
	}

	Matrix::MatrixRowAccess& Matrix::operator[]( size_t a_index ) {
		rowAccessor.setCurrentX(a_index);
		return rowAccessor;
	}

	const Matrix::MatrixRowAccess& Matrix::operator[]( size_t a_index ) const {
		rowAccessor.setCurrentX(a_index);
		return rowAccessor;
	}

	Matrix& Matrix::operator=(const Matrix& a_other){
		*matrixArray = *a_other.matrixArray;
		sizeX = a_other.sizeX;
		sizeY = a_other.sizeY;
		rowAccessor.resize(sizeX);
		return *this;
	}

	Matrix& Matrix::operator=(Matrix&& a_other){
		matrixArray = std::move(a_other.matrixArray);
		a_other.matrixArray.reset();
		sizeX = std::move(a_other.sizeX);
		sizeY = std::move(a_other.sizeY);
		rowAccessor.resize(sizeX);
		return *this;
	}


	TransformMatrix::TransformMatrix( MatrixValue a_value ) :Matrix(4, a_value){
		makeIdentity();
	}

	TransformMatrix::TransformMatrix( const Point<MatrixValue> &a_position ) :Matrix(4){
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

	TransformMatrix& TransformMatrix::makeOrtho( MatrixValue a_left, MatrixValue a_right, MatrixValue a_bottom, MatrixValue a_top, MatrixValue a_near, MatrixValue a_far ){
		MatrixValue a = 2.0 / (a_right - a_left);
		MatrixValue b = 2.0 / (a_top - a_bottom);
		MatrixValue c = -2.0 / (a_far - a_near);

		MatrixValue tx = - ((a_right + a_left)/(a_right - a_left));
		MatrixValue ty = - ((a_top + a_bottom)/(a_top - a_bottom));
		MatrixValue tz = - ((a_far + a_near)/(a_far - a_near));

		clear();
		(*this).access(0, 0) = a;
		(*this).access(1, 1) = b;
		(*this).access(2, 2) = c;
		(*this).access(3, 3) = 1.0;
		(*this).access(3, 0) = tx;
		(*this).access(3, 1) = ty;
		(*this).access(3, 2) = tz;

		return *this;
	}

	TransformMatrix& TransformMatrix::rotateX( MatrixValue a_radian ){
		//1	0	  0
		//0	cos	-sin
		//0	sin	cos
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation.access(1, 1) = cos(a_radian);
		rotation.access(2, 1) = -(sin(a_radian));
		rotation.access(1, 2) = sin(a_radian);
		rotation.access(2, 2) = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::rotateY( MatrixValue a_radian ){
		//cos	 0  sin
		//0		1  0
		//-sin	0  cos
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation.access(0, 0) = cos(a_radian);
		rotation.access(2, 0) = sin(a_radian);
		rotation.access(0, 2) = -(sin(a_radian));
		rotation.access(2, 2) = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::rotateZ( MatrixValue a_radian ){
		//cos	 -sin  0
		//sin	 cos	0
		//0		0	  1
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation.access(0, 0) = cos(a_radian);
		rotation.access(1, 0) = -(sin(a_radian));
		rotation.access(0, 1) = sin(a_radian);
		rotation.access(1, 1) = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::translate( MatrixValue a_x, MatrixValue a_y, MatrixValue a_z /*= 0.0*/ ){
		TransformMatrix translation;
		translation.makeIdentity();
		translation.access(3, 0) = a_x;
		translation.access(3, 1) = a_y;
		translation.access(3, 2) = a_z;
		*this *= translation;
		return *this;
	}

	TransformMatrix& TransformMatrix::scale( MatrixValue a_x, MatrixValue a_y, MatrixValue a_z /*= 1.0*/ ){
		TransformMatrix scaling;
		scaling.makeIdentity();
		scaling.access(0, 0) = a_x;
		scaling.access(1, 1) = a_y;
		scaling.access(2, 2) = a_z;
		*this *= scaling;
		return *this;
	}

	TransformMatrix& MatrixStack::top(){
		return stack.back();
	}

	const TransformMatrix& MatrixStack::top() const{
		return stack.back();
	}

	TransformMatrix& MatrixStack::push(){
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".push()" << std::endl;
		}
		stack.push_back(stack.back());
		return stack.back();
	}

	TransformMatrix& MatrixStack::push( const TransformMatrix &matrix ){
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".push(m)" << std::endl;
		}
		stack.push_back(matrix);
		return stack.back();
	}

	void MatrixStack::pop(){
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".pop()" << std::endl;
		}
		stack.pop_back();
		if(stack.empty()){
			push(TransformMatrix());
		}
	}

	void MatrixStack::clear(){
		if(!name.empty()){
			std::cout << "Matrix: " << name << ".clear()" << std::endl;
		}
		stack.clear();
		push(TransformMatrix());
	}

}

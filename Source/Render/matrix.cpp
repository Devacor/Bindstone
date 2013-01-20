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

	void Matrix::print(){
		for( int y=0;y<sizeY;++y){
			for( int x=0;x<sizeX;++x){
				std::cout << content[x][y] << " ";
			}
			std::cout << std::endl;
		}
	}

	Matrix& Matrix::operator+=( const Matrix& a_other ){
		require(getSizeX() == a_other.getSizeY(), RangeException(std::string("Invalid Matrix multiplication in operator+=, mismatched sizes: ")+boost::lexical_cast<std::string>(getSizeX())+" != "+boost::lexical_cast<std::string>(a_other.getSizeY())));
		for(int x=0; x < getSizeX(); ++x){
			for(int y=0; y < getSizeY(); ++y){
				content[x][y]+=a_other[x][y];
			}
		}
		return *this;
	}

	Matrix& Matrix::operator-=( const Matrix& a_other ){
		require(getSizeX() == a_other.getSizeY(), RangeException(std::string("Invalid Matrix multiplication in operator-=, mismatched sizes: ")+boost::lexical_cast<std::string>(getSizeX())+" != "+boost::lexical_cast<std::string>(a_other.getSizeY())));
		for(int x=0; x < getSizeX(); ++x){
			for(int y=0; y < getSizeY(); ++y){
				content[x][y]-=a_other[x][y];
			}
		}
		return *this;
	}

	Matrix& Matrix::operator*=( const Matrix& a_other ){
		require(getSizeX() == a_other.getSizeY(), RangeException(std::string("Invalid Matrix multiplication in operator*=, mismatched sizes: ")+boost::lexical_cast<std::string>(getSizeX())+" != "+boost::lexical_cast<std::string>(a_other.getSizeY())));
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
		return *this;
	}

	Matrix& Matrix::operator*=( const MatrixValue& a_other ){
		std::for_each(content.begin(), content.end(), [&](MatrixRow &row){
			std::for_each(row.begin(), row.end(), [&](MatrixValue &value){
				value*=a_other;
			});
		});
		return *this;
	}

	Matrix& Matrix::operator/=( const MatrixValue& a_other ){
		std::for_each(content.begin(), content.end(), [&](MatrixRow &row){
			std::for_each(row.begin(), row.end(), [&](MatrixValue &value){
				value/=a_other;
			});
		});
		return *this;
	}

	std::shared_ptr<std::vector<MatrixValue>> Matrix::getMatrixArray(){
		auto matrixArrayRepresentation = std::make_shared<std::vector<MatrixValue>>(sizeX*sizeY);
		for(int y=0; y < sizeY; ++y){
			for(int x=0; x < sizeX; ++x){
				(*matrixArrayRepresentation)[y*sizeX + x] = content[x][y];
			}
		}

		return matrixArrayRepresentation;
	}


	TransformMatrix::TransformMatrix( MatrixValue a_value /*= 0.0*/ ) :Matrix(4, a_value){
		makeIdentity();
	}

	TransformMatrix::TransformMatrix( const Point &a_position ) :Matrix(4){
		makeIdentity();
		position(a_position.x, a_position.y, a_position.z);
	}

	TransformMatrix& TransformMatrix::makeIdentity(){
		content = MatrixContainer(sizeY, MatrixRow(sizeX, 0.0));
		for(int i=0; i < getSizeX() && i < getSizeY();++i){
			content[i][i] = 1;
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

	TransformMatrix& TransformMatrix::rotateX( MatrixValue a_radian ){
		//1	0	  0
		//0	cos	-sin
		//0	sin	cos
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation[1][1] = cos(a_radian);
		rotation[2][1] = -(sin(a_radian));
		rotation[1][2] = sin(a_radian);
		rotation[2][2] = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::rotateY( MatrixValue a_radian ){
		//cos	 0  sin
		//0		1  0
		//-sin	0  cos
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation[0][0] = cos(a_radian);
		rotation[2][0] = sin(a_radian);
		rotation[0][2] = -(sin(a_radian));
		rotation[2][2] = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::rotateZ( MatrixValue a_radian ){
		//cos	 -sin  0
		//sin	 cos	0
		//0		0	  1
		TransformMatrix rotation;
		rotation.makeIdentity();
		rotation[0][0] = cos(a_radian);
		rotation[1][0] = -(sin(a_radian));
		rotation[0][1] = sin(a_radian);
		rotation[1][1] = cos(a_radian);
		*this *= rotation;
		return *this;
	}

	TransformMatrix& TransformMatrix::position( MatrixValue a_x, MatrixValue a_y, MatrixValue a_z /*= 0.0*/ ){
		TransformMatrix translation;
		translation.makeIdentity();
		translation[3][0] = a_x;
		translation[3][1] = a_y;
		translation[3][2] = a_z;
		*this *= translation;
		return *this;
	}

	TransformMatrix& TransformMatrix::translate( MatrixValue a_x, MatrixValue a_y, MatrixValue a_z /*= 0.0*/ ){
		TransformMatrix translation;
		translation.makeIdentity();
		translation[3][0] += a_x;
		translation[3][1] += a_y;
		translation[3][2] += a_z;
		*this *= translation;
		return *this;
	}

	TransformMatrix& TransformMatrix::scale( MatrixValue a_x, MatrixValue a_y, MatrixValue a_z /*= 1.0*/ ){
		TransformMatrix scaling;
		scaling.makeIdentity();
		scaling[0][0] = a_x;
		scaling[1][1] = a_y;
		scaling[2][2] = a_z;
		*this *= scaling;
		return *this;
	}


	TransformMatrix& MatrixStack::top(){
		return stack.back();
	}

	TransformMatrix& MatrixStack::push(){
		stack.push_back(stack.back());
		return stack.back();
	}

	TransformMatrix& MatrixStack::push( const TransformMatrix &matrix ){
		stack.push_back(matrix);
		return stack.back();
	}

	void MatrixStack::pop(){
		stack.pop_back();
		if(stack.empty()){
			push(TransformMatrix());
		}
	}

	void MatrixStack::clear(){
		stack.clear();
		push(TransformMatrix());
	}

}
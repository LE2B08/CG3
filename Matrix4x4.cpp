#include "Matrix4x4.h"

Matrix4x4::Matrix4x4()
{
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			m[i][j] = 0.0f;
		}
	}
}

Matrix4x4::Matrix4x4(float elements[4][4])
{
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			m[i][j] = elements[i][j];
		}
	}
}

Matrix4x4& Matrix4x4::operator+=(const Matrix4x4& other)
{
	// 加算の実装
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m[i][j] += other.m[i][j];
	return *this;
}

Matrix4x4& Matrix4x4::operator-=(const Matrix4x4& other)
{
	// 減算の実装
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m[i][j] -= other.m[i][j];
	return *this;
}

Matrix4x4& Matrix4x4::operator*=(const Matrix4x4& other)
{
	// 乗算の実装
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = 0;
			for (int k = 0; k < 4; ++k) {
				result.m[i][j] += m[i][k] * other.m[k][j];
			}
		}
	}
	*this = result;
	return *this;
}

Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = m1;
	result += m2;
	return result;
}

Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = m1;
	result -= m2;
	return result;
}

Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = m1;
	result *= m2;
	return result;
}

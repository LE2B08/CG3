#pragma once
#include <stdexcept>

/// <summary>
/// 3次元ベクトル
/// </summary>
class Vector3 {
public:
	float x, y, z;

	Vector3() : x(0), y(0), z(0) {};
	Vector3(float x, float y, float z) : x(x), y(y), z(z) {};

	Vector3 operator+() const;
	Vector3 operator-() const;
	Vector3& operator+=(const Vector3& other);
	Vector3& operator-=(const Vector3& other);
	Vector3& operator*=(float s);
	Vector3& operator/=(float s);

	friend Vector3 operator+(const Vector3& v1, const Vector3& v2);
	friend Vector3 operator-(const Vector3& v1, const Vector3& v2);
	friend Vector3 operator*(const Vector3& v1, const Vector3& v2);
	friend Vector3 operator*(const Vector3& v, float s);
	friend Vector3 operator*(float s, const Vector3& v);
	friend Vector3 operator/(const Vector3& v, float s);

	// 等価演算子
	bool operator==(const Vector3& other) const;
	bool operator!=(const Vector3& other) const;

	// [] 演算子のオーバーロード（読み取り用）
	float operator[](int index) const {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		default:
			throw std::out_of_range("Index out of range");
		}
	}

	// [] 演算子のオーバーロード（書き込み用）
	float& operator[](int index) {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		default:
			throw std::out_of_range("Index out of range");
		}
	}
};

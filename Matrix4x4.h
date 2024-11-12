#pragma once

/// <summary>
/// 4x4行列
/// </summary>
class Matrix4x4 final {
public:
	float m[4][4];

	// デフォルトコンストラクタ
	Matrix4x4();

	// 指定された値で初期化するコンストラクタ
	Matrix4x4(float elements[4][4]);

	// 要素ごとに初期化するコンストラクタ
	Matrix4x4(
		float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33) {
		m[0][0] = m00;
		m[0][1] = m01;
		m[0][2] = m02;
		m[0][3] = m03;
		m[1][0] = m10;
		m[1][1] = m11;
		m[1][2] = m12;
		m[1][3] = m13;
		m[2][0] = m20;
		m[2][1] = m21;
		m[2][2] = m22;
		m[2][3] = m23;
		m[3][0] = m30;
		m[3][1] = m31;
		m[3][2] = m32;
		m[3][3] = m33;
	}

	Matrix4x4& operator+=(const Matrix4x4& other);
	Matrix4x4& operator-=(const Matrix4x4& other);
	Matrix4x4& operator*=(const Matrix4x4& other);

	friend Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2);
	friend Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2);
	friend Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
};

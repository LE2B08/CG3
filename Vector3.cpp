#include "Vector3.h"

Vector3 Vector3::operator+() const { return *this; }

Vector3 Vector3::operator-() const { return Vector3(-x, -y, -z); }

Vector3& Vector3::operator+=(const Vector3& other)
{
	x += other.x;
	y += other.y;
	z += other.z;
	return *this;
}

Vector3& Vector3::operator-=(const Vector3& other)
{
	x -= other.x;
	y -= other.y;
	z -= other.z;
	return *this;
}

Vector3& Vector3::operator*=(float s)
{
	x *= s;
	y *= s;
	z *= s;
	return *this;
}

Vector3& Vector3::operator/=(float s)
{
	x /= s;
	y /= s;
	z /= s;
	return *this;
}

bool Vector3::operator==(const Vector3& other) const
{
	return x == other.x && y == other.y && z == other.z;
}

bool Vector3::operator!=(const Vector3& other) const
{
	return !(*this == other);
}

Vector3 operator+(const Vector3& v1, const Vector3& v2) { return Vector3(v1) += v2; }

Vector3 operator-(const Vector3& v1, const Vector3& v2) { return Vector3(v1) -= v2; }

Vector3 operator*(const Vector3& v1, const Vector3& v2) { return Vector3(v1.x * v2.x, v1.y * v2.y, v1.z * v2.z); }

Vector3 operator*(const Vector3& v, float s) { return Vector3(v) *= s; }

Vector3 operator*(float s, const Vector3& v) { return Vector3(v) *= s; }

Vector3 operator/(const Vector3& v, float s) { return Vector3(v) /= s; }

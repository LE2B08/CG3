#pragma once
#include "Transform.h"
#include "Vector3.h"
#include "Vector4.h"

struct Particle final
{
	Transform transform; // 位置
	Vector3 velocity;	 // 速度
	Vector4 color;		 // 色
	float lifeTime;		 // 生存可能な時間
	float currentTIme;	 // 発生してからの経過時間
};
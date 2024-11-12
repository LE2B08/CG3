#pragma once
#include <cstdint>
#include "Transform.h"

struct Emitter final
{
	Transform transform; // !< エミッタのTransform
	uint32_t count;		 // !< 発生数
	float frequency;	 // !< 発生頻度
	float frequencyTime; // !< 頻度用時刻
};

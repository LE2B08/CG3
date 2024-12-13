#pragma once
#include "Vector3.h"
#include "Vector4.h"


// カメラのワールド位置を設定する構造体
struct CameraForGPU
{
	Vector3 worldPosition;
};


///==========================================================
/// DirectionalLightを拡張
///==========================================================
struct DirectionalLight final
{
	Vector4 color;		//!< ライトの色
	Vector3 direction;	//!< ライトの向き
	float intensity;	//!< 輝度
};
// PointLightを定義
struct PointLight
{
	Vector4 color;	  // ライトの色
	Vector3 position; // ライトの位置
	float intensity;  // 輝度
	float radius;	  // ライトの届く最大距離
	float decay;	  // 減衰率
	float padding[2];
};

// スポットライトを定義
struct SpotLight
{
	Vector4 color; // ライトの色
	Vector3 position; // ライトの位置
	float intensity; // スポットライトの輝度
	Vector3 direction; // スポットライトの方向
	float distance; // ライトの届く最大距離
	float decay; // 減衰率
	float cosFalloffStart; // 開始角度の余弦値
	float cosAngle; // スポットライトの余弦
	float padding[2];
};

// エリアライトを定義
struct AreaLight
{
	Vector4 color; // ライトの色
	Vector3 position; // ライトの位置
	Vector3 normal; // 面の法線
	float width; // エリアライトの幅
	float height; // エリアライトの高さ
	float intensity; // 輝度
	float padding[2];

};

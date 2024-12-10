#include "Object3d.hlsli"

// マテリアル
struct Material
{
    float4 color; // オブジェクトの色
    int enableLighting; // ライティングの有無
    float shininess; // 光沢度
    float4x4 uvTransform; // UVTransform
};

//平行光源
struct DirectionalLight
{
    float4 color; // ライトの色
    float3 direction; // ライトの向き
    float intensity; // 輝度
};

// カメラ
struct Camera
{
    float3 worldPosition;
};

// ポイントライト
struct PointLight
{
    float4 color; // ライトの色
    float3 position; // ライトの位置
    float intensity; // 輝度
    float radius; // ライトの届く最大距離
    float decay; // 減衰率
};

//ピクセルシェーダーの出力
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// メインのピクセルシェーダー関数
PixelShaderOutput main(VertexShaderOutput input)
{
    // 出力の初期化
    PixelShaderOutput output;

    // テクスチャの色をサンプリング (テクスチャ座標とサンプラーを使用)
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord); // テクスチャの色

    // UV座標の変換（UV変換行列を使用して）
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // ガンマ補正（ガンマ補正済みのテクスチャの場合、リニア空間に変換）
    textureColor.rgb = pow(textureColor.rgb, 2.2f); // ガンマ補正済みのテクスチャの場合、リニア空間に変換

    // 初期色を黒に設定
    float3 finalColor = float3(0.0f, 0.0f, 0.0f); // 初期色を真っ暗に設定

    // ポイントライトの影響のみを計算
    if (gPointLight.intensity > 0.0f)
    {
        // ポイントライトの方向を計算（ライト位置と現在のピクセル位置から）
        float3 pointLightDir = input.worldPosition - gPointLight.position;
        
        // ポイントライトへの距離
        float distance = length(pointLightDir);
        pointLightDir = normalize(pointLightDir);
        
        if (distance < gPointLight.radius)
        {
            // 法線ベクトルとポイントライト方向とのドット積を計算（拡散光成分）
            float NdotPointLight = max(dot(normalize(input.normal), -pointLightDir), 0.0f);
        
            // ハーフランバートシェーディングで光の拡散を滑らかにする
            float halfLambertPointLightFactor = saturate(pow(NdotPointLight * 0.5f + 0.5f, 2.0f));
        
            // 減衰（距離による減衰、逆二乗の法則）
            float attenuation = pow(max(1.0f - distance / gPointLight.radius, 0.0f), gPointLight.decay);
        
            // 拡散光の色を計算（ポイントライトの色、テクスチャ色、ライト強度を使用）
            float3 pointLightDiffuseColor = gPointLight.color.rgb * textureColor.rgb * gPointLight.intensity * halfLambertPointLightFactor * attenuation;

            // 視点方向を計算
            float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);

            // ハーフベクトル（ライト方向と視点方向の合成ベクトル）
            float3 pointLightHalfVector = normalize(-pointLightDir + viewDir);

            // 光沢度（シェーダーでの反射の強さ）を設定
            float shininess = max(gMaterial.shininess, 50.0f);

            // 鏡面反射光の色を計算（Phong反射モデルに基づく）
            float3 pointLightSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(max(dot(normalize(input.normal), pointLightHalfVector), 0.0f), shininess) * gPointLight.intensity * attenuation;

            // 拡散光と鏡面反射光を最終的な色に加算
            finalColor += pointLightDiffuseColor + pointLightSpecularColor;
        }
    }

    // 最終的な色を出力に設定（saturateは色を[0, 1]の範囲に制限）
    output.color.rgb = saturate(finalColor);

    // ガンマ補正を適用（必要なら）
    output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);

    // アルファ（透明度）の値を設定
    output.color.a = gMaterial.color.a * textureColor.a;

    // α値がほぼ0の場合、ピクセルを破棄（透明部分の処理）
    if (output.color.a < 0.001f)
    {
        discard;
    }

    // 最終的なピクセルカラーを出力
    return output;
}

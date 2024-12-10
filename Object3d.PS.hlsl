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

// スポットライト
struct SpotLight
{
    float4 color; // ライトの色
    float3 position; // ライトの壱
    float intensity; // 輝度
    float3 direction; // スポットライトの方向
    float distance; // ライトの届く最大距離
    float decay; // 減衰率
    float cosAngle; // スポットライトの余弦
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
ConstantBuffer<SpotLight> gSpotLight : register(b4);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ピクセルシェーダー
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord); // テクスチャの色 
    textureColor.rgb = pow(textureColor.rgb, 2.2f); // ガンマ補正済みのテクスチャの場合、リニア空間に変換 
    float3 finalColor = float3(0.0f, 0.0f, 0.0f); // 初期色を真っ暗に設定 
        
    if (gSpotLight.intensity > 0.0f)
    {
        float3 lightDir = normalize(input.worldPosition - gSpotLight.position);
        float distance = length(input.worldPosition - gSpotLight.position); // スポットライトの範囲内かチェック
        float spotEffect = dot(-lightDir, normalize(gSpotLight.direction));
        if (spotEffect > gSpotLight.cosAngle)
        {
            float NdotL = max(dot(normalize(input.normal), -lightDir), 0.0f);
            float halfLambertFactor = saturate(pow(NdotL * 0.5f + 0.5f, 2.0f)); // 距離による減衰計算
            float attenuation = pow(max(1.0 - distance / gSpotLight.distance, 0.0), gSpotLight.decay); // コーンによる減衰計算
            float coneEffect = saturate((spotEffect - gSpotLight.cosAngle) / (1.0 - gSpotLight.cosAngle)); // 拡散光の色
            float3 spotLightDiffuseColor = gSpotLight.color.rgb * textureColor.rgb * gSpotLight.intensity * halfLambertFactor * attenuation * coneEffect;
            float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
            float3 halfVector = normalize(-lightDir + viewDir);
            float shininess = max(gMaterial.shininess, 50.0f);
            float3 spotLightSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(max(dot(normalize(input.normal), halfVector), 0.0f), shininess) * gSpotLight.intensity * attenuation * coneEffect;
            finalColor += spotLightDiffuseColor + spotLightSpecularColor;
        }
    }
    output.color.rgb = saturate(finalColor);
    output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
    output.color.a = gMaterial.color.a * textureColor.a;
    if (output.color.a < 0.001f)
    {
        discard;
    }
    return output;
}
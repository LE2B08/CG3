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
ConstantBuffer<PointLight> gPotintLight : register(b3);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV設定
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy); // テクスチャの色

    // ガンマ補正済みのテクスチャの場合、リニア空間に変換
    textureColor.rgb = pow(textureColor.rgb, 2.2f);

    // ライト方向と法線、カメラ方向の計算
    float3 lightDir = normalize(-gDirectionalLight.direction); // ライト方向（逆方向）
    float3 normal = normalize(input.normal); // 法線の正規化
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition); // 視線方向

    // 環境光（Ambient）
    float3 ambientColor = gMaterial.color.rgb * gDirectionalLight.color.rgb * 0.00f; // 環境光を少し抑える

    // ハーフランバート反射の計算
    float NdotL = dot(normal, lightDir); // 法線と光の角度
    float halfLambertFactor = saturate(pow(NdotL * 0.5f + 0.5f, 2.0f)); // ハーフランバート反射

    // 拡散反射（Diffuse）
    float3 diffuseColor = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * halfLambertFactor;

    // 鏡面反射（Specular）
    float3 specularColor = float3(0.0f, 0.0f, 0.0f);
    if (NdotL > 0.0f)
    {
        float3 halfVector = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f); // 光沢度を調整
        specularColor = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, shininess) * gDirectionalLight.intensity;
    }

    /// ---------- ポイントライトの処理の追加 ---------- ///
    
    // 入射光を計算する
    float3 pointLightDir = normalize(input.worldPosition - gPotintLight.position);
    
    // 拡散反射の計算 : 点光源
    float NdotPointLight = max(dot(normal, -pointLightDir), 0.0f);
    float halfLambertPointLightFactor = saturate(pow(NdotPointLight * 0.5f + 0.5f, 2.0f)); // ハーフランバート反射
    
    float3 pointLightDiffuseColor = gPotintLight.color.rgb * textureColor.rgb * gPotintLight.intensity * halfLambertPointLightFactor;
    
    // 鏡面反射の計算 : 点光源
    float3 pointLightSpecularColor = float3(0.0f, 0.0f, 0.0f);
    if(NdotPointLight > 0.0f)
    {
        float3 pointLightHalfVector = normalize(-pointLightDir + viewDir);
        float NdotPointLight = max(dot(normal, pointLightHalfVector), 0.0f);
        pointLightSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(NdotPointLight, gMaterial.shininess) * gPotintLight.intensity;
    }
    
    // 照明効果の統合
    if (gMaterial.enableLighting != 0)
    {
        // 環境光 + 拡散反射 + 鏡面反射 + 点光源の拡散反射 + 点光源の鏡面反射
        float3 finalColor = ambientColor + diffuseColor + specularColor + pointLightDiffuseColor+pointLightSpecularColor;
        output.color.rgb = saturate(finalColor);

        // ガンマ補正を適用（必要なら）
        output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;

        // ガンマ補正は不要（出力次第で適用）
        output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
    }

    // α値がほぼ0の場合にピクセルを破棄
    if (output.color.a < 0.001f)
    {
        discard;
    }

    return output;
}

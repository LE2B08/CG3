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

//ピクセルシェーダー
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
    float3 ambientColor = gMaterial.color.rgb * gDirectionalLight.color.rgb * 0.05f; // 環境光を少し強調

    // ハーフランバート反射の計算
    float NdotL = dot(normal, lightDir); // 法線と光の角度
    float halfLambertFactor = saturate(pow(NdotL * 0.5f + 0.5f,2.0f)); // ハーフランバート反射

    // 拡散反射（Diffuse）
    float3 diffuseColor = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * halfLambertFactor;

    // 鏡面反射（Specular）
    float3 specularColor = float3(0.0f, 0.0f, 0.0f);
    if (NdotL > 0.0f)
    {
        float3 halfVector = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f); // 光沢度を少し低く調整
        specularColor = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, shininess) * gDirectionalLight.intensity;
    }

    // 照明効果の統合
    if (gMaterial.enableLighting != 0)
    {
        // 最終合成（ハーフランバート反射含む）
        float3 finalColor = ambientColor + diffuseColor + specularColor;
        output.color.rgb = saturate(finalColor);
        
        // ガンマ補正を適用(必要なら)
        //output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f); // ガンマ値2.2を適用
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        // ライティング無効時
        output.color = gMaterial.color * textureColor;
        
        // ガンマ値を適用
        output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
    }
    
    // α値がほぼ0の場合にピクセルを破棄
    if (output.color.a < 0.001f)
    {
        discard;
    }

    return output;
}

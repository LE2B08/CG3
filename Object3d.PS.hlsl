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

//ピクセルシェーダーの出力
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

//ピクセルシェーダー
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // UV設定
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    // テクスチャの色
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // ライト方向と法線、カメラ方向の計算
    float3 lightDir = normalize(-gDirectionalLight.direction);
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    
    // 環境光
    float3 ambientColor = gMaterial.color.rgb * gDirectionalLight.color.rgb * 0.02f; // 環境光を少し減らす
    
    // 照明の基本の色
    float3 diffuseColor = float3(0.0f, 0.0f, 0.0f); // 拡散反射
    float3 specularColor = float3(0.0f, 0.0f, 0.0f); // 鏡面反射
    
    //Lightingする場合
    if (gMaterial.enableLighting != 0)
    {
        /// ---------- 拡散反射（Diffuse Reflection）---------- ///
        float NdotL = max(dot(normal, lightDir), 0.0f); // 法線と光の角度
        float shadowFactor = saturate(NdotL * 0.5f + 0.5f); // 滑らかな影遷移
        // 拡散反射（Diffuse Reflection）
        diffuseColor = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * NdotL * shadowFactor; // 拡散反射を軽減
        
        /// ---------- 鏡面反射（Specular Reflection）---------- ///
        if (NdotL > 0.0f)
        {
            float3 reflectDir = reflect(-lightDir, normal); // 反射ベクトル
            float RdotV = saturate(dot(reflectDir, viewDir)); // 反射ベクトルと視線ベクトルの角度
            float shininess = max(gMaterial.shininess, 50.0f); // 遷移を滑らかに
            
            // ハイライト部分を白色に近づける
            float3 highlightColor = float3(1.0f, 1.0f, 1.0f); // 白いハイライト
            specularColor = highlightColor * pow(RdotV, shininess) * gDirectionalLight.intensity;
        }
        
        // 最終色の合成
        float3 finalColor = ambientColor + diffuseColor + specularColor * 1.2f;
        
        // ライティング結果を合成
        output.color.rgb = saturate(finalColor);
        
        // α値にはライティングを適用しない
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        // ライティングを無効にした場合の処理
        output.color = gMaterial.color * textureColor;
    }
    
    // アルファ値がほぼ0の場合にピクセルを破棄
    if (output.color.a < 0.001f)
    {
        discard;
    }
    
    return output;
}
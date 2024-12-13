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
    float cosFalloffStart; // 開始角度
    float cosAngle; // スポットライトの余弦
};

// エリアライト
struct AreaLight
{
    float4 color; // ライトの色
    float3 position; // ライトの位置
    float3 normal; // 面の法線
    float width; // エリアライトの幅
    float height; // エリアライトの高さ
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
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);
ConstantBuffer<AreaLight> gAreaLight : register(b5);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV設定
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy); // テクスチャの色
    textureColor.rgb = pow(textureColor.rgb, 2.2f); // ガンマ補正済みのテクスチャの場合、リニア空間に変換

    // ライト方向と法線、カメラ方向の計算
    float3 lightDir = normalize(gDirectionalLight.direction); // ライト方向（逆方向）
    float3 normal = normalize(input.normal); // 法線の正規化
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition); // 視線方向（カメラ方向）

    // 環境光（Ambient）
    float3 ambientColor = float3(0.1f, 0.1f, 0.1f); // アンビエントライトの色
    float ambientIntensity = 0.5f;
    
    /// ---------- 平行光源の処理 ---------- ///
    
     // ハーフランバート反射の計算
    float NdotL = dot(normal, lightDir); // 法線と光の角度
    float halfLambertFactor = saturate(pow(NdotL * 0.5f + 0.5f, 2.0f)); // ハーフランバート反射
    
    // 平行光源の拡散反射（Diffuse）
    float3 diffuseColor = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * gDirectionalLight.intensity * halfLambertFactor;

    // 平行光源の鏡面反射（Specular）
    float3 specularColor = float3(0.0f, 0.0f, 0.0f);
    if (gDirectionalLight.intensity > 0.0f && NdotL > 0.0f)
    {
        float3 halfVector = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f); // 光沢度を調整
        specularColor = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, shininess) * gDirectionalLight.intensity;
    }
    
    /// ---------- ポイントライトの処理 ---------- ///
   
    // ポイントライトの方向
    float3 pointLightDir = gPointLight.position - input.worldPosition;
    float distance = length(pointLightDir); // ポイントライトへの距離
    pointLightDir = normalize(pointLightDir); // 正規化
    
    // 減衰の計算（逆二乗の法則）
    float attenuation = 1.0f / (1.0f + gPointLight.decay * pow(distance / (gPointLight.radius + 1.0f), gPointLight.decay));
    attenuation = saturate(attenuation); // 0～1にクランプ
    
    // ポイントライトのハーフランバート反射の計算
    float pointNdotL = dot(normal, pointLightDir);
    float pointLightHalfLambertFactor = saturate(pow(pointNdotL * 0.5f + 0.5f, 2.0f)); // ハーフランバート反射
    
    // ポイントライトの拡散反射
    float3 pointDiffuseColor = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * gPointLight.intensity * pointLightHalfLambertFactor * attenuation;
    
    // ポイントライトの鏡面反射
    float3 pointSpecularColor = float3(0.0f, 0.0f, 0.0f);
    if (gPointLight.intensity > 0.0f && pointNdotL > 0.0f)
    {
        float3 pointHalfVector = normalize(pointLightDir + viewDir);
        float pointNdotH = max(dot(normal, pointHalfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f);
        pointSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(pointNdotH, shininess) * gPointLight.intensity;
    }
    
    /// ---------- スポットライトの処理 ---------- ///
    
    float3 spotLightDir = gSpotLight.position - input.worldPosition; // スポットライトからピクセルへの方向
    float spotLightDistance = length(spotLightDir); // 距離
    spotLightDir = normalize(spotLightDir); // 正規化
    
    // 距離減衰と逆二乗の法則を計算
    float spotAttenuation = 1.0f / (1.0f + gSpotLight.decay * pow(spotLightDistance / (gSpotLight.distance + 1.0f), 2.0f));
    spotAttenuation = saturate(spotAttenuation); // クランプ
    
    // 角度元帥の計算
    float cosAngle = dot(-spotLightDir, normalize(gSpotLight.direction));
    float spotAngleFactor = saturate((cosAngle - gSpotLight.cosAngle) / (gSpotLight.cosFalloffStart - gSpotLight.cosAngle)); // 緩やかに減衰
    
    // スポットライトの拡散反射
    float spotNdotL = dot(normal, spotLightDir); // 法線とライト方向
    float spotLightHalfLambertFactor = saturate(pow(spotNdotL * 0.5f + 0.5f, 2.0f)); // ハーフランバート反射
    float3 spotDiffuseColor = gMaterial.color.rgb * textureColor.rgb * gSpotLight.color.rgb * gSpotLight.intensity * spotLightHalfLambertFactor * spotAttenuation * spotAngleFactor;
    
    // スポットライトの鏡面反射
    float3 spotSpecularColor = float3(0.0f, 0.0f, 0.0f);
    if (gSpotLight.intensity > 0.0f && spotNdotL > 0.0f)
    {
        float3 spotHalfVector = normalize(spotLightDir + viewDir); // ハーフベクトル
        float spotNdotH = max(dot(normal, spotHalfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f);
        spotSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(spotNdotH, shininess) * gSpotLight.intensity * spotAttenuation * spotAngleFactor;
    }
    
    /// ---------- エリアライトの処理 ---------- ///

    // エリアライトの方向ベクトルの計算
    float3 areaLightDir = gAreaLight.position - input.worldPosition;
    float areaDistance = length(areaLightDir); // 距離減衰の計算
    areaLightDir = normalize(areaLightDir);
    
    float areaAttenuation = 1.0f / (1.0f + pow(areaDistance,2.0f));
    areaAttenuation = saturate(dot(normalize(gAreaLight.normal), -areaLightDir)); // 法線との角度に基づく減衰
    
    // エリアライトの拡散反射の計算
    float areaNdotL = max(dot(normal, areaLightDir), 0.0f); // 法線と光の角度
    float areaLightHalfLambertFactor = saturate(pow(areaNdotL * 0.5f + 0.5, 2.0f));
    float3 areaDiffuseColor = gMaterial.color.rgb * gAreaLight.color.rgb * gAreaLight.intensity * areaLightHalfLambertFactor * areaAttenuation;
    
    // エリアライトの鏡面反射の計算
    float3 areaSpecularColor = float3(0.0f, 0.0f, 0.0f);
    if(gAreaLight.intensity > 0.0f && areaNdotL > 0.0f)
    {
        float3 areaHalfVector = normalize(areaLightDir + viewDir);
        float areaNdotH = max(dot(normal, areaHalfVector), 0.0f);
        float shininess = max(gMaterial.shininess, 50.0f);
        areaSpecularColor = float3(1.0f, 1.0f, 1.0f) * pow(areaNdotH, shininess) * gAreaLight.intensity * areaAttenuation;
    }
    
    // 照明効果の統合
    if (gMaterial.enableLighting != 0)
    {
        // 環境光 + 拡散反射 + 鏡面反射 + 点光源の拡散反射 + 点光源の鏡面反射 + スポットライトの拡散反射 + スポットライトの鏡面反射
        float3 finalColor = 
        ambientColor * ambientIntensity
        + diffuseColor + specularColor
        + pointDiffuseColor + pointSpecularColor * attenuation
        + spotDiffuseColor + spotSpecularColor
        + areaDiffuseColor + areaSpecularColor
        ;
        
        output.color.rgb = saturate(finalColor);

        // ガンマ補正を適用（必要なら）
        //output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
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

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

// 影の柔らかさを強化する関数
float ComputeSoftShadow(float3 pointLightPos, float3 worldPos, float3 normal, float radius)
{
    // サンプリング数（制度とパフォーマンスを取る）
    const int sampleCount = 16;
    float shadow = 0.0f;
    
    // ランダムオフセットでサンプリング
    for (int i = 0; i < sampleCount; ++i)
    {
        // ランダムな方向を生成（ランダムなノイズを使うとさらに改善）
        float3 randomDir = normalize(float3(sin(i * 1.3f), cos(i * 2.1f), sin(i * 3.7f)));
        float3 samplePos = worldPos + randomDir * (radius / sampleCount);
        
        // サンプル位置とライトの位置の距離を比較
        float distToLight = length(samplePos - pointLightPos);
        shadow += saturate(1.0f - distToLight / radius);
    }
    
    // 平均化
    return shadow / sampleCount;
}

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
    float3 ambientColor = gMaterial.color.rgb * gDirectionalLight.color.rgb * 0.02f; // 環境光を少し強調

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
        float shininess = max(gMaterial.shininess, 50.0f); // 光沢度を少し低く調整
        specularColor = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, shininess) * gDirectionalLight.intensity;
    }

    // ---------- ポイントライトの処理を追加 ---------- ///
    float3 pointLightDir = normalize(gPotintLight.position - input.worldPosition); // ポイントライトの方向
    float pointNdotL = max(dot(normal, pointLightDir), 0.0f); // ポイントライトと法線の角度
    
    // 距離減衰の計算
    float distance = length(gPotintLight.position - input.worldPosition);
    float attenuation = saturate(1.0f - distance / gPotintLight.radius); // 距離による減衰（半径以内のみ有効）
    attenuation *= pow(attenuation, gPotintLight.decay); // 減衰率を適用
    
     // ライト範囲外の処理
    if (distance > gPotintLight.radius)
    {
        attenuation = 0.0f; // 範囲外では影響なし
    }
    
    // ポイントライトによる拡散反射
    diffuseColor += gMaterial.color.rgb * textureColor.rgb * gPotintLight.color.rgb * pointNdotL * gPotintLight.intensity * attenuation;
    
    // ポイントライトによる鏡面反射
    float3 pointHalfVector = normalize(pointLightDir + viewDir); // ハーフベクトル計算
    float pointNdotH = max(dot(normal, pointHalfVector), 0.0f); // 法線とハーフベクトルの内積
    specularColor += float3(1.0f, 1.0f, 1.0f) * pow(pointNdotH, gMaterial.shininess) * gPotintLight.intensity * attenuation;
    
    // ソフトシャドウの計算
    float softShadow = ComputeSoftShadow(gPotintLight.position, input.worldPosition, normal, gPotintLight.radius);
    
    // ソフトシャドウと影の色
    float3 shadowColor = ambientColor * 0.5f;
    shadowColor *= (1.0f - attenuation);
    diffuseColor = lerp(shadowColor, diffuseColor, softShadow);
    
    // 照明効果の統合
    if (gMaterial.enableLighting != 0)
    {
        // 環境光 + 拡散反射 + 鏡面反射
        float3 finalColor = ambientColor + diffuseColor + specularColor;
        
        // ライトの範囲外を暗くする
        if (distance > gPotintLight.radius)
        {
            finalColor = float3(0.0f, 0.0f, 0.0f); // 範囲外は暗くする
        }
        
        output.color.rgb = saturate(finalColor);
        
        // ガンマ補正を適用(必要なら)
        //output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f); // ガンマ値2.2を適用
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        // ライティング無効時
        output.color = gMaterial.color * textureColor;
        
        // ガンマ値を適用（出力次第で適用）
        output.color.rgb = pow(output.color.rgb, 1.0f / 2.2f);
    }
    
    // α値がほぼ0の場合にピクセルを破棄
    if (output.color.a < 0.001f)
    {
        discard;
    }

    return output;
}

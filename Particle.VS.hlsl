#include "Particle.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

//頂点シェーダーへの入力頂点構造
struct VertexShaderInput
{
    //POSITIONのことをセマンティクスという
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

StructuredBuffer<TransformationMatrix> gTransformationMatricies : register(t0);

//頂点シェーダー
VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    
    //入力された頂点座標を出職データに代入
    output.position = mul(input.position, gTransformationMatricies[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatricies[instanceId].World));
    return output;
}

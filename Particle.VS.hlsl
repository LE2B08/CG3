#include "Particle.hlsli"

struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
};

//頂点シェーダーへの入力頂点構造
struct VertexShaderInput
{
    //POSITIONのことをセマンティクスという
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

StructuredBuffer<ParticleForGPU> gParticle : register(t0);

//頂点シェーダー
VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    
    //入力された頂点座標を出職データに代入
    output.position = mul(input.position, gParticle[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.color = gParticle[instanceId].color;
   
    return output;
}

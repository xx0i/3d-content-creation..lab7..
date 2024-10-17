#pragma pack_matrix( row_major )  
// an ultra simple hlsl vertex shader
struct shaderVars
{
    float3 pos : POSITION;
    float3 norm : NORMAL;
    float2 texCoords : TEXCOORD;
    float4 tangents : TANGENT;
};

struct OUTPUT1
{
    float4 pos : SV_POSITION;
    float3 norm : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangents : TANGENT;
};

struct OUTPUT2
{
    float4 posH : SV_POSITION;
    float3 posW : WORLD;
    float3 normW : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangents : TANGENT;
};

cbuffer other_data
{
    matrix viewMatrix, perspectiveMatrix;
    vector lightColour;
    vector lightDir, camPos;
};

struct storageData
{
    matrix worldMatrix;
};

OUTPUT2 main(shaderVars input : POSITION, storageData world) : SV_POSITION 
{    
    //matrix result = mul(worldMatrix, viewMatrix);
    //result = mul(result, perspectiveMatrix);
    //float4 pos = mul(float4(input.pos, 1), result);
    
    float4 worldPos = mul(float4(input.pos, 1), world.worldMatrix);
    float4 viewPos = mul(worldPos, viewMatrix);
    float4 perspectivePos = mul(viewPos, perspectiveMatrix);
    
    float3 worldNorm = normalize(mul(input.norm, (float3x3) world.worldMatrix));
    
    OUTPUT2 output;
    output.posH = perspectivePos;
    output.posW = worldPos;
    output.normW = worldNorm;
    output.texCoord = input.texCoords;
    output.tangents = input.tangents;
	return output;
}
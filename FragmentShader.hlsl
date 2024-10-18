// an ultra simple hlsl fragment shader
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

StructuredBuffer<storageData> drawInfo : register(b1, space0);

Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space1);

float4 main(OUTPUT2 input) : SV_TARGET
{
    //temp hard coded data
    //static float4 diffuse = { 1.0f, 1.0f, 1.0f, 0.0f };
    static float4 specular = { 1.0f, 1.0f, 1.0f, 1.0f };
    //static float4 emissive = { 0.0f, 0.0f, 0.0f, 1.0f };
    static float4 ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
    static float ns = 160.0f;
    
    float4 textureColour = textures[3].Sample(samplers[0], input.texCoord.xy);
    float textureRoughness = textures[2].Sample(samplers[0], input.texCoord.xy).r;
    float3 normalMap = textures[1].Sample(samplers[0], input.texCoord.xy).xyz;
    normalMap.g = 1.0f - normalMap.g;
    normalMap *= 2.0f;
    normalMap -= 1.0f;
    float4 emissive = textures[0].Sample(samplers[0], input.texCoord.xy);
    
    float3 norm = normalize(input.normW);
    
    float tangentW = input.tangents.w;
    float3 normTan = normalize(input.tangents.xyz);
    float3 binormal = (cross(norm, normTan)) * tangentW;
   
    if (tangentW < 0.0f)
    {
        binormal = -binormal;
    }
    
    binormal = normalize(binormal);
    
    float3x3 tbn = { normTan, binormal, norm };
    
    float3 newNormWorld = mul(normalMap, tbn);
    
    float3 lightDirection = normalize(lightDir);
    float diffuseIntensity = saturate(dot(newNormWorld, -lightDirection));
    float4 finalColour = (diffuseIntensity + ambient) * textureColour * lightColour;

    //half-vector method
    float3 viewDir = normalize(camPos.xyz - input.posW);
    float3 halfVect = normalize(-lightDirection.xyz + viewDir);
    float intensity = pow(saturate(dot(newNormWorld, halfVect)), ns);
    float3 reflected = lightColour.xyz * (float3) specular * (intensity * textureRoughness);
    finalColour += float4(reflected, 1.0f);
    finalColour += emissive; //adding emissive
    
    return finalColour;
}
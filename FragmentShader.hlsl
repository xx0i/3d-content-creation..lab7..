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
    matrix worldMatrix, viewMatrix, perspectiveMatrix;
    vector lightColour;
    vector lightDir, camPos;
};

Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space1);

float4 main(OUTPUT2 input) : SV_TARGET
{
    //temp hard coded data
    //static float4 diffuse = { 1.0f, 1.0f, 1.0f, 0.0f };
    static float4 specular = { 1.0f, 1.0f, 1.0f, 1.0f };
    static float4 emissive = { 0.0f, 0.0f, 0.0f, 1.0f };
    static float4 ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
    static float ns = 160.0f;
    
    float4 textureColour = textures[2].Sample(samplers[0], input.texCoord.xy);
    float textureRoughness = textures[1].Sample(samplers[0], input.texCoord.xy).r;
    
    float3 norm = normalize(input.normW);
    float3 lightDirection = normalize(lightDir);
    float diffuseIntensity = saturate(dot(norm, -lightDirection));
    float4 finalColour = (diffuseIntensity + ambient) * textureColour * lightColour;
    
    //half-vector method
    float3 viewDir = normalize(camPos.xyz - input.posW);
    float3 halfVect = normalize(-lightDirection.xyz + viewDir);
    float intensity = pow(saturate(dot(norm, halfVect)), ns);
    float3 reflected = lightColour.xyz * (float3) specular * (intensity * textureRoughness);
    finalColour += float4(reflected, 1.0f);

    return finalColour;
}
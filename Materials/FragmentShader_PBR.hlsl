// Physically Based Rendering
// Copyright (c) 2017-2018 Michal Siejak

// Physically Based shading model: Lambetrtian diffuse BRDF + Cook-Torrance microfacet specular BRDF + IBL for ambient.

// This implementation is based on "Real Shading in Unreal Engine 4" SIGGRAPH 2013 course notes by Epic Games.
// See: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

// Adapted for training by Lari H. Norri for the course "3DCC" at Full Sail University, 2024.
// If you copy this shader be sure to include the author's "COPYING.txt" file in the same directory.
// All my custom modifications are CC0 and do not need to be attributed.

static const float PI = 3.141592;
static const float Epsilon = 0.00001;

// Constant normal incidence Fresnel factor for all dielectrics.
static const float3 Fdielectric = 0.04;

// ********* BEGIN PBR Math Functions *********

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;

    float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
    return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
    return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

// Shlick's approximation of the Fresnel factor.
float3 fresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// ********* END PBR Math Functions *********

cbuffer SHADER_VARS : register(b0, space0)
{
    float4x4 worldMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4 sunDirection, sunColor, sunAmbient, camPos;
};

struct V_OUT
{
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD_0;
    float4 tangent : TANGENT;
    float3 posW : POSITION;
};

// Identify which 2D texture is which
static const int albedoMap = 0;
static const int roughnessMetalOcclusionMap = 1;
static const int normalMap = 2;
static const int emissiveMap = 3;
static const int brdfMap = 4;
// Identify which sampler is which
static const int defaultSampler = 0;
// Identify which texture cube is which
static const int irradianceMap = 5;
static const int specularMap = 6;

Texture2D textures[] : register(t0, space1);
TextureCube cubeTextures[] : register(t0, space1); // can share space with 2D textures
SamplerState samplers[] : register(s0, space1);

// PBR Pixel Shader Adapted from: https://github.com/Nadrin/PBR/tree/master
float4 main(V_OUT vin) : SV_TARGET
{
   // Sample input textures to get shading model params.
    float3 albedo = textures[albedoMap].Sample(samplers[defaultSampler], vin.uv).rgb;
    float3 MRA = textures[roughnessMetalOcclusionMap].Sample(samplers[defaultSampler], vin.uv).rgb;
    float3 emissive = textures[emissiveMap].Sample(samplers[defaultSampler], vin.uv).rgb;
    float metalness = MRA.b;
    float roughness = MRA.g;
    float occlusion = MRA.r;
    
	// Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(camPos.xyz - vin.posW);

	// Get current fragment's normal and transform to world space.
    float3 rawNrm = textures[normalMap].Sample(samplers[defaultSampler], vin.uv).rgb;
    rawNrm.g = 1.0f - rawNrm.g; // Invert green channel to match DirectX normal map convention.
    float3 N = normalize(2.0 * rawNrm - 1.0);
    // construct TBN
    vin.nrm = normalize(vin.nrm);
    vin.tangent.xyz = normalize(vin.tangent.xyz);
    // compute bi-tangent vector
    float3 binormal = cross(vin.nrm, vin.tangent.xyz) * vin.tangent.w;
    // compute tbn matrix
    float3x3 tbn = float3x3(vin.tangent.xyz, binormal, vin.nrm);
    // apply tbn matrix to normal
    N = normalize(mul(N, tbn));
	// Angle between surface normal and outgoing light direction.
    float cosLo = max(0.0, dot(N, Lo));
		
	// Specular reflection vector.
    float3 Lr = 2.0 * cosLo * N - Lo;

	// Fresnel reflectance at normal incidence (for metals use albedo color).
    float3 F0 = lerp(Fdielectric, albedo, metalness);

	// Direct lighting calculation for analytical lights.
    // Teachers's note: This example is only for one directional light source.
    // For multiple lights, you would loop over all lights and accumulate the results.
    float3 directLighting = 0.0;
    {
        float3 Li = -sunDirection.xyz;
        float3 Lradiance = sunColor.rgb;

		// Half-vector between Li and Lo.
        float3 Lh = normalize(Li + Lo);

		// Calculate angles between surface normal and various light vectors.
        float cosLi = max(0.0, dot(N, Li));
        float cosLh = max(0.0, dot(N, Lh));

		// Calculate Fresnel term for direct lighting. 
        float3 F = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
		// Calculate normal distribution for specular BRDF.
        float D = ndfGGX(cosLh, roughness);
		// Calculate geometric attenuation for specular BRDF.
        float G = gaSchlickGGX(cosLi, cosLo, roughness);

		// Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
		// Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
		// To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
        float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);

		// Lambert diffuse BRDF.
		// We don't scale by 1/PI for lighting & material units to be more convenient.
		// See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
        float3 diffuseBRDF = kd * albedo;

		// Cook-Torrance specular microfacet BRDF.
        float3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

		// Total contribution for this light.
        directLighting += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
    }

	// Ambient lighting (IBL).
    // Teacher's note: This is the IBL part of the shader, its the light from your surroundings.
    // You should only do this once, though some engines have multiple IBLs. (e.g. skybox, reflection probes)
    float3 ambientLighting;
	{
		// Sample diffuse irradiance at normal direction.
        float3 irradiance = cubeTextures[irradianceMap].Sample(samplers[defaultSampler], N).rgb;

		// Calculate Fresnel term for ambient lighting.
		// Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
		// use cosLo instead of angle with light's half-vector (cosLh above).
		// See: https://seblagarde.wordpress.com/2011/08/17/hello-world/
        float3 F = fresnelSchlick(F0, cosLo);

		// Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);

		// Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;

        // determine number of mipmap levels for specular IBL environment map.
        uint width, height, specularTextureLevels;
        cubeTextures[specularMap].GetDimensions(0, width, height, specularTextureLevels);
		// Sample pre-filtered specular reflection environment at correct mipmap level.
        float3 specularIrradiance = cubeTextures[specularMap].SampleLevel(samplers[defaultSampler], Lr, roughness * specularTextureLevels).rgb;

		// Split-sum approximation factors for Cook-Torrance specular BRDF. 
        float2 specularBRDF = textures[brdfMap].Sample(samplers[defaultSampler], float2(cosLo, roughness)).rg;

		// Total specular IBL contribution.
        float3 specularIBL = (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

		// Total ambient lighting contribution.
        ambientLighting = diffuseIBL + specularIBL * occlusion;
    }
    
	// Final fragment color.
    return float4(directLighting + ambientLighting + emissive, 1.0);
}
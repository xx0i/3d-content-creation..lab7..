This folder contains Image Based Lighting Enviroments for use with Physically Based Rendering Shaders

The included files were grabbed from the following KhronosGroup Repo: (note the branch name)
https://github.com/KhronosGroup/glTF-Sample-Environments/tree/updated_environments

The above repo contains other samples and source code to build a tool to generate other environments

The included "lut_ggx.png" contains pre-calculated BRDF values that will be used by our PBR shader.
This file can also be geneated by the aforementioned tool, but I just copied it from this repo:
https://github.com/KhronosGroup/glTF-Sample-Viewer/tree/main/assets/images

Files of Note: 

PVRTexToolSetup-2024_R1.exe
	- I have included an installer for the PVRTexTool which is a powerful GPU texture processor & viewer

lux_ggx.png
	- Pre-Calculated GGX BRDF Texture for use in PBR formula

diffuse.ktx2
	- Pre-Calculated Diffuse Light from the "field" panorama

specular.ktx2
	- Pre-Calculated Specular Light from the "field" panorama


The remaining files are a jpg of the panorama and an HDR image used to derive the light energy
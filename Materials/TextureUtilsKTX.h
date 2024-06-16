#ifndef TEXTUREUTILSKTX_H
#define TEXTUREUTILSKTX_H

// Requires KTX Texture Library and Gateware.h

// function to upload a texture to the GPU
bool UploadKTXTextureToGPU(	GW::GRAPHICS::GVulkanSurface _surface, const std::string& _ktx_img,
							VkBuffer& _outTextureBuffer, VkDeviceMemory& _outTextureMemory,
							VkImage& _outTextureImage, VkImageView& _outTextureImageView)
{
	// Gateware, access to underlying Vulkan queue and command pool & physical device
	VkDevice device;
	VkQueue graphicsQueue;
	VkCommandPool cmdPool;
	VkPhysicalDevice physicalDevice;
	_surface.GetDevice((void**)&device);
	_surface.GetGraphicsQueue((void**)&graphicsQueue);
	_surface.GetCommandPool((void**)&cmdPool);
	_surface.GetPhysicalDevice((void**)&physicalDevice);
	// libktx, temporary variables
	ktxVulkanTexture texture;
	ktxTexture* kTexture;
	KTX_error_code ktxresult;
	ktxVulkanDeviceInfo vdi;
	// used to transfer texture CPU memory to GPU. just need one
	ktxresult = ktxVulkanDeviceInfo_Construct(&vdi, physicalDevice, device,
		graphicsQueue, cmdPool, nullptr);
	if (ktxresult != KTX_error_code::KTX_SUCCESS)
		return false;
	// load texture into CPU memory from file
	ktxresult = ktxTexture_CreateFromNamedFile(_ktx_img.c_str(),
		KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture);
	if (ktxresult != KTX_error_code::KTX_SUCCESS)
		return false;
	// This gets mad if you don't encode/save the .ktx file in a format Vulkan likes
	ktxresult = ktxTexture_VkUploadEx(kTexture, &vdi, &texture,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (ktxresult != KTX_error_code::KTX_SUCCESS)
		return false;
	// after loading all textures you don't need these anymore
	ktxTexture_Destroy(kTexture);
	ktxVulkanDeviceInfo_Destruct(&vdi);
	// Create image view.
	VkImageViewCreateInfo viewInfo = {};
	// Set the non-default values.
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.flags = 0;
	viewInfo.components = {
		VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
	};
	viewInfo.image = texture.image;
	viewInfo.format = texture.imageFormat;
	viewInfo.viewType = texture.viewType;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.layerCount = texture.layerCount;
	viewInfo.subresourceRange.levelCount = texture.levelCount;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.pNext = nullptr;
	VkResult vr = vkCreateImageView(device, &viewInfo, nullptr, &_outTextureImageView);
	if (vr != VkResult::VK_SUCCESS)
		return false;
	// transfer all the data to the output variables
	_outTextureBuffer = nullptr; // handled by the ktx library
	_outTextureMemory = texture.deviceMemory;
	_outTextureImage = texture.image;
	return true;
}
#endif // !TEXTUREUTILSKTX_H
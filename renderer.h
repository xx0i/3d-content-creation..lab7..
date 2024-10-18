#define TINYGLTF_IMPLEMENTATION //needed for linking tinygltf
#define	STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../TinyGLTF/tiny_gltf.h"
#include "TextureUtils.h"

#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib") 
#endif

void PrintLabeledDebugString(const char* label, const char* toPrint)
{
	std::cout << label << toPrint << std::endl;
#if defined WIN32 //OutputDebugStringA is a windows-only function 
	OutputDebugStringA(label);
	OutputDebugStringA(toPrint);
#endif
}

class Renderer
{
	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;

	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;
	VkRenderPass renderPass;
	VkBuffer geometryHandle = nullptr;
	VkDeviceMemory geometryData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule fragmentShader = nullptr;
	// pipeline settings for drawing (also required)
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;
	unsigned int windowWidth, windowHeight;

	tinygltf::Model model; //instance of model from tinygltf
	tinygltf::TinyGLTF loader; //instance of the tinygltf class
	std::string err;
	std::string warn;

	//buffers
	std::vector<VkBuffer> uniformBufferHandle;
	std::vector<VkDeviceMemory> uniformBufferData;
	VkDescriptorSetLayout descriptorSetLayout = nullptr;
	VkDescriptorPool descriptorPool = nullptr;
	std::vector<VkDescriptorSet> descriptorSets = {};

	std::vector<VkBuffer> storageBufferHandle;
	std::vector<VkDeviceMemory> storageBufferData;

	std::vector<uint8_t> geometry{};

	//3d matrices
	GW::MATH::GMatrix interfaceProxy;
	GW::MATH::GMATRIXF worldMatrix = GW::MATH::GIdentityMatrixF;
	GW::MATH::GMATRIXF viewMatrix = GW::MATH::GIdentityMatrixF;
	GW::MATH::GMATRIXF perspectiveMatrix = GW::MATH::GIdentityMatrixF;

	struct shaderVars
	{
		GW::MATH::GMATRIXF viewMatrix, perspectiveMatrix;
		GW::MATH::GVECTORF lightColour;
		GW::MATH::GVECTORF lightDir, camPos;
	};
	shaderVars shaderVarsUniformBuffer{};

	//camera controls
	GW::INPUT::GInput input;
	GW::INPUT::GController controller;
	std::chrono::high_resolution_clock::time_point startTime;

	//lighting information
	GW::MATH::GVECTORF lightColour = { 0.9f, 0.9f, 1.0f, 1.0f };
	GW::MATH::GVECTORF lightDir = { -1.f, -1.0f, 1.0f };

	//texturing
	struct textureInfo
	{
		VkImage image;
		VkImageView imageView;
		VkBuffer textureHandle;
		VkDeviceMemory textureData;
	};
	std::vector<textureInfo> textures;

	VkDescriptorSetLayout textureDescriptorSetLayout = nullptr;
	VkDescriptorSet textureDescriptorSets;
	VkSampler textureSampler{};

	struct storageData
	{
		GW::MATH::GMATRIXF worldMatrix;
	};
	storageData world = {};

public:

	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		win = _win;
		vlk = _vlk;
		UpdateWindowDimensions();
		GetHandlesFromSurface();

		startTime = std::chrono::high_resolution_clock::now();
		interfaceProxy.Create();

		initializeWorldMatrix();
		world.worldMatrix = worldMatrix;
		initializeViewMatrix();
		shaderVarsUniformBuffer.viewMatrix = viewMatrix;
		initializePerspectiveMatrix();
		shaderVarsUniformBuffer.perspectiveMatrix = perspectiveMatrix;
		shaderVarsUniformBuffer.lightColour = lightColour;
		shaderVarsUniformBuffer.lightDir = lightDir;

		//controllers for camera
		input.Create(win);
		controller.Create();
		//loadingRudimentaryfromGltf("C:/full sail/3d content creation/3dcc-lab-5-xx0i/Models/triangle.gltf");
		//loadingRudimentaryfromGltf("C:/full sail/3d content creation/3dcc-lab-5-xx0i/Models/triangle_blender.gltf");
		//loadingRudimentaryfromGltf("C:/full sail/3d content creation/3dcc-lab-5-xx0i/Models/cat_blender.gltf");
		//loadingRudimentaryfromGltf("C:/full sail/3d content creation/3dcc-lab-6-xx0i/Models/fish_blender.gltf");
		loadingRudimentaryfromGltf("C:/full sail/3d content creation/3dcc-lab-7-xx0i/Models/WaterBottle/bottle_blender.gltf");
		loadImageFromGltf();
		createDescriptorLayout();
		InitializeGraphics();
		BindShutdownCallback();
	}

	void initializeViewMatrix()
	{
		uint32_t currentImage;
		vlk.GetSwapchainCurrentImage(currentImage);

		GW::MATH::GVECTORF cameraPosition = { 1.0f, 0.25f, -0.5f };
		GW::MATH::GVECTORF targetPosition = { 0.0f, 0.02f, 0.0f };
		GW::MATH::GVECTORF upVector = { 0.0f, 1.0f, 0.0f };
		interfaceProxy.LookAtLHF(cameraPosition, targetPosition, upVector, viewMatrix);
		shaderVarsUniformBuffer.camPos = cameraPosition;
	}

	void initializePerspectiveMatrix()
	{
		float aspectRatio = 0.0f;
		vlk.GetAspectRatio(aspectRatio);
		interfaceProxy.ProjectionDirectXLHF(G_DEGREE_TO_RADIAN_F(65.0f), aspectRatio, 0.00001f, 10000.0f, perspectiveMatrix);
	}

	void initializeWorldMatrix()
	{
		GW::MATH::GVECTORF negativez = { 1.0f, 1.0f, -1.0f, 1.0f };
		interfaceProxy.ScaleLocalF(worldMatrix, negativez, worldMatrix);
	}

	void loadingRudimentaryfromGltf(std::string filepath)
	{
		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
		//bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, filepath); // for binary glTF(.glb)

		if (!warn.empty()) {
			printf("Warn: %s\n", warn.c_str());
		}

		if (!err.empty()) {
			printf("Err: %s\n", err.c_str());
		}

		if (!ret) {
			printf("Failed to parse glTF\n");
		}
		else if (ret)
		{
			printf("Successfully loaded glTF: %s\n", filepath.c_str());

			// Print number of meshes
			printf("Number of meshes: %zu\n", model.meshes.size());
			for (const auto& mesh : model.meshes) {
				printf("Mesh name: %s\n", mesh.name.c_str());
				printf("Number of primitives: %zu\n", mesh.primitives.size());

				for (const auto& primitive : mesh.primitives) {
					// Print attributes
					for (const auto& attribute : primitive.attributes) {
						printf("Attribute: %s\n", attribute.first.c_str());
					}
				}
			}

			printf("\nNumber of buffers: %zu\n", model.buffers.size());
			for (const auto& buffer : model.buffers) {
				printf("Buffer URI: %s\n", buffer.uri.c_str());
				printf("Buffer byteLength: %zu\n", buffer.data.size());
			}

			// Print buffer views
			printf("\nNumber of buffer views: %zu\n", model.bufferViews.size());
			for (const auto& bufferView : model.bufferViews) {
				printf("Buffer view:\n");
				printf("  Buffer: %d\n", bufferView.buffer);
				printf("  Byte Offset: %d\n", bufferView.byteOffset);
				printf("  Byte Length: %d\n", bufferView.byteLength);
				printf("  Target: %d\n", bufferView.target);
			}
		}
	}

	void loadImageFromGltf()
	{
		unsigned int size = model.images.size();
		textures.resize(size);

		for (int i = 0; i < size; i++)
		{
			tinygltf::Image temp;
			temp.mimeType = getMimeTypeFromUri(model.images[i].uri);
			temp.name = model.images[i].name;
			temp.uri = model.images[i].uri;
			temp.bits = model.images[i].bits;
			temp.height = model.images[i].height;
			temp.width = model.images[i].width;
			temp.component = model.images[i].component;
			temp.pixel_type = model.images[i].pixel_type;
			temp.bufferView = model.images[i].bufferView;
			temp.image = model.images[i].image;
			temp.image.resize(temp.width * temp.height * temp.component);
			UploadTextureToGPU(vlk, temp, textures[i].textureHandle, textures[i].textureData, textures[i].image, textures[i].imageView);
		}

	}

	std::string getMimeTypeFromUri(const std::string& uri)
	{
		if (uri.find(".jpg") != std::string::npos || uri.find(".jpeg") != std::string::npos) {
			return "image/jpeg";
		}
		if (uri.find(".png") != std::string::npos) {
			return "image/png";
		}
		return "unknown";
	}

	void createDescriptorLayout()
	{
		VkDescriptorSetLayoutBinding uniformBinding = {};
		uniformBinding.binding = 0;
		uniformBinding.descriptorCount = 1;
		uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uniformBinding.pImmutableSamplers = nullptr;
		uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding storageBinding = {};
		storageBinding.binding = 1;
		storageBinding.descriptorCount = 1;
		storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		storageBinding.pImmutableSamplers = nullptr;
		storageBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uniformBinding, storageBinding };

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.bindingCount = 2;
		layoutInfo.flags = 0;
		layoutInfo.pBindings = bindings.data();
		layoutInfo.pNext = nullptr;
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

		vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);


		VkDescriptorSetLayoutBinding textureBinding = {};
		textureBinding.binding = 0;
		textureBinding.descriptorCount = textures.size();
		textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureBinding.pImmutableSamplers = nullptr;
		textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorBindingFlagsEXT textureBindingFlag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

		VkDescriptorSetLayoutBindingFlagsCreateInfoEXT textureFlag = {};
		textureFlag.bindingCount = 1;
		textureFlag.pBindingFlags = &textureBindingFlag;
		textureFlag.pNext = nullptr;
		textureFlag.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;

		VkDescriptorSetLayoutCreateInfo textureLayoutInfo = {};
		textureLayoutInfo.bindingCount = 1;
		textureLayoutInfo.flags = 0;
		textureLayoutInfo.pBindings = &textureBinding;
		textureLayoutInfo.pNext = &textureFlag;
		textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

		vkCreateDescriptorSetLayout(device, &textureLayoutInfo, nullptr, &textureDescriptorSetLayout);
	}

private:
	void UpdateWindowDimensions()
	{
		win.GetClientWidth(windowWidth);
		win.GetClientHeight(windowHeight);
	}

	void InitializeGraphics()
	{
		InitializeGeometryBuffer();
		//buffers + descriptors
		initializeUniformBuffer();
		initializeStorageBuffer();
		initializeDescriptorPool();
		initializeDescriptorSets();
		linkDescriptorSetUniformBuffer();
		CompileShaders();
		InitializeGraphicsPipeline();
	}

	void GetHandlesFromSurface()
	{
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		vlk.GetRenderPass((void**)&renderPass);
	}

	void InitializeGeometryBuffer()
	{
		//vertex data
		const tinygltf::Primitive& primitive = model.meshes[0].primitives[0]; //position data
		const tinygltf::Accessor& accessPos = model.accessors[primitive.attributes.at("POSITION")];
		const tinygltf::BufferView& bufferViewPos = model.bufferViews[accessPos.bufferView];
		const float* posData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewPos.buffer].data[bufferViewPos.byteOffset + accessPos.byteOffset]);

		//normal data
		const tinygltf::Accessor& accessNorm = model.accessors[primitive.attributes.at("NORMAL")];
		const tinygltf::BufferView& bufferViewNorm = model.bufferViews[accessNorm.bufferView];
		const float* normData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewNorm.buffer].data[bufferViewNorm.byteOffset + accessNorm.byteOffset]);

		//texcoord data
		const tinygltf::Accessor& accessTex = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const tinygltf::BufferView& bufferViewTex = model.bufferViews[accessTex.bufferView];
		const float* texData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTex.buffer].data[bufferViewTex.byteOffset + accessTex.byteOffset]);

		//tangent data
		const tinygltf::Accessor& accessTan = model.accessors[primitive.attributes.at("TANGENT")];
		const tinygltf::BufferView& bufferViewTan = model.bufferViews[accessTan.bufferView];
		const float* tanData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTan.buffer].data[bufferViewTan.byteOffset + accessTan.byteOffset]);

		//index data
		const tinygltf::Accessor& accessIndices = model.accessors[primitive.indices];
		const tinygltf::BufferView& bufferViewIndices = model.bufferViews[accessIndices.bufferView];
		const unsigned short* indexData = reinterpret_cast<const unsigned short*>
			(&model.buffers[bufferViewIndices.buffer].data[bufferViewIndices.byteOffset]);

		unsigned int indexDataSize = bufferViewIndices.byteLength;
		unsigned int posDataSize = bufferViewPos.byteLength;
		unsigned int normDataSize = bufferViewNorm.byteLength;
		unsigned int texDataSize = bufferViewTex.byteLength;
		unsigned int tanDataSize = bufferViewTan.byteLength;

		unsigned int totalSize = posDataSize + normDataSize + texDataSize + tanDataSize + indexDataSize;

		if (!totalSize % 4 == 0) //adds the padding if needed
		{
			totalSize += totalSize % 4;
		}

		geometry.resize(totalSize);

		std::memcpy(geometry.data(), posData, posDataSize);
		std::memcpy(geometry.data() + posDataSize, normData, normDataSize);
		std::memcpy(geometry.data() + posDataSize + normDataSize, texData, texDataSize);
		std::memcpy(geometry.data() + posDataSize + normDataSize + texDataSize, tanData, tanDataSize);
		std::memcpy(geometry.data() + posDataSize + normDataSize + texDataSize + tanDataSize, indexData, indexDataSize);

		CreateGeometryBuffer(&geometry[0], geometry.size());
	}

	void CreateGeometryBuffer(const void* data, unsigned int sizeInBytes)
	{
		GvkHelper::create_buffer(physicalDevice, device, sizeInBytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &geometryHandle, &geometryData);
		// Transfer triangle data to the vertex buffer. (staging would be prefered here)
		GvkHelper::write_to_buffer(device, geometryData, data, sizeInBytes);
	}

	void initializeUniformBuffer()
	{
		unsigned int bufferSize = sizeof(shaderVars);  //size of the uniform data

		//gets the number of active frames
		uint32_t imageCount;
		vlk.GetSwapchainImageCount(imageCount);

		//resizes the vectors for the uniform buffers for each frame
		uniformBufferHandle.resize(imageCount);
		uniformBufferData.resize(imageCount);

		for (size_t i = 0; i < imageCount; i++) //loops through each active frame and creates a buffer for each
		{
			GvkHelper::create_buffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferHandle[i], &uniformBufferData[i]);
			GvkHelper::write_to_buffer(device, uniformBufferData[i], &shaderVarsUniformBuffer, bufferSize);
		}
	}

	void initializeStorageBuffer()
	{
		unsigned int bufferSize = sizeof(storageData);  //size of the storage data

		//gets the number of active frames
		uint32_t imageCount;
		vlk.GetSwapchainImageCount(imageCount);

		//resizes the vectors for the uniform buffers for each frame
		storageBufferHandle.resize(imageCount);
		storageBufferData.resize(imageCount);

		for (size_t i = 0; i < imageCount; i++) //loops through each active frame and creates a buffer for each
		{
			GvkHelper::create_buffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storageBufferHandle[i], &storageBufferData[i]);
			GvkHelper::write_to_buffer(device, storageBufferData[i], &world, bufferSize);
		}
	}

	void initializeDescriptorPool()
	{
		VkDescriptorPoolSize uniformPoolSize = {};
		uniformPoolSize.descriptorCount = uniformBufferHandle.size();
		uniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

		VkDescriptorPoolSize storagePoolSize = {};
		storagePoolSize.descriptorCount = storageBufferHandle.size();
		storagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

		VkDescriptorPoolSize texturePoolSize = {};
		texturePoolSize.descriptorCount = 1;
		texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		std::vector<VkDescriptorPoolSize> poolSizes = { uniformPoolSize, storagePoolSize, texturePoolSize };

		VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
		descriptorPoolInfo.flags = 0;
		descriptorPoolInfo.maxSets = uniformBufferHandle.size() + 1;
		descriptorPoolInfo.pNext = nullptr;
		descriptorPoolInfo.poolSizeCount = poolSizes.size();
		descriptorPoolInfo.pPoolSizes = poolSizes.data();
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

		vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);
	}

	void initializeDescriptorSets()
	{
		VkDescriptorSetAllocateInfo descriptorAllocateInfo = {};
		descriptorAllocateInfo.descriptorPool = descriptorPool;
		descriptorAllocateInfo.descriptorSetCount = 1;
		descriptorAllocateInfo.pNext = nullptr;
		descriptorAllocateInfo.pSetLayouts = &descriptorSetLayout;
		descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

		descriptorSets.resize(uniformBufferHandle.size());

		for (int i = 0; i < uniformBufferData.size(); i++)
		{
			vkAllocateDescriptorSets(device, &descriptorAllocateInfo, &descriptorSets[i]);
		}

		const uint32_t arraySize = textures.size();

		VkDescriptorSetVariableDescriptorCountAllocateInfoEXT textureAllocateExt = {};
		textureAllocateExt.descriptorSetCount = 1;
		textureAllocateExt.pDescriptorCounts = &arraySize;
		textureAllocateExt.pNext = nullptr;
		textureAllocateExt.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
		
		VkDescriptorSetAllocateInfo descriptorAllocateInfoTexture = {};
		descriptorAllocateInfoTexture.descriptorPool = descriptorPool;
		descriptorAllocateInfoTexture.descriptorSetCount = 1;
		descriptorAllocateInfoTexture.pNext = &textureAllocateExt;
		descriptorAllocateInfoTexture.pSetLayouts = &textureDescriptorSetLayout;
		descriptorAllocateInfoTexture.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

		VkResult result = vkAllocateDescriptorSets(device, &descriptorAllocateInfoTexture, &textureDescriptorSets);
	}

	void linkDescriptorSetUniformBuffer()
	{
		for (int i = 0; i < uniformBufferData.size(); i++)
		{
			VkDescriptorBufferInfo uniformDescriptorBuffer = {};
			uniformDescriptorBuffer.buffer = uniformBufferHandle[i];
			uniformDescriptorBuffer.offset = 0;
			uniformDescriptorBuffer.range = sizeof(shaderVars);

			VkDescriptorBufferInfo storageDescriptorBuffer = {};
			storageDescriptorBuffer.buffer = storageBufferHandle[i];
			storageDescriptorBuffer.offset = 0;
			storageDescriptorBuffer.range = sizeof(storageData);

			VkWriteDescriptorSet writeUniformDescriptor = {};
			writeUniformDescriptor.descriptorCount = 1;
			writeUniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeUniformDescriptor.dstArrayElement = 0;
			writeUniformDescriptor.dstBinding = 0;
			writeUniformDescriptor.dstSet = descriptorSets[i];
			writeUniformDescriptor.pBufferInfo = &uniformDescriptorBuffer;
			writeUniformDescriptor.pImageInfo = nullptr;
			writeUniformDescriptor.pNext = nullptr;
			writeUniformDescriptor.pTexelBufferView = nullptr;
			writeUniformDescriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;


			VkWriteDescriptorSet writeStorageDescriptor = {};
			writeStorageDescriptor.descriptorCount = 1;
			writeStorageDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writeStorageDescriptor.dstArrayElement = 0;
			writeStorageDescriptor.dstBinding = 1;
			writeStorageDescriptor.dstSet = descriptorSets[i];
			writeStorageDescriptor.pBufferInfo = &storageDescriptorBuffer;
			writeStorageDescriptor.pImageInfo = nullptr;
			writeStorageDescriptor.pNext = nullptr;
			writeStorageDescriptor.pTexelBufferView = nullptr;
			writeStorageDescriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

			std::array<VkWriteDescriptorSet, 2> writeDescriptors = { writeUniformDescriptor, writeStorageDescriptor };

			vkUpdateDescriptorSets(device,	writeDescriptors.size(), writeDescriptors.data(), 0, nullptr);
		}
		CreateSampler(vlk, textureSampler);
		std::vector<VkDescriptorImageInfo> infos = {};
		std::vector<VkDescriptorBufferInfo> ff = {};
		for (int i = 0; i < textures.size(); i++)
		{
			VkDescriptorBufferInfo textureDescriptorBuffer = {};
			textureDescriptorBuffer.buffer = textures[i].textureHandle;
			textureDescriptorBuffer.offset = 0;
			textureDescriptorBuffer.range = sizeof(textures);
			ff.push_back(textureDescriptorBuffer);

			VkDescriptorImageInfo imageInfo = {};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = textures[i].imageView;
			imageInfo.sampler = textureSampler;
			infos.push_back(imageInfo);
		}

			VkWriteDescriptorSet writeTextureDescriptor = {};
			writeTextureDescriptor.descriptorCount = textures.size();
			writeTextureDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeTextureDescriptor.dstArrayElement = 0;
			writeTextureDescriptor.dstBinding = 0;
			writeTextureDescriptor.dstSet = textureDescriptorSets;
			writeTextureDescriptor.pBufferInfo = ff.data();
			writeTextureDescriptor.pNext = nullptr;
			writeTextureDescriptor.pTexelBufferView = nullptr;
			writeTextureDescriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeTextureDescriptor.pImageInfo = infos.data();
			vkUpdateDescriptorSets(device, 1, &writeTextureDescriptor, 0, nullptr);
		
	}

	void CompileShaders()
	{
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = CreateCompileOptions();

		CompileVertexShader(compiler, options);
		CompilePixelShader(compiler, options);

		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
	}

	shaderc_compile_options_t CreateCompileOptions()
	{
		shaderc_compile_options_t retval = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(retval, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(retval, true);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(retval);
#endif
		return retval;
	}

	void CompileVertexShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string vertexShaderSource = ReadFileIntoString("../VertexShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource.c_str(), vertexShaderSource.length(),
			shaderc_vertex_shader, "main.vert", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Vertex Shader Errors:\n", shaderc_result_get_error_message(result));
			abort();
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);

		shaderc_result_release(result); // done
	}

	void CompilePixelShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string fragmentShaderSource = ReadFileIntoString("../FragmentShader.hlsl");

		shaderc_compilation_result_t result;

		result = shaderc_compile_into_spv( // compile
			compiler, fragmentShaderSource.c_str(), fragmentShaderSource.length(),
			shaderc_fragment_shader, "main.frag", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Fragment Shader Errors:\n", shaderc_result_get_error_message(result));
			abort();
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &fragmentShader);

		shaderc_result_release(result); // done
	}

	void InitializeGraphicsPipeline()
	{
		// Create Pipeline & Layout (Thanks Tiny!)
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};
		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";

		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = fragmentShader;
		stage_create_info[1].pName = "main";

		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = CreateVkPipelineInputAssemblyStateCreateInfo();
		std::vector<VkVertexInputBindingDescription> vertex_binding_description = CreateVkVertexInputBindingDescriptionArray();

		const tinygltf::Primitive& primitive = model.meshes[0].primitives[0];
		const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
		const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
		const tinygltf::Accessor& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const tinygltf::Accessor& tanAccessor = model.accessors[primitive.attributes.at("TANGENT")];


		std::array<VkVertexInputAttributeDescription, 4> vertex_attribute_description;
		vertex_attribute_description[0].binding = vertex_binding_description[0].binding;
		vertex_attribute_description[0].location = 0;
		vertex_attribute_description[0].format = getFormatGltf(posAccessor); //positions (3)
		vertex_attribute_description[0].offset = 0;

		vertex_attribute_description[1].binding = vertex_binding_description[1].binding;
		vertex_attribute_description[1].location = 1;
		vertex_attribute_description[1].format = getFormatGltf(normAccessor); //normals (3)
		vertex_attribute_description[1].offset = 0;

		vertex_attribute_description[2].binding = vertex_binding_description[2].binding;
		vertex_attribute_description[2].location = 2;
		vertex_attribute_description[2].format = getFormatGltf(texAccessor); //uvs (2)
		vertex_attribute_description[2].offset = 0;

		vertex_attribute_description[3].binding = vertex_binding_description[3].binding;
		vertex_attribute_description[3].location = 3;
		vertex_attribute_description[3].format = getFormatGltf(tanAccessor); //tangents (4)
		vertex_attribute_description[3].offset = 0;

		VkPipelineVertexInputStateCreateInfo input_vertex_info = CreateVkPipelineVertexInputStateCreateInfo(vertex_binding_description.data(), vertex_binding_description.size(), vertex_attribute_description.data(), vertex_attribute_description.size());
		VkViewport viewport = CreateViewportFromWindowDimensions();
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		VkPipelineViewportStateCreateInfo viewport_create_info = CreateVkPipelineViewportStateCreateInfo(&viewport, 1, &scissor, 1);
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = CreateVkPipelineRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo multisample_create_info = CreateVkPipelineMultisampleStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = CreateVkPipelineDepthStencilStateCreateInfo();
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = CreateVkPipelineColorBlendAttachmentState();
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = CreateVkPipelineColorBlendStateCreateInfo(&color_blend_attachment_state, 1);

		// Dynamic State 
		VkDynamicState dynamic_states[2] =
		{
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamic_create_info = CreateVkPipelineDynamicStateCreateInfo(dynamic_states, 2);

		CreatePipelineLayout();

		// Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	}

	VkPipelineInputAssemblyStateCreateInfo CreateVkPipelineInputAssemblyStateCreateInfo()
	{
		VkPipelineInputAssemblyStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		retval.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		retval.primitiveRestartEnable = false;
		return retval;
	}

	VkFormat getFormatGltf(const::tinygltf::Accessor& accessor)
	{
		VkFormat format{};
		if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
		{
			switch (accessor.type)
			{
			case TINYGLTF_TYPE_VEC2:
				format = VK_FORMAT_R32G32_SFLOAT;
				break;
			case TINYGLTF_TYPE_VEC3:
				format = VK_FORMAT_R32G32B32_SFLOAT;
				break;
			case TINYGLTF_TYPE_VEC4:
				format = VK_FORMAT_R32G32B32A32_SFLOAT;
				break;
			default:
				break;
			}
		}
		return format;
	}

	VkVertexInputBindingDescription CreateVkVertexInputBindingDescription()
	{
		VkVertexInputBindingDescription retval = {};
		retval.binding = 0;
		retval.stride = sizeof(float) * 3;
		retval.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return retval;
	}

	std::vector<VkVertexInputBindingDescription> CreateVkVertexInputBindingDescriptionArray()
	{
		const tinygltf::Primitive& primitive = model.meshes[0].primitives[0];
		const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
		const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
		const tinygltf::Accessor& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const tinygltf::Accessor& tanAccessor = model.accessors[primitive.attributes.at("TANGENT")];

		std::vector<VkVertexInputBindingDescription> retval = {};

		VkVertexInputBindingDescription bind0 = {}; //positions (3)
		bind0.binding = 0;
		bind0.stride = getStrideGltf(posAccessor);
		bind0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		retval.push_back(bind0);

		VkVertexInputBindingDescription bind1 = {}; //normals (3)
		bind1.binding = 1;
		bind1.stride = getStrideGltf(normAccessor);
		bind1.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		retval.push_back(bind1);

		VkVertexInputBindingDescription bind2 = {}; //uvs (2)
		bind2.binding = 2;
		bind2.stride = getStrideGltf(texAccessor);
		bind2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		retval.push_back(bind2);

		VkVertexInputBindingDescription bind3 = {}; //tangents (4)
		bind3.binding = 3;
		bind3.stride = getStrideGltf(tanAccessor);
		bind3.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		retval.push_back(bind3);

		return retval;
	}

	uint32_t getStrideGltf(const::tinygltf::Accessor& accessor)
	{
		uint32_t stride{};
		if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
		{
			switch (accessor.type)
			{
			case TINYGLTF_TYPE_VEC2:
				stride = sizeof(float) * 2;
				break;
			case TINYGLTF_TYPE_VEC3:
				stride = sizeof(float) * 3;
				break;
			case TINYGLTF_TYPE_VEC4:
				stride = sizeof(float) * 4;
				break;
			default:
				break;
			}
		}
		return stride;
	}

	VkPipelineVertexInputStateCreateInfo CreateVkPipelineVertexInputStateCreateInfo(
		VkVertexInputBindingDescription* bindingDescriptions, uint32_t bindingCount,
		VkVertexInputAttributeDescription* attributeDescriptions, uint32_t attributeCount)
	{
		VkPipelineVertexInputStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		retval.vertexBindingDescriptionCount = bindingCount;
		retval.pVertexBindingDescriptions = bindingDescriptions;
		retval.vertexAttributeDescriptionCount = attributeCount;
		retval.pVertexAttributeDescriptions = attributeDescriptions;
		return retval;
	}

	VkViewport CreateViewportFromWindowDimensions()
	{
		VkViewport retval = {};
		retval.x = 0;
		retval.y = 0;
		retval.width = static_cast<float>(windowWidth);
		retval.height = static_cast<float>(windowHeight);
		retval.minDepth = 1;
		retval.maxDepth = 0;
		return retval;
	}

	VkRect2D CreateScissorFromWindowDimensions()
	{
		VkRect2D retval = {};
		retval.offset.x = 0;
		retval.offset.y = 0;
		retval.extent.width = windowWidth;
		retval.extent.height = windowHeight;
		return retval;
	}

	VkPipelineViewportStateCreateInfo CreateVkPipelineViewportStateCreateInfo(VkViewport* viewports, uint32_t viewportCount, VkRect2D* scissors, uint32_t scissorCount)
	{
		VkPipelineViewportStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		retval.viewportCount = viewportCount;
		retval.pViewports = viewports;
		retval.scissorCount = scissorCount;
		retval.pScissors = scissors;
		return retval;
	}

	VkPipelineRasterizationStateCreateInfo CreateVkPipelineRasterizationStateCreateInfo()
	{
		VkPipelineRasterizationStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		retval.rasterizerDiscardEnable = VK_FALSE;
		retval.polygonMode = VK_POLYGON_MODE_FILL;
		retval.lineWidth = 1.0f;
		retval.cullMode = VK_CULL_MODE_BACK_BIT;
		retval.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		retval.depthClampEnable = VK_FALSE;
		retval.depthBiasEnable = VK_FALSE;
		retval.depthBiasClamp = 0.0f;
		retval.depthBiasConstantFactor = 0.0f;
		retval.depthBiasSlopeFactor = 0.0f;
		return retval;
	}

	VkPipelineMultisampleStateCreateInfo CreateVkPipelineMultisampleStateCreateInfo()
	{
		VkPipelineMultisampleStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		retval.sampleShadingEnable = VK_FALSE;
		retval.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		retval.minSampleShading = 1.0f;
		retval.pSampleMask = VK_NULL_HANDLE;
		retval.alphaToCoverageEnable = VK_FALSE;
		retval.alphaToOneEnable = VK_FALSE;
		return retval;
	}

	VkPipelineDepthStencilStateCreateInfo CreateVkPipelineDepthStencilStateCreateInfo()
	{
		VkPipelineDepthStencilStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		retval.depthTestEnable = VK_TRUE;
		retval.depthWriteEnable = VK_TRUE;
		retval.depthCompareOp = VK_COMPARE_OP_GREATER;
		retval.depthBoundsTestEnable = VK_FALSE;
		retval.minDepthBounds = 1.0f;
		retval.maxDepthBounds = 0.0f;
		retval.stencilTestEnable = VK_FALSE;
		return retval;
	}

	VkPipelineColorBlendAttachmentState CreateVkPipelineColorBlendAttachmentState()
	{
		VkPipelineColorBlendAttachmentState retval = {};
		retval.colorWriteMask = 0xF;
		retval.blendEnable = VK_FALSE;
		retval.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		retval.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		retval.colorBlendOp = VK_BLEND_OP_ADD;
		retval.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		retval.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		retval.alphaBlendOp = VK_BLEND_OP_ADD;
		return retval;
	}

	VkPipelineColorBlendStateCreateInfo CreateVkPipelineColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState* attachmentStates, uint32_t attachmentCount)
	{
		VkPipelineColorBlendStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		retval.logicOpEnable = VK_FALSE;
		retval.logicOp = VK_LOGIC_OP_COPY;
		retval.attachmentCount = attachmentCount;
		retval.pAttachments = attachmentStates;
		retval.blendConstants[0] = 0.0f;
		retval.blendConstants[1] = 0.0f;
		retval.blendConstants[2] = 0.0f;
		retval.blendConstants[3] = 0.0f;
		return retval;
	}

	VkPipelineDynamicStateCreateInfo CreateVkPipelineDynamicStateCreateInfo(VkDynamicState* dynamicStates, uint32_t dynamicStateCount)
	{
		VkPipelineDynamicStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		retval.dynamicStateCount = dynamicStateCount;
		retval.pDynamicStates = dynamicStates;
		return retval;
	}

	void CreatePipelineLayout()
	{
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 2;
		VkDescriptorSetLayout layouts[2] = {descriptorSetLayout, textureDescriptorSetLayout};
		pipeline_layout_create_info.pSetLayouts = layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 0;
		pipeline_layout_create_info.pPushConstantRanges = nullptr;

		vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipelineLayout);
	}

	void BindShutdownCallback()
	{
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
			});
	}


public:
	void Render()
	{
		uint32_t activeImage;
		vlk.GetSwapchainCurrentImage(activeImage);

		VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();
		SetUpPipeline(commandBuffer);

		//vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[activeImage], 0, 0);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSets, 0, 0);

		const tinygltf::Accessor& indexAccessor = model.accessors[model.meshes[0].primitives[0].indices];
		uint32_t indexCount = indexAccessor.count;
		vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
	}

	void updateCamera()
	{
		float elapsedTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

		GW::MATH::GMATRIXF viewCopy{};
		interfaceProxy.InverseF(viewMatrix, viewCopy);

		uint32_t currentImage;
		vlk.GetSwapchainCurrentImage(currentImage);

		float yChange = 0.0f;
		float states[6] = { 0, 0, 0, 0, 0, 0 };
		static float cameraSpeed = 1.0f;

		input.GetState(G_KEY_SPACE, states[0]);
		input.GetState(G_KEY_LEFTSHIFT, states[1]);
		controller.GetState(0, G_RIGHT_TRIGGER_AXIS, states[2]);
		controller.GetState(0, G_LEFT_TRIGGER_AXIS, states[3]);

		yChange = states[0] - states[1] + states[2] - states[3];
		viewCopy.row4.y += static_cast<float>(yChange * cameraSpeed * elapsedTime);

		const float perFrameSpeed = cameraSpeed * elapsedTime;
		input.GetState(G_KEY_W, states[0]);
		input.GetState(G_KEY_S, states[1]);
		input.GetState(G_KEY_A, states[2]);
		input.GetState(G_KEY_D, states[3]);
		controller.GetState(0, G_LY_AXIS, states[4]);
		controller.GetState(0, G_LX_AXIS, states[5]);
		float zChange = states[0] - states[1] + states[4];
		float xChange = states[3] - states[2] + states[5];
		GW::MATH::GVECTORF translate{ xChange * perFrameSpeed, 0, zChange * perFrameSpeed };
		interfaceProxy.TranslateLocalF(viewCopy, translate, viewCopy);

		unsigned int height;
		win.GetClientHeight(height);

		if (input.GetMouseDelta(states[0], states[1]) != GW::GReturn::SUCCESS)
		{
			states[0] = states[1] = 0;
			states[2] = states[3] = 0;
		}
		controller.GetState(0, G_RY_AXIS, states[2]);
		controller.GetState(0, G_RX_AXIS, states[3]);

		float thumbSpeed = G_PI * elapsedTime;
		float totalPitch = G_PI / 2 * states[1] / height + states[2] * -thumbSpeed;
		GW::MATH::GMATRIXF pitchMatrix{};
		GW::MATH::GMATRIXF identity = GW::MATH::GIdentityMatrixF;
		interfaceProxy.RotateXLocalF(identity, totalPitch, pitchMatrix);
		interfaceProxy.MultiplyMatrixF(pitchMatrix, viewCopy, viewCopy);

		unsigned int width;
		win.GetClientWidth(width);
		float ar = width / static_cast<float>(height);
		float yaw = G_PI / 2 * ar * states[0] / width + states[3] * thumbSpeed;
		GW::MATH::GMATRIXF yawMatrix;
		interfaceProxy.RotateYLocalF(identity, yaw, yawMatrix);
		GW::MATH::GVECTORF pos = viewCopy.row4;
		interfaceProxy.MultiplyMatrixF(viewCopy, yawMatrix, viewCopy);
		viewCopy.row4 = pos;

		interfaceProxy.InverseF(viewCopy, viewMatrix);
		shaderVarsUniformBuffer.viewMatrix = viewMatrix;

		shaderVarsUniformBuffer.camPos = viewCopy.row4;

		GvkHelper::write_to_buffer(device, uniformBufferData[currentImage], &shaderVarsUniformBuffer, sizeof(shaderVars));
		startTime = std::chrono::high_resolution_clock::now();
	}

private:

	VkCommandBuffer GetCurrentCommandBuffer()
	{
		VkCommandBuffer retval;
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);
		vlk.GetCommandBuffer(currentBuffer, (void**)&retval);
		return retval;
	}

	void SetUpPipeline(VkCommandBuffer& commandBuffer)
	{
		UpdateWindowDimensions(); // what is the current client area dimensions?
		SetViewport(commandBuffer);
		SetScissor(commandBuffer);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		BindVertexBuffers(commandBuffer);
		BindIndexBuffers(commandBuffer);
	}

	void SetViewport(const VkCommandBuffer& commandBuffer)
	{
		VkViewport viewport = CreateViewportFromWindowDimensions();
		vkCmdSetViewport(commandBuffer, 1, 0, &viewport);
	}

	void SetScissor(const VkCommandBuffer& commandBuffer)
	{
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void BindVertexBuffers(VkCommandBuffer& commandBuffer)
	{
		const tinygltf::Primitive& primitive = model.meshes[0].primitives[0]; //position data
		const tinygltf::Accessor& accessPos = model.accessors[primitive.attributes.at("POSITION")];
		const tinygltf::BufferView& bufferViewPos = model.bufferViews[accessPos.bufferView];
		const float* posData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewPos.buffer].data[bufferViewPos.byteOffset + accessPos.byteOffset]);
		unsigned int posDataSize = bufferViewPos.byteLength;

		//normal data
		const tinygltf::Accessor& accessNorm = model.accessors[primitive.attributes.at("NORMAL")];
		const tinygltf::BufferView& bufferViewNorm = model.bufferViews[accessNorm.bufferView];
		const float* normData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewNorm.buffer].data[bufferViewNorm.byteOffset + accessNorm.byteOffset]);
		unsigned int normDataSize = bufferViewNorm.byteLength;

		//texcoord data
		const tinygltf::Accessor& accessTex = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const tinygltf::BufferView& bufferViewTex = model.bufferViews[accessTex.bufferView];
		const float* texData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTex.buffer].data[bufferViewTex.byteOffset + accessTex.byteOffset]);
		unsigned int texDataSize = bufferViewTex.byteLength;

		//tangent data
		const tinygltf::Accessor& accessTan = model.accessors[primitive.attributes.at("TANGENT")];
		const tinygltf::BufferView& bufferViewTan = model.bufferViews[accessTan.bufferView];
		const float* tanData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTan.buffer].data[bufferViewTan.byteOffset + accessTan.byteOffset]);
		unsigned int tanDataSize = bufferViewTan.byteLength;

		//position
		VkDeviceSize offsets0[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &geometryHandle, offsets0);

		//normal
		VkDeviceSize offsets1[] = { posDataSize };
		vkCmdBindVertexBuffers(commandBuffer, 1, 1, &geometryHandle, offsets1);

		//texturecoord
		VkDeviceSize offsets2[] = { posDataSize + normDataSize };
		vkCmdBindVertexBuffers(commandBuffer, 2, 1, &geometryHandle, offsets2);

		//tangent
		VkDeviceSize offsets3[] = { posDataSize + normDataSize + texDataSize };
		vkCmdBindVertexBuffers(commandBuffer, 3, 1, &geometryHandle, offsets3);
	}

	void BindIndexBuffers(VkCommandBuffer& commandBuffer)
	{
		const tinygltf::Primitive& primitive = model.meshes[0].primitives[0];
		const tinygltf::Accessor& accessIndicies = model.accessors[primitive.indices];
		const tinygltf::BufferView& bufferViewIndicies = model.bufferViews[accessIndicies.bufferView];

		const tinygltf::Accessor& accessPos = model.accessors[primitive.attributes.at("POSITION")];
		const tinygltf::BufferView& bufferViewPos = model.bufferViews[accessPos.bufferView];
		const float* posData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewPos.buffer].data[bufferViewPos.byteOffset + accessPos.byteOffset]);
		unsigned int posDataSize = bufferViewPos.byteLength;

		//normal data
		const tinygltf::Accessor& accessNorm = model.accessors[primitive.attributes.at("NORMAL")];
		const tinygltf::BufferView& bufferViewNorm = model.bufferViews[accessNorm.bufferView];
		const float* normData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewNorm.buffer].data[bufferViewNorm.byteOffset + accessNorm.byteOffset]);
		unsigned int normDataSize = bufferViewNorm.byteLength;

		//texcoord data
		const tinygltf::Accessor& accessTex = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const tinygltf::BufferView& bufferViewTex = model.bufferViews[accessTex.bufferView];
		const float* texData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTex.buffer].data[bufferViewTex.byteOffset + accessTex.byteOffset]);
		unsigned int texDataSize = bufferViewTex.byteLength;

		//tangent data
		const tinygltf::Accessor& accessTan = model.accessors[primitive.attributes.at("TANGENT")];
		const tinygltf::BufferView& bufferViewTan = model.bufferViews[accessTan.bufferView];
		const float* tanData = reinterpret_cast<const float*>
			(&model.buffers[bufferViewTan.buffer].data[bufferViewTan.byteOffset + accessTan.byteOffset]);
		unsigned int tanDataSize = bufferViewTan.byteLength;

		VkDeviceSize indexOffset = posDataSize + normDataSize + texDataSize + tanDataSize;

		VkIndexType indexType;
		switch (accessIndicies.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: //5123
			indexType = VK_INDEX_TYPE_UINT16;
			break;

		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: //5125
			indexType = VK_INDEX_TYPE_UINT32;
			break;

		default:
			throw std::runtime_error("Unsupported index component type");
		}

		vkCmdBindIndexBuffer(commandBuffer, geometryHandle, indexOffset, indexType);
	}

	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);
		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, geometryHandle, nullptr);
		vkFreeMemory(device, geometryData, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
		//releasing vectors and descriptors
		for (int i = 0; i < uniformBufferHandle.size(); i++)
		{
			vkDestroyBuffer(device, uniformBufferHandle[i], nullptr);

			vkFreeMemory(device, uniformBufferData[i], nullptr);
		}
		uniformBufferHandle.clear();
		uniformBufferData.clear();

		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		for (int i = 0; i < storageBufferHandle.size(); i++)
		{
			vkDestroyBuffer(device, storageBufferHandle[i], nullptr);
			vkFreeMemory(device, storageBufferData[i], nullptr);
		}
		storageBufferHandle.clear();
		storageBufferData.clear();

		for (int i = 0; i < textures.size(); i++)
		{
			vkDestroyBuffer(device, textures[i].textureHandle, nullptr);
			vkFreeMemory(device, textures[i].textureData, nullptr);
			vkDestroyImage(device, textures[i].image, nullptr);
			vkDestroyImageView(device, textures[i].imageView, nullptr);
		}
		textures.clear();

		vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);
		vkDestroySampler(device, textureSampler, nullptr);
	}
};
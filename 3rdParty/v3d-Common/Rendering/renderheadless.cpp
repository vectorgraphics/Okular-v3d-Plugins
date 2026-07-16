/*
* Vulkan Example - Minimal headless rendering example
*
* Copyright (C) 2017-2022 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "renderheadless.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>

#include "seconds.h"
#include "SPIRV/GlslangToSpv.h"
#define HAVE_VULKAN
#include "shaderResources.h"

#define VULKAN_DEBUG 1

HeadlessRenderer::HeadlessRenderer(std::string shaderPath)
	: shaderPath(shaderPath) { 
		if (!glslang::InitializeProcess()) {
			std::cerr << "failed to initialize glslang" << std::endl;
			std::exit(1);
		}

		createInstance();
		createPhysicalDevice();

		VkDeviceQueueCreateInfo queueCreateInfo = requestGraphicsQueue();

		createLogicalDevice(&queueCreateInfo);

		vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

		// Command pool
		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

		// Allocate a single command buffer
		VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &commandBuffer));

	}

HeadlessRenderer::~HeadlessRenderer() { 
	if (meshInitialized) {
		cleanupMeshData();
	}

	if (hostReadableDestinationImageInitalized) {
		destroyHostReadableDestinationImage();
	}

	if (initialized) {
		cleanup();

		vkDestroyBuffer(device, uniformBuffer, nullptr);
		vkFreeMemory(device, uniformBufferMemory, nullptr);

		vkDestroyBuffer(device, materialBuffer, nullptr);
		vkFreeMemory(device, materialBufferMemory, nullptr);

		vkDestroyBuffer(device, lightBuffer, nullptr);
		vkFreeMemory(device, lightBufferMemory, nullptr);

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		initialized = false;
	}

	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyDevice(device, nullptr);

#if VULKAN_DEBUG
	if (debugReportCallback) {
		PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
		assert(vkDestroyDebugReportCallback);
		vkDestroyDebugReportCallback(instance, debugReportCallback, nullptr);
	}
#endif

	vkDestroyInstance(instance, nullptr);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData)
{
	std::cout << "[VALIDATION]: " << std::string(pLayerPrefix) << " - " << std::string(pMessage) << "\n" << std::endl;

	return VK_FALSE;
}

VkDebugReportCallbackEXT debugReportCallback{};

uint32_t HeadlessRenderer::getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
	for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
		if ((typeBits & 1) == 1) {
			if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}
		typeBits >>= 1;
	}
	return 0;
}

VkResult HeadlessRenderer::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data) {
	// Create the buffer handle
	VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, buffer));

	// Create the memory backing up the buffer handle
	VkMemoryRequirements memReqs;
	VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
	vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, memoryPropertyFlags);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, memory));

	if (data != nullptr) {
		void *mapped;
		VK_CHECK_RESULT(vkMapMemory(device, *memory, 0, size, 0, &mapped));
		memcpy(mapped, data, size);
		vkUnmapMemory(device, *memory);
	}

	VK_CHECK_RESULT(vkBindBufferMemory(device, *buffer, *memory, 0));

	return VK_SUCCESS;
}

void HeadlessRenderer::submitWork(VkCommandBuffer cmdBuffer, VkQueue queue) {
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmdBuffer;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);
}

void HeadlessRenderer::createInstance() {
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan headless example";
	appInfo.pEngineName = "VulkanExample";
	appInfo.apiVersion = VK_API_VERSION_1_0;
	
	// Vulkan instance creation (without surface extensions)
	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;

	uint32_t layerCount = 1;
	const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };

	std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };

#if VULKAN_DEBUG
	// Check if layers are available
	uint32_t instanceLayerCount;
	vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
	std::vector<VkLayerProperties> instanceLayers(instanceLayerCount);
	vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayers.data());

	bool layersAvailable = true;
	for (auto layerName : validationLayers) {
		bool layerAvailable = false;
		for (auto instanceLayer : instanceLayers) {
			if (strcmp(instanceLayer.layerName, layerName) == 0) {
				layerAvailable = true;
				break;
			}
		}
		if (!layerAvailable) {
			layersAvailable = false;
			break;
		}
	}

	if (layersAvailable) {
		instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		instanceCreateInfo.ppEnabledLayerNames = validationLayers;
		instanceCreateInfo.enabledLayerCount = layerCount;
	}
#endif

	instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

#if VULKAN_DEBUG
	if (layersAvailable) {
		VkDebugReportCallbackCreateInfoEXT debugReportCreateInfo = {};
		debugReportCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		debugReportCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		debugReportCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;

		// We have to explicitly load this function.
		PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
		assert(vkCreateDebugReportCallbackEXT);
		VK_CHECK_RESULT(vkCreateDebugReportCallbackEXT(instance, &debugReportCreateInfo, nullptr, &debugReportCallback));
	}
#endif
}

void HeadlessRenderer::createPhysicalDevice() {
	uint32_t deviceCount = 0;
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()));
	physicalDevice = physicalDevices[0];
}

VkDeviceQueueCreateInfo HeadlessRenderer::requestGraphicsQueue() {
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	uint32_t queueFamilyCount;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
	for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
		if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queueFamilyIndex = i;
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = i;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			break;
		}
	}

	return queueCreateInfo;
}

void HeadlessRenderer::createLogicalDevice(VkDeviceQueueCreateInfo* queueCreateInfo) {
	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
	deviceFeatures.fragmentStoresAndAtomics = VK_TRUE;

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfo;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
	std::vector<const char*> deviceExtensions = { VK_KHR_MAINTENANCE2_EXTENSION_NAME };

	// Detect fragment shader interlock support
	uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> extProps(extCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, extProps.data());
	for (const auto& ext : extProps) {
		if (strcmp(ext.extensionName, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME) == 0) {
			interlock = true;
			deviceExtensions.push_back(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
			break;
		}
	}

	VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlockFeatures = {};
	if (interlock) {
		interlockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
		interlockFeatures.fragmentShaderPixelInterlock = VK_TRUE;
		deviceCreateInfo.pNext = &interlockFeatures;
		LOG("INTERLOCK enabled\n");
	} else {
		LOG("INTERLOCK not available\n");
	}

	deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	VK_CHECK_RESULT(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
}

void HeadlessRenderer::copyDataToGPU(const std::vector<unsigned char>& data, VkBuffer& buffer, VkDeviceMemory& deviceMemory) {
	const VkDeviceSize dataSize = data.size() * sizeof(unsigned char);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	// Command buffer for copy commands (reused)
	VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer copyCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		&stagingMemory,
		dataSize,
		(void*)data.data()
	);

	createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&buffer,
		&deviceMemory,
		dataSize
	);

	VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
	VkBufferCopy copyRegion = {};
	copyRegion.size = dataSize;
	vkCmdCopyBuffer(copyCmd, stagingBuffer, buffer, 1, &copyRegion);
	VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

	submitWork(copyCmd, queue);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingMemory, nullptr);	
}


void HeadlessRenderer::copyIndexDataToGPU(const std::vector<unsigned int>& indices) {
	const VkDeviceSize indexBufferSize = indices.size() * sizeof(unsigned int);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	// Command buffer for copy commands (reused)
	VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer copyCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	// Indices
	createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		&stagingMemory,
		indexBufferSize,
		(void*)indices.data()
	);

	meshInitialized = true;
	createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&indexBuffer,
		&indexMemory,
		indexBufferSize
	);

	VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
	VkBufferCopy copyRegion = {};
	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, stagingBuffer, indexBuffer, 1, &copyRegion);
	VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

	submitWork(copyCmd, queue);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingMemory, nullptr);
}

void HeadlessRenderer::createUniformBuffer() {
	VkDeviceSize uniformBufferSize = sizeof(UniformBufferObject);

	auto result = createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&uniformBuffer,
		&uniformBufferMemory,
		uniformBufferSize
	);

	// Map the buffer for it whole lifetime so it does not need to be remapped frequently
	vkMapMemory(device, uniformBufferMemory, 0, uniformBufferSize, 0, (void**)&uniformBufferMapped);
}

void HeadlessRenderer::createMaterialBuffer(const std::vector<GPUMaterial>& materials) {
    VkDeviceSize size = sizeof(GPUMaterial) * materials.size();

    auto result = createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &materialBuffer,
        &materialBufferMemory,
        size,
        (void*)materials.data()
    );

	VK_CHECK_RESULT(result);
}

void HeadlessRenderer::createLightBuffer(const std::vector<GPULight>& lights) {
    VkDeviceSize size = sizeof(GPULight) * lights.size();

    auto result = createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &lightBuffer,
        &lightBufferMemory,
        size,
        (void*)lights.data()
    );

	VK_CHECK_RESULT(result);
}

void HeadlessRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 8;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
}

void HeadlessRenderer::createDescirptorSets() {
	std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{ };
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = maxFramesInFlight;
	allocInfo.pSetLayouts = layouts.data();

	descriptorSets.resize(maxFramesInFlight);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()));

	for (size_t i = 0; i < maxFramesInFlight; i++) {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkWriteDescriptorSet descriptorWrite{ };
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[i];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		VkDescriptorBufferInfo materialBufferInfo{};
		materialBufferInfo.buffer = materialBuffer;
		materialBufferInfo.offset = 0;
		materialBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet materialWrite{};
		materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		materialWrite.dstSet = descriptorSets[i];
		materialWrite.dstBinding = 1;
		materialWrite.dstArrayElement = 0;
		materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		materialWrite.descriptorCount = 1;
		materialWrite.pBufferInfo = &materialBufferInfo;

		VkDescriptorBufferInfo lightBufferInfo{};
		lightBufferInfo.buffer = lightBuffer;
		lightBufferInfo.offset = 0;
		lightBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet lightWrite{};
		lightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		lightWrite.dstSet = descriptorSets[i];
		lightWrite.dstBinding = 2;
		lightWrite.dstArrayElement = 0;
		lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightWrite.descriptorCount = 1;
		lightWrite.pBufferInfo = &lightBufferInfo;

		std::array<VkWriteDescriptorSet, 3> writes = {
			descriptorWrite,
			materialWrite,
			lightWrite
		};

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(writes.size()),
			writes.data(),
			0,
			nullptr
		);
	}
}

void HeadlessRenderer::updateTransparencyDescriptors() {
	for (size_t i = 0; i < maxFramesInFlight; i++) {
		VkDescriptorBufferInfo countBufferInfo{};
		countBufferInfo.buffer = countBuffer;
		countBufferInfo.offset = 0;
		countBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo offsetBufferInfo{};
		offsetBufferInfo.buffer = offsetBuffer;
		offsetBufferInfo.offset = 0;
		offsetBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo fragmentBufferInfo{};
		fragmentBufferInfo.buffer = fragmentBuffer;
		fragmentBufferInfo.offset = 0;
		fragmentBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo depthFragBufferInfo{};
		depthFragBufferInfo.buffer = depthFragBuffer;
		depthFragBufferInfo.offset = 0;
		depthFragBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo opaqueColorBufferInfo{};
		opaqueColorBufferInfo.buffer = opaqueColorBuffer;
		opaqueColorBufferInfo.offset = 0;
		opaqueColorBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo opaqueDepthBufferInfo{};
		opaqueDepthBufferInfo.buffer = opaqueDepthBuffer;
		opaqueDepthBufferInfo.offset = 0;
		opaqueDepthBufferInfo.range = VK_WHOLE_SIZE;

		std::array<VkWriteDescriptorSet, 6> writes{};

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = descriptorSets[i];
		writes[0].dstBinding = 3;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &countBufferInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = descriptorSets[i];
		writes[1].dstBinding = 4;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &offsetBufferInfo;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = descriptorSets[i];
		writes[2].dstBinding = 5;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[2].descriptorCount = 1;
		writes[2].pBufferInfo = &fragmentBufferInfo;

		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = descriptorSets[i];
		writes[3].dstBinding = 6;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[3].descriptorCount = 1;
		writes[3].pBufferInfo = &depthFragBufferInfo;

		writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[4].dstSet = descriptorSets[i];
		writes[4].dstBinding = 7;
		writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[4].descriptorCount = 1;
		writes[4].pBufferInfo = &opaqueColorBufferInfo;

		writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[5].dstSet = descriptorSets[i];
		writes[5].dstBinding = 8;
		writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[5].descriptorCount = 1;
		writes[5].pBufferInfo = &opaqueDepthBufferInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	}
}

void HeadlessRenderer::createTransparencyBuffers(int width, int height) {
	pixels = (uint32_t)(width + 1) * (uint32_t)(height + 1);

	uint32_t G = (pixels + groupSize - 1) / groupSize;
	uint32_t Pixels = groupSize * G;
	uint32_t globalSize = localSize * ((G + localSize - 1) / localSize) * sizeof(uint32_t);

	VkDeviceSize countBufferSize = (Pixels + 1) * sizeof(uint32_t);
	VkDeviceSize offsetBufferSize = (Pixels + 2) * sizeof(uint32_t);
	VkDeviceSize opaqueColorSize = pixels * sizeof(glm::vec4);
	VkDeviceSize opaqueDepthSize = sizeof(uint32_t) + (VkDeviceSize)pixels * sizeof(float);

	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &countBuffer, &countBufferMemory, countBufferSize);

	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &globalSumBuffer, &globalSumBufferMemory, globalSize);

	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &offsetBuffer, &offsetBufferMemory, offsetBufferSize);

	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &opaqueColorBuffer, &opaqueColorBufferMemory, opaqueColorSize);

	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &opaqueDepthBuffer, &opaqueDepthBufferMemory, opaqueColorSize);

	VkDeviceSize fragmentBufferSize = maxFragments > 0 ? (VkDeviceSize)maxFragments * sizeof(glm::vec4) : pixels * sizeof(glm::vec4);
	maxFragments = maxFragments > 0 ? maxFragments : pixels;
	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &fragmentBuffer, &fragmentBufferMemory, fragmentBufferSize);

	VkDeviceSize depthBufferSize = fragmentBufferSize;
	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &depthFragBuffer, &depthFragBufferMemory, depthBufferSize);

	VkDeviceSize feedbackBufferSize = 2 * sizeof(uint32_t);
	createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		&feedbackBuffer, &feedbackBufferMemory, feedbackBufferSize);
	vkMapMemory(device, feedbackBufferMemory, 0, feedbackBufferSize, 0, (void**)&feedbackMappedPtr);
}

static void vkFillBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
	vkCmdFillBuffer(cmd, buffer, 0, size, 0);
}

void HeadlessRenderer::zeroTransparencyBuffers() {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer clearCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &clearCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(clearCmd, &cmdBufInfo));

	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(device, globalSumBuffer, &reqs);
	vkFillBuffer(clearCmd, globalSumBuffer, reqs.size);

	vkGetBufferMemoryRequirements(device, opaqueDepthBuffer, &reqs);
	vkFillBuffer(clearCmd, opaqueDepthBuffer, reqs.size);

	vkGetBufferMemoryRequirements(device, countBuffer, &reqs);
	vkFillBuffer(clearCmd, countBuffer, reqs.size);

	VK_CHECK_RESULT(vkEndCommandBuffer(clearCmd));

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &clearCmd;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	vkFreeCommandBuffers(device, commandPool, 1, &clearCmd);
}

void HeadlessRenderer::createAttachments(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight) {
	VkImageCreateInfo image = vks::initializers::imageCreateInfo();
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = colorFormat;
	image.extent.width = targetWidth;
	image.extent.height = targetHeight;
	image.extent.depth = 1;
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;

	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &colorAttachment.image));
	vkGetImageMemoryRequirements(device, colorAttachment.image, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &colorAttachment.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, colorAttachment.image, colorAttachment.memory, 0));

	VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
	colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	colorImageView.format = colorFormat;
	colorImageView.subresourceRange = {};
	colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	colorImageView.subresourceRange.baseMipLevel = 0;
	colorImageView.subresourceRange.levelCount = 1;
	colorImageView.subresourceRange.baseArrayLayer = 0;
	colorImageView.subresourceRange.layerCount = 1;
	colorImageView.image = colorAttachment.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &colorAttachment.view));

	// Depth stencil attachment
	image.format = depthFormat;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &depthAttachment.image));
	vkGetImageMemoryRequirements(device, depthAttachment.image, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &depthAttachment.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, depthAttachment.image, depthAttachment.memory, 0));

	VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = depthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
		depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;
	depthStencilView.image = depthAttachment.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &depthAttachment.view));
}

void HeadlessRenderer::createRenderPipeline(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight) {
	std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
	// Color attachment
	attchmentDescriptions[0].format = colorFormat;
	attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	// Depth attachment
	attchmentDescriptions[1].format = depthFormat;
	attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
	renderPassInfo.pAttachments = attchmentDescriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();
	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

	VkImageView attachments[2];
	attachments[0] = colorAttachment.view;
	attachments[1] = depthAttachment.view;

	VkFramebufferCreateInfo framebufferCreateInfo = vks::initializers::framebufferCreateInfo();
	framebufferCreateInfo.renderPass = renderPass;
	framebufferCreateInfo.attachmentCount = 2;
	framebufferCreateInfo.pAttachments = attachments;
	framebufferCreateInfo.width = targetWidth;
	framebufferCreateInfo.height = targetHeight;
	framebufferCreateInfo.layers = 1;

	VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer));
}

void HeadlessRenderer::createCountRenderPass(int targetWidth, int targetHeight) {
	VkSubpassDescription subpasses[3] = {};
	for (int i = 0; i < 3; i++) {
		subpasses[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	VkSubpassDependency dependencies[3] = {};

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].srcAccessMask = 0;
	dependencies[0].dstAccessMask = 0;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask = 0;
	dependencies[1].dstAccessMask = 0;

	dependencies[2].srcSubpass = 1;
	dependencies[2].dstSubpass = 2;
	dependencies[2].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[2].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 0;
	renderPassInfo.subpassCount = 3;
	renderPassInfo.pSubpasses = subpasses;
	renderPassInfo.dependencyCount = 3;
	renderPassInfo.pDependencies = dependencies;
	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &countRenderPass));

	// Dummy framebuffer for attachment-less count render pass
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.renderPass = countRenderPass;
	fbInfo.attachmentCount = 0;
	fbInfo.pAttachments = nullptr;
	fbInfo.width = (uint32_t)targetWidth;
	fbInfo.height = (uint32_t)targetHeight;
	fbInfo.layers = 1;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &countFramebuffer));
}

void HeadlessRenderer::createTransparentRenderPass(int targetWidth, int targetHeight) {
	VkAttachmentDescription colorAttachmentDesc = {};
	colorAttachmentDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
	colorAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkAttachmentDescription depthAttachmentDesc = {};
	VkFormat depthFormat;
	vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
	depthAttachmentDesc.format = depthFormat;
	depthAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpasses[2] = {};
	// Subpass 0: transparent draw (color + depth)
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = &colorRef;
	subpasses[0].pDepthStencilAttachment = &depthRef;
	// Subpass 1: blend quad (color only)
	subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[1].colorAttachmentCount = 1;
	subpasses[1].pColorAttachments = &colorRef;

	VkSubpassDependency dependencies[3] = {};
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	dependencies[2].srcSubpass = 1;
	dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

	VkAttachmentDescription attachments_desc[2];
	attachments_desc[0] = colorAttachmentDesc;
	attachments_desc[1] = depthAttachmentDesc;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments_desc;
	renderPassInfo.subpassCount = 2;
	renderPassInfo.pSubpasses = subpasses;
	renderPassInfo.dependencyCount = 3;
	renderPassInfo.pDependencies = dependencies;
	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &transparentRenderPass));

	VkImageView attachments[2];
	attachments[0] = colorAttachment.view;
	attachments[1] = depthAttachment.view;

	VkFramebufferCreateInfo fbInfo = vks::initializers::framebufferCreateInfo();
	fbInfo.renderPass = transparentRenderPass;
	fbInfo.attachmentCount = 2;
	fbInfo.pAttachments = attachments;
	fbInfo.width = (uint32_t)targetWidth;
	fbInfo.height = (uint32_t)targetHeight;
	fbInfo.layers = 1;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &transparentFramebuffer));
}

std::vector<char> readFile(const std::string& filename);

void HeadlessRenderer::createTransparencyPipelineLayout() {
	VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::uvec4) + sizeof(glm::vec4), 0);

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
		vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout);
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &transparencyPipelineLayout));
}

void HeadlessRenderer::createComputeDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding bindings[4] = {};

	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[3].binding = 3;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 4;
	layoutInfo.pBindings = bindings;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout));
}

void HeadlessRenderer::createComputeDescriptorPool() {
	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize.descriptorCount = 4;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &computeDescriptorPool));
}

void HeadlessRenderer::createComputeDescriptorSet() {
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = computeDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &computeDescriptorSetLayout;
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet));

	VkDescriptorBufferInfo bufferInfos[4] = {};
	bufferInfos[0].buffer = countBuffer;
	bufferInfos[0].offset = 0;
	bufferInfos[0].range = VK_WHOLE_SIZE;

	bufferInfos[1].buffer = globalSumBuffer;
	bufferInfos[1].offset = 0;
	bufferInfos[1].range = VK_WHOLE_SIZE;

	bufferInfos[2].buffer = offsetBuffer;
	bufferInfos[2].offset = 0;
	bufferInfos[2].range = VK_WHOLE_SIZE;

	bufferInfos[3].buffer = feedbackBuffer;
	bufferInfos[3].offset = 0;
	bufferInfos[3].range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet writes[4] = {};
	for (int i = 0; i < 4; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = computeDescriptorSet;
		writes[i].dstBinding = i;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].descriptorCount = 1;
		writes[i].pBufferInfo = &bufferInfos[i];
	}

	vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void HeadlessRenderer::createComputePipelineLayout() {
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(uint32_t) * 2; // blockSize, final

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstantRange;
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &computePipelineLayout));
}

VkShaderModule HeadlessRenderer::createComputeShaderModule(EShLanguage lang, std::string const & filePath, std::vector<std::string> const & options) {
	std::string header = "#version 450\n";
	for (auto const & option: options) {
		header += "#define " + option + "\n";
	}
	auto fileContents = readFile(filePath.c_str());
	fileContents.emplace_back(0);

	std::vector<char> source(header.begin(), header.end());
	source.insert(source.end(), fileContents.begin(), fileContents.end());

	std::vector<const char*> const shaderSources {source.data()};
	auto const res = getShaderResources();
	auto const compileMessages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
	auto shader = glslang::TShader(lang);
	glslang::TProgram program;
	std::vector<std::uint32_t> spirv;

	shader.setStrings(shaderSources.data(), shaderSources.size());

	if (!shader.parse(&res, 100, false, compileMessages)) {
		std::cout << "failed to parse " + filePath + ":\n" << shader.getInfoLog() << std::endl;
	}

	program.addShader(&shader);
	if (!program.link(compileMessages)) {
		std::cout << "failed to link shader " + filePath << ": " << shader.getInfoLog() << std::endl;
	}

	glslang::GlslangToSpv(*program.getIntermediate(lang), spirv);

	VkShaderModule shaderModule;
	VkShaderModuleCreateInfo moduleCreateInfo{};
	moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleCreateInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
	moduleCreateInfo.pCode = spirv.data();
	VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderModule));

	return shaderModule;
}

void HeadlessRenderer::createComputePipelines() {
	std::vector<std::string> sumOptions{
		"LOCALSIZE " + std::to_string(localSize),
		"BLOCKSIZE " + std::to_string(blockSize)
	};

	VkShaderModule sum1Module = createComputeShaderModule(EShLangCompute, shaderPath + "sum1.glsl", sumOptions);
	VkShaderModule sum2Module = createComputeShaderModule(EShLangCompute, shaderPath + "sum2.glsl", sumOptions);
	VkShaderModule sum3Module = createComputeShaderModule(EShLangCompute, shaderPath + "sum3.glsl", sumOptions);

	VkPipelineShaderStageCreateInfo stageInfo = {};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.layout = computePipelineLayout;

	stageInfo.module = sum1Module;
	pipelineInfo.stage = stageInfo;
	VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computeSum1Pipeline));

	stageInfo.module = sum2Module;
	pipelineInfo.stage = stageInfo;
	VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computeSum2Pipeline));

	stageInfo.module = sum3Module;
	pipelineInfo.stage = stageInfo;
	VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computeSum3Pipeline));

	vkDestroyShaderModule(device, sum1Module, nullptr);
	vkDestroyShaderModule(device, sum2Module, nullptr);

	vkDestroyShaderModule(device, sum3Module, nullptr);
}

void HeadlessRenderer::createCountPipeline(bool useColor, int targetWidth, int targetHeight) {
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &countPipelineCache));

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0, 0, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)targetWidth, (uint32_t)targetHeight } };
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(transparencyPipelineLayout, countRenderPass);
	pipelineCreateInfo.basePipelineIndex = -1;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// Count pipeline uses full stride vertex input but only position attribute
	std::vector<VkVertexInputBindingDescription> vertexInputBindings;
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

	if (useColor) {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
	} else {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
	}
	// Only position attribute at location 0 for count pipeline
	vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
	};

	VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
	vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
	vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

	pipelineCreateInfo.pVertexInputState = &vertexInputState;

	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";

	// Count pipeline uses empty options {}
	std::vector<std::string> countOptions{};
	if (m_Orthographic) {
		countOptions.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", countOptions);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "count.glsl", countOptions);

	countShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, countPipelineCache, 1, &pipelineCreateInfo, nullptr, &countPipeline));
}

void HeadlessRenderer::createTransparentPipeline(bool useColor, int targetWidth, int targetHeight) {
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &transparentPipelineCache));

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0, 0, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)targetWidth, (uint32_t)targetHeight } };
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(transparencyPipelineLayout, transparentRenderPass);
	pipelineCreateInfo.subpass = 0;
	pipelineCreateInfo.basePipelineIndex = -1;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	std::vector<VkVertexInputBindingDescription> vertexInputBindings;
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

	if (useColor) {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)
		};
	} else {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)
		};
	}

	VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
	vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
	vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

	pipelineCreateInfo.pVertexInputState = &vertexInputState;

	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";


	std::vector<std::string> options{ "NORMAL", "TRANSPARENT", "MATERIAL" };
	if (srgb) {
		options.push_back("OUTPUT_AS_SRGB");
	}
	if (interlock) {
		options.push_back("HAVE_INTERLOCK");
	}
	if (useColor) {
		options.push_back("GENERAL");
		options.push_back("COLOR");
	}
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	transparentShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, transparentPipelineCache, 1, &pipelineCreateInfo, nullptr, &transparentPipeline));
}

void HeadlessRenderer::createBlendPipeline(int targetWidth, int targetHeight) {
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &blendPipelineCache));

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0, 0, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)targetWidth, (uint32_t)targetHeight } };
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(transparencyPipelineLayout, transparentRenderPass);
	pipelineCreateInfo.subpass = 1;
	pipelineCreateInfo.basePipelineIndex = -1;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// No vertex input for blend pipeline (full-screen triangle via gl_VertexIndex)
	VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	vertexInputState.vertexBindingDescriptionCount = 0;
	vertexInputState.pVertexBindingDescriptions = nullptr;
	vertexInputState.vertexAttributeDescriptionCount = 0;
	vertexInputState.pVertexAttributeDescriptions = nullptr;

	pipelineCreateInfo.pVertexInputState = &vertexInputState;

	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";

	std::vector<std::string> options;
	if (srgb) {
		options.push_back("OUTPUT_AS_SRGB");
	}
	options.push_back("ARRAYSIZE 32");
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "screen.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "blend.glsl", options);

	blendShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, blendPipelineCache, 1, &pipelineCreateInfo, nullptr, &blendPipeline));
}

void HeadlessRenderer::createDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding materialBinding{};
	materialBinding.binding = 1;
	materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialBinding.descriptorCount = 1;
	materialBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding lightBinding{};
	lightBinding.binding = 2;
	lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightBinding.descriptorCount = 1;
	lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// Transparency bindings (3-8)
	VkDescriptorSetLayoutBinding countBinding{};
	countBinding.binding = 3;
	countBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	countBinding.descriptorCount = 1;
	countBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding offsetBinding{};
	offsetBinding.binding = 4;
	offsetBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	offsetBinding.descriptorCount = 1;
	offsetBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding fragmentBinding{};
	fragmentBinding.binding = 5;
	fragmentBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	fragmentBinding.descriptorCount = 1;
	fragmentBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding depthFragBinding{};
	depthFragBinding.binding = 6;
	depthFragBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	depthFragBinding.descriptorCount = 1;
	depthFragBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding opaqueColorBinding{};
	opaqueColorBinding.binding = 7;
	opaqueColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	opaqueColorBinding.descriptorCount = 1;
	opaqueColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding opaqueDepthBinding{};
	opaqueDepthBinding.binding = 8;
	opaqueDepthBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	opaqueDepthBinding.descriptorCount = 1;
	opaqueDepthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		uboLayoutBinding,
		materialBinding,
		lightBinding,
		countBinding,
		offsetBinding,
		fragmentBinding,
		depthFragBinding,
		opaqueColorBinding,
		opaqueDepthBinding
	};

	VkDescriptorSetLayoutCreateInfo descriptorLayout =
    vks::initializers::descriptorSetLayoutCreateInfo(bindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));
}

std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		std::cout << "failed to open file " + filename << std::endl;

	size_t fileSize = (size_t) file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkShaderModule HeadlessRenderer::createShaderModule(EShLanguage lang, std::string const & filePath, std::vector<std::string> const & options) {
	std::string header = "#version 450\n";

	for (auto const & option: options) {
		header += "#define " + option + "\n";
	}
	auto fileContents= readFile(filePath.c_str());
	fileContents.emplace_back(0); // terminate string

	std::vector<char> source(header.begin(), header.end());
	source.insert(source.end(), fileContents.begin(), fileContents.end());

	std::vector<const char*> const shaderSources {source.data()};
	auto const res = getShaderResources();
	auto const compileMessages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
	auto shader = glslang::TShader(lang);
	glslang::TProgram program;
	std::vector<std::uint32_t> spirv;

	shader.setStrings(shaderSources.data(), shaderSources.size());

	if (!shader.parse(&res, 100, false, compileMessages)) {
		std::stringstream s(fileContents.data());
		std::string line;
		unsigned int k=0;
		while(getline(s,line))
			cerr << ++k << ": " << line << std::endl;
		std::cout << "\n failed to parse "
								+ filePath
								+ ":\n" + shader.getInfoLog()
								+ " " + shader.getInfoDebugLog() << std::endl;
	}

	program.addShader(&shader);

	if (!program.link(compileMessages)) {
		std::cout << "failed to link shader "
								+ filePath
								+ ": " + shader.getInfoLog() << std::endl;
	}

	glslang::GlslangToSpv(*program.getIntermediate(lang), spirv);

	VkShaderModule shaderModule;
	VkShaderModuleCreateInfo moduleCreateInfo{};
	moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleCreateInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
	moduleCreateInfo.pCode = spirv.data();

	VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderModule));

	return shaderModule;
}
void HeadlessRenderer::createGraphicsPipeline(bool useColor, int targetWidth, int targetHeight) {
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
		vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout);

	VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::uvec4) + sizeof(glm::vec4), 0);
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

	// Create pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0, 0, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)targetWidth, (uint32_t)targetHeight } };
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// Vertex bindings and attributes
	std::vector<VkVertexInputBindingDescription> vertexInputBindings;
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

	if (useColor) {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0), 					// Position
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),		// Normal
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),				// Material Index
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)	// Color
		};
	} else {
		vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0), 				// Position
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),	// Normal
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)			// Material Index
		};
	}

	VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
	vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
	vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

	pipelineCreateInfo.pVertexInputState = &vertexInputState;

	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";

	std::vector<std::string> options{ "NORMAL", "OPAQUE", "MATERIAL" };
	if (interlock) {
		options.push_back("HAVE_INTERLOCK");
	}
	if (useColor) {
		options.push_back("GENERAL");
		options.push_back("COLOR");
	}
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	shaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
}

void HeadlessRenderer::recordCommandBuffer(int targetWidth, int targetHeight, size_t indexCount, size_t lightCount) {
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

	VkClearValue clearValues[2];
	clearValues[0].color.float32[0] = m_BackgroundColor.r;
	clearValues[0].color.float32[1] = m_BackgroundColor.g;
	clearValues[0].color.float32[2] = m_BackgroundColor.b;
	clearValues[0].color.float32[3] = m_BackgroundColor.a;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderArea.extent.width = targetWidth;
	renderPassBeginInfo.renderArea.extent.height = targetHeight;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.framebuffer = framebuffer;

	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Render scene
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	glm::uvec4 constants{ 0 };
	constants.x = lightCount;
	vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

	vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);


	vkCmdEndRenderPass(commandBuffer);
	VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void HeadlessRenderer::recordCountCommandBuffer(size_t indexCount, size_t lightCount) {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer countCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &countCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(countCmd, &cmdBufInfo));

	// Begin count render pass (no clear values, no framebuffer)
	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderArea.extent.width = currentTargetSize.x;
	renderPassBeginInfo.renderArea.extent.height = currentTargetSize.y;
	renderPassBeginInfo.clearValueCount = 0;
	renderPassBeginInfo.pClearValues = nullptr;
	renderPassBeginInfo.renderPass = countRenderPass;
	renderPassBeginInfo.framebuffer = countFramebuffer;

	vkCmdBeginRenderPass(countCmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Bind descriptor sets once for all subpasses
	vkCmdBindDescriptorSets(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparencyPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

	// Bind pipeline and buffers
	vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, countPipeline);

	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(countCmd, 0, 1, &vertexBuffer, offsets);
	vkCmdBindIndexBuffer(countCmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	glm::uvec4 constants{ 0 };
	constants.x = lightCount;
	constants.y = currentTargetSize.x;
	vkCmdPushConstants(countCmd, transparencyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);

	// Draw indexed (subpass 0: opaque count)
	vkCmdDrawIndexed(countCmd, indexCount, 1, 0, 0, 0);

	// Advance to subpass 1 (transparent count) and subpass 2
	vkCmdNextSubpass(countCmd, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdNextSubpass(countCmd, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdEndRenderPass(countCmd);
	VK_CHECK_RESULT(vkEndCommandBuffer(countCmd));

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &countCmd;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	vkFreeCommandBuffers(device, commandPool, 1, &countCmd);
}

void HeadlessRenderer::recordComputeCommandBuffer() {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer computeCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &computeCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(computeCmd, &cmdBufInfo));

	uint32_t g = (elements + groupSize - 1) / groupSize;
	uint32_t blockSize_val = (g + localSize - 1) / localSize;
	uint32_t final_val = elements - 1;

	VkMemoryBarrier writeBarrier = {};
	writeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	writeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	writeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	// Push constants for compute
	uint32_t pushConstants[2] = { blockSize_val, final_val };
	vkCmdPushConstants(computeCmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), pushConstants);

	// Bind compute descriptor set
	vkCmdBindDescriptorSets(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);

	// Barrier: fragment shader writes -> compute shader reads
	vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

	// Dispatch sum1
	vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSum1Pipeline);
	vkCmdDispatch(computeCmd, g, 1, 1);

	// Barrier between compute passes
	vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

	// Dispatch sum2
	vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSum2Pipeline);
	vkCmdDispatch(computeCmd, 1, 1, 1);

	// Barrier between compute passes
	vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

	// Dispatch sum3
	vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSum3Pipeline);
	vkCmdDispatch(computeCmd, g, 1, 1);

	// Barrier for host readback
	VkMemoryBarrier hostReadBarrier = {};
	hostReadBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	hostReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	hostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

	vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
		0, 1, &hostReadBarrier, 0, nullptr, 0, nullptr);

	VK_CHECK_RESULT(vkEndCommandBuffer(computeCmd));

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &computeCmd;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	vkFreeCommandBuffers(device, commandPool, 1, &computeCmd);

	// Read feedback
	fragments = feedbackMappedPtr[1];
	uint32_t maxDepth = feedbackMappedPtr[0];

	if (maxDepth > maxSize) {
		maxSize = 1;
		while (maxSize < maxDepth) maxSize *= 2;
	}

	// Grow fragment buffers if needed
	if (fragments > maxFragments) {
		VkDeviceSize newFragmentBufferSize = (VkDeviceSize)(fragments * 11 / 10) * sizeof(glm::vec4);
		vkDestroyBuffer(device, fragmentBuffer, nullptr);
		vkFreeMemory(device, fragmentBufferMemory, nullptr);
		vkDestroyBuffer(device, depthFragBuffer, nullptr);
		vkFreeMemory(device, depthFragBufferMemory, nullptr);

		maxFragments = fragments * 11 / 10;
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &fragmentBuffer, &fragmentBufferMemory, newFragmentBufferSize);
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &depthFragBuffer, &depthFragBufferMemory, newFragmentBufferSize);

		// Update descriptors
		updateTransparencyDescriptors();

		VkDescriptorBufferInfo fragmentBufferInfo = {};
		fragmentBufferInfo.buffer = fragmentBuffer;
		fragmentBufferInfo.offset = 0;
		fragmentBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo depthFragBufferInfo = {};
		depthFragBufferInfo.buffer = depthFragBuffer;
		depthFragBufferInfo.offset = 0;
		depthFragBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writes[2] = {};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = descriptorSets[0];
		writes[0].dstBinding = 5;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &fragmentBufferInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = descriptorSets[0];
		writes[1].dstBinding = 6;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &depthFragBufferInfo;

		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
	}
}

void HeadlessRenderer::recordTransparentCommandBuffer(size_t indexCount, size_t lightCount) {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer transCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &transCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(transCmd, &cmdBufInfo));

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderArea.extent.width = currentTargetSize.x;
	renderPassBeginInfo.renderArea.extent.height = currentTargetSize.y;

	VkClearValue clearValues[2];
	clearValues[0].color.float32[0] = m_BackgroundColor.r;
	clearValues[0].color.float32[1] = m_BackgroundColor.g;
	clearValues[0].color.float32[2] = m_BackgroundColor.b;
	clearValues[0].color.float32[3] = m_BackgroundColor.a;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.renderPass = transparentRenderPass;
	renderPassBeginInfo.framebuffer = transparentFramebuffer;

	vkCmdBeginRenderPass(transCmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Subpass 0: Transparent draw
	vkCmdBindDescriptorSets(transCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparencyPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);
	vkCmdBindPipeline(transCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);

	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(transCmd, 0, 1, &vertexBuffer, offsets);
	vkCmdBindIndexBuffer(transCmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	glm::uvec4 constants{ 0 };
	constants.x = lightCount;
	constants.y = currentTargetSize.x;
	vkCmdPushConstants(transCmd, transparencyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);

	vkCmdDrawIndexed(transCmd, indexCount, 1, 0, 0, 0);

	// Advance to subpass 1: Blend
	vkCmdNextSubpass(transCmd, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(transCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
	glm::vec4 background;
	background.r = m_BackgroundColor.r;
	background.g = m_BackgroundColor.g;
	background.b = m_BackgroundColor.b;
	background.a = m_BackgroundColor.a;
	VkDeviceSize pushSize = sizeof(glm::uvec4) + sizeof(glm::vec4);
	uint8_t* pushData = new uint8_t[pushSize];
	memcpy(pushData, &constants, sizeof(glm::uvec4));
	memcpy(pushData + sizeof(glm::uvec4), &background, sizeof(glm::vec4));
	vkCmdPushConstants(transCmd, transparencyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
	delete[] pushData;

	vkCmdDraw(transCmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(transCmd);
	VK_CHECK_RESULT(vkEndCommandBuffer(transCmd));

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &transCmd;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	vkFreeCommandBuffers(device, commandPool, 1, &transCmd);
}

unsigned char* HeadlessRenderer::copyToHost(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout) {
	// Copy framebuffer image to host visible image
	unsigned char* returnData;
	
	if (targetSize.x != hostReadableDestinationImageSize.x || targetSize.y != hostReadableDestinationImageSize.y) {
		if (hostReadableDestinationImageInitalized) {
			destroyHostReadableDestinationImage();
		}

		hostReadableDestinationImageSize = targetSize;

		createHostReadableDestinationImage(hostReadableDestinationImageSize);

		// Map image memory so we can copy from it
		vkMapMemory(device, hostReadableDestinationImageMemory, 0, VK_WHOLE_SIZE, 0, (void**)&hostReadableDestinationImageMapped);

		hostReadableDestinationImageInitalized = true;
	}

	// Do the actual blit from the offscreen image to our host visible destination image
	VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer copyCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

	// Transition destination image to transfer destination layout
	vks::tools::insertImageMemoryBarrier(
		copyCmd,
		hostReadableDestinationImage,
		0,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

	// colorAttachment.image is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned
	VkImageCopy imageCopyRegion{};
	imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopyRegion.srcSubresource.layerCount = 1;
	imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopyRegion.dstSubresource.layerCount = 1;
	imageCopyRegion.extent.width = targetSize.x;
	imageCopyRegion.extent.height = targetSize.y;
	imageCopyRegion.extent.depth = 1;

	vkCmdCopyImage(
		copyCmd,
		colorAttachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		hostReadableDestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&imageCopyRegion);

	// Transition destination image to general layout, which is the required layout for mapping the image memory later on
	vks::tools::insertImageMemoryBarrier(
		copyCmd,
		hostReadableDestinationImage,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_MEMORY_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

	VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

	submitWork(copyCmd, queue);

	// Get layout of the image (including row pitch)
	VkImageSubresource subResource{};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout subResourceLayout;
	vkGetImageSubresourceLayout(device, hostReadableDestinationImage, &subResource, &subResourceLayout);

	*imageSubresourceLayout = subResourceLayout;

	const char* imagedata = (const char*)hostReadableDestinationImageMapped;

	imagedata += subResourceLayout.offset;

	returnData = new unsigned char[imageSubresourceLayout->size];
	std::memcpy(returnData, imagedata, imageSubresourceLayout->size);

	return returnData;
}

void HeadlessRenderer::createHostReadableDestinationImage(glm::ivec2 size) {

	// Create the linear tiled destination image to copy to and to read the memory from
	VkImageCreateInfo imgCreateInfo(vks::initializers::imageCreateInfo());
	imgCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imgCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imgCreateInfo.extent.width = size.x;
	imgCreateInfo.extent.height = size.y;
	imgCreateInfo.extent.depth = 1;
	imgCreateInfo.arrayLayers = 1;
	imgCreateInfo.mipLevels = 1;
	imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
	imgCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	// Create the image
	VK_CHECK_RESULT(vkCreateImage(device, &imgCreateInfo, nullptr, &hostReadableDestinationImage));

	// Create memory to back up the image
	VkMemoryRequirements memRequirements;
	VkMemoryAllocateInfo memAllocInfo(vks::initializers::memoryAllocateInfo());
	vkGetImageMemoryRequirements(device, hostReadableDestinationImage, &memRequirements);
	memAllocInfo.allocationSize = memRequirements.size;

	// Memory must be host visible to copy from
	memAllocInfo.memoryTypeIndex = getMemoryTypeIndex(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &hostReadableDestinationImageMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device, hostReadableDestinationImage, hostReadableDestinationImageMemory, 0));
}

void HeadlessRenderer::destroyHostReadableDestinationImage() {
	vkUnmapMemory(device, hostReadableDestinationImageMemory);
	vkFreeMemory(device, hostReadableDestinationImageMemory, nullptr);
	vkDestroyImage(device, hostReadableDestinationImage, nullptr);
}

void HeadlessRenderer::cleanup() {
	vkDestroyImageView(device, colorAttachment.view, nullptr);
	vkDestroyImage(device, colorAttachment.image, nullptr);
	vkFreeMemory(device, colorAttachment.memory, nullptr);
	vkDestroyImageView(device, depthAttachment.view, nullptr);
	vkDestroyImage(device, depthAttachment.image, nullptr);
	vkFreeMemory(device, depthAttachment.memory, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	vkDestroyFramebuffer(device, framebuffer, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipelineCache(device, pipelineCache, nullptr);


	for (auto shadermodule : shaderModules) {
		vkDestroyShaderModule(device, shadermodule, nullptr);
	}

	// Cleanup transparency resources
	if (countRenderPass) vkDestroyRenderPass(device, countRenderPass, nullptr);
	if (countFramebuffer) vkDestroyFramebuffer(device, countFramebuffer, nullptr);
	if (transparentRenderPass) vkDestroyRenderPass(device, transparentRenderPass, nullptr);
	if (transparentFramebuffer) vkDestroyFramebuffer(device, transparentFramebuffer, nullptr);
	if (transparencyPipelineLayout) vkDestroyPipelineLayout(device, transparencyPipelineLayout, nullptr);

	if (countPipeline) vkDestroyPipeline(device, countPipeline, nullptr);
	if (countPipelineCache) vkDestroyPipelineCache(device, countPipelineCache, nullptr);
	for (auto m : countShaderModules) vkDestroyShaderModule(device, m, nullptr);
	countShaderModules.clear();

	if (transparentPipeline) vkDestroyPipeline(device, transparentPipeline, nullptr);
	if (transparentPipelineCache) vkDestroyPipelineCache(device, transparentPipelineCache, nullptr);
	for (auto m : transparentShaderModules) vkDestroyShaderModule(device, m, nullptr);
	transparentShaderModules.clear();

	if (blendPipeline) vkDestroyPipeline(device, blendPipeline, nullptr);
	if (blendPipelineCache) vkDestroyPipelineCache(device, blendPipelineCache, nullptr);
	for (auto m : blendShaderModules) vkDestroyShaderModule(device, m, nullptr);
	blendShaderModules.clear();

	if (computeSum1Pipeline) vkDestroyPipeline(device, computeSum1Pipeline, nullptr);
	if (computeSum2Pipeline) vkDestroyPipeline(device, computeSum2Pipeline, nullptr);
	if (computeSum3Pipeline) vkDestroyPipeline(device, computeSum3Pipeline, nullptr);
	if (computePipelineLayout) vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
	if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
	if (computeDescriptorPool) vkDestroyDescriptorPool(device, computeDescriptorPool, nullptr);

	if (countBuffer) { vkDestroyBuffer(device, countBuffer, nullptr); vkFreeMemory(device, countBufferMemory, nullptr); }
	if (globalSumBuffer) { vkDestroyBuffer(device, globalSumBuffer, nullptr); vkFreeMemory(device, globalSumBufferMemory, nullptr); }
	if (offsetBuffer) { vkDestroyBuffer(device, offsetBuffer, nullptr); vkFreeMemory(device, offsetBufferMemory, nullptr); }
	if (opaqueColorBuffer) { vkDestroyBuffer(device, opaqueColorBuffer, nullptr); vkFreeMemory(device, opaqueColorBufferMemory, nullptr); }
	if (opaqueDepthBuffer) { vkDestroyBuffer(device, opaqueDepthBuffer, nullptr); vkFreeMemory(device, opaqueDepthBufferMemory, nullptr); }
	if (fragmentBuffer) { vkDestroyBuffer(device, fragmentBuffer, nullptr); vkFreeMemory(device, fragmentBufferMemory, nullptr); }
	if (depthFragBuffer) { vkDestroyBuffer(device, depthFragBuffer, nullptr); vkFreeMemory(device, depthFragBufferMemory, nullptr); }
	if (feedbackBuffer) {
		if (feedbackMappedPtr) vkUnmapMemory(device, feedbackBufferMemory);
		vkDestroyBuffer(device, feedbackBuffer, nullptr);
		vkFreeMemory(device, feedbackBufferMemory, nullptr);
	}

	countRenderPass = VK_NULL_HANDLE;
	countFramebuffer = VK_NULL_HANDLE;
	transparentRenderPass = VK_NULL_HANDLE;
	transparentFramebuffer = VK_NULL_HANDLE;
	transparencyPipelineLayout = VK_NULL_HANDLE;
	countPipeline = VK_NULL_HANDLE;
	countPipelineCache = VK_NULL_HANDLE;
	transparentPipeline = VK_NULL_HANDLE;
	transparentPipelineCache = VK_NULL_HANDLE;
	blendPipeline = VK_NULL_HANDLE;
	blendPipelineCache = VK_NULL_HANDLE;
	computeSum1Pipeline = VK_NULL_HANDLE;
	computeSum2Pipeline = VK_NULL_HANDLE;
	computeSum3Pipeline = VK_NULL_HANDLE;
	computePipelineLayout = VK_NULL_HANDLE;
	computeDescriptorSetLayout = VK_NULL_HANDLE;
	computeDescriptorPool = VK_NULL_HANDLE;
	countBuffer = VK_NULL_HANDLE; countBufferMemory = VK_NULL_HANDLE;
	globalSumBuffer = VK_NULL_HANDLE; globalSumBufferMemory = VK_NULL_HANDLE;
	offsetBuffer = VK_NULL_HANDLE; offsetBufferMemory = VK_NULL_HANDLE;
	opaqueColorBuffer = VK_NULL_HANDLE; opaqueColorBufferMemory = VK_NULL_HANDLE;
	opaqueDepthBuffer = VK_NULL_HANDLE; opaqueDepthBufferMemory = VK_NULL_HANDLE;
	fragmentBuffer = VK_NULL_HANDLE; fragmentBufferMemory = VK_NULL_HANDLE;
	depthFragBuffer = VK_NULL_HANDLE; depthFragBufferMemory = VK_NULL_HANDLE;
	feedbackBuffer = VK_NULL_HANDLE; feedbackBufferMemory = VK_NULL_HANDLE;
	feedbackMappedPtr = nullptr;
}

void HeadlessRenderer::copyMeshToGPU(const Mesh& mesh) {
	copyDataToGPU(mesh.vertices, vertexBuffer, vertexMemory);
	copyIndexDataToGPU(mesh.indices);

	m_IndexCount = mesh.indices.size();
}

unsigned char* HeadlessRenderer::render(
	glm::ivec2 targetSize, 
	VkSubresourceLayout* imageSubresourceLayout, 
	const glm::mat4& view, 
	const glm::mat4& proj, 
	const std::vector<V3dMaterial>& materials, 
	const std::vector<V3dHeaderInfo::Light>& lights,
	MeshPipelineMode pipelineMode,
	const glm::vec4& bgColor,
	bool orthographic
) {
	m_BackgroundColor = bgColor;
	m_Orthographic = orthographic;

	if (m_IndexCount == 0) {
		std::cout << "ERROR, no mesh sent to GPU" << std::endl;
	}

	// Check if we need to recreate the pipeline due to content type change
	bool pipelineModeChanged = (pipelineMode != currentPipelineMode);

	if (m_IndexCount > 0 && !meshInitialized) {
		pipelineModeChanged = true;
	}

	bool recreatedResources{ false };

	if (currentTargetSize != targetSize || pipelineModeChanged) {
		if (initialized) {
			cleanup();

			vkDestroyBuffer(device, uniformBuffer, nullptr);
			vkFreeMemory(device, uniformBufferMemory, nullptr);

			vkDestroyBuffer(device, materialBuffer, nullptr);
			vkFreeMemory(device, materialBufferMemory, nullptr);

			vkDestroyBuffer(device, lightBuffer, nullptr);
			vkFreeMemory(device, lightBufferMemory, nullptr);

			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

			initialized = false;
		}

		VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
		VkFormat depthFormat;

		vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);

		createAttachments(colorFormat, depthFormat, targetSize.x, targetSize.y);

		createRenderPipeline(colorFormat, depthFormat, targetSize.x, targetSize.y);

		createDescriptorSetLayout();

		createGraphicsPipeline(pipelineMode == MeshPipelineMode::ColorOnly || pipelineMode == MeshPipelineMode::Mixed, targetSize.x, targetSize.y);

		createUniformBuffer();
		// TODO potentially move
		std::vector<GPUMaterial> mats(materials.size());
		int i = 0;
		for (auto& mat : materials) {
			mats[i].diffuse = glm::vec4{ mat.diffuse.r, mat.diffuse.g, mat.diffuse.b, mat.diffuse.a };

			mats[i].emissive   = mat.emissive;
			mats[i].specular   = mat.specular;
			mats[i].parameters = glm::vec4(
				mat.shininess,   // roughness
				mat.metallic,    // metallic
				mat.fresnel0,    // fresnel
				pipelineMode == MeshPipelineMode::ColorOnly || pipelineMode == MeshPipelineMode::Mixed ? 1.0f : 0.0f
			);
			++i;
		}

		createMaterialBuffer(mats);

		std::vector<GPULight> gpuLights(1);
		gpuLights[0].direction = glm::vec4{ lights[0].direction.x, lights[0].direction.y, lights[0].direction.z, 0.0f };
		gpuLights[0].color = glm::vec4{ lights[0].color.r, lights[0].color.g, lights[0].color.b, 1.0f };

		createLightBuffer(gpuLights);

		createDescriptorPool();
		createDescirptorSets();

		// Create transparency resources
		createTransparencyBuffers(targetSize.x, targetSize.y);
		updateTransparencyDescriptors();
		createCountRenderPass(targetSize.x, targetSize.y);
		createTransparentRenderPass(targetSize.x, targetSize.y);
		createTransparencyPipelineLayout();
		createComputeDescriptorSetLayout();
		createComputeDescriptorPool();
		createComputeDescriptorSet();
		createComputePipelineLayout();
		createComputePipelines();

		bool useColor = pipelineMode == MeshPipelineMode::ColorOnly || pipelineMode == MeshPipelineMode::Mixed;
		createCountPipeline(useColor, targetSize.x, targetSize.y);
		createTransparentPipeline(useColor, targetSize.x, targetSize.y);
		createBlendPipeline(targetSize.x, targetSize.y);

		initialized = true;
		currentPipelineMode = pipelineMode;
		currentTargetSize = targetSize;

		recreatedResources = true;
	}

	UniformBufferObject ubo;
	ubo.projViewMat = proj * view;
	ubo.viewMat = view;
	ubo.normMat = glm::inverse(view);

	// TODO switch to "or" instead of "and"
	if (cachedUbo.projViewMat != ubo.projViewMat && cachedUbo.viewMat != ubo.viewMat && cachedUbo.normMat != ubo.normMat) {
		std::memcpy(uniformBufferMapped, &ubo, sizeof(UniformBufferObject));
	}

	// Always update material and light buffers on every render call.
	// Multiple models share a single HeadlessRenderer instance; if the target
	// size hasn't changed the recreation block above is skipped, but each model
	// can have its own materials and lighting.  Destroying + recreating these
	// small host-visible buffers ensures every scene renders with its own data.
	if (initialized && !recreatedResources) {
		vkDestroyBuffer(device, materialBuffer, nullptr);
		vkFreeMemory(device, materialBufferMemory, nullptr);
		vkDestroyBuffer(device, lightBuffer, nullptr);
		vkFreeMemory(device, lightBufferMemory, nullptr);

		std::vector<GPUMaterial> mats(materials.size());
		for (size_t i = 0; i < materials.size(); ++i) {
			mats[i].diffuse    = glm::vec4{ materials[i].diffuse.r, materials[i].diffuse.g, materials[i].diffuse.b, materials[i].diffuse.a };
			mats[i].emissive   = materials[i].emissive;
			mats[i].specular   = materials[i].specular;
			mats[i].parameters = glm::vec4(
				materials[i].shininess,
				materials[i].metallic,
				materials[i].fresnel0,
				pipelineMode == MeshPipelineMode::ColorOnly || pipelineMode == MeshPipelineMode::Mixed ? 1.0f : 0.0f
			);
		}
		createMaterialBuffer(mats);

		std::vector<GPULight> gpuLights(1);
		gpuLights[0].direction = glm::vec4{ lights[0].direction.x, lights[0].direction.y, lights[0].direction.z, 0.0f };
		gpuLights[0].color     = glm::vec4{ lights[0].color.r, lights[0].color.g, lights[0].color.b, 1.0f };
		createLightBuffer(gpuLights);

		// Re-bind the new buffers into the descriptor sets
		VkDescriptorBufferInfo materialBufferInfo{};
		materialBufferInfo.buffer = materialBuffer;
		materialBufferInfo.offset = 0;
		materialBufferInfo.range  = VK_WHOLE_SIZE;

		VkWriteDescriptorSet materialWrite{};
		materialWrite.sType          = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		materialWrite.dstSet         = descriptorSets[0];
		materialWrite.dstBinding     = 1;
		materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		materialWrite.descriptorCount = 1;
		materialWrite.pBufferInfo    = &materialBufferInfo;

		VkDescriptorBufferInfo lightBufferInfo{};
		lightBufferInfo.buffer = lightBuffer;
		lightBufferInfo.offset = 0;
		lightBufferInfo.range  = VK_WHOLE_SIZE;

		VkWriteDescriptorSet lightWrite{};
		lightWrite.sType          = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		lightWrite.dstSet         = descriptorSets[0];
		lightWrite.dstBinding     = 2;
		lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightWrite.descriptorCount = 1;
		lightWrite.pBufferInfo    = &lightBufferInfo;

		std::array<VkWriteDescriptorSet, 2> writes = { materialWrite, lightWrite };
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	// Detect if the scene has any transparent materials
	bool hasTransparency = false;
	for (const auto& mat : materials) {
		if (mat.diffuse.a < 1.0f) {
			hasTransparency = true;
			break;
		}
	}

	if (!hasTransparency) {
		// Fully opaque scene: use only the opaque pass
		recordCommandBuffer(targetSize.x, targetSize.y, m_IndexCount, lights.size());
		submitWork(commandBuffer, queue);
	} else {
		// Scene has transparency: skip opaque pass entirely.
		// Follow vkrender.cc approach -- all geometry goes through the A-buffer path.
		// The color attachment is cleared to transparent by the transparent render pass,
		// and the blend shader composites the final result.

		// Transition images from UNDEFINED (since we skipped the opaque pass) to attachment layouts.
		zeroTransparencyBuffers();

		VkCommandBufferAllocateInfo transAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VkCommandBuffer layoutCmd;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &transAllocInfo, &layoutCmd));
		VkCommandBufferBeginInfo transBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(layoutCmd, &transBufInfo));

		VkImageMemoryBarrier colorBarrier = {};
		colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorBarrier.srcAccessMask = 0;
		colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		colorBarrier.image = colorAttachment.image;
		colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorBarrier.subresourceRange.baseMipLevel = 0;
		colorBarrier.subresourceRange.levelCount = 1;
		colorBarrier.subresourceRange.baseArrayLayer = 0;
		colorBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

		VkImageMemoryBarrier depthBarrier = {};
		depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.srcAccessMask = 0;
		depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.image = depthAttachment.image;
		depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		depthBarrier.subresourceRange.baseMipLevel = 0;
		depthBarrier.subresourceRange.levelCount = 1;
		depthBarrier.subresourceRange.baseArrayLayer = 0;
		depthBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

		VK_CHECK_RESULT(vkEndCommandBuffer(layoutCmd));
		VkSubmitInfo layoutSubmit = vks::initializers::submitInfo();
		layoutSubmit.commandBufferCount = 1;
		layoutSubmit.pCommandBuffers = &layoutCmd;
		VkFenceCreateInfo layoutFenceInfo = vks::initializers::fenceCreateInfo();
		VkFence layoutFence;
		VK_CHECK_RESULT(vkCreateFence(device, &layoutFenceInfo, nullptr, &layoutFence));
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &layoutSubmit, layoutFence));
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &layoutFence, VK_TRUE, UINT64_MAX));
		vkDestroyFence(device, layoutFence, nullptr);
		vkFreeCommandBuffers(device, commandPool, 1, &layoutCmd);

		elements = pixels;
		recordCountCommandBuffer(m_IndexCount, lights.size());
		recordComputeCommandBuffer();
		if (fragments > 0) {
			recordTransparentCommandBuffer(m_IndexCount, lights.size());
		}
	}

	unsigned char* returnData = copyToHost(targetSize, imageSubresourceLayout);

	vkQueueWaitIdle(queue);

	return returnData;
}

void HeadlessRenderer::cleanupMeshData() {
	vkDestroyBuffer(device, vertexBuffer, nullptr);
	vkFreeMemory(device, vertexMemory, nullptr);
	vkDestroyBuffer(device, indexBuffer, nullptr);
	vkFreeMemory(device, indexMemory, nullptr);

	meshInitialized = false;
}

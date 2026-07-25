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
#include <tinyexr.h>
#include <cstdlib>

// Global VertexBuffers from asymptote/render.h (declared extern, defined in bezierpatch.cc / beziercurve.cc).
// Accessible transitively via V3dObject.h → render.h.
using namespace camp;

HeadlessRenderer::HeadlessRenderer(std::string shaderPath)
	: shaderPath(shaderPath) { 
		if (!glslang::InitializeProcess()) {
			std::cerr << "v3d: failed to initialize glslang, disabling rendering." << std::endl;
			initialized = false;
			return;
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
	if (hostReadableDestinationImageInitalized) {
		destroyHostReadableDestinationImage();
	}

	// Clean up IBL resources before destroying device
	destroyIBLResources();

	if (initialized) {
		cleanup();

		vkDestroyBuffer(device, uniformBuffer, nullptr);
		vkFreeMemory(device, uniformBufferMemory, nullptr);

		if (materialBufferMapped) vkUnmapMemory(device, materialBufferMemory);
		vkDestroyBuffer(device, materialBuffer, nullptr);
		vkFreeMemory(device, materialBufferMemory, nullptr);

		if (lightBufferMapped) vkUnmapMemory(device, lightBufferMemory);
		vkDestroyBuffer(device, lightBuffer, nullptr);
		vkFreeMemory(device, lightBufferMemory, nullptr);

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		initialized = false;
	}

	// Do not explicitly destroy commandPool, device, or instance.
	// Asymptote's vkrender.cc takes the same approach: it skips explicit
	// Vulkan teardown and lets process exit clean up.  The Intel ANV driver
	// crashes inside vkDestroyDevice when validation layers are active
	// (NULL deref in vk_meta_device_finish hash table iteration).
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
	appInfo.apiVersion = VK_API_VERSION_1_4;
	
	// Vulkan instance creation (without surface extensions)
	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;

	uint32_t layerCount = 1;
	const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };

	std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };

	bool enableValidation = getenv("OKULAR_V3D_DEBUG") != nullptr;
	if (enableValidation) {
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
		} else {
			enableValidation = false; // layers not available, disable callback too
		}
	}

	instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

	if (enableValidation) {
		VkDebugReportCallbackCreateInfoEXT debugReportCreateInfo = {};
		debugReportCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		debugReportCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		debugReportCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;

		// We have to explicitly load this function.
		PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
		assert(vkCreateDebugReportCallbackEXT);
		VK_CHECK_RESULT(vkCreateDebugReportCallbackEXT(instance, &debugReportCreateInfo, nullptr, &debugReportCallback));
	}
}

void HeadlessRenderer::createPhysicalDevice() {
	uint32_t deviceCount = 0;
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
	if (deviceCount == 0) {
		std::cerr << "v3d: no Vulkan physical devices found, disabling rendering." << std::endl;
		initialized = false;
		return;
	}
	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()));
	physicalDevice = physicalDevices[0];

	VkPhysicalDeviceProperties deviceProps;
	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
	maxComputeWorkGroupCountX = deviceProps.limits.maxComputeWorkGroupCount[0];
	maxFramebufferWidth = deviceProps.limits.maxFramebufferWidth;
	maxFramebufferHeight = deviceProps.limits.maxFramebufferHeight;
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
	deviceFeatures.fillModeNonSolid = VK_TRUE;

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
    // Guard: vkCreateBuffer with size==0 is invalid usage.
    if (materials.empty()) {
        std::cout << "v3d: no materials found, creating minimal buffer." << std::endl;
        GPUMaterial emptyMat{};
        createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &materialBuffer, &materialBufferMemory, sizeof(GPUMaterial), &emptyMat);
        VkDeviceSize size = sizeof(GPUMaterial);
        vkMapMemory(device, materialBufferMemory, 0, size, 0, &materialBufferMapped);
        return;
    }

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

	// Map the buffer for its whole lifetime so we can update via memcpy
	vkMapMemory(device, materialBufferMemory, 0, size, 0, &materialBufferMapped);
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

	// Map the buffer for its whole lifetime so we can update via memcpy
	vkMapMemory(device, lightBufferMemory, 0, size, 0, &lightBufferMapped);
}

void HeadlessRenderer::createDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes;

    poolSizes.reserve(5); // 2 base + up to 3 IBL

    VkDescriptorPoolSize uboPool{};
    uboPool.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPool.descriptorCount = maxFramesInFlight;
    poolSizes.push_back(uboPool);

    VkDescriptorPoolSize storageBufferPool{};
    storageBufferPool.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storageBufferPool.descriptorCount = maxFramesInFlight * 8;
    poolSizes.push_back(storageBufferPool);

    if (ibl) {
        VkDescriptorPoolSize combinedImageSamplerPool{};
        combinedImageSamplerPool.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        combinedImageSamplerPool.descriptorCount = maxFramesInFlight * 3; // irradiance, brdf, reflection
        poolSizes.push_back(combinedImageSamplerPool);
    }

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

		// Write IBL sampler descriptors if enabled
		if (ibl) {
			VkDescriptorImageInfo irradianceInfo{};
			irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			irradianceInfo.imageView = irradianceView;
			irradianceInfo.sampler = irradianceSampler;

			VkDescriptorImageInfo brdfInfo{};
			brdfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			brdfInfo.imageView = brdfView;
			brdfInfo.sampler = brdfSampler;

			VkDescriptorImageInfo reflectionInfo{};
			reflectionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			reflectionInfo.imageView = reflectionView;
			reflectionInfo.sampler = reflectionSampler;

			VkWriteDescriptorSet irradianceWrite{};
			irradianceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			irradianceWrite.dstSet = descriptorSets[i];
			irradianceWrite.dstBinding = 11;
			irradianceWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			irradianceWrite.descriptorCount = 1;
			irradianceWrite.pImageInfo = &irradianceInfo;

			VkWriteDescriptorSet brdfWrite{};
			brdfWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			brdfWrite.dstSet = descriptorSets[i];
			brdfWrite.dstBinding = 12;
			brdfWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			brdfWrite.descriptorCount = 1;
			brdfWrite.pImageInfo = &brdfInfo;

			VkWriteDescriptorSet reflectionWrite{};
			reflectionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			reflectionWrite.dstSet = descriptorSets[i];
			reflectionWrite.dstBinding = 13;
			reflectionWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			reflectionWrite.descriptorCount = 1;
			reflectionWrite.pImageInfo = &reflectionInfo;

			VkWriteDescriptorSet iblWrites[3] = { irradianceWrite, brdfWrite, reflectionWrite };
			vkUpdateDescriptorSets(device, 3, iblWrites, 0, nullptr);
		}
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

	// Resolve attachment: final composited output for the graphics render pass.
	// The blend shader (subpass 2) writes here; copyToHost reads from this image.
	// Matches vkrender.cc's colorResolveAttachment pattern (non-MSAA path).
	image.format = colorFormat;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &resolveAttachment.image));
	vkGetImageMemoryRequirements(device, resolveAttachment.image, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &resolveAttachment.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, resolveAttachment.image, resolveAttachment.memory, 0));

	VkImageViewCreateInfo resolveImageView = vks::initializers::imageViewCreateInfo();
	resolveImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	resolveImageView.format = colorFormat;
	resolveImageView.subresourceRange = {};
	resolveImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	resolveImageView.subresourceRange.baseMipLevel = 0;
	resolveImageView.subresourceRange.levelCount = 1;
	resolveImageView.subresourceRange.baseArrayLayer = 0;
	resolveImageView.subresourceRange.layerCount = 1;
	resolveImageView.image = resolveAttachment.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &resolveImageView, nullptr, &resolveAttachment.view));
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

void HeadlessRenderer::createGraphicsRenderPass(int targetWidth, int targetHeight) {
	// Matches vkrender.cc createGraphicsRenderPass() non-MSAA path exactly.
	// 3 attachments: color(0), depth(1), colorResolve(2).
	// Non-MSAA: subpass 0 uses colorResolve as its sole color attachment (no resolve).

	VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkFormat depthFormat;
	vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);

	// Attachment 0: color — MSAA target (unused in non-MSAA path)
	VkAttachmentDescription colorAttachmentDesc = {};
	colorAttachmentDesc.format = colorFormat;
	colorAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // non-MSAA: don't care
	colorAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Attachment 1: depth
	VkAttachmentDescription depthAttachmentDesc = {};
	depthAttachmentDesc.format = depthFormat;
	depthAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Attachment 2: colorResolve — final composited output
	VkAttachmentDescription resolveAttachmentDesc = {};
	resolveAttachmentDesc.format = colorFormat;
	resolveAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	resolveAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // non-MSAA: clear
	resolveAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	resolveAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	resolveAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	resolveAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	resolveAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkAttachmentReference colorRef       = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthRef       = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkAttachmentReference resolveRef     = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	// Subpasses (non-MSAA path: colorResolve is the active color target)
	VkSubpassDescription subpasses[3] = {};

	// Subpass 0: all geometry — resolve attachment + depth
	// Non-MSAA: write directly to colorResolve (matches vkrender.cc line 3137:
	// subpasses[0].pColorAttachments = &colorResolveAttachmentRef).
	// With interlock, opaque geometry writes outColor here AND to opaqueColor buffer.
	// Without interlock, fragments are discarded but the attachment is cleared anyway.
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = &resolveRef;
	subpasses[0].pResolveAttachments = nullptr; // non-MSAA: no resolve
	subpasses[0].pDepthStencilAttachment = &depthRef;

	// Subpass 1: transparentData — no attachments (vkrender.cc pattern)
	subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	// Subpass 2: blend quad — colorResolve only, no depth
	subpasses[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[2].colorAttachmentCount = 1;
	subpasses[2].pColorAttachments = &resolveRef;
	subpasses[2].pResolveAttachments = nullptr; // non-MSAA: no resolve

	// Dependencies matching vkrender.cc
	VkSubpassDependency dependencies[3] = {};

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = 0;
	dependencies[0].dstAccessMask = 0;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	dependencies[2].srcSubpass = 1;
	dependencies[2].dstSubpass = 2;
	dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkAttachmentDescription attachments_desc[3];
	attachments_desc[0] = colorAttachmentDesc;
	attachments_desc[1] = depthAttachmentDesc;
	attachments_desc[2] = resolveAttachmentDesc;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 3;
	renderPassInfo.pAttachments = attachments_desc;
	renderPassInfo.subpassCount = 3;
	renderPassInfo.pSubpasses = subpasses;
	renderPassInfo.dependencyCount = 3;
	renderPassInfo.pDependencies = dependencies;
	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &graphicsRenderPass));

	// Create opaque render pass: color + depth, 1 subpass.
	VkAttachmentDescription opaqueAttachments[2];
	opaqueAttachments[0] = colorAttachmentDesc;
	opaqueAttachments[1] = depthAttachmentDesc;
	opaqueAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // opaque: clear the color

	VkSubpassDescription opaqueSubpass = {};
	opaqueSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	opaqueSubpass.colorAttachmentCount = 1;
	VkAttachmentReference opaqueColorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference opaqueDepthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	opaqueSubpass.pColorAttachments = &opaqueColorRef;
	opaqueSubpass.pDepthStencilAttachment = &opaqueDepthRef;

	VkSubpassDependency opaqueDep = {};
	opaqueDep.srcSubpass = VK_SUBPASS_EXTERNAL;
	opaqueDep.dstSubpass = 0;
	opaqueDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	opaqueDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	opaqueDep.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	opaqueDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	VkRenderPassCreateInfo opaqueRenderPassInfo = {};
	opaqueRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	opaqueRenderPassInfo.attachmentCount = 2;
	opaqueRenderPassInfo.pAttachments = opaqueAttachments;
	opaqueRenderPassInfo.subpassCount = 1;
	opaqueRenderPassInfo.pSubpasses = &opaqueSubpass;
	opaqueRenderPassInfo.dependencyCount = 1;
	opaqueRenderPassInfo.pDependencies = &opaqueDep;
	VK_CHECK_RESULT(vkCreateRenderPass(device, &opaqueRenderPassInfo, nullptr, &opaqueRenderPass));

	// Framebuffer for opaque path: [colorAttachment.view, depthAttachment.view]
	VkImageView opaqueAttachmentsViews[2];
	opaqueAttachmentsViews[0] = colorAttachment.view;
	opaqueAttachmentsViews[1] = depthAttachment.view;

	VkFramebufferCreateInfo fbInfo = vks::initializers::framebufferCreateInfo();
	fbInfo.attachmentCount = 2;
	fbInfo.pAttachments = opaqueAttachmentsViews;
	fbInfo.width = (uint32_t)targetWidth;
	fbInfo.height = (uint32_t)targetHeight;
	fbInfo.layers = 1;
	fbInfo.renderPass = opaqueRenderPass;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &opaqueFramebuffer));

	// Framebuffer for graphics path: [colorAttachment.view, depthAttachment.view, resolveAttachment.view]
	VkImageView graphicsAttachmentsViews[3];
	graphicsAttachmentsViews[0] = colorAttachment.view;
	graphicsAttachmentsViews[1] = depthAttachment.view;
	graphicsAttachmentsViews[2] = resolveAttachment.view;

	fbInfo.attachmentCount = 3;
	fbInfo.pAttachments = graphicsAttachmentsViews;
	fbInfo.renderPass = graphicsRenderPass;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &graphicsFramebuffer));
}

std::vector<char> readFile(const std::string& filename);

void HeadlessRenderer::createGraphicsPipelineLayout() {
	// Single shared pipeline layout for ALL graphics pipelines (opaque + transparent + count + blend).
	// Matches vkrender.cc: createGraphicsPipelineLayout() creates ONE graphicsPipelineLayout
	// used by all pipeline types. Push constants = uvec4(constants) + vec4(background) = 32 bytes.
	VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::uvec4) + sizeof(glm::vec4), 0);

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
		vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout);
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &graphicsPipelineLayout));
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

void HeadlessRenderer::createMaterialCountPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, countRenderPass);
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

	// MaterialVertex stride, only position attribute at location 0
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
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

	std::vector<std::string> countOptions{};
	if (m_Orthographic) {
		countOptions.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", countOptions);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "count.glsl", countOptions);

	countShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &materialCountPipeline));
}

void HeadlessRenderer::createColorCountPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, countRenderPass);
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

	// ColorVertex stride, only position attribute at location 0
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
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

	std::vector<std::string> countOptions{};
	if (m_Orthographic) {
		countOptions.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", countOptions);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "count.glsl", countOptions);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &colorCountPipeline));
}

// Triangle count pipeline: ColorVertex+GENERAL, countRenderPass subpass 0.
// Matches vkrender.cc: trianglePipelines[PIPELINE_COUNT] — used in refreshBuffers()
// to draw triangleData in subpass 0 of the count pass (inside if (!interlock)).
void HeadlessRenderer::createTriangleCountPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, countRenderPass);
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

	// ColorVertex stride + all 4 attributes (position, normal, material, color)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)
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

	std::vector<std::string> countOptions{};
	if (m_Orthographic) {
		countOptions.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", countOptions);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "count.glsl", countOptions);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &triangleCountPipeline));
}

// Transparent count pipeline: ColorVertex, countRenderPass subpass 1.
// Matches vkrender.cc: transparentPipelines[PIPELINE_COUNT] — used in refreshBuffers()
// to draw transparentData in subpass 1 of the count pass (after nextSubpass).
void HeadlessRenderer::createTransparentCountPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, countRenderPass);
	pipelineCreateInfo.subpass = 1;  // subpass 1 — matches vkrender.cc transparentPipelines config.graphicsSubpass=1
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

	// ColorVertex stride, only position attribute at location 0 (same as colorCountPipeline)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
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

	std::vector<std::string> countOptions{};
	if (m_Orthographic) {
		countOptions.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", countOptions);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "count.glsl", countOptions);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &transparentCountPipeline));
}

void HeadlessRenderer::createMaterialTransparentPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
	pipelineCreateInfo.subpass = 0;  // Matches vkrender.cc: materialPipelines[PIPELINE_TRANSPARENT] uses config.graphicsSubpass=0
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

	// MaterialVertex stride + attributes
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)
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

	std::vector<std::string> options{ "NORMAL", "MATERIAL" };
	if (ibl) { options.push_back("USE_IBL"); }
	if (srgb) { options.push_back("OUTPUT_AS_SRGB"); }
	if (interlock) { options.push_back("HAVE_INTERLOCK"); }
	if (m_Orthographic) { options.push_back("ORTHOGRAPHIC"); }

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &materialTransparentPipeline));
}

void HeadlessRenderer::createColorTransparentPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
	pipelineCreateInfo.subpass = 0;  // Matches vkrender.cc: materialPipelines[PIPELINE_TRANSPARENT] uses config.graphicsSubpass=0
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

	// ColorVertex stride + attributes
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)
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

	// Color-transparent pipeline: COLOR objects in colorData always have POSITIVE
	// material indices (MaterialIndex=materialIndex for non-transparent patches).
	// Transparent COLOR objects go to transparentData, not colorData.
	// Therefore GENERAL is NOT needed here — it would cause an off-by-one error
	// (materials[abs(inMaterial)-1]) and skip the per-vertex color path.
	std::vector<std::string> options{ "NORMAL", "MATERIAL", "COLOR" };
	if (ibl) { options.push_back("USE_IBL"); }
	if (srgb) { options.push_back("OUTPUT_AS_SRGB"); }
	if (interlock) { options.push_back("HAVE_INTERLOCK"); }
	if (m_Orthographic) { options.push_back("ORTHOGRAPHIC"); }

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &colorTransparentPipeline));
}

// Triangle-transparent pipeline: ColorVertex+GENERAL, TRIANGLE_LIST, graphicsRenderPass subpass 0.
// Matches vkrender.cc: trianglePipelines[PIPELINE_TRANSPARENT] with triangleShaderOptions = {"COLOR","NORMAL","GENERAL"}.
// Used for triangleData (V3dTriangleGroup) in the transparent path.
void HeadlessRenderer::createTriangleTransparentPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
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

	// ColorVertex stride + all 4 attributes (position, normal, material, color)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)
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

	// Matches vkrender.cc triangleShaderOptions + MATERIAL: COLOR + NORMAL + GENERAL + MATERIAL
	std::vector<std::string> options{ "NORMAL", "MATERIAL", "COLOR", "GENERAL" };
	if (ibl) { options.push_back("USE_IBL"); }
	if (srgb) { options.push_back("OUTPUT_AS_SRGB"); }
	if (interlock) { options.push_back("HAVE_INTERLOCK"); }
	if (m_Orthographic) { options.push_back("ORTHOGRAPHIC"); }

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &triangleTransparentPipeline));
}

// Line-transparent pipeline: MaterialVertex with LINE_LIST topology for lineData
// in the transparent path (subpass 0).  Matches vkrender.cc: linePipelines[PIPELINE_TRANSPARENT]
// which uses eLineList topology regardless of opaque/transparent mode.
void HeadlessRenderer::createLineTransparentPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
	pipelineCreateInfo.subpass = 0;  // lineData drawn in subpass 0 alongside material/color data
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

	// MaterialVertex stride + attributes (same as materialTransparentPipeline)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)
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

	// Same shader options as vkrender.cc linePipelines[PIPELINE_TRANSPARENT]:
	// NORMAL + MATERIAL (no TRANSPARENT, no OPAQUE).  The fragment shader selects
	// the interlock path when HAVE_INTERLOCK is defined and OPAQUE is not,
	// or the A-buffer atomic+discard path when neither INTERLOCK nor OPAQUE is set.
	std::vector<std::string> options{ "NORMAL", "MATERIAL" };
	if (ibl) { options.push_back("USE_IBL"); }
	if (srgb) { options.push_back("OUTPUT_AS_SRGB"); }
	if (interlock) { options.push_back("HAVE_INTERLOCK"); }
	options.push_back("LOCALSIZE " + std::to_string(localSize));
	options.push_back("BLOCKSIZE " + std::to_string(blockSize));
	options.push_back("ARRAYSIZE " + std::to_string(maxSize));
	if (m_Orthographic) { options.push_back("ORTHOGRAPHIC"); }

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &lineTransparentPipeline));
}

// Transparent pipeline for transparentData in subpass 1.
// Matches vkrender.cc: transparentPipelines[PIPELINE_TRANSPARENT] uses graphicsSubpass=1.
void HeadlessRenderer::createTransparentPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
	pipelineCreateInfo.subpass = 1;  // transparentData drawn in subpass 1 (after nextSubpass)
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

	// ColorVertex stride + attributes (same as colorTransparentPipeline)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)
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

	std::vector<std::string> options{ "NORMAL", "TRANSPARENT", "MATERIAL", "COLOR", "GENERAL" };
	// Matches vkrender.cc transparentShaderOptions + MATERIAL — HAS GENERAL for negative material indices.
	if (ibl) { options.push_back("USE_IBL"); }
	if (srgb) { options.push_back("OUTPUT_AS_SRGB"); }
	if (interlock) { options.push_back("HAVE_INTERLOCK"); }
	if (m_Orthographic) { options.push_back("ORTHOGRAPHIC"); }

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &transparentPipeline));
}

void HeadlessRenderer::createBlendPipeline(int targetWidth, int targetHeight) {
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			(currentDrawMode == DRAWMODE_WIREFRAME || currentDrawMode == DRAWMODE_OUTLINE)
				? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, graphicsRenderPass);
	pipelineCreateInfo.subpass = 2;  // Blend quad in subpass 2
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
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &blendPipeline));
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

	// IBL bindings (11-13) - mirror asymptote's vkrender.cc
	VkDescriptorSetLayoutBinding irradianceSamplerBinding{};
	irradianceSamplerBinding.binding = 11;
	irradianceSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	irradianceSamplerBinding.descriptorCount = 1;
	irradianceSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding brdfSamplerBinding{};
	brdfSamplerBinding.binding = 12;
	brdfSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	brdfSamplerBinding.descriptorCount = 1;
	brdfSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding reflectionSamplerBinding{};
	reflectionSamplerBinding.binding = 13;
	reflectionSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	reflectionSamplerBinding.descriptorCount = 1;
	reflectionSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

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

	if (ibl) {
		bindings.push_back(irradianceSamplerBinding);
		bindings.push_back(brdfSamplerBinding);
		bindings.push_back(reflectionSamplerBinding);
	}

	VkDescriptorSetLayoutCreateInfo descriptorLayout =
    vks::initializers::descriptorSetLayoutCreateInfo(bindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));
}

std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		std::cout << "v3d: failed to open shader file " + filename << std::endl;
		return {};
	}

	size_t fileSize = (size_t) file.tellg();
	if (fileSize == 0) {
		std::cout << "v3d: shader file is empty: " + filename << std::endl;
		return {};
	}

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
void HeadlessRenderer::createMaterialPipeline(DrawMode drawMode, int targetWidth, int targetHeight) {
	// Material pipeline: MaterialVertex, no GENERAL/COLOR (matches vkrender.cc materialPipelines)
	VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPolygonMode polygonMode = (drawMode == DRAWMODE_WIREFRAME || drawMode == DRAWMODE_OUTLINE)
		? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(primitiveTopology, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(polygonMode, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, opaqueRenderPass);
	pipelineCreateInfo.subpass = 0;
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// MaterialVertex: position(12) + normal(12) + material(4) = 28 bytes
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0), 				// Position
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),	// Normal
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)			// Material Index
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

	// Material shader options: NORMAL + OPAQUE + MATERIAL (no GENERAL, no COLOR)
	std::vector<std::string> options{ "NORMAL", "OPAQUE", "MATERIAL" };
	if (ibl) {
		options.push_back("USE_IBL");
	}
	if (interlock) {
		options.push_back("HAVE_INTERLOCK");
	}
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	materialShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &materialPipeline));
}

void HeadlessRenderer::createColorPipeline(DrawMode drawMode, int targetWidth, int targetHeight) {
	// Color pipeline: ColorVertex, with COLOR (matches vkrender.cc colorPipelines)
	VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPolygonMode polygonMode = (drawMode == DRAWMODE_WIREFRAME || drawMode == DRAWMODE_OUTLINE)
		? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(primitiveTopology, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(polygonMode, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, opaqueRenderPass);
	pipelineCreateInfo.subpass = 0;
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// ColorVertex: position(12) + normal(12) + material(4) + color(16) = 44 bytes
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(ColorVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0), 					// Position
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),		// Normal
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6),				// Material Index
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float)*7)	// Color
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

	// Color shader options: NORMAL + OPAQUE + MATERIAL + COLOR (no GENERAL)
	// Opaque COLOR objects store POSITIVE material indices (MaterialIndex=materialIndex),
	// so materials[inMaterial] works directly. GENERAL would cause an off-by-one error
	// (materials[abs(inMaterial)-1]) and skip the per-vertex color path.
	// GENERAL is only needed for transparent COLOR objects which store negative indices.
	std::vector<std::string> options{ "NORMAL", "OPAQUE", "MATERIAL", "COLOR" };
	if (ibl) {
		options.push_back("USE_IBL");
	}
	if (interlock) {
		options.push_back("HAVE_INTERLOCK");
	}
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	colorShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &colorPipeline));
}

void HeadlessRenderer::recordCommandBuffer(int targetWidth, int targetHeight, size_t lightCount) {
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

	VkClearValue clearValues[2];
	clearValues[0].color.float32[0] = m_BackgroundColor.r;
	clearValues[0].color.float32[1] = m_BackgroundColor.g;
	clearValues[0].color.float32[2] = m_BackgroundColor.b;
	clearValues[0].color.float32[3] = m_BackgroundColor.a;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;

	// Upload global VertexBuffers to persistent GPU buffers BEFORE the render pass.
	// Matches vkrender.cc: uploadPersistentBuffer records into a separate transfer
	// command buffer that is submitted and completed (via semaphore) before the
	// graphics render pass begins.  We record directly into our single command
	// buffer but outside the render pass, followed by a pipeline barrier.
	if (!materialData.indices.empty() && !materialData.materialVertices.empty()) {
		VkDeviceSize vsize = materialData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = materialData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(commandBuffer, materialVertexBuffer, materialVertexMemory, materialVertexBufferSize,
		                         materialVertexStagingBuffer, materialVertexStagingMemory, materialVertexStgSize,
		                         materialData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(commandBuffer, materialIndexBuffer, materialIndexMemory, materialIndexBufferSize,
		                         materialIndexStagingBuffer, materialIndexStagingMemory, materialIndexStgSize,
		                         materialData.indices.data(), isize, false);
	}

	if (!lineData.indices.empty() && !lineData.materialVertices.empty()) {
		VkDeviceSize vsize = lineData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = lineData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(commandBuffer, lineVertexBuffer, lineVertexMemory, lineVertexBufferSize,
		                         lineVertexStagingBuffer, lineVertexStagingMemory, lineVertexStgSize,
		                         lineData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(commandBuffer, lineIndexBuffer, lineIndexMemory, lineIndexBufferSize,
		                         lineIndexStagingBuffer, lineIndexStagingMemory, lineIndexStgSize,
		                         lineData.indices.data(), isize, false);
	}

	// Memory barrier: ensure all vkCmdCopyBuffer transfers complete before
	// vertex/index buffers are read by the draw calls inside the render pass.
	VkBufferMemoryBarrier bufBarrier = {};
	bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	bufBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	bufBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufBarrier.offset = 0;
	bufBarrier.size = VK_WHOLE_SIZE;

	if (materialVertexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = materialVertexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}
	if (materialIndexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = materialIndexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}
	if (lineVertexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = lineVertexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}
	if (lineIndexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = lineIndexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}

	// Upload colorData to persistent GPU buffers (vkrender.cc: drawColors draws from colorBuffers)
	if (!colorData.indices.empty() && !colorData.colorVertices.empty()) {
		VkDeviceSize vsize = colorData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = colorData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(commandBuffer, colorVertexBuffer, colorVertexMemory, colorVertexBufferSize,
		                         colorVertexStagingBuffer, colorVertexStagingMemory, colorVertexStgSize,
		                         colorData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(commandBuffer, colorIndexBuffer, colorIndexMemory, colorIndexBufferSize,
		                         colorIndexStagingBuffer, colorIndexStagingMemory, colorIndexStgSize,
		                         colorData.indices.data(), isize, false);
	}

	// Memory barrier for color buffers too
	if (colorVertexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = colorVertexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}
	if (colorIndexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = colorIndexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}

	// Upload transparentData to persistent GPU buffers (vkrender.cc: drawTransparent draws from transparentBuffers)
	if (!transparentData.indices.empty() && !transparentData.colorVertices.empty()) {
		VkDeviceSize vsize = transparentData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = transparentData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(commandBuffer, transparentVertexBuffer, transparentVertexMemory, transparentVertexBufferSize,
		                         transparentVertexStagingBuffer, transparentVertexStagingMemory, transparentVertexStgSize,
		                         transparentData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(commandBuffer, transparentIndexBuffer, transparentIndexMemory, transparentIndexBufferSize,
		                         transparentIndexStagingBuffer, transparentIndexStagingMemory, transparentIndexStgSize,
		                         transparentData.indices.data(), isize, false);
	}

	// Memory barrier for transparent buffers too
	if (transparentVertexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = transparentVertexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}
	if (transparentIndexBuffer != VK_NULL_HANDLE) {
		bufBarrier.buffer = transparentIndexBuffer;
		vkCmdPipelineBarrier(commandBuffer,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
	}

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderArea.extent.width = targetWidth;
	renderPassBeginInfo.renderArea.extent.height = targetHeight;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.renderPass = opaqueRenderPass;
	renderPassBeginInfo.framebuffer = opaqueFramebuffer;

	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Match vkrender.cc buildPushConstants(): push 64 bytes (uvec4 constants + vec4 background).
	// nlights = 0 for OUTLINE/WIREFRAME disables the BRDF lighting loop.
	struct PushConstants { glm::uvec4 constants; glm::vec4 background; } pushConsts{};
	pushConsts.constants.x = (currentDrawMode != DRAWMODE_NORMAL) ? 0u : static_cast<uint32_t>(lightCount);
	pushConsts.background = m_BackgroundColor;

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

	// --- Draw materialData (material Bezier patches & triangles) ---
	// Follows vkrender.cc drawBuffer: skip if indices empty, upload + draw otherwise.
	if (!materialData.indices.empty()) {
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, materialPipeline);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &materialVertexBuffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, materialIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(commandBuffer, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConsts), &pushConsts);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(materialData.indices.size()), 1, 0, 0, 0);
	}

	// --- Draw colorData (vertex-dependent colors) ---
	// Follows vkrender.cc drawColors: separate draw call from colorBuffers using colorPipelines.
	// In Mixed mode, materialData is drawn first, then colorData overlays the colored elements.
	if (!colorData.indices.empty()) {
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, colorPipeline);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &colorVertexBuffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, colorIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(commandBuffer, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConsts), &pushConsts);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(colorData.indices.size()), 1, 0, 0, 0);
	}

	// --- Draw lineData (BezierCurve edges, V3dLineSegment) via LINE_LIST pipeline ---
	// Follows vkrender.cc drawBuffers order: drawLines() after drawMaterials().
	if (!lineData.indices.empty() && linePipeline != VK_NULL_HANDLE) {
		VkDeviceSize lineOffsets[1] = { 0 };
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &lineVertexBuffer, lineOffsets);
		vkCmdBindIndexBuffer(commandBuffer, lineIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(commandBuffer, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConsts), &pushConsts);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(lineData.indices.size()), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(commandBuffer);
	VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

VkCommandBuffer HeadlessRenderer::recordCountCommandBuffer(size_t indexCount, size_t lightCount) {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer countCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &countCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(countCmd, &cmdBufInfo));

	// Upload vertex data into the count command buffer (vkrender.cc: drawBuffer does inline upload).
	// This ensures GPU buffers exist before any bind/draw calls in this or later command buffers.
	VkBufferMemoryBarrier countBarrier = {};
	countBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	countBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	countBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	countBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	countBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	countBarrier.offset = 0;
	countBarrier.size = VK_WHOLE_SIZE;

	if (!materialData.indices.empty() && !materialData.materialVertices.empty()) {
		VkDeviceSize vsize = materialData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = materialData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(countCmd, materialVertexBuffer, materialVertexMemory, materialVertexBufferSize,
		                         materialVertexStagingBuffer, materialVertexStagingMemory, materialVertexStgSize,
		                         materialData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(countCmd, materialIndexBuffer, materialIndexMemory, materialIndexBufferSize,
		                         materialIndexStagingBuffer, materialIndexStagingMemory, materialIndexStgSize,
		                         materialData.indices.data(), isize, false);
	}
	if (!lineData.indices.empty() && !lineData.materialVertices.empty()) {
		VkDeviceSize vsize = lineData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = lineData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(countCmd, lineVertexBuffer, lineVertexMemory, lineVertexBufferSize,
		                         lineVertexStagingBuffer, lineVertexStagingMemory, lineVertexStgSize,
		                         lineData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(countCmd, lineIndexBuffer, lineIndexMemory, lineIndexBufferSize,
		                         lineIndexStagingBuffer, lineIndexStagingMemory, lineIndexStgSize,
		                         lineData.indices.data(), isize, false);
	}
	if (!colorData.indices.empty() && !colorData.colorVertices.empty()) {
		VkDeviceSize vsize = colorData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = colorData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(countCmd, colorVertexBuffer, colorVertexMemory, colorVertexBufferSize,
		                         colorVertexStagingBuffer, colorVertexStagingMemory, colorVertexStgSize,
		                         colorData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(countCmd, colorIndexBuffer, colorIndexMemory, colorIndexBufferSize,
		                         colorIndexStagingBuffer, colorIndexStagingMemory, colorIndexStgSize,
		                         colorData.indices.data(), isize, false);
	}
	if (!transparentData.indices.empty() && !transparentData.colorVertices.empty()) {
		VkDeviceSize vsize = transparentData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = transparentData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(countCmd, transparentVertexBuffer, transparentVertexMemory, transparentVertexBufferSize,
		                         transparentVertexStagingBuffer, transparentVertexStagingMemory, transparentVertexStgSize,
		                         transparentData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(countCmd, transparentIndexBuffer, transparentIndexMemory, transparentIndexBufferSize,
		                         transparentIndexStagingBuffer, transparentIndexStagingMemory, transparentIndexStgSize,
		                         transparentData.indices.data(), isize, false);
	}
	if (!triangleData.indices.empty() && !triangleData.colorVertices.empty()) {
		VkDeviceSize vsize = triangleData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = triangleData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(countCmd, triangleVertexBuffer, triangleVertexMemory, triangleVertexBufferSize,
		                         triangleVertexStagingBuffer, triangleVertexStagingMemory, triangleVertexStgSize,
		                         triangleData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(countCmd, triangleIndexBuffer, triangleIndexMemory, triangleIndexBufferSize,
		                         triangleIndexStagingBuffer, triangleIndexStagingMemory, triangleIndexStgSize,
		                         triangleData.indices.data(), isize, false);
	}

	// Barriers: ensure uploads complete before count draw reads vertex/index data.
	VkBuffer* allBuffers[] = { &materialVertexBuffer, &materialIndexBuffer,
	                           &lineVertexBuffer, &lineIndexBuffer,
	                           &colorVertexBuffer, &colorIndexBuffer,
	                           &transparentVertexBuffer, &transparentIndexBuffer,
	                           &triangleVertexBuffer, &triangleIndexBuffer };
	for (auto* b : allBuffers) {
		if (*b != VK_NULL_HANDLE) {
			countBarrier.buffer = *b;
			vkCmdPipelineBarrier(countCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			                     0, 0, nullptr, 1, &countBarrier, 0, nullptr);
		}
	}

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
	vkCmdBindDescriptorSets(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

	// Bind material count pipeline and buffers (vkrender.cc: drawBuffer returns early if indices empty)
	glm::uvec4 constants{ 0 };
	constants.x = lightCount;
	constants.y = currentTargetSize.x;

	// When interlock=true, opaque geometry writes directly to opaqueColor/opaqueDepth
	// in the graphics pass via fragment shader interlock.  Do NOT also count them
	// here via atomics, or we double-count and get stale A-buffer garbage during rotation.
	// Matches vkrender.cc: refreshBuffers() wraps these draws in "if (!interlock)".
	if (!interlock) {
		if (indexCount > 0) {
			vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, materialCountPipeline);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(countCmd, 0, 1, &materialVertexBuffer, offsets);
			vkCmdBindIndexBuffer(countCmd, materialIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdPushConstants(countCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);

			// Draw indexed (subpass 0: opaque count)
			vkCmdDrawIndexed(countCmd, indexCount, 1, 0, 0, 0);
		}

		// Also count colorData vertices (vkrender.cc: drawBuffer for colorBuffers in count pass)
		if (!colorData.indices.empty()) {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, colorCountPipeline);
			vkCmdBindVertexBuffers(countCmd, 0, 1, &colorVertexBuffer, offsets);
			vkCmdBindIndexBuffer(countCmd, colorIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(countCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);
			vkCmdDrawIndexed(countCmd, static_cast<uint32_t>(colorData.indices.size()), 1, 0, 0, 0);
		}

		// Also count lineData vertices (vkrender.cc: drawBuffer for lineBuffers in count pass)
		if (!lineData.indices.empty()) {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, materialCountPipeline);
			vkCmdBindVertexBuffers(countCmd, 0, 1, &lineVertexBuffer, offsets);
			vkCmdBindIndexBuffer(countCmd, lineIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(countCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);
			vkCmdDrawIndexed(countCmd, static_cast<uint32_t>(lineData.indices.size()), 1, 0, 0, 0);
		}

		// Also count triangleData vertices (vkrender.cc: drawBuffer for triangleBuffers in count pass)
		if (!triangleData.indices.empty()) {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triangleCountPipeline);
			vkCmdBindVertexBuffers(countCmd, 0, 1, &triangleVertexBuffer, offsets);
			vkCmdBindIndexBuffer(countCmd, triangleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(countCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);
			vkCmdDrawIndexed(countCmd, static_cast<uint32_t>(triangleData.indices.size()), 1, 0, 0, 0);
		}
	}

	// Advance to subpass 1 for transparentData counting (vkrender.cc: nextSubpass + drawTransparent)
	vkCmdNextSubpass(countCmd, VK_SUBPASS_CONTENTS_INLINE);

	if (!transparentData.indices.empty()) {
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindPipeline(countCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentCountPipeline);
		vkCmdBindVertexBuffers(countCmd, 0, 1, &transparentVertexBuffer, offsets);
		vkCmdBindIndexBuffer(countCmd, transparentIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(countCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::uvec4), &constants);
		vkCmdDrawIndexed(countCmd, static_cast<uint32_t>(transparentData.indices.size()), 1, 0, 0, 0);
	}

	// Advance to subpass 2 (empty, just like vkrender.cc)
	vkCmdNextSubpass(countCmd, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdEndRenderPass(countCmd);
	VK_CHECK_RESULT(vkEndCommandBuffer(countCmd));

	return countCmd;
}

VkCommandBuffer HeadlessRenderer::recordComputeCommandBuffer() {
	VkCommandBufferAllocateInfo cmdBufAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer computeCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &computeCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(computeCmd, &cmdBufInfo));

	uint32_t g = (elements + groupSize - 1) / groupSize;
	g = std::min(g, maxComputeWorkGroupCountX);
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

	// Do NOT submit here.  refreshBuffers() batch-submits count + compute
	// together in a single vkQueueSubmit(commandBufferCount=2), ensuring proper
	// storage buffer visibility.  Submitting independently causes atomicAdd on
	// offsetBuffer to deadlock (Bug #2: GPU hang when interlock=false).

	return computeCmd;
}

void HeadlessRenderer::refreshBuffers(size_t indexCount, size_t lightCount) {
	VkCommandBuffer countCmd = recordCountCommandBuffer(indexCount, lightCount);
	VkCommandBuffer computeCmd = recordComputeCommandBuffer();

	// Batch submit: count + compute in a single vkQueueSubmit.
	// This ensures proper storage buffer visibility across the two passes.
	// Without batching, atomicAdd on offsetBuffer in the transparent graphics pass deadlocks.
	VkCommandBuffer cmds[2] = { countCmd, computeCmd };
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 2;
	submitInfo.pCommandBuffers = cmds;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	vkFreeCommandBuffers(device, commandPool, 1, &countCmd);
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

unsigned char* HeadlessRenderer::copyToHost(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout, bool useResolve) {
	// Copy framebuffer image to host visible image.
	// When useResolve=true, reads from resolveAttachment (final composite from blend pass).
	// When useResolve=false, reads from colorAttachment (opaque path).
	VkImage srcImage = useResolve ? resolveAttachment.image : colorAttachment.image;
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

	// Transition source image to TRANSFER_SRC_OPTIMAL.
	// For resolve path (useResolve=true), finalLayout is already TRANSFER_SRC_OPTIMAL,
	// so oldLayout matches and no actual transition occurs.
	// For opaque path (useResolve=false), transitions from COLOR_ATTACHMENT_OPTIMAL.
	VkImageMemoryBarrier srcBarrier = {};
	srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	srcBarrier.oldLayout = useResolve ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	srcBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.image = srcImage;
	srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	srcBarrier.subresourceRange.baseMipLevel = 0;
	srcBarrier.subresourceRange.levelCount = 1;
	srcBarrier.subresourceRange.baseArrayLayer = 0;
	srcBarrier.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(copyCmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

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
		srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

	// Transition source image back to COLOR_ATTACHMENT_OPTIMAL
	// (vkrender.cc: exportSingleTile transitions back after copy).
	vks::tools::insertImageMemoryBarrier(
		copyCmd,
		srcImage,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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

// ============================================================================
// IBL (Image-Based Lighting) - follows asymptote/vkrender.cc initIBL() pattern
// ============================================================================

static std::vector<float> loadEXR(const std::string& filePath, int& width, int& height) {
	float* data = nullptr;
	char const* err = nullptr;
	int ret = LoadEXR(&data, &width, &height, filePath.c_str(), &err);
	if (ret != TINYEXR_SUCCESS) {
		std::cerr << "TinyEXR Error: " << (err ? err : "unknown") << " loading: " << filePath << std::endl;
		if (err) FreeEXRErrorMessage(err);
		return {};
	}
	std::vector<float> result(data, data + width * height * 4);
	free(data);
	return result;
}

void HeadlessRenderer::copyIBLDataToImage(const float* data, VkDeviceSize dataSize,
    VkImage image, uint32_t w, uint32_t h, uint32_t layerOffset) {
	// Create staging buffer
	VkBufferCreateInfo bufCI = {};
	bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufCI.size = dataSize;
	bufCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBuffer stagingBuf;
	VK_CHECK_RESULT(vkCreateBuffer(device, &bufCI, nullptr, &stagingBuf));

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, stagingBuf, &memReqs);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory stagingMem;
	VK_CHECK_RESULT(vkAllocateMemory(device, &alloc, nullptr, &stagingMem));

	void* mapped;
	VK_CHECK_RESULT(vkMapMemory(device, stagingMem, 0, dataSize, 0, &mapped));
	memcpy(mapped, data, dataSize);
	vkUnmapMemory(device, stagingMem);
	VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuf, stagingMem, 0));

	// Copy buffer -> image via one-time command buffer
	VkCommandBufferAllocateInfo cmdAlloc = {};
	cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAlloc.commandPool = commandPool;
	cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdAlloc.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdAlloc, &cmd));

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &begin));

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, static_cast<int32_t>(layerOffset) };
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(cmd, stagingBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region);

	VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	VkFenceCreateInfo fenceCI = {};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);
	vkFreeCommandBuffers(device, commandPool, 1, &cmd);

	vkDestroyBuffer(device, stagingBuf, nullptr);
	vkFreeMemory(device, stagingMem, nullptr);
}

void HeadlessRenderer::transitionImageLayout(VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkCommandBufferAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc.commandPool = commandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &alloc, &cmd));

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &begin));

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
		0, nullptr, 0, nullptr, 1, &barrier);

	VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	VkFenceCreateInfo fenceCI = {};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);
	vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

static void createSampler(VkDevice device, VkSampler& sampler) {
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = VK_FILTER_LINEAR;
	info.minFilter = VK_FILTER_LINEAR;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info.minLod = 0.0f;
	info.maxLod = 0.0f;
	info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
}

VkResult HeadlessRenderer::createIBLImage(const std::vector<float>& data,
    uint32_t width, uint32_t height,
    VkImage& image, VkDeviceMemory& memory,
    VkImageView& imageView, VkSampler& sampler) {

	VkImageCreateInfo imgCI = {};
	imgCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgCI.imageType = VK_IMAGE_TYPE_2D;
	imgCI.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgCI.extent.width = width;
	imgCI.extent.height = height;
	imgCI.extent.depth = 1;
	imgCI.mipLevels = 1;
	imgCI.arrayLayers = 1;
	imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device, &imgCI, nullptr, &image));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, image, &memReqs);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &alloc, nullptr, &memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, image, memory, 0));

	transitionImageLayout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	copyIBLDataToImage(data.data(), data.size() * sizeof(float),
		image, width, height, 0);

	transitionImageLayout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkImageViewCreateInfo viewCI = {};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.image = image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &imageView));

	createSampler(device, sampler);

	return VK_SUCCESS;
}

VkResult HeadlessRenderer::createIBLImage3D(const std::vector<std::vector<float>>& layers,
    uint32_t width, uint32_t height,
    VkImage& image, VkDeviceMemory& memory,
    VkImageView& imageView, VkSampler& sampler) {

	VkImageCreateInfo imgCI = {};
	imgCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgCI.imageType = VK_IMAGE_TYPE_3D;
	imgCI.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgCI.extent.width = width;
	imgCI.extent.height = height;
	imgCI.extent.depth = static_cast<uint32_t>(layers.size());
	imgCI.mipLevels = 1;
	imgCI.arrayLayers = 1;
	imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device, &imgCI, nullptr, &image));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, image, &memReqs);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &alloc, nullptr, &memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, image, memory, 0));

	transitionImageLayout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	for (uint32_t i = 0; i < static_cast<uint32_t>(layers.size()); ++i) {
		copyIBLDataToImage(layers[i].data(), layers[i].size() * sizeof(float),
			image, width, height, i);
	}

	transitionImageLayout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkImageViewCreateInfo viewCI = {};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.image = image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
	viewCI.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &imageView));

	createSampler(device, sampler);

	return VK_SUCCESS;
}

void HeadlessRenderer::initIBL(const std::string& iblPath) {
	// Load diffuse.exr -> irradiance (2D)
	int w = 0, h = 0;
	auto diffuseData = loadEXR(iblPath + "/diffuse.exr", w, h);
	if (diffuseData.empty()) {
		std::cerr << "v3d: failed to load IBL diffuse.exr from " << iblPath << std::endl;
		return;
	}
	createIBLImage(diffuseData, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
		irradianceImg, irradianceImgMemory, irradianceView, irradianceSampler);

	// Load refl.exr -> brdf (2D) - from imageDir (parent of iblPath)
	std::string imageDir = iblPath.substr(0, iblPath.find_last_of('/'));
	w = 0; h = 0;
	std::string reflPath = imageDir + "/refl.exr";
	auto brdfData = loadEXR(reflPath, w, h);
	if (brdfData.empty()) {
		std::cerr << "v3d: failed to load IBL refl.exr from " << reflPath << std::endl;
		return;
	}
	createIBLImage(brdfData, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
		brdfImg, brdfImgMemory, brdfView, brdfSampler);

	// Load refl0.exr .. refl10.exr -> reflection (3D, depth=11)
	constexpr int NTEXTURES = 11;
	std::vector<std::vector<float>> layers;
	layers.reserve(NTEXTURES);
	w = 0; h = 0;
	for (int i = 0; i < NTEXTURES; ++i) {
		auto layerData = loadEXR(iblPath + "/refl" + std::to_string(i) + ".exr", w, h);
		if (layerData.empty()) {
			std::cerr << "v3d: failed to load IBL refl" << i << ".exr from " << iblPath << std::endl;
			return;
		}
		layers.push_back(std::move(layerData));
	}
	createIBLImage3D(layers, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
		reflectionImg, reflectionImgMemory, reflectionView, reflectionSampler);
}

void HeadlessRenderer::destroyIBLResources() {
	if (irradianceSampler) { vkDestroySampler(device, irradianceSampler, nullptr); irradianceSampler = VK_NULL_HANDLE; }
	if (irradianceView) { vkDestroyImageView(device, irradianceView, nullptr); irradianceView = VK_NULL_HANDLE; }
	if (irradianceImg) { vkDestroyImage(device, irradianceImg, nullptr); irradianceImg = VK_NULL_HANDLE; }
	if (irradianceImgMemory) { vkFreeMemory(device, irradianceImgMemory, nullptr); irradianceImgMemory = VK_NULL_HANDLE; }

	if (brdfSampler) { vkDestroySampler(device, brdfSampler, nullptr); brdfSampler = VK_NULL_HANDLE; }
	if (brdfView) { vkDestroyImageView(device, brdfView, nullptr); brdfView = VK_NULL_HANDLE; }
	if (brdfImg) { vkDestroyImage(device, brdfImg, nullptr); brdfImg = VK_NULL_HANDLE; }
	if (brdfImgMemory) { vkFreeMemory(device, brdfImgMemory, nullptr); brdfImgMemory = VK_NULL_HANDLE; }

	if (reflectionSampler) { vkDestroySampler(device, reflectionSampler, nullptr); reflectionSampler = VK_NULL_HANDLE; }
	if (reflectionView) { vkDestroyImageView(device, reflectionView, nullptr); reflectionView = VK_NULL_HANDLE; }
	if (reflectionImg) { vkDestroyImage(device, reflectionImg, nullptr); reflectionImg = VK_NULL_HANDLE; }
	if (reflectionImgMemory) { vkFreeMemory(device, reflectionImgMemory, nullptr); reflectionImgMemory = VK_NULL_HANDLE; }
}

void HeadlessRenderer::cleanup() {
	vkDestroyImageView(device, colorAttachment.view, nullptr);
	vkDestroyImage(device, colorAttachment.image, nullptr);
	vkFreeMemory(device, colorAttachment.memory, nullptr);
	vkDestroyImageView(device, depthAttachment.view, nullptr);
	vkDestroyImage(device, depthAttachment.image, nullptr);
	vkFreeMemory(device, depthAttachment.memory, nullptr);
	vkDestroyImageView(device, resolveAttachment.view, nullptr);
	vkDestroyImage(device, resolveAttachment.image, nullptr);
	vkFreeMemory(device, resolveAttachment.memory, nullptr);
	vkDestroyPipeline(device, materialPipeline, nullptr);
	vkDestroyPipeline(device, colorPipeline, nullptr);

	for (auto shadermodule : materialShaderModules) {
		vkDestroyShaderModule(device, shadermodule, nullptr);
	}
	materialShaderModules.clear();
	for (auto shadermodule : colorShaderModules) {
		vkDestroyShaderModule(device, shadermodule, nullptr);
	}
	colorShaderModules.clear();

	// Cleanup line pipeline resources
	if (linePipeline) vkDestroyPipeline(device, linePipeline, nullptr);
	for (auto m : lineShaderModules) vkDestroyShaderModule(device, m, nullptr);
	lineShaderModules.clear();
	if (lineVertexBuffer) { vkDestroyBuffer(device, lineVertexBuffer, nullptr); vkFreeMemory(device, lineVertexMemory, nullptr); }
	if (lineIndexBuffer) { vkDestroyBuffer(device, lineIndexBuffer, nullptr); vkFreeMemory(device, lineIndexMemory, nullptr); }
	linePipeline = VK_NULL_HANDLE;
	lineVertexBuffer = VK_NULL_HANDLE;
	lineVertexMemory = VK_NULL_HANDLE;
	lineIndexBuffer = VK_NULL_HANDLE;
	lineIndexMemory = VK_NULL_HANDLE;

	// Cleanup transparency resources
	if (countRenderPass) vkDestroyRenderPass(device, countRenderPass, nullptr);
	if (countFramebuffer) vkDestroyFramebuffer(device, countFramebuffer, nullptr);
	if (opaqueRenderPass) vkDestroyRenderPass(device, opaqueRenderPass, nullptr);
	if (opaqueFramebuffer) vkDestroyFramebuffer(device, opaqueFramebuffer, nullptr);
	if (graphicsRenderPass) vkDestroyRenderPass(device, graphicsRenderPass, nullptr);
	if (graphicsFramebuffer) vkDestroyFramebuffer(device, graphicsFramebuffer, nullptr);
	if (graphicsPipelineLayout) vkDestroyPipelineLayout(device, graphicsPipelineLayout, nullptr);

	if (materialCountPipeline) vkDestroyPipeline(device, materialCountPipeline, nullptr);
	if (colorCountPipeline) vkDestroyPipeline(device, colorCountPipeline, nullptr);
	if (transparentCountPipeline) vkDestroyPipeline(device, transparentCountPipeline, nullptr);
	for (auto m : countShaderModules) vkDestroyShaderModule(device, m, nullptr);
	countShaderModules.clear();

	if (materialTransparentPipeline) vkDestroyPipeline(device, materialTransparentPipeline, nullptr);
	if (colorTransparentPipeline) vkDestroyPipeline(device, colorTransparentPipeline, nullptr);
	if (lineTransparentPipeline) vkDestroyPipeline(device, lineTransparentPipeline, nullptr);
	if (transparentPipeline) vkDestroyPipeline(device, transparentPipeline, nullptr);
	for (auto m : transparentShaderModules) vkDestroyShaderModule(device, m, nullptr);
	transparentShaderModules.clear();

	if (blendPipeline) vkDestroyPipeline(device, blendPipeline, nullptr);
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
	graphicsRenderPass = VK_NULL_HANDLE;
	graphicsFramebuffer = VK_NULL_HANDLE;
	graphicsPipelineLayout = VK_NULL_HANDLE;
	materialCountPipeline = VK_NULL_HANDLE;
	colorCountPipeline = VK_NULL_HANDLE;
	materialTransparentPipeline = VK_NULL_HANDLE;
	colorTransparentPipeline = VK_NULL_HANDLE;
	transparentPipeline = VK_NULL_HANDLE;
	blendPipeline = VK_NULL_HANDLE;
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

	// Cleanup persistent VertexBuffer GPU buffers.
	// CRITICAL: must nullify BOTH buffer AND memory handles after freeing.
	// If memory handles are left dangling, uploadToPersistentBuffer() will
	// vkMapMemory() on already-freed memory on the next frame, silently
	// corrupting vertex/index data and breaking rendering permanently.
	if (materialVertexBuffer) { vkDestroyBuffer(device, materialVertexBuffer, nullptr); vkFreeMemory(device, materialVertexMemory, nullptr); }
	materialVertexBuffer = VK_NULL_HANDLE; materialVertexMemory = VK_NULL_HANDLE; materialVertexBufferSize = 0;
	if (materialIndexBuffer) { vkDestroyBuffer(device, materialIndexBuffer, nullptr); vkFreeMemory(device, materialIndexMemory, nullptr); }
	materialIndexBuffer = VK_NULL_HANDLE; materialIndexMemory = VK_NULL_HANDLE; materialIndexBufferSize = 0;
	if (colorVertexBuffer) { vkDestroyBuffer(device, colorVertexBuffer, nullptr); vkFreeMemory(device, colorVertexMemory, nullptr); }
	colorVertexBuffer = VK_NULL_HANDLE; colorVertexMemory = VK_NULL_HANDLE; colorVertexBufferSize = 0;
	if (colorIndexBuffer) { vkDestroyBuffer(device, colorIndexBuffer, nullptr); vkFreeMemory(device, colorIndexMemory, nullptr); }
	colorIndexBuffer = VK_NULL_HANDLE; colorIndexMemory = VK_NULL_HANDLE; colorIndexBufferSize = 0;
	if (transparentVertexBuffer) { vkDestroyBuffer(device, transparentVertexBuffer, nullptr); vkFreeMemory(device, transparentVertexMemory, nullptr); }
	transparentVertexBuffer = VK_NULL_HANDLE; transparentVertexMemory = VK_NULL_HANDLE; transparentVertexBufferSize = 0;
	if (transparentIndexBuffer) { vkDestroyBuffer(device, transparentIndexBuffer, nullptr); vkFreeMemory(device, transparentIndexMemory, nullptr); }
	transparentIndexBuffer = VK_NULL_HANDLE; transparentIndexMemory = VK_NULL_HANDLE; transparentIndexBufferSize = 0;
	if (lineVertexBuffer) { vkDestroyBuffer(device, lineVertexBuffer, nullptr); vkFreeMemory(device, lineVertexMemory, nullptr); }
	lineVertexBuffer = VK_NULL_HANDLE; lineVertexMemory = VK_NULL_HANDLE; lineVertexBufferSize = 0;
	if (lineIndexBuffer) { vkDestroyBuffer(device, lineIndexBuffer, nullptr); vkFreeMemory(device, lineIndexMemory, nullptr); }
	lineIndexBuffer = VK_NULL_HANDLE; lineIndexMemory = VK_NULL_HANDLE; lineIndexBufferSize = 0;
	// Triangle persistent buffers
	if (triangleVertexBuffer) { vkDestroyBuffer(device, triangleVertexBuffer, nullptr); vkFreeMemory(device, triangleVertexMemory, nullptr); }
	triangleVertexBuffer = VK_NULL_HANDLE; triangleVertexMemory = VK_NULL_HANDLE; triangleVertexBufferSize = 0;
	if (triangleIndexBuffer) { vkDestroyBuffer(device, triangleIndexBuffer, nullptr); vkFreeMemory(device, triangleIndexMemory, nullptr); }
	triangleIndexBuffer = VK_NULL_HANDLE; triangleIndexMemory = VK_NULL_HANDLE; triangleIndexBufferSize = 0;

	// Cleanup persistent staging buffers (prevents leaks on full resource recreation).
	if (materialVertexStagingBuffer) { vkDestroyBuffer(device, materialVertexStagingBuffer, nullptr); vkFreeMemory(device, materialVertexStagingMemory, nullptr); }
	materialVertexStagingBuffer = VK_NULL_HANDLE; materialVertexStagingMemory = VK_NULL_HANDLE; materialVertexStgSize = 0;
	if (materialIndexStagingBuffer) { vkDestroyBuffer(device, materialIndexStagingBuffer, nullptr); vkFreeMemory(device, materialIndexStagingMemory, nullptr); }
	materialIndexStagingBuffer = VK_NULL_HANDLE; materialIndexStagingMemory = VK_NULL_HANDLE; materialIndexStgSize = 0;
	if (colorVertexStagingBuffer) { vkDestroyBuffer(device, colorVertexStagingBuffer, nullptr); vkFreeMemory(device, colorVertexStagingMemory, nullptr); }
	colorVertexStagingBuffer = VK_NULL_HANDLE; colorVertexStagingMemory = VK_NULL_HANDLE; colorVertexStgSize = 0;
	if (colorIndexStagingBuffer) { vkDestroyBuffer(device, colorIndexStagingBuffer, nullptr); vkFreeMemory(device, colorIndexStagingMemory, nullptr); }
	colorIndexStagingBuffer = VK_NULL_HANDLE; colorIndexStagingMemory = VK_NULL_HANDLE; colorIndexStgSize = 0;
	if (transparentVertexStagingBuffer) { vkDestroyBuffer(device, transparentVertexStagingBuffer, nullptr); vkFreeMemory(device, transparentVertexStagingMemory, nullptr); }
	transparentVertexStagingBuffer = VK_NULL_HANDLE; transparentVertexStagingMemory = VK_NULL_HANDLE; transparentVertexStgSize = 0;
	if (transparentIndexStagingBuffer) { vkDestroyBuffer(device, transparentIndexStagingBuffer, nullptr); vkFreeMemory(device, transparentIndexStagingMemory, nullptr); }
	transparentIndexStagingBuffer = VK_NULL_HANDLE; transparentIndexStagingMemory = VK_NULL_HANDLE; transparentIndexStgSize = 0;
	if (lineVertexStagingBuffer) { vkDestroyBuffer(device, lineVertexStagingBuffer, nullptr); vkFreeMemory(device, lineVertexStagingMemory, nullptr); }
	lineVertexStagingBuffer = VK_NULL_HANDLE; lineVertexStagingMemory = VK_NULL_HANDLE; lineVertexStgSize = 0;
	if (lineIndexStagingBuffer) { vkDestroyBuffer(device, lineIndexStagingBuffer, nullptr); vkFreeMemory(device, lineIndexStagingMemory, nullptr); }
	lineIndexStagingBuffer = VK_NULL_HANDLE; lineIndexStagingMemory = VK_NULL_HANDLE; lineIndexStgSize = 0;
	if (triangleVertexStagingBuffer) { vkDestroyBuffer(device, triangleVertexStagingBuffer, nullptr); vkFreeMemory(device, triangleVertexStagingMemory, nullptr); }
	triangleVertexStagingBuffer = VK_NULL_HANDLE; triangleVertexStagingMemory = VK_NULL_HANDLE; triangleVertexStgSize = 0;
	if (triangleIndexStagingBuffer) { vkDestroyBuffer(device, triangleIndexStagingBuffer, nullptr); vkFreeMemory(device, triangleIndexStagingMemory, nullptr); }
	triangleIndexStagingBuffer = VK_NULL_HANDLE; triangleIndexStagingMemory = VK_NULL_HANDLE; triangleIndexStgSize = 0;

	// Cleanup IBL resources
	destroyIBLResources();
}

void HeadlessRenderer::createLinePipeline(int targetWidth, int targetHeight) {
	// Line pipeline: LINE_LIST + POLYGON_MODE_LINE (matches vkrender.cc linePipelines exactly)
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

	VkViewport viewport = { 0.0f, (float)targetHeight, (float)targetWidth, -(float)targetHeight, 0.0f, 1.0f };
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
		vks::initializers::pipelineCreateInfo(graphicsPipelineLayout, opaqueRenderPass);
	pipelineCreateInfo.subpass = 0;
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();

	// Line pipeline uses MaterialVertex (same as main material pipeline)
	std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(MaterialVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0), 				// Position
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float)*3),	// Normal
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32_SINT, sizeof(float)*6)			// Material Index
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

	// Same shader options as the main material pipeline (no COLOR)
	std::vector<std::string> options{ "NORMAL", "OPAQUE", "MATERIAL" };
	if (ibl) {
		options.push_back("USE_IBL");
	}
	if (interlock) {
		options.push_back("HAVE_INTERLOCK");
	}
	if (m_Orthographic) {
		options.push_back("ORTHOGRAPHIC");
	}

	shaderStages[0].module = createShaderModule(EShLangVertex, shaderPath + "vertex.glsl", options);
	shaderStages[1].module = createShaderModule(EShLangFragment, shaderPath + "fragment.glsl", options);

	lineShaderModules = { shaderStages[0].module, shaderStages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &linePipeline));
}


// Matches vkrender.cc createGraphicsPipelines(): destroy and recreate ALL pipeline
// objects only. Attachments, descriptor sets, buffers, render passes, and the
// graphicsPipelineLayout survive.  Only VkPipeline + VkShaderModule objects change.
void HeadlessRenderer::recreateGraphicsPipelines(DrawMode drawMode, int targetWidth, int targetHeight) {
	// Update currentDrawMode BEFORE creating pipelines so all create*() functions
	// that read currentDrawMode (e.g., transparent pipelines) see the correct mode.
	// Matches vkrender.cc: mode is updated in cycleMode() before createGraphicsPipelines().
	currentDrawMode = drawMode;

	// Matches vkrender.cc: device->waitIdle() waits for ALL queues before
	// destroying pipelines, not just vkQueueWaitIdle(queue).
	VkResult res = vkDeviceWaitIdle(device);
	if (res != VK_SUCCESS) {
		std::cerr << "[v3d-error] vkDeviceWaitIdle failed: " << res << std::endl;
	}

	// Destroy all pipeline objects + shader modules
	vkDestroyPipeline(device, materialPipeline, nullptr);
	for (auto& m : materialShaderModules) vkDestroyShaderModule(device, m, nullptr);
	materialShaderModules.clear();
	vkDestroyPipeline(device, colorPipeline, nullptr);
	for (auto& m : colorShaderModules) vkDestroyShaderModule(device, m, nullptr);
	colorShaderModules.clear();
	if (linePipeline) vkDestroyPipeline(device, linePipeline, nullptr);
	for (auto m : lineShaderModules) vkDestroyShaderModule(device, m, nullptr);
	lineShaderModules.clear();

	if (materialCountPipeline) vkDestroyPipeline(device, materialCountPipeline, nullptr);
	if (colorCountPipeline) vkDestroyPipeline(device, colorCountPipeline, nullptr);
	if (triangleCountPipeline) vkDestroyPipeline(device, triangleCountPipeline, nullptr);
	if (transparentCountPipeline) vkDestroyPipeline(device, transparentCountPipeline, nullptr);
	for (auto m : countShaderModules) vkDestroyShaderModule(device, m, nullptr);
	countShaderModules.clear();

	if (materialTransparentPipeline) vkDestroyPipeline(device, materialTransparentPipeline, nullptr);
	if (colorTransparentPipeline) vkDestroyPipeline(device, colorTransparentPipeline, nullptr);
	if (triangleTransparentPipeline) vkDestroyPipeline(device, triangleTransparentPipeline, nullptr);
	if (lineTransparentPipeline) vkDestroyPipeline(device, lineTransparentPipeline, nullptr);
	if (transparentPipeline) vkDestroyPipeline(device, transparentPipeline, nullptr);
	for (auto m : transparentShaderModules) vkDestroyShaderModule(device, m, nullptr);
	transparentShaderModules.clear();

	if (blendPipeline) vkDestroyPipeline(device, blendPipeline, nullptr);
	for (auto m : blendShaderModules) vkDestroyShaderModule(device, m, nullptr);
	blendShaderModules.clear();

	// Recreate all pipelines with correct polygon mode using the shared layout.
	// Matches vkrender.cc: createGraphicsPipelines() creates ALL pipeline objects
	// using the existing graphicsPipelineLayout; no new layouts or caches created.
	createMaterialPipeline(drawMode, targetWidth, targetHeight);
	createColorPipeline(drawMode, targetWidth, targetHeight);
	createLinePipeline(targetWidth, targetHeight);
	createMaterialCountPipeline(targetWidth, targetHeight);
	createColorCountPipeline(targetWidth, targetHeight);
	createTriangleCountPipeline(targetWidth, targetHeight);
	createTransparentCountPipeline(targetWidth, targetHeight);
	createMaterialTransparentPipeline(targetWidth, targetHeight);
	createColorTransparentPipeline(targetWidth, targetHeight);
	createTriangleTransparentPipeline(targetWidth, targetHeight);
	createLineTransparentPipeline(targetWidth, targetHeight);
	createTransparentPipeline(targetWidth, targetHeight);
	createBlendPipeline(targetWidth, targetHeight);
}

// Upload CPU data to a persistent GPU buffer (grows as needed), recording the copy
// into the already-begun commandBuffer.  Follows vkrender.cc uploadPersistentBuffer:
// allocate device-local dst once, reuse; staging buffer created per-upload.
void HeadlessRenderer::uploadToPersistentBuffer(
    VkCommandBuffer cmd,
    VkBuffer& dstBuf, VkDeviceMemory& dstMem, VkDeviceSize& dstSize,
    VkBuffer& stgBuf, VkDeviceMemory& stgMem, VkDeviceSize& stgSize,
    const void* data, VkDeviceSize dataSize, bool isVertex)
{
	// Vulkan doesn't allow zero-size buffers
	dataSize = std::max<VkDeviceSize>(16, dataSize);

	// Grow persistent device buffer if needed
	if (dstBuf == VK_NULL_HANDLE || dstSize < dataSize) {
		if (dstBuf != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, dstBuf, nullptr);
			vkFreeMemory(device, dstMem, nullptr);
		}
		VkBufferUsageFlags dstUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		                            (isVertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		createBuffer(dstUsage,
		             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &dstBuf, &dstMem, dataSize);
		dstSize = dataSize;
	}

	// Grow persistent staging buffer if needed (double each time, like vkrender.cc)
	if (data != nullptr) {
		if (stgBuf == VK_NULL_HANDLE || stgSize < dataSize) {
			if (stgBuf != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, stgBuf, nullptr);
				vkFreeMemory(device, stgMem, nullptr);
			}
			VkDeviceSize newSize = 16;
			while (newSize < dataSize) newSize *= 2;
			createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			             &stgBuf, &stgMem, newSize, nullptr);
			stgSize = newSize;
		}

		// Copy data into the persistent staging buffer on the CPU side
		void* stgPtr;
		vkMapMemory(device, stgMem, 0, dataSize, 0, &stgPtr);
		memcpy(stgPtr, data, dataSize);
		vkUnmapMemory(device, stgMem);

		// Record the GPU copy command
		VkBufferCopy region{ 0, 0, dataSize };
		vkCmdCopyBuffer(cmd, stgBuf, dstBuf, 1, &region);
	}
}

	unsigned char* HeadlessRenderer::render(
	glm::ivec2 targetSize, 
	VkSubresourceLayout* imageSubresourceLayout, 
	const glm::dmat4& view, 
	const glm::dmat4& proj, 
	const glm::dmat3& normMat,
	const std::vector<V3dMaterial>& materials, 
	const std::vector<V3dHeaderInfo::Light>& lights,
	MeshPipelineMode /*pipelineMode*/,  // Unused: unified path uses hasTransparency instead of pipelineMode
	const glm::vec4& bgColor,
	bool orthographic,
	bool useIBL,
	const std::string& iblPath,
	DrawMode drawMode
) {
	m_BackgroundColor = bgColor;
	m_Orthographic = orthographic;

	// Track IBL state changes - recreate pipelines if IBL toggles
	bool iblChanged = (useIBL != ibl);
	if (iblChanged) {
		destroyIBLResources();
		ibl = useIBL;
	}

	groupSize = localSize * blockSize;

	targetSize.x = std::min(targetSize.x, (int)maxFramebufferWidth);
	targetSize.y = std::min(targetSize.y, (int)maxFramebufferHeight);

	// Unify with vkrender.cc: use a single Opaque boolean instead of pipelineMode.
	// pipelineMode oscillates between OUTLINE(MaterialOnly) and NORMAL(Mixed), causing
	// full resource recreation on every draw mode cycle. vkrender.cc avoids this by
	// using only the Opaque flag to choose pipelines at draw time.
	bool drawModeChanged = (drawMode != currentDrawMode);

	// Full recreation needed: size changed, IBL toggled, or first initialization.
	// Matches vkrender.cc: full recreate only on resize/IBL change, not draw mode.
	bool needsFullRecreate = (currentTargetSize != targetSize) || iblChanged;
	if (!initialized) {
		needsFullRecreate = true;
	}

	// If ONLY draw mode changed, do lightweight pipeline recreation.
	// Matches vkrender.cc: cycleMode → recreatePipeline=true → createGraphicsPipelines()
	// which destroys/recreates ALL VkPipeline objects only; attachments, descriptor
	// sets, buffers, render passes all survive.
	if (drawModeChanged && currentTargetSize == targetSize && !iblChanged) {
		recreateGraphicsPipelines(drawMode, targetSize.x, targetSize.y);
		currentDrawMode = drawMode;
	} else if (needsFullRecreate) {
		if (initialized) {
			cleanup();

			vkDestroyBuffer(device, uniformBuffer, nullptr);
			vkFreeMemory(device, uniformBufferMemory, nullptr);

			if (materialBufferMapped) vkUnmapMemory(device, materialBufferMemory);
			materialBufferMapped = nullptr;
			vkDestroyBuffer(device, materialBuffer, nullptr);
			vkFreeMemory(device, materialBufferMemory, nullptr);

			if (lightBufferMapped) vkUnmapMemory(device, lightBufferMemory);
			lightBufferMapped = nullptr;
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

		createDescriptorSetLayout();

		// Initialize IBL resources before creating pipelines (shaders need USE_IBL define)
		if (ibl && !iblPath.empty()) {
			initIBL(iblPath);
		}

		// Create render passes BEFORE any pipeline that references them.
		// Matches vkrender.cc: createCountRenderPass -> createGraphicsRenderPass -> createGraphicsPipelines
		createCountRenderPass(targetSize.x, targetSize.y);
		createGraphicsRenderPass(targetSize.x, targetSize.y);

		createGraphicsPipelineLayout();
		createMaterialPipeline(drawMode, targetSize.x, targetSize.y);
		createColorPipeline(drawMode, targetSize.x, targetSize.y);
		createLinePipeline(targetSize.x, targetSize.y);

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
				mat.lightOn   // lightOn flag (was incorrectly derived from pipelineMode)
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
		createComputeDescriptorSetLayout();
		createComputeDescriptorPool();
		createComputeDescriptorSet();
		createComputePipelineLayout();
		createComputePipelines();

		createMaterialCountPipeline(targetSize.x, targetSize.y);
		createColorCountPipeline(targetSize.x, targetSize.y);
		createTriangleCountPipeline(targetSize.x, targetSize.y);
		createTransparentCountPipeline(targetSize.x, targetSize.y);

		// Set currentDrawMode BEFORE creating draw-mode-dependent pipelines.
		// Transparent pipelines read currentDrawMode for polygon mode selection.
		currentDrawMode = drawMode;

		createMaterialTransparentPipeline(targetSize.x, targetSize.y);
		createColorTransparentPipeline(targetSize.x, targetSize.y);
		createTriangleTransparentPipeline(targetSize.x, targetSize.y);
		createLineTransparentPipeline(targetSize.x, targetSize.y);
		createTransparentPipeline(targetSize.x, targetSize.y);
		createBlendPipeline(targetSize.x, targetSize.y);

		initialized = true;
		currentTargetSize = targetSize;
	} else {
		// No recreation needed.
	}

	UniformBufferObject ubo{ };
	// Match vkrender.cc byte-for-byte: use pre-computed dmat3 normMat member.
	glm::dmat4 projView = proj * view;
	ubo.projViewMat = glm::mat4(projView);
	ubo.viewMat = glm::mat4(view);
	// Fill normMat as 3 vec4 columns for std140 mat3 layout (48 bytes)
	ubo.normMat[0] = glm::vec4(normMat[0], 0.0);
	ubo.normMat[1] = glm::vec4(normMat[1], 0.0);
	ubo.normMat[2] = glm::vec4(normMat[2], 0.0);

	// Update UBO whenever ANY matrix changes (|| not &&).
	// Multiple scenes share the same renderer; each has its own view/projection.
	bool uboChanged = (cachedUbo.projViewMat != ubo.projViewMat) ||
	                  (cachedUbo.viewMat != ubo.viewMat);
	if (!uboChanged) {
		for (int i = 0; i < 3; ++i) {
			if (cachedUbo.normMat[i] != ubo.normMat[i]) { uboChanged = true; break; }
		}
	}
	if (uboChanged) {
		std::memcpy(uniformBufferMapped, &ubo, sizeof(UniformBufferObject));
		cachedUbo = ubo;
	}

	// Update material and light buffers via mapped memory instead of
	// destroying + recreating.  Multiple models share a single HeadlessRenderer
	// instance; if the target size hasn't changed the recreation block above is
	// skipped, but each model can have its own materials and lighting.  Using
	// Update material and light buffers via mapped memory every frame.
	// Multiple models share a single HeadlessRenderer; each can have its own
	// materials and lighting.  The GPU buffer must be refreshed before drawing,
	// even after pipeline recreation (recreatedResources=true), otherwise the
	// first frame renders with stale/zero material data.
	if (initialized) {
		std::vector<GPUMaterial> mats(materials.size());
		for (size_t i = 0; i < materials.size(); ++i) {
			mats[i].diffuse    = glm::vec4{ materials[i].diffuse.r, materials[i].diffuse.g, materials[i].diffuse.b, materials[i].diffuse.a };
			mats[i].emissive   = materials[i].emissive;
			mats[i].specular   = materials[i].specular;
			mats[i].parameters = glm::vec4(
				materials[i].shininess,
				materials[i].metallic,
				materials[i].fresnel0,
				materials[i].lightOn
			);
		}
		VkDeviceSize matSize = mats.empty() ? sizeof(GPUMaterial) : sizeof(GPUMaterial) * mats.size();
		if (materialBufferMapped) {
			if (mats.empty()) {
				std::memset(materialBufferMapped, 0, matSize);
			} else {
				std::memcpy(materialBufferMapped, mats.data(), matSize);
			}
		}

		GPULight gpuLight{};
		gpuLight.direction = glm::vec4{ lights[0].direction.x, lights[0].direction.y, lights[0].direction.z, 0.0f };
		gpuLight.color     = glm::vec4{ lights[0].color.r, lights[0].color.g, lights[0].color.b, 1.0f };
		if (lightBufferMapped) {
			std::memcpy(lightBufferMapped, &gpuLight, sizeof(GPULight));
		}
	}


	// Detect if the scene needs the A-buffer compositing path.
	// Matches vkrender.cc: Opaque = transparentData.indices.empty()
	bool hasTransparency = !transparentData.indices.empty();

	// === UNIFIED RENDER PATH ===
	// Matches vkrender.cc drawBuffers() exactly:
	//   - beginGraphicsFrameRender picks opaque or transparent render pass based on Opaque flag
	//   - ALL geometry drawn in the same render pass subpass 0
	//   - Each draw call uses the appropriate pipeline (opaque or transparent)
	//   - If not Opaque: nextSubpass → drawTransparent → nextSubpass → blendFrame

	bool isOpaque = !hasTransparency;

	// For transparent scenes: zero buffers and run count+compute passes first.
	if (!isOpaque) {
		zeroTransparencyBuffers();
		elements = pixels;
		refreshBuffers(materialData.indices.size(), lights.size());
	}

	// Allocate a fresh command buffer for this frame (vkrender.cc pattern).
	VkCommandBufferAllocateInfo cmdAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VkCommandBuffer gfxCmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdAllocInfo, &gfxCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(gfxCmd, &cmdBufInfo));

	// --- Upload ALL vertex data before the render pass ---
	VkBufferMemoryBarrier bufBarrier = {};
	bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	bufBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	bufBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufBarrier.offset = 0;
	bufBarrier.size = VK_WHOLE_SIZE;

	if (!materialData.indices.empty() && !materialData.materialVertices.empty()) {
		VkDeviceSize vsize = materialData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = materialData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(gfxCmd, materialVertexBuffer, materialVertexMemory, materialVertexBufferSize,
		                         materialVertexStagingBuffer, materialVertexStagingMemory, materialVertexStgSize,
		                         materialData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(gfxCmd, materialIndexBuffer, materialIndexMemory, materialIndexBufferSize,
		                         materialIndexStagingBuffer, materialIndexStagingMemory, materialIndexStgSize,
		                         materialData.indices.data(), isize, false);
	}

	if (!lineData.indices.empty() && !lineData.materialVertices.empty()) {
		VkDeviceSize vsize = lineData.materialVertices.size() * sizeof(MaterialVertex);
		VkDeviceSize isize = lineData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(gfxCmd, lineVertexBuffer, lineVertexMemory, lineVertexBufferSize,
		                         lineVertexStagingBuffer, lineVertexStagingMemory, lineVertexStgSize,
		                         lineData.materialVertices.data(), vsize, true);
		uploadToPersistentBuffer(gfxCmd, lineIndexBuffer, lineIndexMemory, lineIndexBufferSize,
		                         lineIndexStagingBuffer, lineIndexStagingMemory, lineIndexStgSize,
		                         lineData.indices.data(), isize, false);
	}

	if (!colorData.indices.empty() && !colorData.colorVertices.empty()) {
		VkDeviceSize vsize = colorData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = colorData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(gfxCmd, colorVertexBuffer, colorVertexMemory, colorVertexBufferSize,
		                         colorVertexStagingBuffer, colorVertexStagingMemory, colorVertexStgSize,
		                         colorData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(gfxCmd, colorIndexBuffer, colorIndexMemory, colorIndexBufferSize,
		                         colorIndexStagingBuffer, colorIndexStagingMemory, colorIndexStgSize,
		                         colorData.indices.data(), isize, false);
	}

	if (!transparentData.indices.empty() && !transparentData.colorVertices.empty()) {
		VkDeviceSize vsize = transparentData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = transparentData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(gfxCmd, transparentVertexBuffer, transparentVertexMemory, transparentVertexBufferSize,
		                         transparentVertexStagingBuffer, transparentVertexStagingMemory, transparentVertexStgSize,
		                         transparentData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(gfxCmd, transparentIndexBuffer, transparentIndexMemory, transparentIndexBufferSize,
		                         transparentIndexStagingBuffer, transparentIndexStagingMemory, transparentIndexStgSize,
		                         transparentData.indices.data(), isize, false);
	}

	if (!triangleData.indices.empty() && !triangleData.colorVertices.empty()) {
		VkDeviceSize vsize = triangleData.colorVertices.size() * sizeof(ColorVertex);
		VkDeviceSize isize = triangleData.indices.size() * sizeof(uint32_t);
		uploadToPersistentBuffer(gfxCmd, triangleVertexBuffer, triangleVertexMemory, triangleVertexBufferSize,
		                         triangleVertexStagingBuffer, triangleVertexStagingMemory, triangleVertexStgSize,
		                         triangleData.colorVertices.data(), vsize, true);
		uploadToPersistentBuffer(gfxCmd, triangleIndexBuffer, triangleIndexMemory, triangleIndexBufferSize,
		                         triangleIndexStagingBuffer, triangleIndexStagingMemory, triangleIndexStgSize,
		                         triangleData.indices.data(), isize, false);
	}

	// Pipeline barriers: ensure all uploads complete before vertex fetch.
	VkBuffer* barrierBuffers[] = { &materialVertexBuffer, &materialIndexBuffer,
	                               &lineVertexBuffer, &lineIndexBuffer,
	                               &colorVertexBuffer, &colorIndexBuffer,
	                               &transparentVertexBuffer, &transparentIndexBuffer,
	                               &triangleVertexBuffer, &triangleIndexBuffer };
	for (auto* bufPtr : barrierBuffers) {
		if (*bufPtr != VK_NULL_HANDLE) {
			bufBarrier.buffer = *bufPtr;
			vkCmdPipelineBarrier(gfxCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			                     0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
		}
	}

	// --- Begin render pass (matches vkrender.cc beginGraphicsFrameRender) ---
	VkClearValue clearValues[3];
	clearValues[0].color.float32[0] = m_BackgroundColor.r;
	clearValues[0].color.float32[1] = m_BackgroundColor.g;
	clearValues[0].color.float32[2] = m_BackgroundColor.b;
	clearValues[0].color.float32[3] = m_BackgroundColor.a;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;
	clearValues[2].color.float32[0] = m_BackgroundColor.r;
	clearValues[2].color.float32[1] = m_BackgroundColor.g;
	clearValues[2].color.float32[2] = m_BackgroundColor.b;
	clearValues[2].color.float32[3] = m_BackgroundColor.a;

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderArea.extent.width = targetSize.x;
	renderPassBeginInfo.renderArea.extent.height = targetSize.y;
	// Opaque: 2 clear values (color + depth). Transparent: 3 (color + depth + resolve).
	renderPassBeginInfo.clearValueCount = isOpaque ? 2 : 3;
	renderPassBeginInfo.pClearValues = clearValues;
	// Matches vkrender.cc: Opaque ? opaqueGraphicsRenderPass : graphicsRenderPass
	renderPassBeginInfo.renderPass = isOpaque ? opaqueRenderPass : graphicsRenderPass;
	renderPassBeginInfo.framebuffer = isOpaque ? opaqueFramebuffer : graphicsFramebuffer;

	vkCmdBeginRenderPass(gfxCmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Push constants for all draw calls: uvec4 constants + vec4 background (32 bytes).
	VkDeviceSize pushSize = sizeof(glm::uvec4) + sizeof(glm::vec4);
	uint8_t* pushData = new uint8_t[pushSize];
	glm::uvec4 transConstants{ 0 };
	transConstants.x = (currentDrawMode != DRAWMODE_NORMAL) ? 0u : static_cast<uint32_t>(lights.size());
	transConstants.y = targetSize.x;
	memcpy(pushData, &transConstants, sizeof(glm::uvec4));
	glm::vec4 background = m_BackgroundColor;
	memcpy(pushData + sizeof(glm::uvec4), &background, sizeof(glm::vec4));

	vkCmdBindDescriptorSets(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

	VkDeviceSize offsets[1] = { 0 };

	// === SUBPASS 0: ALL geometry ===
	// Matches vkrender.cc drawBuffers(): getPipelineType() selects opaque or transparent pipeline.
	// materialData → (isOpaque ? materialPipeline : materialTransparentPipeline)
	VkPipeline matPipeline = isOpaque ? materialPipeline : materialTransparentPipeline;
	VkPipeline colPipeline = isOpaque ? colorPipeline : colorTransparentPipeline;
	VkPipeline lnPipeline  = isOpaque ? linePipeline : lineTransparentPipeline;

	// materialData (MaterialVertex format)
	if (!materialData.indices.empty() && materialVertexBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, matPipeline);
		vkCmdBindVertexBuffers(gfxCmd, 0, 1, &materialVertexBuffer, offsets);
		vkCmdBindIndexBuffer(gfxCmd, materialIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
		vkCmdDrawIndexed(gfxCmd, static_cast<uint32_t>(materialData.indices.size()), 1, 0, 0, 0);
	}

	// colorData (ColorVertex format) — matches vkrender.cc drawColors()
	if (!colorData.indices.empty() && colorVertexBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, colPipeline);
		vkCmdBindVertexBuffers(gfxCmd, 0, 1, &colorVertexBuffer, offsets);
		vkCmdBindIndexBuffer(gfxCmd, colorIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
		vkCmdDrawIndexed(gfxCmd, static_cast<uint32_t>(colorData.indices.size()), 1, 0, 0, 0);
	}

	// lineData (LINE_LIST topology, MaterialVertex format) — matches vkrender.cc drawLines()
	if (!lineData.indices.empty() && lineVertexBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lnPipeline);
		vkCmdBindVertexBuffers(gfxCmd, 0, 1, &lineVertexBuffer, offsets);
		vkCmdBindIndexBuffer(gfxCmd, lineIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
		vkCmdDrawIndexed(gfxCmd, static_cast<uint32_t>(lineData.indices.size()), 1, 0, 0, 0);
	}

	// triangleData (ColorVertex+GENERAL format) — always uses transparent pipeline since it needs GENERAL path
	// Matches vkrender.cc: drawTriangles() uses getPipelineType(trianglePipelines)
	if (!triangleData.indices.empty() && triangleVertexBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triangleTransparentPipeline);
		vkCmdBindVertexBuffers(gfxCmd, 0, 1, &triangleVertexBuffer, offsets);
		vkCmdBindIndexBuffer(gfxCmd, triangleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
		vkCmdDrawIndexed(gfxCmd, static_cast<uint32_t>(triangleData.indices.size()), 1, 0, 0, 0);
	}

	// === If transparent: subpass 1 (transparentData) + subpass 2 (blend quad) ===
	if (!isOpaque) {
		vkCmdNextSubpass(gfxCmd, VK_SUBPASS_CONTENTS_INLINE);

		if (!transparentData.indices.empty() && transparentVertexBuffer != VK_NULL_HANDLE) {
			vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);
			vkCmdBindVertexBuffers(gfxCmd, 0, 1, &transparentVertexBuffer, offsets);
			vkCmdBindIndexBuffer(gfxCmd, transparentIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
			vkCmdDrawIndexed(gfxCmd, static_cast<uint32_t>(transparentData.indices.size()), 1, 0, 0, 0);
		}

		vkCmdNextSubpass(gfxCmd, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(gfxCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
		vkCmdPushConstants(gfxCmd, graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
		delete[] pushData;

		vkCmdDraw(gfxCmd, 3, 1, 0, 0);
	} else {
		delete[] pushData;
	}

	vkCmdEndRenderPass(gfxCmd);
	VK_CHECK_RESULT(vkEndCommandBuffer(gfxCmd));

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &gfxCmd;
	VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);
	vkFreeCommandBuffers(device, commandPool, 1, &gfxCmd);

	unsigned char* returnData = copyToHost(targetSize, imageSubresourceLayout, hasTransparency);

	// Matches vkrender.cc: device->waitIdle() ensures ALL GPU work is complete
	// before returning, not just one queue.
	VkResult res = vkDeviceWaitIdle(device);
	if (res != VK_SUCCESS) {
		std::cerr << "[v3d-error] vkDeviceWaitIdle failed: " << res << std::endl;
	}

	return returnData;
}

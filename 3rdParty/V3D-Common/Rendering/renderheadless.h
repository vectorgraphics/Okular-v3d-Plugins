#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <array>
#include <iostream>
#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "../3rdParty/VulkanTools/VulkanTools.h"

#define DEBUG (!NDEBUG)

#define BUFFER_ELEMENTS 32

#define LOG(...) printf(__VA_ARGS__)

#define VULKAN_DEBUG 1

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData
);

class HeadlessRenderer
{
public:
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	uint32_t queueFamilyIndex;
	VkPipelineCache pipelineCache;
	VkQueue queue;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	std::vector<VkShaderModule> shaderModules;

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;

	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;

	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
	};

	VkFramebuffer framebuffer;
	FrameBufferAttachment colorAttachment, depthAttachment;
	VkRenderPass renderPass;

	std::string shaderPath;

	VkDebugReportCallbackEXT debugReportCallback{};

	HeadlessRenderer(std::string shaderPath);
	~HeadlessRenderer();

private:
	void createInstance();
	void createPhysicalDevice();
	VkDeviceQueueCreateInfo requestGraphicsQueue();
	void createLogicalDevice(VkDeviceQueueCreateInfo* queueCreateInfo);
	void copyVertexDataToGPU(const std::vector<float>& vertices);
	void copyIndexDataToGPU(const std::vector<unsigned int>& indices);
	void createAttachments(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight);
	void createRenderPipeline(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight);
	void createGraphicsPipeline();
	void recordCommandBuffer(int targetWidth, int targetHeight, size_t indexCount, const glm::mat4& mvp);
	unsigned char* copyToHost(int targetWidth, int targetHeight, VkSubresourceLayout* imageSubresourceLayout);

	void cleanup();

public:
	unsigned char* render(int targetWidth, int targetHeight, VkSubresourceLayout* imageSubresourceLayout, const std::vector<float>& vertices, const std::vector<unsigned int>& indices, const glm::mat4& mvp);

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

	VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);
    
    // Submit command buffer to a queue and wait for fence until queue operations have been finished
	void submitWork(VkCommandBuffer cmdBuffer, VkQueue queue);
};
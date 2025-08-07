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

#include "../V3dFile/Mesh.h"

#define DEBUG (!NDEBUG)

#define BUFFER_ELEMENTS 32

#define LOG(...) printf(__VA_ARGS__)

#define VULKAN_DEBUG 1

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

	VkImage hostReadableDestinationImage;
	VkDeviceMemory hostReadableDestinationImageMemory;
	glm::ivec2 hostReadableDestinationImageSize{ 0, 0 };

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
	unsigned char* copyToHost(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout);

	void createHostReadableDestinationImage(glm::ivec2 size);
	void destroyHostReadableDestinationImage();

	void cleanup();

public:
	void copyMeshToGPU(const Mesh& mesh);

	unsigned char* render(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout, const glm::mat4& mvp);

	void cleanupMeshData();

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

	VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);
    
    // Submit command buffer to a queue and wait for fence until queue operations have been finished
	void submitWork(VkCommandBuffer cmdBuffer, VkQueue queue);

	unsigned int m_IndexCount{ 0 };
};

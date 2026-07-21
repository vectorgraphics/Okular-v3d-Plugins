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
#include "../V3dFile/V3dObjects.h"
#include "../V3dFile/V3dHeaderInfo.h"
#include "Public/ShaderLang.h"

#define BUFFER_ELEMENTS 32

static inline bool v3dDebugEnabled() {
#ifdef DEBUG
    return true;  // debug builds always enable Vulkan validation + debug output
#else
    return std::getenv("OKULAR_V3D_DEBUG") != nullptr;
#endif
}

#define LOG(...) do { if (v3dDebugEnabled()) printf(__VA_ARGS__); } while(0)

struct UniformBufferObject {
	glm::mat4 projViewMat { };
	glm::mat4 viewMat { };
	glm::mat4 normMat { };
};

struct GPUMaterial
{
    glm::vec4 diffuse;
    glm::vec4 emissive;
    glm::vec4 specular;
    glm::vec4 parameters;
};

struct GPULight
{
  	glm::vec4 direction;
  	glm::vec4 color;
};

class HeadlessRenderer
{
public:
	static constexpr uint32_t maxFramesInFlight = 1; // TODO potentially have multiple frames in flight

	VkInstance instance{ VK_NULL_HANDLE };
	VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
	VkDevice device{ VK_NULL_HANDLE };
	uint32_t maxComputeWorkGroupCountX{ 65535 };
	uint32_t maxFramebufferWidth{ 16384 };
	uint32_t maxFramebufferHeight{ 16384 };
	uint32_t queueFamilyIndex;
	VkPipelineCache pipelineCache;
	VkQueue queue;
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	std::vector<VkShaderModule> shaderModules;

	bool meshInitialized{ false };
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;

	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;

	UniformBufferObject cachedUbo{ };
	VkBuffer uniformBuffer;
	VkDeviceMemory uniformBufferMemory;
	void* uniformBufferMapped{ nullptr };

	VkBuffer materialBuffer;
	VkDeviceMemory materialBufferMemory;
	void* materialBufferMapped{ nullptr };

	VkBuffer lightBuffer;
	VkDeviceMemory lightBufferMemory;
	void* lightBufferMapped{ nullptr };

	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;

	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
	};

	VkImage hostReadableDestinationImage;
	VkDeviceMemory hostReadableDestinationImageMemory;
	glm::ivec2 hostReadableDestinationImageSize{ 0, 0 };
	bool hostReadableDestinationImageInitalized{ false };
	void* hostReadableDestinationImageMapped;

	glm::ivec2 currentTargetSize{ 0, 0 };
	bool initialized{ false };
	bool interlock{ false };
	bool srgb{ false }; // TODO: control via env var
	MeshPipelineMode currentPipelineMode{ MeshPipelineMode::MaterialOnly };

	VkFramebuffer framebuffer;
	FrameBufferAttachment colorAttachment, depthAttachment;
	VkRenderPass renderPass;

	// Transparency buffers
	VkBuffer countBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory countBufferMemory{ VK_NULL_HANDLE };

	VkBuffer globalSumBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory globalSumBufferMemory{ VK_NULL_HANDLE };

	VkBuffer offsetBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory offsetBufferMemory{ VK_NULL_HANDLE };

	VkBuffer opaqueColorBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory opaqueColorBufferMemory{ VK_NULL_HANDLE };

	VkBuffer opaqueDepthBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory opaqueDepthBufferMemory{ VK_NULL_HANDLE };

	VkBuffer fragmentBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory fragmentBufferMemory{ VK_NULL_HANDLE };

	VkBuffer depthFragBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory depthFragBufferMemory{ VK_NULL_HANDLE };

	VkBuffer feedbackBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory feedbackBufferMemory{ VK_NULL_HANDLE };
	uint32_t* feedbackMappedPtr{ nullptr };

	// Compute resources
	VkDescriptorSetLayout computeDescriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorPool computeDescriptorPool{ VK_NULL_HANDLE };
	VkDescriptorSet computeDescriptorSet{ VK_NULL_HANDLE };
	VkPipelineLayout computePipelineLayout{ VK_NULL_HANDLE };
	VkPipeline computeSum1Pipeline{ VK_NULL_HANDLE };
	VkPipeline computeSum2Pipeline{ VK_NULL_HANDLE };
	VkPipeline computeSum3Pipeline{ VK_NULL_HANDLE };

	// Transparency graphics resources
	VkRenderPass countRenderPass{ VK_NULL_HANDLE };
	VkFramebuffer countFramebuffer{ VK_NULL_HANDLE };
	VkRenderPass transparentRenderPass{ VK_NULL_HANDLE };
	VkFramebuffer transparentFramebuffer{ VK_NULL_HANDLE };

	VkPipelineLayout transparencyPipelineLayout{ VK_NULL_HANDLE };

	VkPipeline countPipeline{ VK_NULL_HANDLE };
	VkPipelineCache countPipelineCache{ VK_NULL_HANDLE };
	std::vector<VkShaderModule> countShaderModules;

	VkPipeline transparentPipeline{ VK_NULL_HANDLE };
	VkPipelineCache transparentPipelineCache{ VK_NULL_HANDLE };
	std::vector<VkShaderModule> transparentShaderModules;

	VkPipeline blendPipeline{ VK_NULL_HANDLE };
	VkPipelineCache blendPipelineCache{ VK_NULL_HANDLE };
	std::vector<VkShaderModule> blendShaderModules;

	// IBL (Image-Based Lighting) resources
	bool ibl{ false };
	VkImage irradianceImg{ VK_NULL_HANDLE };
	VkDeviceMemory irradianceImgMemory{ VK_NULL_HANDLE };
	VkImageView irradianceView{ VK_NULL_HANDLE };
	VkSampler irradianceSampler{ VK_NULL_HANDLE };

	VkImage brdfImg{ VK_NULL_HANDLE };
	VkDeviceMemory brdfImgMemory{ VK_NULL_HANDLE };
	VkImageView brdfView{ VK_NULL_HANDLE };
	VkSampler brdfSampler{ VK_NULL_HANDLE };

	VkImage reflectionImg{ VK_NULL_HANDLE };
	VkDeviceMemory reflectionImgMemory{ VK_NULL_HANDLE };
	VkImageView reflectionView{ VK_NULL_HANDLE };
	VkSampler reflectionSampler{ VK_NULL_HANDLE };

	// Transparency state
	uint32_t pixels{ 0 };
	uint32_t groupSize;
	uint32_t localSize{ 256 };
	uint32_t blockSize{ 8 };
	uint32_t elements{ 0 };
	uint32_t fragments{ 0 };
	uint32_t maxFragments{ 0 };
	uint32_t maxSize{ 1 };

	std::string shaderPath;
	float queuePriority{ 0.5f };

	VkDebugReportCallbackEXT debugReportCallback{};

	HeadlessRenderer(std::string shaderPath);
	~HeadlessRenderer();

private:
	void createInstance();
	void createPhysicalDevice();
	VkDeviceQueueCreateInfo requestGraphicsQueue();
	void createLogicalDevice(VkDeviceQueueCreateInfo* queueCreateInfo);
	void copyDataToGPU(const std::vector<unsigned char>& data, VkBuffer& buffer, VkDeviceMemory& deviceMemory);
	void copyIndexDataToGPU(const std::vector<unsigned int>& indices);
	void createUniformBuffer();
	void createMaterialBuffer(const std::vector<GPUMaterial>& materials);
	void createLightBuffer(const std::vector<GPULight>& lights);
	void createDescriptorPool();
	void createDescirptorSets();
	void createAttachments(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight);
	VkShaderModule createShaderModule(EShLanguage lang, std::string const & filePath, std::vector<std::string> const & options);
	void createRenderPipeline(VkFormat colorFormat, VkFormat depthFormat, int targetWidth, int targetHeight);
	void createDescriptorSetLayout();
	void createGraphicsPipeline(bool useColor, int targetWidth, int targetHeight);
	void recordCommandBuffer(int targetWidth, int targetHeight, size_t indexCount, size_t lightCount);
	void recordCountCommandBuffer(size_t indexCount, size_t lightCount);
	void recordComputeCommandBuffer();
	void recordTransparentCommandBuffer(size_t indexCount, size_t lightCount);
	unsigned char* copyToHost(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout);

	void createHostReadableDestinationImage(glm::ivec2 size);
	void destroyHostReadableDestinationImage();

	// Transparency pipeline creation
	void createTransparencyBuffers(int width, int height);
	void zeroTransparencyBuffers();
	VkShaderModule createComputeShaderModule(EShLanguage lang, std::string const & filePath, std::vector<std::string> const & options);
	void updateTransparencyDescriptors();
	void createCountRenderPass(int targetWidth, int targetHeight);
	void createTransparentRenderPass(int targetWidth, int targetHeight);
	void createTransparencyPipelineLayout();
	void createComputeDescriptorSetLayout();
	void createComputeDescriptorPool();
	void createComputeDescriptorSet();
	void createComputePipelineLayout();
	void createComputePipelines();
	void createCountPipeline(bool useColor, int targetWidth, int targetHeight);
	void createTransparentPipeline(bool useColor, int targetWidth, int targetHeight);
	void createBlendPipeline(int targetWidth, int targetHeight);

	// IBL (Image-Based Lighting)
	VkResult createIBLImage(const std::vector<float>& data, uint32_t width, uint32_t height,
	                        VkImage& image, VkDeviceMemory& memory,
	                        VkImageView& imageView, VkSampler& sampler);
	VkResult createIBLImage3D(const std::vector<std::vector<float>>& layers,
	                          uint32_t width, uint32_t height,
	                          VkImage& image, VkDeviceMemory& memory,
	                          VkImageView& imageView, VkSampler& sampler);
	void initIBL(const std::string& iblPath);
	void destroyIBLResources();
	void copyIBLDataToImage(const float* data, VkDeviceSize dataSize,
	                        VkImage image, uint32_t w, uint32_t h, uint32_t layerOffset);
	void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

	void cleanup();

public:
	void copyMeshToGPU(const Mesh& mesh);

	unsigned char* render(
		glm::ivec2 targetSize, 
		VkSubresourceLayout* imageSubresourceLayout, 
		const glm::mat4& view, 
		const glm::mat4& proj, 
		const std::vector<V3dMaterial>& materials,
		const std::vector<V3dHeaderInfo::Light>& lights,
		MeshPipelineMode pipelineMode,
		const glm::vec4& background,
		bool orthographic = false,
		bool useIBL = false,
		const std::string& iblPath = ""
	);

	void cleanupMeshData();

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

	VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);
    
    // Submit command buffer to a queue and wait for fence until queue operations have been finished
	void submitWork(VkCommandBuffer cmdBuffer, VkQueue queue);

	unsigned int m_IndexCount{ 0 };
	glm::vec4 m_BackgroundColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool m_Orthographic{ false };
};

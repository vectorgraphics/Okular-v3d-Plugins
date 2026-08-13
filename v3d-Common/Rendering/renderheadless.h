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
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "../3rdParty/VulkanTools/VulkanTools.h"

#include "../V3dFile/Mesh.h"
#include "../V3dFile/V3dObjects.h"
#include "../V3dFile/V3dHeaderInfo.h"
#include "V3dModel.h"
#include "Public/ShaderLang.h"

// Vertex input trait specializations: map a vertex struct type to its
// Vulkan binding/attribute description free functions.  Matches vkrender.cc.
template<typename V> struct VertexInputTraits;

template<> struct VertexInputTraits<MaterialVertex> {
    static VkVertexInputBindingDescription binding();
    static std::vector<VkVertexInputAttributeDescription> attributes(bool count);
};

template<> struct VertexInputTraits<ColorVertex> {
    static VkVertexInputBindingDescription binding();
    static std::vector<VkVertexInputAttributeDescription> attributes(bool count);
};

template<> struct VertexInputTraits<PointVertex> {
    static VkVertexInputBindingDescription binding();
    static std::vector<VkVertexInputAttributeDescription> attributes(bool count);
};

// Pipeline configuration for createGraphicsPipeline<V>().  Matches vkrender.cc
// PipelineConfig -- one config per pipeline, no boilerplate duplication.
struct PipelineConfig {
	VkPrimitiveTopology topology;
	VkPolygonMode polygonMode;
	std::vector<std::string> shaderOptions;
	VkRenderPass renderPass;
	int subpass;
	bool enableDepthWrite;
	std::string vertexShaderName;  // e.g., "vertex" or "screen"
	std::string fragmentShaderName; // e.g., "fragment", "count", "blend"
};

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
	glm::mat4 projViewMat{ };
	glm::mat4 viewMat{ };
	// GLSL mat3 in std140 = 3 columns of vec4 (48 bytes)
	glm::vec4 normMat[3];
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
	PFN_vkQueueSubmit2 vkQueueSubmit2Fn{ nullptr };  // Loaded dynamically (matches vkrender.cc dispatch)
	uint32_t maxComputeWorkGroupCountX{ 65535 };
	uint32_t maxFramebufferWidth{ 16384 };
	uint32_t maxFramebufferHeight{ 16384 };
	uint32_t queueFamilyIndex;
	VkQueue queue;
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout graphicsPipelineLayout{ VK_NULL_HANDLE };  // Single shared layout for ALL graphics pipelines (matches vkrender.cc)
	VkPipeline materialPipeline{ VK_NULL_HANDLE };  // Opaque pipeline: MaterialVertex, no GENERAL/COLOR
	VkPipeline colorPipeline{ VK_NULL_HANDLE };     // Opaque pipeline: ColorVertex, with GENERAL+COLOR

	// Line pipeline: LINE_LIST topology for lineData (BezierCurve edges, V3dLineSegment)
	VkPipeline linePipeline{ VK_NULL_HANDLE };

	// Point pipeline: POINT_LIST topology for pointData (V3dPixel)
	VkPipeline pointPipeline{ VK_NULL_HANDLE };

	// Persistent GPU buffers per VertexBuffer type, following vkrender.cc FrameBufferPair pattern.
	// Each pair (vertex+index) is allocated once and grows as needed; data is uploaded
	// via staging buffer + vkCmdCopyBuffer inside the recorded command buffer.
	VkBuffer materialVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory materialVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize materialVertexBufferSize{ 0 };
	VkBuffer materialIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory materialIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize materialIndexBufferSize{ 0 };

	VkBuffer lineVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory lineVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize lineVertexBufferSize{ 0 };
	VkBuffer lineIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory lineIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize lineIndexBufferSize{ 0 };

	// Persistent staging buffers (vkrender.cc: FrameBufferPair vertexStagingBuffer / indexStagingBuffer)
	VkBuffer materialVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory materialVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize materialVertexStgSize{ 0 };
	VkBuffer materialIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory materialIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize materialIndexStgSize{ 0 };

	// Color vertex buffers (vkrender.cc: colorBuffers -- separate from materialBuffers)
	VkBuffer colorVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory colorVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize colorVertexBufferSize{ 0 };
	VkBuffer colorIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory colorIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize colorIndexBufferSize{ 0 };
	VkBuffer colorVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory colorVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize colorVertexStgSize{ 0 };
	VkBuffer colorIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory colorIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize colorIndexStgSize{ 0 };

	// Transparent vertex buffers (vkrender.cc: transparentBuffers -- ColorVertex format)
	VkBuffer transparentVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory transparentVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize transparentVertexBufferSize{ 0 };
	VkBuffer transparentIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory transparentIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize transparentIndexBufferSize{ 0 };
	VkBuffer transparentVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory transparentVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize transparentVertexStgSize{ 0 };
	VkBuffer transparentIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory transparentIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize transparentIndexStgSize{ 0 };
	// Triangle vertex buffers (vkrender.cc: triangleBuffers -- ColorVertex format, GENERAL path)
	VkBuffer triangleVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory triangleVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize triangleVertexBufferSize{ 0 };
	VkBuffer triangleIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory triangleIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize triangleIndexBufferSize{ 0 };
	VkBuffer triangleVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory triangleVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize triangleVertexStgSize{ 0 };
	VkBuffer triangleIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory triangleIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize triangleIndexStgSize{ 0 };
	VkBuffer lineVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory lineVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize lineVertexStgSize{ 0 };
	VkBuffer lineIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory lineIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize lineIndexStgSize{ 0 };

	// Point vertex buffers (for V3dPixel / pointData -- PointVertex format)
	VkBuffer pointVertexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory pointVertexMemory{ VK_NULL_HANDLE };
	VkDeviceSize pointVertexBufferSize{ 0 };
	VkBuffer pointIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory pointIndexMemory{ VK_NULL_HANDLE };
	VkDeviceSize pointIndexBufferSize{ 0 };
	VkBuffer pointVertexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory pointVertexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize pointVertexStgSize{ 0 };
	VkBuffer pointIndexStagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory pointIndexStagingMemory{ VK_NULL_HANDLE };
	VkDeviceSize pointIndexStgSize{ 0 };

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
	bool Opaque{ true };  // Matches Asymptote: set once per QueueMesh via setOpaque()
	bool srgb{ false }; // TODO: control via env var
	MeshPipelineMode currentPipelineMode{ MeshPipelineMode::MaterialOnly };
	DrawMode currentDrawMode{ DRAWMODE_NORMAL };
	bool currentUseColor{ false };

	FrameBufferAttachment colorAttachment, depthAttachment, resolveAttachment;

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
	VkRenderPass opaqueRenderPass{ VK_NULL_HANDLE };
	VkFramebuffer opaqueFramebuffer{ VK_NULL_HANDLE };
	VkRenderPass graphicsRenderPass{ VK_NULL_HANDLE };  // 3-subpass: opaque(0) + transparent(1) + blend(2)
	VkFramebuffer graphicsFramebuffer{ VK_NULL_HANDLE };

	VkPipeline materialCountPipeline{ VK_NULL_HANDLE };
	VkPipeline colorCountPipeline{ VK_NULL_HANDLE };
	VkPipeline triangleCountPipeline{ VK_NULL_HANDLE };    // ColorVertex+GENERAL, countRenderPass subpass 0 (for triangleData count)
	VkPipeline transparentCountPipeline{ VK_NULL_HANDLE };  // ColorVertex, countRenderPass subpass 1 (for transparentData count)

	VkPipeline materialTransparentPipeline{ VK_NULL_HANDLE };  // MaterialVertex, TRIANGLE_LIST, graphicsRenderPass subpass 0
	VkPipeline colorTransparentPipeline{ VK_NULL_HANDLE };     // ColorVertex, TRIANGLE_LIST, graphicsRenderPass subpass 0
	VkPipeline triangleTransparentPipeline{ VK_NULL_HANDLE };  // ColorVertex+GENERAL, TRIANGLE_LIST, graphicsRenderPass subpass 0 (for triangleData)
	VkPipeline lineTransparentPipeline{ VK_NULL_HANDLE };      // MaterialVertex, LINE_LIST, graphicsRenderPass subpass 0 (for lineData)
	VkPipeline transparentPipeline{ VK_NULL_HANDLE };          // ColorVertex, graphicsRenderPass subpass 1 (transparentData)

	VkPipeline blendPipeline{ VK_NULL_HANDLE };
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

	// Timeline semaphore synchronization (matches vkrender.cc renderTimelineSemaphore).
	// Enables GPU-side chaining between count/compute and graphics submissions.
	VkSemaphore timelineSemaphore{ VK_NULL_HANDLE };
	uint64_t currentTimelineValue{ 0 };
	uint64_t computeTimelineValue{ 0 };

	// Binary semaphore for vertex upload synchronization (matches vkrender.cc transferDoneSemaphore).
	// Signals when transfer command buffer completes; count/compute submit waits on it.
	VkSemaphore transferDoneSemaphore{ VK_NULL_HANDLE };
	VkCommandBuffer transferCommandBuffer{ VK_NULL_HANDLE };
	VkFence transferFence{ VK_NULL_HANDLE };  // Tracks transfer completion for safe reset (matches vkrender.cc transferFence)
	bool transferHasPendingWork{ false };
	bool copied{ false };  // Per-frame guard: prevents double-uploads within a single frame (matches vkrender.cc)

	// Persistent count+compute command buffers (matches vkrender.cc pattern:
	// allocated once, reset and re-recorded each frame in refreshBuffers).
	VkCommandBuffer countCommandBuffer{ VK_NULL_HANDLE };
	VkCommandBuffer computeCommandBuffer{ VK_NULL_HANDLE };

	// Compute fence created in signaled state (matches vkrender.cc inComputeFence).
	VkFence inComputeFence{ VK_NULL_HANDLE };

	// Fence for the main graphics submit. Ensures GPU has finished executing
	// the previous render's command buffer before we reset/re-record it.
	// This is essential because we use a single shared HeadlessRenderer from
	// multiple Okular threads — the mutex serializes CPU access but does not
	// prevent GPU-side races on shared command buffers and sync objects.
	VkFence graphicsFence{ VK_NULL_HANDLE };

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
	void createDescriptorSetLayout();
	void createMaterialPipeline(DrawMode drawMode, int targetWidth, int targetHeight);
	void createColorPipeline(DrawMode drawMode, int targetWidth, int targetHeight);
	void createLinePipeline(int targetWidth, int targetHeight);
	void createPointPipeline(int targetWidth, int targetHeight);
	void recreateGraphicsPipelines(DrawMode drawMode, int targetWidth, int targetHeight);
	void uploadToPersistentBuffer(VkCommandBuffer cmd, VkBuffer& dstBuf, VkDeviceMemory& dstMem, VkDeviceSize& dstSize,
	                              VkBuffer& stgBuf, VkDeviceMemory& stgMem, VkDeviceSize& stgSize,
	                              const void* data, VkDeviceSize dataSize, bool isVertex);
	void recordCountCommandBuffer(size_t indexCount, size_t lightCount);
	void recordComputeCommandBuffer();
	// Transfer recording split (matches vkrender.cc pattern):
	// beginTransferRecording -> recordUploads(cmd, remesh) -> endAndSubmitTransfers
	void beginTransferRecording();
	void recordUploads(VkCommandBuffer cmd, bool remesh);
	void endAndSubmitTransfers();
	void uploadVertexData();  // Thin wrapper: calls all three above
	void refreshBuffers(size_t indexCount, size_t lightCount);
	void readFeedback();  // Matches vkrender.cc resizeFragmentBuffer(): wait fence + invalidate + read feedback
	unsigned char* copyToHost(glm::ivec2 targetSize, VkSubresourceLayout* imageSubresourceLayout, bool useResolve = false);

	void createHostReadableDestinationImage(glm::ivec2 size);
	void destroyHostReadableDestinationImage();

	// Transparency pipeline creation
	void createTransparencyBuffers(int width, int height);
	VkSemaphore createTimelineSemaphore(uint64_t initialValue);
	void zeroTransparencyBuffers();
	VkShaderModule createComputeShaderModule(EShLanguage lang, std::string const & filePath, std::vector<std::string> const & options);
	void updateTransparencyDescriptors();
	void createCountRenderPass(int targetWidth, int targetHeight);
	void createGraphicsRenderPass(int targetWidth, int targetHeight);
	void createGraphicsPipelineLayout();  // Single shared layout for ALL graphics pipelines (matches vkrender.cc)
	void createComputeDescriptorSetLayout();
	void createComputeDescriptorPool();
	void createComputeDescriptorSet();
	void createComputePipelineLayout();
	void createComputePipelines();
	void createMaterialCountPipeline(int targetWidth, int targetHeight);
	void createColorCountPipeline(int targetWidth, int targetHeight);
	void createTriangleCountPipeline(int targetWidth, int targetHeight);
	void createTransparentCountPipeline(int targetWidth, int targetHeight);
	void createMaterialTransparentPipeline(int targetWidth, int targetHeight);
	void createColorTransparentPipeline(int targetWidth, int targetHeight);
	void createTriangleTransparentPipeline(int targetWidth, int targetHeight);
	void createLineTransparentPipeline(int targetWidth, int targetHeight);
	void createTransparentPipeline(int targetWidth, int targetHeight);
	void createBlendPipeline(int targetWidth, int targetHeight);

	// Templated pipeline creation: assembles all state from helpers + VertexInputTraits<V>,
	// compiles shaders with the given options, and creates the pipeline.
	// Matches vkrender.cc createGraphicsPipeline<V>() pattern.
	template<typename V>
	void createGraphicsPipeline(const PipelineConfig& config,
	                           int targetWidth, int targetHeight,
	                           VkPipeline* out);

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
	unsigned char* render(
		glm::ivec2 targetSize,
		VkSubresourceLayout* imageSubresourceLayout,
		const glm::dmat4& view,
		const glm::dmat4& proj,
		const glm::dmat3& normMat,
		const std::vector<V3dMaterial>& materials,
		const std::vector<V3dHeaderInfo::Light>& lights,
		MeshPipelineMode pipelineMode,
		const glm::vec4& background,
		bool orthographic = false,
		bool useIBL = false,
		const std::string& iblPath = "",
		DrawMode drawMode = DRAWMODE_NORMAL,
		bool remesh = true
	);

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

	VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);

    // Submit command buffer to a queue and wait for fence until queue operations have been finished
	void submitWork(VkCommandBuffer cmdBuffer, VkQueue queue);

	glm::vec4 m_BackgroundColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool m_Orthographic{ false };
	bool remesh{ true };  // Per-frame upload gate (matches vkrender.cc global remesh flag)
};

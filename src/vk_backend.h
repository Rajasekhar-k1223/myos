#pragma once
/*
 * Software Vulkan subset for ElseaOS.
 *
 * This is a CPU rasteriser that implements the standard Vulkan API.
 * No GPU required — the "device" is pure software, the "swapchain" presents
 * to the VESA framebuffer, and shaders are SPIR-V modules interpreted by
 * src/spirv.c.
 *
 * Covered subset (enough to render a textured triangle or a 3-D scene):
 *   Instance / device / queue lifecycle
 *   Memory allocation (kmalloc-backed VkDeviceMemory)
 *   Buffers (vertex, index, uniform)
 *   Images + image views (RGBA8 + D32 depth)
 *   Samplers (nearest + bilinear)
 *   Render passes + framebuffers
 *   Graphics pipeline (vertex input, rasterization, blending, depth test)
 *   Shader modules (SPIR-V stored and interpreted at draw time)
 *   Descriptor sets (uniform buffers + combined image samplers)
 *   Command buffers (recorded, replayed on vkQueueSubmit)
 *   Swapchain KHR extension (backed by VESA framebuffer)
 *   Semaphores + fences (no-op on single-threaded CPU)
 *
 * Not supported:
 *   Compute / geometry / tessellation shaders
 *   Secondary command buffers
 *   Multisampling
 *   Multiple queues / queue families
 *   Transfer queue, blit operations
 *   Buffer views, push descriptor, dynamic rendering
 */
#include <stdint.h>
#include <stddef.h>

/* ── Basic types ─────────────────────────────────────────────────────────── */
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkFlags;
typedef uint32_t VkSampleCountFlags;

#define VK_TRUE  1u
#define VK_FALSE 0u
#define VK_NULL_HANDLE 0

/* Dispatchable / non-dispatchable handles (all pointers to internal structs) */
typedef struct vk_instance_t*     VkInstance;
typedef struct vk_phys_dev_t*     VkPhysicalDevice;
typedef struct vk_device_t*       VkDevice;
typedef struct vk_queue_t*        VkQueue;
typedef struct vk_semaphore_t*    VkSemaphore;
typedef struct vk_fence_t*        VkFence;
typedef struct vk_cmd_pool_t*     VkCommandPool;
typedef struct vk_cmd_buf_t*      VkCommandBuffer;
typedef struct vk_render_pass_t*  VkRenderPass;
typedef struct vk_framebuffer_t*  VkFramebuffer;
typedef struct vk_pipeline_t*     VkPipeline;
typedef struct vk_pipeline_layout_t* VkPipelineLayout;
typedef struct vk_dsl_t*          VkDescriptorSetLayout;
typedef struct vk_desc_pool_t*    VkDescriptorPool;
typedef struct vk_desc_set_t*     VkDescriptorSet;
typedef struct vk_shader_t*       VkShaderModule;
typedef struct vk_buf_t*          VkBuffer;
typedef struct vk_img_t*          VkImage;
typedef struct vk_imgview_t*      VkImageView;
typedef struct vk_sampler_t*      VkSampler;
typedef struct vk_devmem_t*       VkDeviceMemory;
typedef struct vk_swapchain_t*    VkSwapchainKHR;
typedef struct vk_surface_t*      VkSurfaceKHR;

/* ── Result codes ────────────────────────────────────────────────────────── */
typedef enum {
    VK_SUCCESS = 0,
    VK_NOT_READY = 1,
    VK_TIMEOUT = 2,
    VK_ERROR_OUT_OF_HOST_MEMORY = -1,
    VK_ERROR_INITIALIZATION_FAILED = -3,
    VK_ERROR_FEATURE_NOT_PRESENT = -8,
    VK_ERROR_FORMAT_NOT_SUPPORTED = -11,
    VK_SUBOPTIMAL_KHR = 1000001003,
    VK_ERROR_OUT_OF_DATE_KHR = -1000001004,
} VkResult;

/* ── Enums ───────────────────────────────────────────────────────────────── */
typedef enum { VK_FORMAT_UNDEFINED=0, VK_FORMAT_R8G8B8A8_UNORM=37,
    VK_FORMAT_B8G8R8A8_UNORM=44, VK_FORMAT_D32_SFLOAT=126,
    VK_FORMAT_R32G32_SFLOAT=103, VK_FORMAT_R32G32B32_SFLOAT=106,
    VK_FORMAT_R32G32B32A32_SFLOAT=109 } VkFormat;

typedef enum { VK_IMAGE_LAYOUT_UNDEFINED=0, VK_IMAGE_LAYOUT_GENERAL=1,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL=2,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL=3,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR=1000001002 } VkImageLayout;

typedef enum { VK_ATTACHMENT_LOAD_OP_LOAD=0, VK_ATTACHMENT_LOAD_OP_CLEAR=1,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE=2 } VkAttachmentLoadOp;
typedef enum { VK_ATTACHMENT_STORE_OP_STORE=0, VK_ATTACHMENT_STORE_OP_DONT_CARE=1 } VkAttachmentStoreOp;

typedef enum { VK_PIPELINE_BIND_POINT_GRAPHICS=0, VK_PIPELINE_BIND_POINT_COMPUTE=1 } VkPipelineBindPoint;

typedef enum { VK_SHADER_STAGE_VERTEX_BIT=1, VK_SHADER_STAGE_FRAGMENT_BIT=16,
    VK_SHADER_STAGE_ALL_GRAPHICS=31 } VkShaderStageFlagBits;
typedef VkFlags VkShaderStageFlags;

typedef enum { VK_VERTEX_INPUT_RATE_VERTEX=0, VK_VERTEX_INPUT_RATE_INSTANCE=1 } VkVertexInputRate;
typedef enum { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST=3, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP=4,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN=5, VK_PRIMITIVE_TOPOLOGY_LINE_LIST=1 } VkPrimitiveTopology;

typedef enum { VK_POLYGON_MODE_FILL=0, VK_POLYGON_MODE_LINE=1 } VkPolygonMode;
typedef enum { VK_CULL_MODE_NONE=0, VK_CULL_MODE_FRONT_BIT=1, VK_CULL_MODE_BACK_BIT=2 } VkCullModeFlags;
typedef enum { VK_FRONT_FACE_COUNTER_CLOCKWISE=0, VK_FRONT_FACE_CLOCKWISE=1 } VkFrontFace;
typedef enum { VK_COMPARE_OP_NEVER=0, VK_COMPARE_OP_LESS=1, VK_COMPARE_OP_EQUAL=2,
    VK_COMPARE_OP_LESS_OR_EQUAL=3, VK_COMPARE_OP_GREATER=4, VK_COMPARE_OP_NOT_EQUAL=5,
    VK_COMPARE_OP_GREATER_OR_EQUAL=6, VK_COMPARE_OP_ALWAYS=7 } VkCompareOp;

typedef enum { VK_BLEND_FACTOR_ZERO=0, VK_BLEND_FACTOR_ONE=1,
    VK_BLEND_FACTOR_SRC_ALPHA=6, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA=7,
    VK_BLEND_FACTOR_DST_ALPHA=8, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA=9 } VkBlendFactor;
typedef enum { VK_BLEND_OP_ADD=0, VK_BLEND_OP_SUBTRACT=1 } VkBlendOp;

typedef enum { VK_DESCRIPTOR_TYPE_SAMPLER=0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER=1,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE=2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=7 } VkDescriptorType;

typedef enum { VK_IMAGE_VIEW_TYPE_2D=1 } VkImageViewType;

typedef enum { VK_BUFFER_USAGE_TRANSFER_SRC_BIT=1, VK_BUFFER_USAGE_TRANSFER_DST_BIT=2,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT=128, VK_BUFFER_USAGE_INDEX_BUFFER_BIT=64,
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT=16 } VkBufferUsageFlagBits;
typedef VkFlags VkBufferUsageFlags;

typedef enum { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT=2, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT=4,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT=1 } VkMemoryPropertyFlagBits;

typedef enum { VK_IMAGE_USAGE_SAMPLED_BIT=4, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT=16,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT=32,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT=2 } VkImageUsageFlagBits;
typedef VkFlags VkImageUsageFlags;

typedef enum { VK_FILTER_NEAREST=0, VK_FILTER_LINEAR=1 } VkFilter;
typedef enum { VK_SAMPLER_ADDRESS_MODE_REPEAT=0, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE=2 } VkSamplerAddressMode;

typedef enum { VK_INDEX_TYPE_UINT16=0, VK_INDEX_TYPE_UINT32=1 } VkIndexType;

typedef enum { VK_COMMAND_BUFFER_LEVEL_PRIMARY=0 } VkCommandBufferLevel;
typedef enum { VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT=1,
    VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT=4 } VkCommandBufferUsageFlagBits;

typedef enum { VK_SAMPLE_COUNT_1_BIT=1 } VkSampleCountFlagBits;
typedef enum { VK_LOGIC_OP_COPY=3 } VkLogicOp;

typedef enum { VK_SUBPASS_CONTENTS_INLINE=0 } VkSubpassContents;
typedef enum { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT=0x10000,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT=0x400 } VkPipelineStageFlagBits;
typedef enum { VK_ACCESS_SHADER_READ_BIT=32, VK_ACCESS_MEMORY_READ_BIT=0x8000,
    VK_ACCESS_MEMORY_WRITE_BIT=0x10000 } VkAccessFlagBits;
typedef enum { VK_IMAGE_ASPECT_COLOR_BIT=1, VK_IMAGE_ASPECT_DEPTH_BIT=2 } VkImageAspectFlagBits;
typedef enum { VK_IMAGE_TYPE_2D=1 } VkImageType;
typedef enum { VK_IMAGE_TILING_LINEAR=0, VK_IMAGE_TILING_OPTIMAL=1 } VkImageTiling;
typedef enum { VK_SHARING_MODE_EXCLUSIVE=0 } VkSharingMode;

typedef enum { VK_COLOR_COMPONENT_R_BIT=1, VK_COLOR_COMPONENT_G_BIT=2,
    VK_COLOR_COMPONENT_B_BIT=4, VK_COLOR_COMPONENT_A_BIT=8 } VkColorComponentFlagBits;
typedef VkFlags VkColorComponentFlags;
#define VK_COLOR_COMPONENT_RGBA_BITS 0xF

typedef enum { VK_PRESENT_MODE_FIFO_KHR=2, VK_PRESENT_MODE_IMMEDIATE_KHR=0 } VkPresentModeKHR;
typedef enum { VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR=1 } VkSurfaceTransformFlagBitsKHR;
typedef enum { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR=1 } VkCompositeAlphaFlagBitsKHR;

/* ── Structs ─────────────────────────────────────────────────────────────── */
typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y; } VkOffset2D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { float x, y, width, height, minDepth, maxDepth; } VkViewport;
typedef struct { uint32_t x, y, z; } VkQueueFamilyProperties_internal;

typedef struct { float r, g, b, a; } VkClearColorValue_f;
typedef union { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef struct { float depth; uint32_t stencil; } VkClearDepthStencilValue;
typedef union { VkClearColorValue color; VkClearDepthStencilValue depthStencil; } VkClearValue;

typedef struct { uint32_t memoryTypeCount; } VkPhysicalDeviceMemoryProperties;
typedef struct { uint32_t apiVersion, driverVersion, vendorID, deviceID; int deviceType;
    char deviceName[256]; } VkPhysicalDeviceProperties;
typedef struct { uint32_t queueFlags, queueCount, timestampValidBits; VkExtent3D minImageTransferGranularity; } VkQueueFamilyProperties;

typedef struct { uint32_t sType; const void* pNext; } VkStructBase;

typedef struct { uint32_t sType; const void* pNext; uint32_t flags;
    const char* pApplicationName; uint32_t applicationVersion;
    const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { uint32_t sType; const void* pNext; const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures; } VkDeviceCreateInfo;

typedef struct { VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { uint32_t sType; const void* pNext; VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex; } VkMemoryAllocateInfoFull;
typedef struct { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkDeviceSize size;
    VkBufferUsageFlags usage; VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; } VkBufferCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkImageType imageType;
    VkFormat format; VkExtent3D extent; uint32_t mipLevels, arrayLayers;
    VkSampleCountFlagBits samples; VkImageTiling tiling; VkImageUsageFlags usage;
    VkSharingMode sharingMode; uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices; VkImageLayout initialLayout; } VkImageCreateInfo;

typedef struct { uint32_t aspectMask; uint32_t baseMipLevel, levelCount;
    uint32_t baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkImage image;
    VkImageViewType viewType; VkFormat format;
    struct { uint32_t r, g, b, a; } components;
    VkImageSubresourceRange subresourceRange; } VkImageViewCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkFilter magFilter, minFilter;
    uint32_t mipmapMode; VkSamplerAddressMode addressModeU, addressModeV, addressModeW;
    float mipLodBias; VkBool32 anisotropyEnable; float maxAnisotropy;
    VkBool32 compareEnable; VkCompareOp compareOp;
    float minLod, maxLod; uint32_t borderColor; VkBool32 unnormalizedCoordinates; } VkSamplerCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t codeSize;
    const uint32_t* pCode; } VkShaderModuleCreateInfo;

typedef struct { VkFormat format; VkSampleCountFlagBits samples;
    VkAttachmentLoadOp loadOp, stencilLoadOp;
    VkAttachmentStoreOp storeOp, stencilStoreOp;
    VkImageLayout initialLayout, finalLayout; uint32_t flags; } VkAttachmentDescription;
typedef struct { uint32_t attachment; VkImageLayout layout; } VkAttachmentReference;
typedef struct { uint32_t flags; VkPipelineBindPoint pipelineBindPoint;
    uint32_t inputAttachmentCount; const VkAttachmentReference* pInputAttachments;
    uint32_t colorAttachmentCount; const VkAttachmentReference* pColorAttachments;
    const VkAttachmentReference* pResolveAttachments;
    const VkAttachmentReference* pDepthStencilAttachment;
    uint32_t preserveAttachmentCount; const uint32_t* pPreserveAttachments; } VkSubpassDescription;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t attachmentCount; const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount; const VkSubpassDescription* pSubpasses;
    uint32_t dependencyCount; const void* pDependencies; } VkRenderPassCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkRenderPass renderPass; uint32_t attachmentCount; const VkImageView* pAttachments;
    uint32_t width, height, layers; } VkFramebufferCreateInfo;

typedef struct { uint32_t binding, stride; VkVertexInputRate inputRate; } VkVertexInputBindingDescription;
typedef struct { uint32_t location, binding; VkFormat format; uint32_t offset; } VkVertexInputAttributeDescription;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t vertexBindingDescriptionCount; const VkVertexInputBindingDescription* pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount; const VkVertexInputAttributeDescription* pVertexAttributeDescriptions; } VkPipelineVertexInputStateCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkPrimitiveTopology topology; VkBool32 primitiveRestartEnable; } VkPipelineInputAssemblyStateCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t viewportCount;
    const VkViewport* pViewports; uint32_t scissorCount; const VkRect2D* pScissors; } VkPipelineViewportStateCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkBool32 depthClampEnable, rasterizerDiscardEnable;
    VkPolygonMode polygonMode; VkCullModeFlags cullMode; VkFrontFace frontFace;
    VkBool32 depthBiasEnable; float depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor;
    float lineWidth; } VkPipelineRasterizationStateCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkSampleCountFlagBits rasterizationSamples;
    VkBool32 sampleShadingEnable; float minSampleShading; const uint32_t* pSampleMask;
    VkBool32 alphaToCoverageEnable, alphaToOneEnable; } VkPipelineMultisampleStateCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkBool32 depthTestEnable, depthWriteEnable; VkCompareOp depthCompareOp;
    VkBool32 depthBoundsTestEnable; VkBool32 stencilTestEnable;
    uint32_t front, back; float minDepthBounds, maxDepthBounds; } VkPipelineDepthStencilStateCreateInfo;
typedef struct { VkBool32 blendEnable;
    VkBlendFactor srcColorBlendFactor, dstColorBlendFactor; VkBlendOp colorBlendOp;
    VkBlendFactor srcAlphaBlendFactor, dstAlphaBlendFactor; VkBlendOp alphaBlendOp;
    VkColorComponentFlags colorWriteMask; } VkPipelineColorBlendAttachmentState;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkBool32 logicOpEnable;
    VkLogicOp logicOp; uint32_t attachmentCount; const VkPipelineColorBlendAttachmentState* pAttachments;
    float blendConstants[4]; } VkPipelineColorBlendStateCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t dynamicStateCount;
    const uint32_t* pDynamicStates; } VkPipelineDynamicStateCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t stage;
    VkShaderModule module; const char* pName; const void* pSpecializationInfo; } VkPipelineShaderStageCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t setLayoutCount; const VkDescriptorSetLayout* pSetLayouts;
    uint32_t pushConstantRangeCount; const void* pPushConstantRanges; } VkPipelineLayoutCreateInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t stageCount; const VkPipelineShaderStageCreateInfo* pStages;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
    const void* pTessellationState;
    const VkPipelineViewportStateCreateInfo* pViewportState;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
    const VkPipelineDynamicStateCreateInfo* pDynamicState;
    VkPipelineLayout layout; VkRenderPass renderPass;
    uint32_t subpass; VkPipeline basePipelineHandle; int32_t basePipelineIndex; } VkGraphicsPipelineCreateInfo;

typedef struct { uint32_t binding; VkDescriptorType descriptorType; uint32_t descriptorCount;
    VkShaderStageFlags stageFlags; const VkSampler* pImmutableSamplers; } VkDescriptorSetLayoutBinding;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding* pBindings; } VkDescriptorSetLayoutCreateInfo;
typedef struct { VkDescriptorType type; uint32_t descriptorCount; } VkDescriptorPoolSize;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t maxSets;
    uint32_t poolSizeCount; const VkDescriptorPoolSize* pPoolSizes; } VkDescriptorPoolCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount; const VkDescriptorSetLayout* pSetLayouts; } VkDescriptorSetAllocateInfo;

typedef struct { VkBuffer buffer; VkDeviceSize offset, range; } VkDescriptorBufferInfo;
typedef struct { VkSampler sampler; VkImageView imageView; VkImageLayout imageLayout; } VkDescriptorImageInfo;
typedef struct { uint32_t sType; const void* pNext; VkDescriptorSet dstSet;
    uint32_t dstBinding, dstArrayElement, descriptorCount; VkDescriptorType descriptorType;
    const VkDescriptorImageInfo* pImageInfo; const VkDescriptorBufferInfo* pBufferInfo;
    const void* pTexelBufferView; } VkWriteDescriptorSet;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkCommandPool commandPool;
    VkCommandBufferLevel level; uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkRenderPass renderPass; uint32_t subpass; VkFramebuffer framebuffer;
    VkBool32 occlusionQueryEnable; VkFlags queryFlags, pipelineStatistics; } VkCommandBufferInheritanceInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    const VkCommandBufferInheritanceInfo* pInheritanceInfo; } VkCommandBufferBeginInfo;

typedef struct { uint32_t sType; const void* pNext; VkRenderPass renderPass;
    VkFramebuffer framebuffer; VkRect2D renderArea;
    uint32_t clearValueCount; const VkClearValue* pClearValues; } VkRenderPassBeginInfo;

typedef struct { uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores; const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores; } VkSubmitInfo;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags;
    VkSurfaceKHR surface; uint32_t minImageCount; VkFormat imageFormat;
    uint32_t imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage; VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR preTransform; VkCompositeAlphaFlagBitsKHR compositeAlpha;
    VkPresentModeKHR presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain; } VkSwapchainCreateInfoKHR;

typedef struct { uint32_t sType; const void* pNext; VkSwapchainKHR swapchain;
    uint32_t imageIndex; VkResult* pResults; } VkPresentInfoKHR_single;
typedef struct { uint32_t sType; const void* pNext;
    uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores;
    uint32_t swapchainCount; const VkSwapchainKHR* pSwapchains;
    const uint32_t* pImageIndices; VkResult* pResults; } VkPresentInfoKHR;

typedef struct { uint32_t sType; const void* pNext; VkFlags flags; } VkSemaphoreCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; } VkFenceCreateInfo;

typedef struct { uint32_t aspectMask; uint32_t mipLevel; uint32_t arrayLayer; } VkImageSubresource;
typedef struct { VkImageSubresourceRange subresourceRange; uint32_t srcQueueFamilyIndex,dstQueueFamilyIndex; VkImageLayout oldLayout, newLayout; VkImage image; VkFlags srcAccessMask, dstAccessMask; uint32_t sType; const void* pNext; } VkImageMemoryBarrier;

/* ── ElseaOS surface (backed by VESA framebuffer) ────────────────────────── */
typedef struct { uint32_t* pixels; int width, height; } VkElseaSurfaceCreateInfoKHR;
VkResult vkCreateElseaSurface(VkInstance inst, const VkElseaSurfaceCreateInfoKHR* info,
                               const void* alloc, VkSurfaceKHR* surface);

/* ── API declarations ────────────────────────────────────────────────────── */
VkResult vkCreateInstance(const VkInstanceCreateInfo*, const void*, VkInstance*);
void     vkDestroyInstance(VkInstance, const void*);
VkResult vkEnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);
void     vkGetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties*);
void     vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
void     vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
VkResult vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
void     vkDestroyDevice(VkDevice, const void*);
void     vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue*);
VkResult vkDeviceWaitIdle(VkDevice);
VkResult vkAllocateMemory(VkDevice, const VkMemoryAllocateInfoFull*, const void*, VkDeviceMemory*);
void     vkFreeMemory(VkDevice, VkDeviceMemory, const void*);
VkResult vkMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
void     vkUnmapMemory(VkDevice, VkDeviceMemory);
VkResult vkCreateBuffer(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*);
void     vkDestroyBuffer(VkDevice, VkBuffer, const void*);
void     vkGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements*);
VkResult vkBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
VkResult vkCreateImage(VkDevice, const VkImageCreateInfo*, const void*, VkImage*);
void     vkDestroyImage(VkDevice, VkImage, const void*);
void     vkGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements*);
VkResult vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VkResult vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*);
void     vkDestroyImageView(VkDevice, VkImageView, const void*);
VkResult vkCreateSampler(VkDevice, const VkSamplerCreateInfo*, const void*, VkSampler*);
void     vkDestroySampler(VkDevice, VkSampler, const void*);
VkResult vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
void     vkDestroyShaderModule(VkDevice, VkShaderModule, const void*);
VkResult vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo*, const void*, VkRenderPass*);
void     vkDestroyRenderPass(VkDevice, VkRenderPass, const void*);
VkResult vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo*, const void*, VkFramebuffer*);
void     vkDestroyFramebuffer(VkDevice, VkFramebuffer, const void*);
VkResult vkCreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*);
void     vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const void*);
VkResult vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*);
void     vkDestroyDescriptorPool(VkDevice, VkDescriptorPool, const void*);
VkResult vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
void     vkUpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
VkResult vkCreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
void     vkDestroyPipelineLayout(VkDevice, VkPipelineLayout, const void*);
VkResult vkCreateGraphicsPipelines(VkDevice, void*, uint32_t, const VkGraphicsPipelineCreateInfo*, const void*, VkPipeline*);
void     vkDestroyPipeline(VkDevice, VkPipeline, const void*);
VkResult vkCreateCommandPool(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
void     vkDestroyCommandPool(VkDevice, VkCommandPool, const void*);
VkResult vkAllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
void     vkFreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
VkResult vkBeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*);
VkResult vkEndCommandBuffer(VkCommandBuffer);
void     vkCmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
void     vkCmdEndRenderPass(VkCommandBuffer);
void     vkCmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
void     vkCmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
void     vkCmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
void     vkCmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
void     vkCmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
void     vkCmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
void     vkCmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
void     vkCmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
void     vkCmdPipelineBarrier(VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const VkImageMemoryBarrier*);
void     vkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const void*);
VkResult vkQueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
VkResult vkQueueWaitIdle(VkQueue);
VkResult vkCreateSemaphore(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);
void     vkDestroySemaphore(VkDevice, VkSemaphore, const void*);
VkResult vkCreateFence(VkDevice, const VkFenceCreateInfo*, const void*, VkFence*);
void     vkDestroyFence(VkDevice, VkFence, const void*);
VkResult vkWaitForFences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
VkResult vkResetFences(VkDevice, uint32_t, const VkFence*);
VkResult vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
void     vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR, const void*);
VkResult vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
VkResult vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
VkResult vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);
void     vkCmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*);

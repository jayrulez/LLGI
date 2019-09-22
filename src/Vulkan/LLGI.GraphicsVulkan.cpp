#include "LLGI.GraphicsVulkan.h"
#include "LLGI.BaseVulkan.h"
#include "LLGI.CommandListVulkan.h"
#include "LLGI.ConstantBufferVulkan.h"
#include "LLGI.IndexBufferVulkan.h"
#include "LLGI.PipelineStateVulkan.h"
#include "LLGI.ShaderVulkan.h"
#include "LLGI.SingleFrameMemoryPoolVulkan.h"
#include "LLGI.TextureVulkan.h"
#include "LLGI.VertexBufferVulkan.h"

namespace LLGI
{

GraphicsVulkan::GraphicsVulkan(const vk::Device& device,
							   const vk::Queue& quque,
							   const vk::CommandPool& commandPool,
							   const vk::PhysicalDevice& pysicalDevice,
							   const PlatformView& platformView,
							   std::function<void(vk::CommandBuffer&)> addCommand,
							   std::function<void(PlatformStatus&)> getStatus,
							   RenderPassPipelineStateCacheVulkan* renderPassPipelineStateCache)
	: vkDevice(device)
	, vkQueue(quque)
	, vkCmdPool(commandPool)
	, vkPysicalDevice(pysicalDevice)
	, addCommand_(addCommand)
	, getStatus_(getStatus)
	, renderPassPipelineStateCache_(renderPassPipelineStateCache)
{
	swapBufferCount_ = platformView.renderPasses.size();

	for (size_t i = 0; i < static_cast<size_t>(swapBufferCount_); i++)
	{
		/*
		auto renderPass = CreateSharedPtr(new RenderPassVulkan(renderPassPipelineStateCache, GetDevice(), nullptr));
		renderPass->Initialize(platformView.colors[i],
							   platformView.depths[i],
							   platformView.colorViews[i],
							   platformView.depthViews[i],
							   platformView.imageSize,
							   platformView.format);
		renderPasses.push_back(renderPass);
		*/
	}
	renderPasses = platformView.renderPasses;

	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.anisotropyEnable = false;
	samplerInfo.maxAnisotropy = 1;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = false;
	samplerInfo.compareEnable = false;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	defaultSampler_ = vkDevice.createSampler(samplerInfo);

	SafeAddRef(renderPassPipelineStateCache_);
	if (renderPassPipelineStateCache_ == nullptr)
	{
		renderPassPipelineStateCache_ = new RenderPassPipelineStateCacheVulkan(device, nullptr);
	}
}

GraphicsVulkan::~GraphicsVulkan()
{
	renderPasses.clear();

	SafeRelease(renderPassPipelineStateCache_);

	if (defaultSampler_)
	{
		vkDevice.destroySampler(defaultSampler_);
	}
}

void GraphicsVulkan::SetWindowSize(const Vec2I& windowSize) { throw "Not inplemented"; }

void GraphicsVulkan::Execute(CommandList* commandList)
{
	auto commandList_ = static_cast<CommandListVulkan*>(commandList);
	auto cmdBuf = commandList_->GetCommandBuffer();
	addCommand_(cmdBuf);
}

void GraphicsVulkan::WaitFinish() { vkQueue.waitIdle(); }

RenderPass* GraphicsVulkan::GetCurrentScreen(const Color8& clearColor, bool isColorCleared, bool isDepthCleared)
{
	PlatformStatus status;
	getStatus_(status);
	auto currentRenderPass = renderPasses[status.currentSwapBufferIndex];

	currentRenderPass->SetClearColor(clearColor);
	currentRenderPass->SetIsColorCleared(isColorCleared);
	currentRenderPass->SetIsDepthCleared(isDepthCleared);
	return currentRenderPass.get();
}

VertexBuffer* GraphicsVulkan::CreateVertexBuffer(int32_t size)
{
	auto obj = new VertexBufferVulkan();
	if (!obj->Initialize(this, size))
	{
		SafeRelease(obj);
		return nullptr;
	}

	return obj;
}

IndexBuffer* GraphicsVulkan::CreateIndexBuffer(int32_t stride, int32_t count)
{

	auto obj = new IndexBufferVulkan();
	if (!obj->Initialize(this, stride, count))
	{
		SafeRelease(obj);
		return nullptr;
	}

	return obj;
}

Shader* GraphicsVulkan::CreateShader(DataStructure* data, int32_t count)
{
	auto obj = new ShaderVulkan();
	if (!obj->Initialize(this, data, count))
	{
		SafeRelease(obj);
		return nullptr;
	}
	return obj;
}

PipelineState* GraphicsVulkan::CreatePiplineState()
{

	auto pipelineState = new PipelineStateVulkan();

	if (pipelineState->Initialize(this))
	{
		return pipelineState;
	}

	SafeRelease(pipelineState);
	return nullptr;
}

SingleFrameMemoryPool* GraphicsVulkan::CreateSingleFrameMemoryPool(int32_t constantBufferPoolSize, int32_t drawingCount)
{
	return new SingleFrameMemoryPoolVulkan(this, true, swapBufferCount_, constantBufferPoolSize, drawingCount);
}

CommandList* GraphicsVulkan::CreateCommandList(SingleFrameMemoryPool* memoryPool)
{
	auto mp = static_cast<SingleFrameMemoryPoolVulkan*>(memoryPool);

	auto commandList = new CommandListVulkan();
	if (commandList->Initialize(this, mp->GetDrawingCount()))
	{
		return commandList;
	}
	SafeRelease(commandList);
	return nullptr;
}

ConstantBuffer* GraphicsVulkan::CreateConstantBuffer(int32_t size)
{
	auto obj = new ConstantBufferVulkan();
	if (!obj->Initialize(this, size, ConstantBufferType::LongTime))
	{
		SafeRelease(obj);
		return nullptr;
	}
	return obj;
}

RenderPass* GraphicsVulkan::CreateRenderPass(const Texture** textures, int32_t textureCount, Texture* depthTexture)
{
	throw "Not inplemented";

	if (textureCount > 1)
		throw "Not inplemented";

	auto renderPass = new RenderPassVulkan(renderPassPipelineStateCache_, GetDevice(), this);
	if (!renderPass->Initialize((const TextureVulkan**)textures, textureCount, (TextureVulkan*)depthTexture))
	{
		SafeRelease(renderPass);
	}

	return renderPass;
}
Texture* GraphicsVulkan::CreateTexture(const Vec2I& size, bool isRenderPass, bool isDepthBuffer)
{
	auto obj = new TextureVulkan();
	if (!obj->Initialize(this, true, size, isRenderPass, isDepthBuffer))
	{
		SafeRelease(obj);
		return nullptr;
	}

	return obj;
}

Texture* GraphicsVulkan::CreateTexture(uint64_t id) { throw "Not inplemented"; }

std::vector<uint8_t> GraphicsVulkan::CaptureRenderTarget(Texture* renderTarget)
{
	if (!renderTarget)
	{
		return std::vector<uint8_t>();
	}

	std::vector<uint8_t> result;
	VkDevice device = static_cast<VkDevice>(GetDevice());
	vkDeviceWaitIdle(device);

	auto texture = static_cast<TextureVulkan*>(renderTarget);
	auto width = texture->GetSizeAs2D().X;
	auto height = texture->GetSizeAs2D().Y;
	auto size = texture->GetMemorySize();
	auto image = static_cast<VkImage>(texture->GetImage());

	VulkanBuffer destBuffer;
	if (!destBuffer.Initialize(
			this, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		goto Exit;
	}

	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		// Swapchain image (VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) -> copy source (VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.pNext = nullptr;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commandBuffer,
								 VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT,
								 0,
								 0,
								 nullptr,
								 0,
								 nullptr,
								 1,
								 &imageMemoryBarrier);
		}

		// Copy to destBuffer
		{
			VkBufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = {0, 0, 0};
			region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
			vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destBuffer.GetNativeBuffer(), 1, &region);
		}

		// Undo layout
		{
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.pNext = nullptr;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commandBuffer,
								 VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT,
								 0,
								 0,
								 nullptr,
								 0,
								 nullptr,
								 1,
								 &imageMemoryBarrier);
		}

		// Submit and Wait
		if (!EndSingleTimeCommands(commandBuffer))
		{
			goto Exit;
		}
	}

	// Blit
	{
		void* rawData = nullptr;
		vkMapMemory(device, destBuffer.GetNativeBufferMemory(), 0, destBuffer.GetSize(), 0, &rawData);
		result.resize(destBuffer.GetSize());
		memcpy(result.data(), rawData, result.size());
		vkUnmapMemory(device, destBuffer.GetNativeBufferMemory());
	}

Exit:
	destBuffer.Dispose();
	return result;
}

RenderPassPipelineState* GraphicsVulkan::CreateRenderPassPipelineState(RenderPass* renderPass)
{
	assert(renderPass == nullptr);
	auto rpvk = static_cast<RenderPassVulkan*>(renderPass);

	auto ret = rpvk->renderPassPipelineState;
	SafeAddRef(ret);
	return ret;
}

int32_t GraphicsVulkan::GetSwapBufferCount() const { return swapBufferCount_; }

uint32_t GraphicsVulkan::GetMemoryTypeIndex(uint32_t bits, const vk::MemoryPropertyFlags& properties)
{
	return LLGI::GetMemoryTypeIndex(vkPysicalDevice, bits, properties);
}

VkCommandBuffer GraphicsVulkan::BeginSingleTimeCommands()
{
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = static_cast<VkCommandPool>(GetCommandPool());
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(static_cast<VkDevice>(GetDevice()), &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

bool GraphicsVulkan::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	LLGI_VK_CHECK(vkQueueSubmit(static_cast<VkQueue>(vkQueue), 1, &submitInfo, VK_NULL_HANDLE));
	LLGI_VK_CHECK(vkQueueWaitIdle(static_cast<VkQueue>(vkQueue)));

	vkFreeCommandBuffers(static_cast<VkDevice>(GetDevice()), static_cast<VkCommandPool>(GetCommandPool()), 1, &commandBuffer);

	return true;
}

} // namespace LLGI
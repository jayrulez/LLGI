#include "LLGI.VertexBufferVulkan.h"

namespace LLGI
{

bool VertexBufferVulkan::Initialize(GraphicsVulkan* graphics, int32_t size)
{

	SafeAddRef(graphics);
	graphics_ = CreateSharedPtr(graphics);

	cpuBuf = std::unique_ptr<Buffer>(new Buffer(graphics));
	gpuBuf = std::unique_ptr<Buffer>(new Buffer(graphics));

	// create a buffer on cpu
	{
		vk::BufferCreateInfo vertexBufferInfo;
		vertexBufferInfo.size = size;
		vertexBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		cpuBuf->buffer_ = graphics_->GetDevice().createBuffer(vertexBufferInfo);

		vk::MemoryRequirements memReqs = graphics_->GetDevice().getBufferMemoryRequirements(cpuBuf->buffer_);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = graphics_->GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
		cpuBuf->devMem = graphics_->GetDevice().allocateMemory(memAlloc);
		graphics_->GetDevice().bindBufferMemory(cpuBuf->buffer_, cpuBuf->devMem, 0);
	}

	// create a buffer on gpu
	{
		vk::BufferCreateInfo vertexBufferInfo;
		vertexBufferInfo.size = size;
		vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
		gpuBuf->buffer_ = graphics_->GetDevice().createBuffer(vertexBufferInfo);

		vk::MemoryRequirements memReqs = graphics_->GetDevice().getBufferMemoryRequirements(gpuBuf->buffer_);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = graphics_->GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		gpuBuf->devMem = graphics_->GetDevice().allocateMemory(memAlloc);
		graphics_->GetDevice().bindBufferMemory(gpuBuf->buffer_, gpuBuf->devMem, 0);
	}

	memSize = size;

	return true;
}

bool VertexBufferVulkan::InitializeAsShortTime(SingleFrameMemoryPoolVulkan* memoryPool, int32_t size)
{
    throw "Not implemented.";
    return true;
}

VertexBufferVulkan::VertexBufferVulkan() {}

VertexBufferVulkan ::~VertexBufferVulkan() {}

void* VertexBufferVulkan::Lock()
{
	data = graphics_->GetDevice().mapMemory(cpuBuf->devMem, 0, memSize, vk::MemoryMapFlags());
	return data;
}

void* VertexBufferVulkan::Lock(int32_t offset, int32_t size)
{
	data = graphics_->GetDevice().mapMemory(cpuBuf->devMem, offset, size, vk::MemoryMapFlags());
	return data;
}

void VertexBufferVulkan::Unlock() { 
	
	graphics_->GetDevice().unmapMemory(cpuBuf->devMem); 

	// copy buffer
	vk::CommandBufferAllocateInfo cmdBufInfo;
	cmdBufInfo.commandPool = graphics_->GetCommandPool();
	cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
	cmdBufInfo.commandBufferCount = 1;
	vk::CommandBuffer copyCommandBuffer = graphics_->GetDevice().allocateCommandBuffers(cmdBufInfo)[0];

	vk::CommandBufferBeginInfo cmdBufferBeginInfo;

	copyCommandBuffer.begin(cmdBufferBeginInfo);

	vk::BufferCopy copyRegion;
	copyRegion.size = memSize;
	copyCommandBuffer.copyBuffer(cpuBuf->buffer_, gpuBuf->buffer_, copyRegion);
	
	copyCommandBuffer.end();

	// submit and wait to execute command
	std::array<vk::SubmitInfo,1> copySubmitInfos;
	copySubmitInfos[0].commandBufferCount = 1;
	copySubmitInfos[0].pCommandBuffers = &copyCommandBuffer;

	graphics_->GetQueue().submit(static_cast<uint32_t>(copySubmitInfos.size()), copySubmitInfos.data(), vk::Fence());
	graphics_->GetQueue().waitIdle();
	
	graphics_->GetDevice().freeCommandBuffers(graphics_->GetCommandPool(), copyCommandBuffer);
}

int32_t VertexBufferVulkan::GetSize() { return memSize; }

} // namespace LLGI
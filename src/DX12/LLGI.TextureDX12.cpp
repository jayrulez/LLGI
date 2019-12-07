#include "LLGI.TextureDX12.h"
#include "LLGI.CommandListDX12.h"

namespace LLGI
{

TextureDX12::TextureDX12(GraphicsDX12* graphics, bool hasStrongRef) : graphics_(graphics), hasStrongRef_(hasStrongRef)
{
	if (hasStrongRef_)
	{
		SafeAddRef(graphics_);
	}

	assert(graphics_ != nullptr);
	device_ = graphics_->GetDevice();
	commandQueue_ = graphics_->GetCommandQueue();

	SafeAddRef(device_);
	SafeAddRef(commandQueue_);
	memset(&footprint_, 0, sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT));
}

TextureDX12::TextureDX12(ID3D12Resource* textureResource, ID3D12Device* device, ID3D12CommandQueue* commandQueue)
	: texture_(textureResource), commandQueue_(commandQueue), device_(device)
{
	type_ = TextureType::Screen;

	SafeAddRef(texture_);
	SafeAddRef(device_);
	SafeAddRef(commandQueue_);
	memset(&footprint_, 0, sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT));

	auto desc = texture_->GetDesc();
	dxgiFormat_ = desc.Format;

	format_ = ConvertFormat(desc.Format);
	textureSize_ = Vec2I(desc.Width, desc.Height);
	memorySize_ = GetTextureMemorySize(format_, textureSize_);
}

TextureDX12::~TextureDX12()
{
	SafeRelease(texture_);
	SafeRelease(buffer_);

	if (hasStrongRef_)
	{
		SafeRelease(graphics_);
	}
	SafeRelease(device_);
	SafeRelease(commandQueue_);
}

bool TextureDX12::Initialize(const Vec2I& size, TextureType type, const TextureFormatType formatType)
{
	type_ = type;

	if (type_ == TextureType::Depth)
	{
		format_ = TextureFormatType::Uknown;
		dxgiFormat_ = DXGI_FORMAT_D32_FLOAT;
		memorySize_ = size.X * size.Y * 4;
	}
	else
	{
		format_ = formatType;
		dxgiFormat_ = ConvertFormat(formatType);
		textureSize_ = Vec2I(size.X, size.Y);
		memorySize_ = GetTextureMemorySize(format_, textureSize_);
	}

	if (type_ == TextureType::Render)
	{
		texture_ = CreateResourceBuffer(device_,
										D3D12_HEAP_TYPE_DEFAULT,
										dxgiFormat_,
										D3D12_RESOURCE_DIMENSION_TEXTURE2D,
										D3D12_RESOURCE_STATE_GENERIC_READ,
										D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
										size);
		state_ = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	else if (type_ == TextureType::Depth)
	{
		texture_ = CreateResourceBuffer(device_,
										D3D12_HEAP_TYPE_DEFAULT,
										dxgiFormat_,
										D3D12_RESOURCE_DIMENSION_TEXTURE2D,
										D3D12_RESOURCE_STATE_DEPTH_READ,
										D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
										size);
		state_ = D3D12_RESOURCE_STATE_DEPTH_READ;
	}
	else if (type_ == TextureType::Color)
	{
		texture_ = CreateResourceBuffer(device_,
										D3D12_HEAP_TYPE_DEFAULT,
										dxgiFormat_,
										D3D12_RESOURCE_DIMENSION_TEXTURE2D,
										D3D12_RESOURCE_STATE_COPY_DEST,
										D3D12_RESOURCE_FLAG_NONE,
										size);

		state_ = D3D12_RESOURCE_STATE_COPY_DEST;
	}
	textureSize_ = size;
	if (texture_ == nullptr)
		return false;

	// TODO: when it's NOT editable, do NOT call CreateBuffer.
	CreateBuffer();

	return true;
}

void TextureDX12::CreateBuffer()
{
	UINT64 size = 0;
	auto textureDesc = texture_->GetDesc();
	device_->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint_, nullptr, nullptr, &size);

	buffer_ = CreateResourceBuffer(device_,
								   D3D12_HEAP_TYPE_UPLOAD,
								   DXGI_FORMAT_UNKNOWN,
								   D3D12_RESOURCE_DIMENSION_BUFFER,
								   D3D12_RESOURCE_STATE_GENERIC_READ,
								   D3D12_RESOURCE_FLAG_NONE,
								   Vec2I(size, 1));
	assert(buffer_ != nullptr);

	if (footprint_.Footprint.RowPitch != memorySize_ / textureSize_.Y)
	{
		lockedBuffer_.resize(memorySize_);
	}
}

void* TextureDX12::Lock()
{
	if (lockedBuffer_.size() > 0)
	{
		return lockedBuffer_.data();
	}
	else
	{
		void* ptr;
		buffer_->Map(0, nullptr, &ptr);
		return ptr;
	}
}

void TextureDX12::Unlock()
{
	if (lockedBuffer_.size() > 0)
	{
		uint8_t* ptr = nullptr;
		buffer_->Map(0, nullptr, (void**)&ptr);

		for (int32_t i = 0; i < textureSize_.Y; i++)
		{
			auto p = ptr + i * footprint_.Footprint.RowPitch;
			auto rowPitch = memorySize_ / textureSize_.Y;
			memcpy(p, lockedBuffer_.data() + rowPitch * i, rowPitch);
		}

		buffer_->Unmap(0, nullptr);
	}
	else
	{
		buffer_->Unmap(0, nullptr);
	}

	ID3D12CommandAllocator* commandAllocator = nullptr;
	ID3D12GraphicsCommandList* commandList = nullptr;
	ID3D12Fence* fence = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	HANDLE event = CreateEvent(0, 0, 0, 0);

	D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};

	auto hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	if (FAILED(hr))
	{
		goto FAILED_EXIT;
	}

	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL, IID_PPV_ARGS(&commandList));
	if (FAILED(hr))
	{
		goto FAILED_EXIT;
	}

	hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr))
	{
		goto FAILED_EXIT;
	}

	if (event == nullptr)
	{
		goto FAILED_EXIT;
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 1;

	hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
	if (event == nullptr)
	{
		goto FAILED_EXIT;
	}

	src.pResource = buffer_;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint_;

	dst.pResource = texture_;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	ResourceBarrior(commandList, D3D12_RESOURCE_STATE_GENERIC_READ);

	commandList->Close();
	ID3D12CommandList* list[] = {commandList};
	commandQueue->ExecuteCommandLists(1, list);

	// TODO optimize it
	hr = commandQueue->Signal(fence, 1);
	fence->SetEventOnCompletion(1, event);
	WaitForSingleObject(event, INFINITE);

FAILED_EXIT:
	SafeRelease(commandList);
	SafeRelease(commandAllocator);
	SafeRelease(commandQueue);
	SafeRelease(fence);

	if (event != nullptr)
	{
		CloseHandle(event);
	}
}

Vec2I TextureDX12::GetSizeAs2D() const { return textureSize_; }

void TextureDX12::ResourceBarrior(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES state)
{
	if (state_ == state)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture_;
	barrier.Transition.StateBefore = state_;
	barrier.Transition.StateAfter = state;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);
	state_ = state;
}

} // namespace LLGI
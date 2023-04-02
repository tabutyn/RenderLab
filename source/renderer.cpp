#include "renderer.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"

#undef GLFW_EXPOSE_NATIVE_WIN32
#undef TINYGLTF_IMPLEMENTATION
#undef STBI_MSC_SECURE_CRT
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_IMPLEMENTATION

using namespace Microsoft::WRL;
using namespace std::chrono;

Renderer::Renderer(UINT width, UINT height, std::string title) :
	m_width(width),
	m_height(height),
	m_title(title)
{
	m_aspectRatio = static_cast<FLOAT>(width) / static_cast<FLOAT>(height);
	currentFrameTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
	lastFrameTime = currentFrameTime;

	for (UINT n = 0; n < FrameCount; n++) {
		m_renderTargets[n].clearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		m_renderTargets[n].clearValue.Color[0] = 0.0f;
		m_renderTargets[n].clearValue.Color[1] = 0.1f;
		m_renderTargets[n].clearValue.Color[2] = 0.2f;
		m_renderTargets[n].clearValue.Color[3] = 1.0f;
	}

	outputFloatImage = new float_t[width * height * 4];
	outputCharImage = new uint8_t[width * height * 4];

	m_viewport.TopLeftX = (FLOAT)0.0f;
	m_viewport.TopLeftY = (FLOAT)0.0f;
	m_viewport.Width = static_cast<FLOAT>(width);
	m_viewport.Height = static_cast<FLOAT>(height);
	m_viewport.MinDepth = (FLOAT)0.0f;
	m_viewport.MaxDepth = (FLOAT)1.0f;

	m_scissorRect.left = (LONG)0;
	m_scissorRect.top = (LONG)0;
	m_scissorRect.right = (LONG)width;
	m_scissorRect.bottom = (LONG)height;

	CHAR moduleName[512];
	memset(moduleName, 0, _countof(moduleName));
	GetModuleFileNameA(NULL, moduleName, _countof(moduleName));

	CHAR* lastBackslash = strrchr(moduleName, '\\');
	*(lastBackslash + 1) = '\0';
	std::string moduleDir;
	moduleDir.append(moduleName);
	m_vertexShaderPath = moduleDir + "vertexShader.hlsl";
	m_pixelShaderPath = moduleDir + "pixelShader.hlsl";
	m_grayPixelShaderPath = moduleDir + "grayPixelShader.hlsl";

	std::string error;
	std::string warning;
	std::string modelPath = moduleDir + "Cube\\Cube.gltf";

	tinygltf::TinyGLTF gltfContext;
	gltfContext.LoadASCIIFromFile(&m_gltfModel, &error, &warning, modelPath.c_str());
	if (!error.empty()) {
		OutputDebugString(error.c_str());
	}
	if (!warning.empty()) {
		OutputDebugString(warning.c_str());
	}
}

Renderer::~Renderer() {
}

double_t Renderer::GetDeltaTime() {
	lastFrameTime = currentFrameTime;
	currentFrameTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
	return (static_cast<double_t>(currentFrameTime.count()) - static_cast<double_t>(lastFrameTime.count())) / 1000.0;
}

uint64_t Renderer::alignPow2(uint64_t value, uint64_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

void Renderer::Init() {
	UINT dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

	ComPtr<ID3D12Debug> debugController;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		OutputDebugString("-------------------------Failed to create ID3D12Debug Interface\n");
	}
	debugController->EnableDebugLayer();

	if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory))))
	{
		OutputDebugString("-------------------------Failed to create DXGIFactory7\n");
		return;
	}

	ComPtr<IDXGIAdapter1> enumerateAdapter;
	for (UINT adapterIndex = 0;
		SUCCEEDED(m_factory->EnumAdapters1(adapterIndex, &enumerateAdapter));
		adapterIndex++) {
		if (SUCCEEDED(enumerateAdapter->QueryInterface(IID_PPV_ARGS(&m_adapter)))) {
			DXGI_ADAPTER_DESC3 adapterDescriptor;
			m_adapter->GetDesc3(&adapterDescriptor);
			// Atleast 1GB of dedicated VRAM
			if (adapterDescriptor.DedicatedVideoMemory > ((SIZE_T)1 << 30)) {
				break;
			}
		}
	}
	if (FAILED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_device)))) {
		OutputDebugString("-------------------------Failed to create d3d12Device\n");
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureData;
	m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureData, sizeof(featureData));
	if (featureData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		OutputDebugString("--------------------------Failed to create device with ray tracing support\n");
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_directCommandQueue)))) {
		OutputDebugString("-------------------------Failed to create direct d3d12CommandQueue\n");
	}

	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyCommandQueue)))) {
		OutputDebugString("-------------------------Failed to create copy d3d12CommandQueue\n");
	}

	if (FAILED(m_device->CreateFence(m_directFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_directFence)))) {
		OutputDebugString("-------------------------Failed to create direct fence\n");
	}

	if (FAILED(m_device->CreateFence(m_copyFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)))) {
		OutputDebugString("-------------------------Failed to create copy fence\n");
	}

	for (UINT n = 0; n < FrameCount; ++n) {
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_directCommandAllocators[n])))) {
			OutputDebugString("-------------------------Failed to create direct command allocator\n");
		}
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator[n])))) {
			OutputDebugString("-------------------------Failed to create copy command allocator\n");
		}
	}
	if (FAILED(m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_directCommandList)))) {
		OutputDebugString("-------------------------Failed to create direct command list\n");
	}
	if (FAILED(m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_copyCommandList)))) {
		OutputDebugString("-------------------------Failed to create copy command list\n");
	}
	for (UINT n = 0; n < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++n) {
		m_descriptorSizes[n] = m_device->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)n);
	}

	for (UINT n = 0; n < FrameCount; ++n) {
		RenderTarget& renderTarget = m_renderTargets[n];
		ComPtr<ID3D12DescriptorHeap>& rtvDescriptorHeap = m_rtvDescriptorHeaps[n];
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = FrameCount;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)))) {
			OutputDebugString("-------------------------Failed to create rtv descriptor heap.\n");
		}
		renderTarget.rtvDescriptor = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

		ComPtr<ID3D12DescriptorHeap>& dsvDescriptorHeap = m_dsvDescriptorHeaps[n];
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvDescriptorHeap)))) {
			OutputDebugString("-------------------------Failed to create rtv descriptor heap.\n");
		}
		renderTarget.dsvDescriptor = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = m_width;
		resourceDesc.Height = m_height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, &renderTarget.clearValue, IID_PPV_ARGS(&renderTarget.texture));
		m_device->CreateRenderTargetView(renderTarget.texture.Get(), nullptr, renderTarget.rtvDescriptor);

		resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&renderTarget.depthTexture));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		m_device->CreateDepthStencilView(renderTarget.depthTexture.Get(), &dsvDesc, renderTarget.dsvDescriptor);

		dsDesc.DepthEnable = true;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = true;
		dsDesc.StencilReadMask = 0xFF;
		dsDesc.StencilWriteMask = 0xFF;
		dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_INCR;
		dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		dsDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_DECR;
		dsDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		D3D12_RESOURCE_DESC srcTextureDesc = renderTarget.texture->GetDesc();
		m_device->GetCopyableFootprints(&srcTextureDesc, 0, 1, 0, &renderTarget.footprint, &renderTarget.rowCount, &renderTarget.rowSize, &renderTarget.size);

		heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = renderTarget.size;
		resourceDesc.Height = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&renderTarget.dest));


		renderTarget.srcCopyLocation.pResource = renderTarget.texture.Get();
		renderTarget.srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		renderTarget.srcCopyLocation.SubresourceIndex = 0;

		renderTarget.dstCopyLocation.pResource = renderTarget.dest.Get();
		renderTarget.dstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		renderTarget.dstCopyLocation.PlacedFootprint = renderTarget.footprint;
	}

	if (FAILED(m_copyCommandList->Reset(m_copyCommandAllocator[0].Get(), nullptr))) {
		OutputDebugString("-------------------------Failed to reset copy command list\n");
	}

	std::vector<ComPtr<ID3D12Resource> > stagingResources;
	stagingResources.reserve(256);
	for (auto& gltfBuffer : m_gltfModel.buffers) {
		ComPtr<ID3D12Resource> dstBuffer;

		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = gltfBuffer.data.size();
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&dstBuffer)))) {
			OutputDebugString("-------------------------Failed to create destination buffer\n");
		}
		m_buffers.push_back(dstBuffer);

		ComPtr<ID3D12Resource> srcBuffer;
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&srcBuffer)))) {
			OutputDebugString("-------------------------Failed to create source buffer\n");
		}
		stagingResources.push_back(srcBuffer);

		void* data;
		if (FAILED(srcBuffer->Map(0, nullptr, &data))) {
			OutputDebugString("-------------------------Failed to map source buffer\n");
		}
		memcpy(data, &gltfBuffer.data[0], gltfBuffer.data.size());
		m_copyCommandList->CopyBufferRegion(dstBuffer.Get(), 0, srcBuffer.Get(), 0, gltfBuffer.data.size());
	}

	for (tinygltf::Image& gltfImage : m_gltfModel.images) {
		ComPtr<ID3D12Resource> dstTexture;

		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = gltfImage.width;
		resourceDesc.Height = gltfImage.height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&dstTexture)))) {
			OutputDebugString("-------------------------Failed to create destination image\n");
		}
		m_textures.push_back(dstTexture);

		D3D12_RESOURCE_DESC dstTextureDesc = dstTexture->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT rowCount;
		UINT64 rowSize;
		UINT64 size;
		m_device->GetCopyableFootprints(&dstTextureDesc, 0, 1, 0, &footprint, &rowCount, &rowSize, &size);

		ComPtr<ID3D12Resource> srcBuffer;
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = size;
		resourceDesc.Height = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if(FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&srcBuffer)))){
			OutputDebugString("-------------------------Failed to create source image buffer\n");
		}
		stagingResources.push_back(srcBuffer);

		void* data;
		if (FAILED(srcBuffer->Map(0, nullptr, &data))) {
			OutputDebugString("-------------------------Failed to map source image buffer\n");
		}
		for (UINT rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
			memcpy(static_cast<uint8_t*>(data) + rowSize * rowIndex, &gltfImage.image[0] + gltfImage.width * gltfImage.component * rowIndex, gltfImage.width * gltfImage.component);
		}
		D3D12_TEXTURE_COPY_LOCATION dstCopyLocation = {};
		dstCopyLocation.pResource = dstTexture.Get();
		dstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstCopyLocation.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION srcCopyLocation = {};
		srcCopyLocation.pResource = srcBuffer.Get();
		srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcCopyLocation.PlacedFootprint = footprint;

		m_copyCommandList->CopyTextureRegion(&dstCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
	}

	if (FAILED(m_copyCommandList->Close())) {
		OutputDebugString("-------------------------Failed to close copy command list\n");
	}

	ID3D12CommandList* copyCommandLists[] = {m_copyCommandList.Get()};
	m_copyCommandQueue->ExecuteCommandLists(_countof(copyCommandLists), copyCommandLists);
	m_copyCommandQueue->Signal(m_copyFence.Get(), ++m_copyFenceValue);

	for (tinygltf::Sampler& gltfSampler : m_gltfModel.samplers) {
		D3D12_SAMPLER_DESC samplerDesc = {};
		switch (gltfSampler.minFilter) {
		case TINYGLTF_TEXTURE_FILTER_NEAREST:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			break;
		case TINYGLTF_TEXTURE_FILTER_LINEAR:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			break;
		case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
			break;
		case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			break;
		case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
			break;
		case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
			if (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
				samplerDesc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
			else
				samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			break;
		default:
			samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			break;
		}

		auto getTextureAddress = [](int wrap) {
			switch (wrap) {
			case TINYGLTF_TEXTURE_WRAP_REPEAT:
				return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
				return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
				return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			default:
				OutputDebugString("-------------------------Invalide wrap mode in gltf file\n");
				return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			}
		};

		samplerDesc.AddressU = getTextureAddress(gltfSampler.wrapS);
		samplerDesc.AddressV = getTextureAddress(gltfSampler.wrapT);
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MaxLOD = 256;

		m_samplerDescs.push_back(samplerDesc);
	}

	for (tinygltf::Material gltfMaterial : m_gltfModel.materials) {
		Material material = {};
		material.name = gltfMaterial.name;

		D3D12_BLEND_DESC& blendDesc = material.blendDesc;
		if (gltfMaterial.alphaMode == "BLEND") {
			blendDesc.RenderTarget[0].BlendEnable = true;
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		}
		else if (gltfMaterial.alphaMode == "MASK") {
			OutputDebugString("--------------------- MASK alpha mode is not support but was specified in the gltf file\n");
		}

		blendDesc.RenderTarget[0].RenderTargetWriteMask =
			D3D12_COLOR_WRITE_ENABLE_ALL;

		D3D12_RASTERIZER_DESC& rasterizerDesc = material.rasterizerDesc;
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
//		if (gltfMaterial.doubleSided) {
	//	}
		//else {
			//rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		//}

		rasterizerDesc.FrontCounterClockwise = true;
		rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;
		rasterizerDesc.ForcedSampleCount = 0;
		rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		auto& buffer = material.buffer;
		auto& bufferData = material.bufferData;

		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = alignPow2(sizeof(PBRMetallicRoughness), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)))) {
			OutputDebugString("---------------------Failed to create commited resource for pbr descriptor.\n");
		}
		if (FAILED(buffer->Map(0, nullptr, &bufferData))) {
			OutputDebugString("---------------------Failed to map pbr buffer descriptor\n");
		};

		auto& SRVDescriptorHeap = material.SRVDescriptorHeap;
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptorHeapDesc.NumDescriptors = 5;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&SRVDescriptorHeap)))) {
			OutputDebugString("---------------------Failed to crreate descriptor heap for pbr material.\n");
		}

		auto& samplerDescriptorHeap = material.samplerDescriptorHeap;
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		if (FAILED(m_device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&samplerDescriptorHeap)))) {
			OutputDebugString("---------------------Failed to crreate sampler heap for pbr material.\n");
		}

		auto& gltfPBRMetallicRoughness = gltfMaterial.pbrMetallicRoughness;
		auto PBRMetallicRoughness = static_cast<Renderer::PBRMetallicRoughness*>(bufferData);

		auto& baseColorFactor = PBRMetallicRoughness->baseColorFactor;
		baseColorFactor.x = static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[0]);
		baseColorFactor.y = static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[1]);
		baseColorFactor.z = static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[2]);
		baseColorFactor.w = static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[3]);

		auto& gltfBaseColorTexture = gltfPBRMetallicRoughness.baseColorTexture;
		auto& baseColorTexture = PBRMetallicRoughness->baseColorTexture;
		if (gltfBaseColorTexture.index >= 0) {
			baseColorTexture.textureIndex = 0;
			baseColorTexture.samplerIndex = 0;
		}
		else {
			baseColorTexture.textureIndex = -1;
			baseColorTexture.samplerIndex = -1;
		}
		PBRMetallicRoughness->metallicFactor =
			static_cast<float>(gltfPBRMetallicRoughness.metallicFactor);
		PBRMetallicRoughness->roughnessFactor =
			static_cast<float>(gltfPBRMetallicRoughness.roughnessFactor);

		auto& gltfMetallicRoughnessTexture = gltfPBRMetallicRoughness.metallicRoughnessTexture;
		auto& metallicRoughnessTexture = PBRMetallicRoughness->metallicRoughnessTexture;
		if (gltfMetallicRoughnessTexture.index >= 0) {
			metallicRoughnessTexture.textureIndex = 1;
			metallicRoughnessTexture.samplerIndex = 1;
		}
		else {
			metallicRoughnessTexture.textureIndex = -1;
			metallicRoughnessTexture.samplerIndex = -1;
		}
		auto srvDescriptor = SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		auto samplerDescriptor = samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		if (gltfBaseColorTexture.index >= 0) {
			auto& gltfTexture = m_gltfModel.textures[gltfBaseColorTexture.index];
			auto texture = m_textures[gltfTexture.source].Get();
			m_device->CreateShaderResourceView(texture, nullptr, srvDescriptor);
			auto& samplerDesc = m_samplerDescs[gltfTexture.sampler];
			m_device->CreateSampler(&samplerDesc, samplerDescriptor);
		}
		srvDescriptor.ptr +=
			m_descriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
		samplerDescriptor.ptr +=
			m_descriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER];
		auto& glTFMetallicRoughnessTexture = gltfPBRMetallicRoughness.metallicRoughnessTexture;
		if (gltfMetallicRoughnessTexture.index >= 0) {
			auto& gltfTexture = m_gltfModel.textures[gltfMetallicRoughnessTexture.index];
			auto texture = m_textures[gltfTexture.source].Get();
			m_device->CreateShaderResourceView(texture, nullptr, srvDescriptor);

			auto& samplerDesc = m_samplerDescs[gltfTexture.sampler];
			m_device->CreateSampler(&samplerDesc, samplerDescriptor);
		}
		m_materials.push_back(material);
	}

	for (auto& gltfMesh : m_gltfModel.meshes) {
		Mesh mesh = {};
		mesh.name = gltfMesh.name;

		auto& primitives = mesh.primitives;
		for (auto& gltfPrimitive : gltfMesh.primitives) {
			Primitive primitive = {};
			auto& attributes = primitive.attributes;
			for (auto& [attributeName, accessorIndex] : gltfPrimitive.attributes) {
				const auto& gltfAccessor = m_gltfModel.accessors[accessorIndex];
				const auto& gltfBufferView = m_gltfModel.bufferViews[gltfAccessor.bufferView];

				Attribute attribute = {};
				attribute.name = attributeName;
				switch (gltfAccessor.type) {
				case TINYGLTF_TYPE_VEC2:
					attribute.format = DXGI_FORMAT_R32G32_FLOAT;
					break;
				case TINYGLTF_TYPE_VEC3:
					attribute.format = DXGI_FORMAT_R32G32B32_FLOAT;
					break;
				case TINYGLTF_TYPE_VEC4:
					attribute.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					break;
				}
				attribute.vertexBufferView.BufferLocation = m_buffers[gltfBufferView.buffer]->GetGPUVirtualAddress() + gltfBufferView.byteOffset + gltfAccessor.byteOffset;
				attribute.vertexBufferView.SizeInBytes = static_cast<UINT>(gltfBufferView.byteLength - gltfAccessor.byteOffset);
				attribute.vertexBufferView.StrideInBytes = gltfAccessor.ByteStride(gltfBufferView);
				attributes.emplace_back(attribute);

				if (attributeName == "POSITION") {
					primitive.vertexCount = static_cast<uint32_t>(gltfAccessor.count);
				}
			}

			auto& primitiveTopology = primitive.primitiveTopology;
			switch (gltfPrimitive.mode) {
			case TINYGLTF_MODE_POINTS:
				primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
				break;
			case TINYGLTF_MODE_LINE:
				primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
				break;
			case TINYGLTF_MODE_LINE_STRIP:
				primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
				break;
			case TINYGLTF_MODE_TRIANGLES:
				primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				break;
			case TINYGLTF_MODE_TRIANGLE_STRIP:
				primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
				break;
			default:
				assert(false);
			}

			if (gltfPrimitive.indices >= 0) {
				const auto& gltfAccessor = m_gltfModel.accessors[gltfPrimitive.indices];
				const auto& gltfBufferView = m_gltfModel.bufferViews[gltfAccessor.bufferView];

				auto& indexBufferView = primitive.indexBufferView;
				indexBufferView.BufferLocation = m_buffers[gltfBufferView.buffer]->GetGPUVirtualAddress() + gltfBufferView.byteOffset + gltfAccessor.byteOffset;
				indexBufferView.SizeInBytes = static_cast<UINT>(gltfBufferView.byteLength - gltfAccessor.byteOffset);
				switch (gltfAccessor.componentType) {
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					indexBufferView.Format = DXGI_FORMAT_R8_UINT;
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					indexBufferView.Format = DXGI_FORMAT_R16_UINT;
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					indexBufferView.Format = DXGI_FORMAT_R32_UINT;
					break;
				}
				auto& indexCount = primitive.indexCount;
				indexCount = static_cast<uint32_t>(gltfAccessor.count);
			}

			auto buildDefines = [](const std::vector<Attribute>& attributes) {
				std::vector<D3D_SHADER_MACRO> defines;
				for (auto& attribute : attributes) {
					if (attribute.name == "NORMAL")
						defines.push_back({ "HAS_NORMAL", "1" });
					else if (attribute.name == "TANGENT")
						defines.push_back({ "HAS_TANGENT", "1" });
					else if (attribute.name == "TEXCOORD_0")
						defines.push_back({ "HAS_TEXCOORD_0", "1" });
				}
				defines.push_back({ nullptr, nullptr });

				return defines;
			};
			auto compileShaderFromFile = [](LPCWSTR filePath, D3D_SHADER_MACRO* defines, LPCSTR target, ID3DBlob** shader) {
				UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
				ComPtr<ID3DBlob> error;
				if (FAILED(D3DCompileFromFile(filePath, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", target, flags, 0, shader, &error))) {
					OutputDebugString("------------------------------Failed to compile shader\n");
					OutputDebugString(static_cast<char*>(error->GetBufferPointer()));
				}
			};
			auto createRootSignature = [this](D3D12_ROOT_SIGNATURE_DESC* rootSignatureDesc, ID3D12RootSignature** rootSignature) {
				ComPtr<ID3DBlob> serializeRootSignature;
				ComPtr<ID3DBlob> error;
				if (FAILED(D3D12SerializeRootSignature(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializeRootSignature.GetAddressOf(), error.GetAddressOf()))) {
					OutputDebugString("------------------------------Failed to serialize Root Signiture\n");
					OutputDebugString(static_cast<char*>(error->GetBufferPointer()));
				}
				if(FAILED(m_device->CreateRootSignature(0, serializeRootSignature->GetBufferPointer(), serializeRootSignature->GetBufferSize(), IID_PPV_ARGS(rootSignature)))){
					OutputDebugString("------------------------------Failed to create root signature\n");
				}
			};
			auto buildInputElementDescs = [](const std::vector<Attribute>& attributes) {
				std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
				for (auto& attribute : attributes) {
					D3D12_INPUT_ELEMENT_DESC inputElementDesc = {};
					inputElementDesc.SemanticName = &attribute.name[0];
					inputElementDesc.Format = attribute.format;
					if (attribute.name == "TEXCOORD_0") {
						inputElementDesc.SemanticName = "TEXCOORD_";
						inputElementDesc.SemanticIndex = 0;
					}
					inputElementDesc.InputSlot =
						static_cast<UINT>(inputElementDescs.size());
					inputElementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
					inputElementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
					inputElementDescs.push_back(inputElementDesc);
				}
				return inputElementDescs;
			};

			if (gltfPrimitive.material >= 0) {
				primitive.material = &m_materials[gltfPrimitive.material];

				auto& rootSignature = primitive.rootSignature;

				D3D12_DESCRIPTOR_RANGE SRVDescriptorRange = {};
				SRVDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				SRVDescriptorRange.NumDescriptors = 5;

				D3D12_DESCRIPTOR_RANGE samplerDescriptorRange = {};
				samplerDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
				samplerDescriptorRange.NumDescriptors = 5;

				D3D12_ROOT_PARAMETER rootParams[5] = {};
				rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				rootParams[0].Descriptor = { 0, 0 };
				rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
				rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
				rootParams[1].Descriptor = { 1, 0 };
				rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
				rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				rootParams[2].Descriptor = { 2, 0 };
				rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
				rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				rootParams[3].DescriptorTable = { 1, &SRVDescriptorRange };
				rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
				rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				rootParams[4].DescriptorTable = { 1, &samplerDescriptorRange };
				rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

				D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
				rootSignatureDesc.NumParameters = _countof(rootParams);
				rootSignatureDesc.pParameters = &rootParams[0];
				rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
				createRootSignature(&rootSignatureDesc, &rootSignature);

				auto defines = buildDefines(attributes);

				ComPtr<ID3DBlob> vertexShader;
				std::wstring vertexShaderPath(m_vertexShaderPath.begin(), m_vertexShaderPath.end());
				ComPtr<ID3DBlob> pixelShader;
				std::wstring pixelShaderPath(m_pixelShaderPath.begin(), m_pixelShaderPath.end());

				compileShaderFromFile(vertexShaderPath.c_str(), &defines[0], "vs_5_1", &vertexShader);
				compileShaderFromFile(pixelShaderPath.c_str(), &defines[0], "ps_5_1", &pixelShader);
				auto inputElementDescs = buildInputElementDescs(attributes);

				D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
				pipelineStateDesc.pRootSignature = rootSignature.Get();
				pipelineStateDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
				pipelineStateDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
				pipelineStateDesc.BlendState = primitive.material->blendDesc;
				pipelineStateDesc.SampleMask = UINT_MAX;
				pipelineStateDesc.RasterizerState = primitive.material->rasterizerDesc;
				pipelineStateDesc.DepthStencilState = dsDesc;
				pipelineStateDesc.InputLayout = { inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size()) };
				switch (primitive.primitiveTopology) {
				case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
					break;
				case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
				case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
					break;
				case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
				case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					break;
				default:
					OutputDebugString("-------------------------Unsupported primitiveTopology\n");
				}
				pipelineStateDesc.NumRenderTargets = 1;
				pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
				pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
				pipelineStateDesc.SampleDesc = { 1, 0 };
				auto& pipelineState = primitive.pipelineState;
				if (FAILED(m_device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState)))) {
					OutputDebugString("---------------------------Failed to create pipelineState\n");
				}
			}
			else {
				auto& rootSignature = primitive.rootSignature;
				D3D12_ROOT_PARAMETER rootParams[2] = {};
				rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				rootParams[0].Descriptor = { 0, 0 };
				rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
				rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				rootParams[1].Descriptor = { 1, 0 };
				rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

				D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
				rootSignatureDesc.NumParameters = _countof(rootParams);
				rootSignatureDesc.pParameters = &rootParams[0];
				rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
				createRootSignature(&rootSignatureDesc, &rootSignature);
				auto defines = buildDefines(attributes);
				ComPtr<ID3DBlob> vertexShader;
				ComPtr<ID3DBlob> pixelShader;
				std::wstring vertexShaderPath(m_vertexShaderPath.begin(), m_vertexShaderPath.end());
				std::wstring grayPixelShaderPath(m_grayPixelShaderPath.begin(), m_grayPixelShaderPath.end());
				compileShaderFromFile(vertexShaderPath.c_str(), &defines[0], "vs_5_1", &vertexShader);
				compileShaderFromFile(grayPixelShaderPath.c_str(), &defines[0], "vs_5_1", &pixelShader);

				auto inputElementDescs = buildInputElementDescs(attributes);
				D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
				pipelineStateDesc.pRootSignature = rootSignature.Get();
				pipelineStateDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
				pipelineStateDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
				pipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
				pipelineStateDesc.SampleMask = UINT_MAX;
				pipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
				pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
				pipelineStateDesc.RasterizerState.FrontCounterClockwise = true;
				pipelineStateDesc.DepthStencilState = dsDesc;
				pipelineStateDesc.InputLayout = { inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size()) };
				switch (primitive.primitiveTopology) {
				case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
					break;
				case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
				case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
					break;
				case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
				case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
					pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					break;
				default:
					OutputDebugString("-------------------------Unsupported primitiveTopology\n");
				}
				pipelineStateDesc.NumRenderTargets = 1;
				pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UINT;
				pipelineStateDesc.SampleDesc = { 1, 0 };

				auto& pipelineState = primitive.pipelineState;
				if (FAILED(m_device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState)))) {
					OutputDebugString("-------------------------Failed to create graphics pipeline state\n");
				}
			}
			primitives.push_back(primitive);
		}
		m_meshes.push_back(mesh);
	}

	for (auto& gltfNode : m_gltfModel.nodes) {
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = alignPow2(sizeof(PBRMetallicRoughness), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> buffer;
		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)))) {
			OutputDebugString("---------------------------------Failed to create node buffer\n");
		}

		void* data;
		if (FAILED(buffer->Map(0, nullptr, &data))) {
			OutputDebugString("---------------------------------Failed to map buffer\n");
		}
		if (gltfNode.matrix.empty()) {
			XMStoreFloat4x4(static_cast<DirectX::XMFLOAT4X4*>(data),
				XMMatrixIdentity());
		}
		else {
			float* element = static_cast<float*>(data);
			for (auto value : gltfNode.matrix) {
				*element = static_cast<float>(value);
				++element;
			}
		}
		m_nodeBuffers.push_back(buffer);
	}
	if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
		HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		m_copyFence->SetEventOnCompletion(m_copyFenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = alignPow2(sizeof(PBRMetallicRoughness), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc = { 1, 0 };
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cameraBuffer));

	//todo: raytracing
	//todo: output depth
	//todo: consume lightfield config file
}

void Renderer::Update(double_t deltaTime) {
	void* data;
	m_cameraBuffer->Map(0, nullptr, &data);

	auto* cameraData = static_cast<Camera*>(data);

	constexpr auto kRadius = 3.0;
	static auto degree = 0.0;
	degree += 10.0 * deltaTime;
	auto radian = degree * XM_PI / 180.0;

	XMMATRIX P = XMMatrixPerspectiveFovRH(90.0f * XM_PI / 180.0f, m_aspectRatio, 0.01f, 100.0f);
	XMStoreFloat4x4(&cameraData->P, XMMatrixTranspose(P));

	XMMATRIX V = XMMatrixLookAtRH(
		XMVectorSet(static_cast<float>(kRadius * cos(radian)), 0.0f, static_cast<float>(kRadius * sin(radian)), 1.0f),
		XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&cameraData->V, XMMatrixTranspose(V));

	XMMATRIX VP = XMMatrixMultiply(V, P);
	XMStoreFloat4x4(&cameraData->VP, XMMatrixTranspose(VP));
	m_cameraBuffer->Unmap(0, nullptr);
}


void Renderer::DrawNode(uint64_t nodeIndex) {
	const auto& gltfNode = m_gltfModel.nodes[nodeIndex];

	if (gltfNode.mesh >= 0) {
		const auto& mesh = m_meshes[gltfNode.mesh];

		for (auto& primitive : mesh.primitives) {
			m_directCommandList->SetGraphicsRootSignature(primitive.rootSignature.Get());
			m_directCommandList->SetPipelineState(primitive.pipelineState.Get());
			m_directCommandList->IASetPrimitiveTopology(primitive.primitiveTopology);

			for (auto i = 0; i != primitive.attributes.size(); ++i) {
				m_directCommandList->IASetVertexBuffers(i, 1, &primitive.attributes[i].vertexBufferView);
			}

			ID3D12DescriptorHeap* descriptorHeaps[] = { primitive.material->SRVDescriptorHeap.Get(), primitive.material->samplerDescriptorHeap.Get() };
			m_directCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			m_directCommandList->SetGraphicsRootConstantBufferView(0, m_cameraBuffer->GetGPUVirtualAddress());
			m_directCommandList->SetGraphicsRootConstantBufferView(1, m_nodeBuffers[nodeIndex]->GetGPUVirtualAddress());
			m_directCommandList->SetGraphicsRootConstantBufferView(2, primitive.material->buffer->GetGPUVirtualAddress());
			m_directCommandList->SetGraphicsRootDescriptorTable(3, primitive.material->SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			m_directCommandList->SetGraphicsRootDescriptorTable(4, primitive.material->samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

			if (primitive.indexCount) {
				m_directCommandList->IASetIndexBuffer(&primitive.indexBufferView);
				m_directCommandList->DrawIndexedInstanced(primitive.indexCount, 1, 0, 0, 0);
			}
			else {
				m_directCommandList->DrawInstanced(primitive.vertexCount, 1, 0, 0);
			}
		}
	}

	for (auto childNodeIndex : gltfNode.children) {
		DrawNode(childNodeIndex);
	}
}

void Renderer::Render() {
	fIndex = (fIndex + 1) % FrameCount;
	auto directCommandAllocator = m_directCommandAllocators[fIndex].Get();
	auto copyCommandAllocator = m_copyCommandAllocator[fIndex].Get();
	directCommandAllocator->Reset();
	copyCommandAllocator->Reset();
	m_directCommandList->Reset(directCommandAllocator, nullptr);
	m_copyCommandList->Reset(copyCommandAllocator, nullptr);
	m_directCommandList->RSSetViewports(1, &m_viewport);
	m_directCommandList->RSSetScissorRects(1, &m_scissorRect);

	auto& renderTarget = m_renderTargets[fIndex];
	auto texture = renderTarget.texture.Get();
	auto dest = renderTarget.dest.Get();
	auto footprint = renderTarget.footprint;
	auto rtvDescriptor = renderTarget.rtvDescriptor;
	auto dsvDescriptor = renderTarget.dsvDescriptor;

	D3D12_RESOURCE_BARRIER resourceBarrier = {};
	resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	resourceBarrier.Transition.pResource = texture;
	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_directCommandList->ResourceBarrier(1, &resourceBarrier);

	m_directCommandList->OMSetRenderTargets(1, &rtvDescriptor, false, &dsvDescriptor);
	m_directCommandList->ClearDepthStencilView(renderTarget.dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	m_directCommandList->ClearRenderTargetView(rtvDescriptor, renderTarget.clearValue.Color, 0, nullptr);


	auto& scene = m_gltfModel.scenes[m_gltfModel.defaultScene];
	for (auto nodeIndex : scene.nodes) {
		DrawNode(nodeIndex);
	}

	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	m_directCommandList->ResourceBarrier(1, &resourceBarrier);
	m_directCommandList->Close();

	ID3D12CommandList* commandLists[] = {m_directCommandList.Get()};
	m_directCommandQueue->ExecuteCommandLists(1, commandLists);
	m_directCommandQueue->Signal(m_directFence.Get(), ++m_directFenceValue);

	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	m_copyCommandList->ResourceBarrier(1, &resourceBarrier);

	m_copyCommandList->CopyTextureRegion(&renderTarget.dstCopyLocation, 0, 0, 0, &renderTarget.srcCopyLocation, nullptr);

	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	m_copyCommandList->ResourceBarrier(1, &resourceBarrier);
	m_copyCommandList->Close();

	ID3D12CommandList* copyCommandLists[] = { m_copyCommandList.Get() };

	if (m_directFence->GetCompletedValue() < m_directFenceValue) {
		auto event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		m_directFence->SetEventOnCompletion(m_directFenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	m_copyCommandQueue->ExecuteCommandLists(1, copyCommandLists);
	m_copyCommandQueue->Signal(m_copyFence.Get(), ++m_copyFenceValue);
	if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
		auto event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		m_copyFence->SetEventOnCompletion(m_copyFenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}

	void* data;
	if (FAILED(dest->Map(0, nullptr, &data))) {
		OutputDebugString("-------------------------Failed to map dest image buffer\n");
	}

	for (UINT rowIndex = 0; rowIndex < renderTarget.rowCount; ++rowIndex) {
		memcpy(reinterpret_cast<uint8_t*>(outputFloatImage) + rowIndex * m_width * 16, static_cast<uint8_t*>(data) + rowIndex * renderTarget.footprint.Footprint.RowPitch, m_width * 16);
	}
	for (UINT i = 0; i < m_width * m_height * 4; ++i) {
		outputCharImage[i] = (uint8_t)(outputFloatImage[i]*255.0f);
	}
	std::string formated_name = std::format("output\\output{}.png", fCounter);
	stbi_write_png(formated_name.c_str(), m_width, m_height, 4, outputCharImage, m_width * 4);
	OutputDebugString("-----------------------------------wrote image ");
	OutputDebugString(formated_name.c_str());
	OutputDebugString("\n");
	fCounter++;
}

void Renderer::Destroy() {
}

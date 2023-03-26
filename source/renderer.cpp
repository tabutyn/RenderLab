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

Renderer::Renderer(UINT width, UINT height, std::wstring title, HINSTANCE hInstance) :
	m_width(width),
	m_height(height),
	m_title(title),
	m_hInstance(hInstance)
{
	m_aspectRatio = static_cast<FLOAT>(width) / static_cast<FLOAT>(height);
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

	WCHAR moduleName[512];
	memset(moduleName, 0, _countof(moduleName));
	GetModuleFileNameW(NULL, moduleName, _countof(moduleName));

	WCHAR* lastBackslash = wcsrchr(moduleName, L'\\');
	*(lastBackslash + 1) = '\0';
	m_moduleDir.append(moduleName);
	m_shaderPath = m_moduleDir + L"shaders.hlsl";

	std::string error;
	std::string warning;
	m_modelPath = m_moduleDir + L"Cube\\Cube.gltf";
	std::string modelPath(m_modelPath.begin(), m_modelPath.end());

	gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, modelPath.c_str());
	if (!error.empty()) {
		OutputDebugString(error.c_str());
	}
	if (!warning.empty()) {
		OutputDebugString(warning.c_str());
	}

}

Renderer::~Renderer() {
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
			UINT tom = (SIZE_T)1 << 30;
			// Atleast 1GB of dedicated VRAM
			if (adapterDescriptor.DedicatedVideoMemory > ((SIZE_T)1 << 30)) {
				break;
			}
		}
	}
	if (FAILED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_device)))) {
		OutputDebugString("-------------------------Failed to create d3d12Device\n");
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

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = 0;

	ComPtr<IDXGISwapChain1> swapChain;
	if (FAILED(m_factory->CreateSwapChainForHwnd(m_directCommandQueue.Get(), m_hWnd, &swapChainDesc, nullptr, nullptr, &swapChain))) {
		OutputDebugString("-------------------------Failed to create swap chain\n");
	}
	if (FAILED(swapChain.As(&m_swapChain))) {
		OutputDebugString("-------------------------Failed to cast IDXGISwapChain1 to IDXGISwapChain3\n");
	}
	m_factory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER);

	for (UINT n = 0; n < FrameCount; ++n) {
		if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_swapChainBuffers[n])))) {
			OutputDebugString("-------------------------Failed to get swapChain buffer\n");
		}
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_directCommandAllocators[n])))) {
			OutputDebugString("-------------------------Failed to create direct command allocator\n");
		}
	}
	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator)))) {
		OutputDebugString("-------------------------Failed to create copy command allocator\n");
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
		ComPtr<ID3D12DescriptorHeap>& rtvDescriptorHeap = m_rtvDescriptorHeaps[n];
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)))) {
			OutputDebugString("-------------------------Failed to create rtv descriptor heap.\n");
		}
		RenderTarget& renderTarget = m_renderTargets[n];
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		renderTarget.texture = m_swapChainBuffers[n];
		renderTarget.viewDescriptor = rtvDescriptor;
		m_device->CreateRenderTargetView(m_swapChainBuffers[n].Get(), nullptr, rtvDescriptor);
	}

	if (FAILED(m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr))) {
		OutputDebugString("-------------------------Failed to reset copy command list\n");
	}

	std::vector<ComPtr<ID3D12Resource> > stagingResources;
	stagingResources.reserve(256);


	for (auto& gltfBuffer : gltfModel.buffers) {
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
		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&dstBuffer)))) {
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

	for (tinygltf::Image& gltfImage : gltfModel.images) {
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

		if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&dstTexture)))) {
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

	for (tinygltf::Sampler& gltfSampler : gltfModel.samplers) {
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

	for (tinygltf::Material gltfMaterial : gltfModel.materials) {
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

		if (gltfMaterial.doubleSided) {
			rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
		}
		else {
			rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		}

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
		auto PBRMetallicRoughness =
			static_cast<Renderer::PBRMetallicRoughness*>(bufferData);

		auto& baseColorFactor = PBRMetallicRoughness->baseColorFactor;

		baseColorFactor.x =
			static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[0]);
		baseColorFactor.y =
			static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[1]);
		baseColorFactor.z =
			static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[2]);
		baseColorFactor.w =
			static_cast<float>(gltfPBRMetallicRoughness.baseColorFactor[3]);

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
			auto& gltfTexture = gltfModel.textures[gltfBaseColorTexture.index];
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
			auto& gltfTexture = gltfModel.textures[gltfMetallicRoughnessTexture.index];
			auto texture = m_textures[gltfTexture.source].Get();
			m_device->CreateShaderResourceView(texture, nullptr, srvDescriptor);

			auto& samplerDesc = m_samplerDescs[gltfTexture.sampler];
			m_device->CreateSampler(&samplerDesc, samplerDescriptor);
		}
		m_materials.push_back(material);
	}

	if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
		HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		m_copyFence->SetEventOnCompletion(m_copyFenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionsedRootSignitureDesc = {};
	versionsedRootSignitureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versionsedRootSignitureDesc.Desc_1_1.NumParameters = 0;
	versionsedRootSignitureDesc.Desc_1_1.pParameters = nullptr;
	versionsedRootSignitureDesc.Desc_1_1.NumStaticSamplers = 0;
	versionsedRootSignitureDesc.Desc_1_1.pStaticSamplers = nullptr;
	versionsedRootSignitureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeVersionedRootSignature(&versionsedRootSignitureDesc, &signature, &error))) {
		OutputDebugString("-------------------------Failed to serailize versioned root signature\n");
	}

	if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)))) {
		OutputDebugString("-------------------------Failed to create root signature\n");
	}

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

	ComPtr<ID3DBlob> errorMessage;
	if (FAILED(D3DCompileFromFile(m_shaderPath.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errorMessage))) {
		OutputDebugString("------------------------Failed to compile vertex shader\n");
		char* errorString = static_cast<char*>(errorMessage->GetBufferPointer());
		OutputDebugString(errorString);
	}

	if (FAILED(D3DCompileFromFile(m_shaderPath.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errorMessage))) {
		OutputDebugString("------------------------Failed to compile pixel shader\n");
		char* errorString = static_cast<char*>(errorMessage->GetBufferPointer());
		OutputDebugString(errorString);
	}

	D3D12_SHADER_BYTECODE vsByteCode = {};
	vsByteCode.pShaderBytecode = vertexShader->GetBufferPointer();
	vsByteCode.BytecodeLength = vertexShader->GetBufferSize();

	D3D12_SHADER_BYTECODE psByteCode = {};
	psByteCode.pShaderBytecode = pixelShader->GetBufferPointer();
	psByteCode.BytecodeLength = pixelShader->GetBufferSize();

	D3D12_SHADER_BYTECODE nullBytecode = {};
	nullBytecode.pShaderBytecode = nullptr;
	nullBytecode.BytecodeLength = 0;

	D3D12_STREAM_OUTPUT_DESC nullStreamOutputDesc = {};
	nullStreamOutputDesc.pSODeclaration = nullptr;
	nullStreamOutputDesc.NumEntries = 0;
	nullStreamOutputDesc.pBufferStrides = nullptr;
	nullStreamOutputDesc.NumStrides = 0;
	nullStreamOutputDesc.RasterizedStream = 0;

	D3D12_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = TRUE;
	rasterizerDesc.MultisampleEnable = FALSE;
	rasterizerDesc.AntialiasedLineEnable = FALSE;
	rasterizerDesc.ForcedSampleCount = 0;
	rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	D3D12_DEPTH_STENCILOP_DESC depthStencilopDesc = {};
	depthStencilopDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilopDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	depthStencilopDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	depthStencilopDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	depthStencilDesc.StencilEnable = FALSE;
	depthStencilDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	depthStencilDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	depthStencilDesc.FrontFace = depthStencilopDesc;
	depthStencilDesc.BackFace = depthStencilopDesc;

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1;
	sampleDesc.Quality = 0;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = vsByteCode;
	psoDesc.PS = psByteCode;
	psoDesc.DS = nullBytecode;
	psoDesc.HS = nullBytecode;
	psoDesc.GS = nullBytecode;
	psoDesc.StreamOutput = nullStreamOutputDesc;
	psoDesc.BlendState = blendDesc;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.RasterizerState = rasterizerDesc;
	psoDesc.DepthStencilState = depthStencilDesc;
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc = sampleDesc;
	psoDesc.NodeMask = 0;
	psoDesc.CachedPSO = { nullptr, 0 };
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)))) {
		OutputDebugString("-------------------------Failed to create pipeline state object\n");
	}


	Vertex triangleVertices[] =
	{
		{ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f } },
		{ { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f } },
		{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f } }
	};

	const UINT vertexBufferSize = sizeof(triangleVertices);

	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProperties.CreationNodeMask = 0;
	heapProperties.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	resourceDesc.Width = vertexBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)))) {
		OutputDebugString("-------------------------Failed to create comitted resource\n");
	}

	UINT8* pVertexDataBegin;
	D3D12_RANGE range = {};
	range.Begin = 0;
	range.End = 0;
	if (FAILED(m_vertexBuffer->Map(0, &range, reinterpret_cast<void**>(&pVertexDataBegin)))) {
		OutputDebugString("-------------------------Failed to map vertex buffer\n");
	}

	memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
	m_vertexBuffer->Unmap(0, nullptr);
	
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

void Renderer::Update() {
	if (m_directFence->GetCompletedValue() < m_directFenceValue) {
		auto event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		m_directFence->SetEventOnCompletion(m_directFenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	// TODO: Camera
}

void Renderer::Render() {
	if (FAILED(m_directCommandAllocators[0]->Reset())) {
		OutputDebugString("--------------------------Failed to reset command allocator\n");
	}

	if (FAILED(m_directCommandList->Reset(m_directCommandAllocators[0].Get(), m_pipelineState.Get()))) {
		OutputDebugString("--------------------------Failed to rest command list\n");
	}

	m_directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_directCommandList->RSSetViewports(1, &m_viewport);
	m_directCommandList->RSSetScissorRects(1, &m_scissorRect);

	D3D12_RESOURCE_BARRIER renderTargetBarrier = {};
	renderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	renderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	renderTargetBarrier.Transition.pResource = m_renderTargets[m_frameIndex].texture.Get();
	renderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	renderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	renderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_directCommandList->ResourceBarrier(1, &renderTargetBarrier);


	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescriptorHeaps[m_frameIndex]->GetCPUDescriptorHandleForHeapStart();
	m_directCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_directCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_directCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_directCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_directCommandList->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER presentTargetBarrier = {};
	presentTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	presentTargetBarrier.Transition.pResource = m_renderTargets[m_frameIndex].texture.Get();
	presentTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	presentTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	presentTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	m_directCommandList->ResourceBarrier(1, &presentTargetBarrier);

	if (FAILED(m_directCommandList->Close())) {
		OutputDebugString("-------------------------Failed to close command list\n");
	}

	ID3D12CommandList* ppCommandLists[] = { m_directCommandList.Get() };
	m_directCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	
	DXGI_PRESENT_PARAMETERS presentParameters = {};
	presentParameters.DirtyRectsCount = 0;
	if (FAILED(m_swapChain->Present1(1, 0, &presentParameters))) {
		OutputDebugString("-------------------------Failed to present swap chain\n");
	}

	const UINT64 fence = m_directFenceValue;
	if (FAILED(m_directCommandQueue->Signal(m_directFence.Get(), fence))) {
		OutputDebugString("-------------------------Failed to signal from command queue\n");
	}
	m_directFenceValue++;

	if (m_directFence->GetCompletedValue() < fence) {
		m_directFence->SetEventOnCompletion(fence, m_directFenceEvent);
		WaitForSingleObject(m_directFenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Renderer::Destroy() {
}

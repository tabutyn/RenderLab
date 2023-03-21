#include "renderer.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cmath>
#include <string.h>

using namespace Microsoft::WRL;

Renderer::Renderer(UINT width, UINT height, std::wstring title, HINSTANCE hInstance) :
	m_width(width),
	m_height(height),
	m_title(title),
	m_hInstance(hInstance)
{
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
}

Renderer::~Renderer() {
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
	if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)))) {
		OutputDebugString("-------------------------Failed to create d3d12CommandQueue\n");
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
	
	if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hWnd, &swapChainDesc, nullptr, nullptr, &swapChain))) {
		OutputDebugString("-------------------------Failed to create swap chain\n");
	}
	m_factory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(swapChain.As(&m_swapChain))) {
		OutputDebugString("-------------------------Failed to cast IDXGISwapChain1 to IDXGISwapChain3\n");
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)))) {
		OutputDebugString("-------------------------Failed to create rtv descriptor heap.\n");
	}
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT n = 0; n < FrameCount; n++)
	{
		if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])))) {
			OutputDebugString("-------------------------Failed to GetBuffer from swapChain\n");
		}
		m_device->CreateRenderTargetView(m_renderTargets[n].Get(), NULL, rtvHandle);
		rtvHandle.ptr += m_rtvDescriptorSize;
	}

	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)))) {
		OutputDebugString("-------------------------Failed to create Command Allocator\n");
	}

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
		OutputDebugString("-------------------------Failed to serailize versioned root signature");
	}

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

	if (FAILED(D3DCompileFromFile(m_shaderPath.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr))) {
		OutputDebugString("------------------------Failed to compile vertex shader");
	}

	if (FAILED(D3DCompileFromFile(m_shaderPath.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr))) {
		OutputDebugString("------------------------Failed to compile pixel shader");
	}

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	if (FAILED(m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_commandList)))) {
		OutputDebugString("-------------------------Failed to create command list\n");
	}

	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
		OutputDebugString("-------------------------Failed to create fence\n");
	}

	m_fenceValue = 1;
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr) {
		OutputDebugString("-------------------------Failed to create event for fence\n");
	}
}

void Renderer::Render() {
	if (FAILED(m_commandAllocator->Reset())) {
		OutputDebugString("--------------------------Failed to reset command allocator\n");
	}
	if (FAILED(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()))) {
		OutputDebugString("--------------------------Failed to rest command list\n");
	}

	D3D12_RESOURCE_BARRIER renderTargetBarrier = {};
	renderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	renderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	renderTargetBarrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
	renderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	renderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	renderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	m_commandList->ResourceBarrier(1, &renderTargetBarrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle1 = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle1.ptr += m_frameIndex * m_rtvDescriptorSize;
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle1, clearColor, 0, nullptr);

	D3D12_RESOURCE_BARRIER presentTargetBarrier = {};
	presentTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	presentTargetBarrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
	presentTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	presentTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	presentTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	m_commandList->ResourceBarrier(1, &presentTargetBarrier);
	if (FAILED(m_commandList->Close())) {
		OutputDebugString("-------------------------Failed to close command list\n");
	}

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	
	DXGI_PRESENT_PARAMETERS presentParameters = {};
	presentParameters.DirtyRectsCount = 0;
	m_swapChain->Present1(1, 0, &presentParameters);

	const UINT64 fence = m_fenceValue;
	if (FAILED(m_commandQueue->Signal(m_fence.Get(), fence))) {
		OutputDebugString("-------------------------Failed to signal from command queue\n");
	}
	m_fenceValue++;

	if (m_fence->GetCompletedValue() < fence) {
		m_fence->SetEventOnCompletion(fence, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Renderer::Destroy() {
}

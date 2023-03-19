#include "renderer.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cmath>

using namespace Microsoft::WRL;

Renderer::Renderer(UINT width, UINT height, std::string title) :
	m_rtvDescriptorSize(0),
	m_frameIndex(0),
	m_width(width),
	m_height(height),
	m_title(title)
{
}

Renderer::~Renderer() {
}

void Renderer::Init() {
	UINT dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

	ComPtr<ID3D12Debug> debugController;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		OutputDebugString("-----------------------Failed to create ID3D12Debug Interface\n");
	}
	debugController->EnableDebugLayer();

	if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory))))
	{
		OutputDebugString("-----------------------Failed to create DXGIFactory7\n");
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
		OutputDebugString("----------------------Failed to create d3d12Device\n");
	}
	
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)))) {
		OutputDebugString("---------------------Failed to create d3d12CommandQueue\n");
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
		OutputDebugString("--------------------------Failed to create swap chain\n");
	}
	m_factory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(swapChain.As(&m_swapChain))) {
		OutputDebugString("---------------------Failed to cast IDXGISwapChain1 to IDXGISwapChain3\n");
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)))) {
		OutputDebugString("---------------------Failed to create rtv descriptor heap.\n");
	}
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT n = 0; n < FrameCount; n++)
	{
		if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])))) {
			OutputDebugString("----------------------------------Failed to GetBuffer from swapChain\n");
		}
		m_device->CreateRenderTargetView(m_renderTargets[n].Get(), NULL, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}

	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)))) {
		OutputDebugString("-----------------------Failed to create Command Allocator\n");
	}

	if (FAILED(m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_commandList)))) {
		OutputDebugString("------------------------Failed to create command list\n");
	}
	
	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
		OutputDebugString("------------------------Failed to create fence\n");
	}

	m_fenceValue = 1;
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr) {
		OutputDebugString("-------------------------Failed to create event for fence");
	}


}

void Renderer::Update() {
}

void Renderer::Render() {
}

void Renderer::Destroy() {
}

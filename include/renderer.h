#include <wrl/client.h>
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <D3Dcompiler.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Renderer {
public:
	Renderer(UINT width, UINT height, std::wstring title, HINSTANCE hInstance);
	~Renderer();

	void Init();
	void Render();
	void Destroy();

	LONG GetWidth() const { return m_width; }
	LONG GetHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }
	void SetWindowHandle(HWND hWnd) { m_hWnd = hWnd; }

private:
	static const UINT FrameCount = 2;
	static const UINT TextureWidth = 256;
	static const UINT TextureHeight = 256;
	static const UINT TexturePixelSize = 4;

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT2 uv;
	};

	ComPtr<IDXGIFactory7> m_factory;
	ComPtr<IDXGIAdapter4> m_adapter;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device8> m_device;
	ComPtr<ID3D12Resource2> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Fence1> m_fence;

	ComPtr<ID3D12Resource2> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = { 0, 0, 0 };
	ComPtr<ID3D12Resource2> m_texture;

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	UINT64 m_fenceValue = 0;
	HANDLE m_fenceEvent = 0;
	UINT m_frameIndex = 0;

	UINT m_rtvDescriptorSize = 0;

	HINSTANCE m_hInstance = 0;
	HWND m_hWnd = 0;
	LONG m_width;
	LONG m_height;
	FLOAT m_aspectRatio;
	std::wstring m_title;
	std::wstring m_moduleDir;
	std::wstring m_shaderPath;
};
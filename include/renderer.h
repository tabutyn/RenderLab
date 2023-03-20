#include <wrl/client.h>
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Renderer {
public:
	Renderer(UINT width, UINT height, std::string title);
	~Renderer();

	void Init();
	void Render();
	void Destroy();

	LONG GetWidth() const { return m_width; }
	LONG GetHeight() const { return m_height; }
	const CHAR* GetTitle() const { return m_title.c_str(); }

	void SetWindowHandle(HWND hWnd) { m_hWnd = hWnd; }

private:
	static const UINT FrameCount = 2;

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT3 color;
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
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Fence1> m_fence;

	ComPtr<ID3D12Resource2> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = { 0, 0, 0 };

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	UINT64 m_fenceValue = 0;
	HANDLE m_fenceEvent = 0;
	UINT m_frameIndex = 0;

	UINT m_rtvDescriptorSize = 0;

	HWND m_hWnd = 0;
	LONG m_width;
	LONG m_height;
	std::string m_title;
};
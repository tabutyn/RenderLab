#include "d3dx12.h"
#include <string>
#include <dxgi1_6.h>

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

	ComPtr<IDXGIFactory7> m_factory;
	ComPtr<IDXGIAdapter4> m_adapter;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device9> m_device;
	ComPtr<ID3D12Resource2> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Fence1> m_fence;
	UINT64 m_fenceValue;
	HANDLE m_fenceEvent;

	UINT m_rtvDescriptorSize;
	UINT m_frameIndex;

	HWND m_hWnd;
	LONG m_width;
	LONG m_height;
	std::string m_title;
};
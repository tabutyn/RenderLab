#define NOMINMAX
#include <wrl/client.h>
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <D3Dcompiler.h>
#include <cmath>
#include "tiny_gltf.h"
#include "json.hpp"

enum RenderPassType { RENDER_PASS_TYPE_PRESENT = 0, RENDER_PASS_TYPE_COUNT };

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Renderer {
public:
	Renderer(UINT width, UINT height, std::wstring title, HINSTANCE hInstance);
	~Renderer();

	void Init();
	void Update();
	void Render();
	void Destroy();

	LONG GetWidth() const { return m_width; }
	LONG GetHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }
	void SetWindowHandle(HWND hWnd) { m_hWnd = hWnd; }

private:
	static const UINT FrameCount = 2;

	tinygltf::TinyGLTF gltfContext;
	tinygltf::Model gltfModel;

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT2 uv;
	};

	struct RenderTarget {
		ComPtr<ID3D12Resource> texture;
		D3D12_CPU_DESCRIPTOR_HANDLE viewDescriptor;
	};

	struct Material {
		std::string name;
		D3D12_BLEND_DESC blendDesc;
		D3D12_RASTERIZER_DESC rasterizerDesc;
		ComPtr<ID3D12Resource> pBuffer;
		void* pBufferData;
		ComPtr<ID3D12DescriptorHeap> pSRVDescriptorHeap;
		ComPtr<ID3D12DescriptorHeap> pSamplerDescriptorHeap;
	};

	struct Attribute {
		std::string name;
		DXGI_FORMAT format;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	};

	struct Primitive {
		std::vector<Attribute> attributes;
		uint32_t vertexCount;
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology;
		D3D12_INDEX_BUFFER_VIEW indexBufferView;
		uint32_t indexCount;
		Material* pMaterial;
		ComPtr<ID3D12RootSignature> pRootSignature;
		ComPtr<ID3D12PipelineState> pPipelineState;
	};

	struct Mesh {
		std::string name;
		std::vector<Primitive> primitives;
	};

	struct Node {
		DirectX::XMFLOAT4X4 M;
	};

	struct Camera {
		DirectX::XMFLOAT4X4 V;
		DirectX::XMFLOAT4X4 P;
		DirectX::XMFLOAT4X4 VP;
	};

	ComPtr<IDXGIFactory7> m_factory;
	ComPtr<IDXGIAdapter4> m_adapter;
	ComPtr<ID3D12Device8> m_device;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Resource2> m_swapChainBuffers[FrameCount];

	ComPtr<ID3D12CommandQueue> m_directCommandQueue;
	ComPtr<ID3D12Fence1> m_directFence;
	UINT64 m_directFenceValue = 0;
	HANDLE m_directFenceEvent = 0;
	ComPtr<ID3D12CommandAllocator> m_directCommandAllocators[FrameCount];
	ComPtr<ID3D12GraphicsCommandList> m_directCommandList;

	ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
	ComPtr<ID3D12Fence1> m_copyFence;
	UINT64 m_copyFenceValue = 0;
	HANDLE m_copyFenceEvent = 0;
	ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;

	UINT m_descriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	ComPtr<ID3D12DescriptorHeap> m_rtvDescriptorHeaps[FrameCount];
	RenderTarget m_renderTargets[FrameCount];

	std::vector<ComPtr<ID3D12Resource>> m_buffers;
	std::vector<ComPtr<ID3D12Resource>> m_textures;
	std::vector<D3D12_SAMPLER_DESC> m_samplerDescs;
	std::vector<Material> m_materials;
	std::vector<Mesh> m_meshes;
	std::vector<ComPtr<ID3D12Resource>> m_nodeBuffers;
	ComPtr<ID3D12Resource> m_cameraBuffer;

	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;

	ComPtr<ID3D12Resource2> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = { 0, 0, 0 };
	ComPtr<ID3D12Resource2> m_texture;

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	UINT m_frameIndex = 0;

	HINSTANCE m_hInstance = 0;
	HWND m_hWnd = 0;
	LONG m_width;
	LONG m_height;
	FLOAT m_aspectRatio;
	std::wstring m_title;
	std::wstring m_moduleDir;
	std::wstring m_shaderPath;
	std::wstring m_modelPath;
};

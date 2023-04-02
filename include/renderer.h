#define NOMINMAX
#include <wrl/client.h>
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <D3Dcompiler.h>
#include <cmath>
#include <chrono>
#include "tiny_gltf.h"
#include "json.hpp"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Renderer {
public:
	Renderer(UINT width, UINT height, std::string title);
	~Renderer();

	void Init();
	void Update(double_t deltaTime);
	void DrawNode(uint64_t nodeIndex);
	void Render();
	void Destroy();

	LONG GetWidth() const { return m_width; }
	LONG GetHeight() const { return m_height; }

	double_t GetDeltaTime();

	const CHAR* GetTitle() const { return m_title.c_str(); }

private:
	static const UINT FrameCount = 2;
	UINT fIndex = 0;
	UINT fCounter = 0;

	uint64_t alignPow2(uint64_t value, uint64_t alignement);

	struct RenderTarget {
		ComPtr<ID3D12Resource> texture;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = {};
		ComPtr<ID3D12Resource> depthTexture;
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = {};
		ComPtr<ID3D12Resource> dest;
		D3D12_CLEAR_VALUE clearValue = {};
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT rowCount = 0;
		UINT64 rowSize = 0;
		UINT64 size = 0;
		D3D12_TEXTURE_COPY_LOCATION srcCopyLocation = {};
		D3D12_TEXTURE_COPY_LOCATION dstCopyLocation = {};
	};

	struct TextureInfo {
		int32_t textureIndex;
		int32_t samplerIndex;
	};

	struct PBRMetallicRoughness {
		DirectX::XMFLOAT4 baseColorFactor;
		TextureInfo baseColorTexture;
		float metallicFactor;
		float roughnessFactor;
		TextureInfo metallicRoughnessTexture;
	};

	struct Material {
		std::string name;
		D3D12_BLEND_DESC blendDesc;
		D3D12_RASTERIZER_DESC rasterizerDesc;
		ComPtr<ID3D12Resource> buffer;
		void* bufferData;
		ComPtr<ID3D12DescriptorHeap> SRVDescriptorHeap;
		ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap;
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
		Material* material;
		ComPtr<ID3D12RootSignature> rootSignature;
		ComPtr<ID3D12PipelineState> pipelineState;
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

	tinygltf::Model m_gltfModel;

	ComPtr<IDXGIFactory7> m_factory;
	ComPtr<IDXGIAdapter4> m_adapter;
	ComPtr<ID3D12Device8> m_device;
	RenderTarget m_renderTargets[FrameCount];

	ComPtr<ID3D12CommandQueue> m_directCommandQueue;
	ComPtr<ID3D12Fence1> m_directFence;
	ComPtr<ID3D12CommandAllocator> m_directCommandAllocators[FrameCount];
	ComPtr<ID3D12GraphicsCommandList4> m_directCommandList;
	UINT64 m_directFenceValue = 0;
	HANDLE m_directFenceEvent = 0;

	ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
	ComPtr<ID3D12Fence1> m_copyFence;
	ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator[FrameCount];
	ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;
	UINT64 m_copyFenceValue = 0;
	HANDLE m_copyFenceEvent = 0;

	UINT m_descriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	ComPtr<ID3D12DescriptorHeap> m_rtvDescriptorHeaps[FrameCount];
	ComPtr<ID3D12DescriptorHeap> m_dsvDescriptorHeaps[FrameCount];
	D3D12_DEPTH_STENCIL_DESC dsDesc;

	std::vector<ComPtr<ID3D12Resource>> m_buffers;
	std::vector<ComPtr<ID3D12Resource>> m_textures;
	std::vector<D3D12_SAMPLER_DESC> m_samplerDescs;
	std::vector<Material> m_materials;
	std::vector<Mesh> m_meshes;
	std::vector<ComPtr<ID3D12Resource>> m_nodeBuffers;
	ComPtr<ID3D12Resource> m_cameraBuffer;

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	float_t* outputFloatImage = nullptr;
	uint8_t* outputCharImage = nullptr;

	LONG m_width;
	LONG m_height;
	FLOAT m_aspectRatio;
	std::string m_title;
	std::chrono::milliseconds currentFrameTime;
	std::chrono::milliseconds lastFrameTime;
	std::string m_vertexShaderPath;
	std::string m_pixelShaderPath;
	std::string m_grayPixelShaderPath;
};

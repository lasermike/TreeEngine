#pragma once

struct RenderPlatform
{

};

struct PlatformMaterial
{
	/*
#if defined(TREE3D12)

	ID3D12Resource*			m_texture;
	ID3DBlob*				m_vertexShader;
	ID3DBlob*				m_pixelShader;
	D3D12_INPUT_ELEMENT_DESC* m_inputLayout;

	CComPtr<ID3D12Resource> m_constBuffer;
#else
	ID3D11ShaderResourceView* m_texture;
	ID3D11VertexShader*       m_vertexShader;
	ID3D11PixelShader*        m_pixelShader;
	ID3D11InputLayout*        m_inputLayout;

	CComPtr<ID3D11Buffer>     m_constBuffer;

#endif

	// NYI
#if defined(TREE3D12)
	void* m_samplerState;
	void* m_rasterizer;
	void* m_depthState;
#else
	ID3D11SamplerState*       m_samplerState;
	ID3D11RasterizerState*    m_rasterizer;
	ID3D11DepthStencilState*  m_depthState;
#endif

	PlatformMaterial() :
		m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
		m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
		m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer)
*/
};
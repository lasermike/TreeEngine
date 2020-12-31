#include "pch.h"
#include "RenderPlatform11UWP.h"
#include "RenderPlatform.h"

RenderPlatform11* m_pPlatform = nullptr;

extern "C" RENDERPLATFORM_API HRESULT Create(RenderData* data)
{
    m_pPlatform = new RenderPlatform11(data);
    return S_OK;
}

//
//////////////

#if defined(TREENGINE_WIN32)
RENDERPLATFORM_API SetWindow(HWND hwnd)
{
    m_pPlatform->SetWindow(hwnd);
}

#else

RENDERPLATFORM_API void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi)
{
    GameCommon::SetContentFileRoot();

    m_pPlatform->SetWindow(window, logicalDpi);
}

#endif

RENDERPLATFORM_API HRESULT InitDevice()
{
    return m_pPlatform->InitDevice();
}

RENDERPLATFORM_API HRESULT UninitDevice()
{
    return m_pPlatform->UninitDevice();
}

RENDERPLATFORM_API HRESULT ReleaseSwapChainResources()
{
    return m_pPlatform->ReleaseSwapChainResources();
}

RENDERPLATFORM_API HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    return m_pPlatform->OnResize(windowWidth, windowHeight, renderToSharedTexture);
}

RENDERPLATFORM_API IDXGISwapChain* GetSwapChain()
{
    return m_pPlatform->GetSwapChain();
}

RENDERPLATFORM_API HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    return m_pPlatform->UpdateView(cbNeverChanges, shadowPass);
}

RENDERPLATFORM_API HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass)
{
    return m_pPlatform->UpdateViewProjection(pViewMat, pProjMat, shadowPass);
}

RENDERPLATFORM_API HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData)
{
    return m_pPlatform->InitGameLevelGraphics(maxInstances, useShadowMaps, geometryData);
}

RENDERPLATFORM_API HRESULT UninitGameLevelGraphics()
{
    return m_pPlatform->UninitGameLevelGraphics();
}

RENDERPLATFORM_API HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView)
{
    return m_pPlatform->BeginNewFrame(resetCommandList, buffer, dataView);
}

RENDERPLATFORM_API HRESULT EndFrame(D3DBuffer* buffer)
{
    return m_pPlatform->EndFrame(buffer);
}

RENDERPLATFORM_API HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor)
{
    return m_pPlatform->RenderProlog(oculus, wireframe, useAlphaBlendedRenderTarget, useShadowMaps, clearColor);
}

RENDERPLATFORM_API HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture)
{
    return m_pPlatform->RenderEpilog(oculus, useShadowMaps, renderToSharedTexture);
}

RENDERPLATFORM_API HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer)
{
    return m_pPlatform->RenderSceneSetup(pass, instancedBuffer);
}

RENDERPLATFORM_API HRESULT SetRenderPhase(RenderState state)
{
    return m_pPlatform->SetRenderPhase(state);
}

RENDERPLATFORM_API HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    return m_pPlatform->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation,  BaseVertexLocation, StartInstanceLocation);
}

RENDERPLATFORM_API HRESULT BeginDrawText()
{
    return m_pPlatform->BeginDrawText();
}

RENDERPLATFORM_API HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText)
{
    return m_pPlatform->DrawText2(sx, sy, dwColor, strText);
}

RENDERPLATFORM_API HRESULT EndDrawText()
{
    return m_pPlatform->EndDrawText();
}

RENDERPLATFORM_API HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                          ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial)
{
    return m_pPlatform->CreateMaterial(name, texture, vs, ps, shadowVs, shadowPs, shaderMaterial, renderState, materialNum, newMaterial);
}

RENDERPLATFORM_API HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass)
{
    return m_pPlatform->SetRenderUnit(ru, pass);
}

RENDERPLATFORM_API HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture)
{
    return m_pPlatform->LoadTexture(textureFilename, textureIndex, loadedTexture);
}

RENDERPLATFORM_API HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture)
{
    return m_pPlatform->CreateTexture2D(name, points, width, height, textureIndex, texture);
}

RENDERPLATFORM_API HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer)
{
    return m_pPlatform->CreateD3DBuffer(sizeBytes, numInstances, d3dBuffer);
}

RENDERPLATFORM_API HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
{
    return m_pPlatform->LoadVertexShader(shaderFilename, shader);
}

RENDERPLATFORM_API HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
{
    return m_pPlatform->LoadPixelShader(shaderFilename, shader);
}

RENDERPLATFORM_API HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit)
{
    return m_pPlatform->CreateRenderUnit(material, mesh, renderUnit);
}

RENDERPLATFORM_API void SetFrameSceneData(CBChangesEveryFrame* cb)
{
    return m_pPlatform->SetFrameSceneData(cb);
}

RENDERPLATFORM_API D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer)
{
    return m_pPlatform->GetVertexBuffer(geometryBuffer);
}

RENDERPLATFORM_API D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer)
{
    return m_pPlatform->GetIndexBuffer(geometryBuffer);
}

RENDERPLATFORM_API HRESULT GetViewport(Viewport& viewport)
{
    return m_pPlatform->GetViewport(viewport);
}

//RENDERPLATFORM_API IUnknown* GetDevice()
//{
//    return m_pPlatform->GetDevice();
//}


#pragma once

#include "RenderPlatform.h"

#ifdef RENDERPLATFORM_EXPORTS
#define RENDERPLATFORM_API extern "C" __declspec(dllexport)
#else
#define RENDERPLATFORM_API extern "C" __declspec(dllimport)
#endif

RENDERPLATFORM_API HRESULT Create(RenderData* data);

/////

#if defined(TREENGINE_WIN32)
RENDERPLATFORM_API void SetWindow(HWND hwnd);
#else
RENDERPLATFORM_API void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi);
#endif


RENDERPLATFORM_API HRESULT InitDevice();
RENDERPLATFORM_API HRESULT UninitDevice();

RENDERPLATFORM_API HRESULT ReleaseSwapChainResources();
RENDERPLATFORM_API HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
RENDERPLATFORM_API IDXGISwapChain* GetSwapChain();

RENDERPLATFORM_API HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass);
RENDERPLATFORM_API HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass);

RENDERPLATFORM_API HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData);
RENDERPLATFORM_API HRESULT UninitGameLevelGraphics();

RENDERPLATFORM_API HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
RENDERPLATFORM_API HRESULT EndFrame(D3DBuffer* buffer);

RENDERPLATFORM_API HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
RENDERPLATFORM_API HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

RENDERPLATFORM_API HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer);
RENDERPLATFORM_API HRESULT SetRenderPhase(RenderState state);

RENDERPLATFORM_API HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

RENDERPLATFORM_API HRESULT BeginDrawText();
RENDERPLATFORM_API HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
RENDERPLATFORM_API HRESULT EndDrawText();

// Materials
RENDERPLATFORM_API HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
    ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
RENDERPLATFORM_API HRESULT SetMaterial(Material* material, RenderPass pass);
RENDERPLATFORM_API HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
RENDERPLATFORM_API HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);
RENDERPLATFORM_API HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

RENDERPLATFORM_API HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader);
RENDERPLATFORM_API HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader);

RENDERPLATFORM_API void SetFrameSceneData(CBChangesEveryFrame* cb);

RENDERPLATFORM_API D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer);
RENDERPLATFORM_API D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer);

RENDERPLATFORM_API HRESULT GetViewport(Viewport& viewport);

RENDERPLATFORM_API IUnknown* GetDevice();

 #pragma once
#include "pch.h"
#include "RenderData.h"
#include "RenderPlatform.h"
#include "StockRenderStates.h"

class WorldObject;

enum DisplayMode
{
    Monitor = 0,
    Oculus
};

enum MaterialTypes
{
    LogMaterial,
    TwigMaterial,
    GroundMaterial,

    MaterialTypesMax
};

enum ShaderType
{
    ShaderType_VertexShader,
    ShaderType_PixelShader,
    ShaderType_ComputeShader,
};

interface IRenderFrame
{
    virtual HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances) = 0;
    virtual HRESULT GetInstanceIndex(WorldObject* object, UINT&) = 0;
    virtual RenderData& GetRenderData() = 0;
};

class RenderManager : public IRenderFrame
{
    // Filled in during scene initialization
    std::map<wstring, Material*>                    m_materials;
    std::map<wstring, Mesh>                         m_meshes;
    std::map<wstring, LoadedTexture*>               m_textures;

    std::map<wstring, VertexShader*>                m_vertexShaders;
    std::map<wstring, PixelShader*>                 m_pixelShaders;

    std::list<RenderUnit*>                          m_renderUnits;
    std::map<WorldObject*, UINT>                    m_objectToInstanceBufferOffset;  // Filled in during scene initialization
    UINT                                            m_nextInstanceBufferOffset;		 // Used during initialization

    // Filled in each frame by each world object via SetInstances()
    std::map<RenderUnit*, std::map<WorldObject*, std::pair<UINT, UINT>>> m_perFrameInstanceData;

    RenderData                                      m_renderData;

    // 11 or 12
    RenderPlatform*                                 m_platform;

    DisplayMode                         m_displayMode;

    // App resources.

    DoubleBuffer                        m_instancedBuffer;

    GeometryGenerator                   m_geometryGenerator;
    GeometryBufferData                  m_geometryData;

    int m_lastMaxInstances;
    bool m_lastUseShadowMaps;

    DirectionalLight                    m_light;  // Doesn't belong here, will move later

    HRESULT LoadTexture(const wchar_t* textureFilename, LoadedTexture** loadedTexture);
    HRESULT LoadShader(const wchar_t* shaderFilename, ShaderType shaderType);
    HRESULT Render(RenderUnit* renderUnit, RenderPass pass);
    HRESULT RenderScene(RenderPass pass);

    void BuildShadowTransform();
    void DrawSceneToShadowMap();

public:
    RenderManager();
    ~RenderManager();

    HRESULT SetPlatform(HMODULE platformDLL);

    HRESULT InitDevice();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    void UninitDevice();

    RenderPlatform* GetPlatformBase() { return m_platform; }

    RenderData& GetRenderData() { return m_renderData; }

    RenderPlatform* GetPlatform() { return (RenderPlatform*)m_platform; }

    GeometryBufferData& GetGeometryBufferData() { return m_geometryData; }

    // Accessor methods for Oculus
    //HRESULT GetViewport(Viewport& viewport);

    HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass);
    HRESULT UpdateView(XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height);
    HRESULT CreateMaterial(const wchar_t* name, const wchar_t* textureFilename,
                           const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename, 
                           const wchar_t* shadowVertexShaderFilename, const wchar_t* shadowPixelShaderFilename, 
                           ShaderMaterial& shaderMaterial, StockRenderState state, Material** newMaterial);
    HRESULT CreateMesh(const wchar_t* name, D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer,
        const GeometryBufferData::BufferOffsets* bufferIndices, InputLayouts inputLayout, Mesh** newMesh);

    HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);
    HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances);
    HRESULT GetInstanceIndex(WorldObject* object, UINT&);

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps);
    virtual HRESULT UninitGameLevelGraphics();

    HRESULT BeginNewFrame();
    HRESULT EndFrame();

    void Render(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp, bool showShadowBuffer,
                bool m_renderToSharedTexture, float* clearColor);

    HRESULT DrawFrameStats();
    HRESULT RenderShadowMap();

    HRESULT ChangePlatform(int platform);
};


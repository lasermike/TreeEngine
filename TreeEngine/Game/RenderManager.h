 #pragma once
#include "pch.h"
#include "RenderData.h"
#include "RenderPlatform.h"
#include "StockRenderStates.h"
#include "OpacityMicromapBaker.h"
#include <memory>

class WorldObject;
class Settings;
class SceneRoot;
struct GltfTextureSource;
struct MaterialTextureSources;

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
    // objectTransformSlot is the instance-buffer slot of a transform applied on the GPU after each instance's World;
    // UINT_MAX means identity.
    virtual HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances,
        UINT objectTransformSlot = UINT_MAX) = 0;
    virtual HRESULT GetInstanceIndex(WorldObject* object, UINT&) = 0;
    virtual RenderData& GetRenderData() = 0;
};

interface IDebugUI
{
    virtual HRESULT UpdateDebugUI(ImGuiContext * imGuiContext) = 0;
};

class RenderManager : public IRenderFrame
{
    // Filled in during scene initialization
    std::map<wstring, Material*>                    m_materials;
    std::map<wstring, Mesh>                         m_meshes;
    std::map<wstring, LoadedTexture*>               m_textures;
    LoadedTexture*                                m_environmentDiffuse = nullptr;
    LoadedTexture*                                m_environmentSpecular = nullptr;
    std::map<std::wstring, int> m_gltfTextureSlots;

    std::map<wstring, VertexShader*>                m_vertexShaders;
    std::map<wstring, PixelShader*>                 m_pixelShaders;

    std::list<RenderUnit*>                          m_renderUnits;
    std::map<WorldObject*, UINT>                    m_objectToInstanceBufferOffset;  // Filled in during scene initialization
    UINT                                            m_nextInstanceBufferOffset;		 // Used during initialization

    // Filled in each frame by each world object via SetInstances()
    struct InstanceRange
    {
        UINT start = 0;
        UINT count = 0;
        UINT objectTransformSlot = UINT_MAX;
    };
    std::map<RenderUnit*, std::map<WorldObject*, InstanceRange>> m_perFrameInstanceData;

    RenderData                                      m_renderData;

    // 11 or 12
    RenderPlatform*                                 m_platform;

    Settings*                                       m_settings;

    IDebugUI*                                       m_pDebugUI;

    DisplayMode                         m_displayMode;

    // App resources.

    DoubleBuffer                        m_instancedBuffer;

    GeometryGenerator                   m_geometryGenerator;
    GeometryBufferData                  m_geometryData;

    int m_lastMaxInstances;
    bool m_lastUseShadowMaps;

    // Opacity micromaps for the scene's alpha-tested leaves (see RegisterLeafOpacity).
    std::vector<int> m_instanceOpacityMicromap; // Per instance slot; RenderData::instanceOpacityMicromap points here.
    std::vector<uint8_t> m_ommData;
    std::vector<UINT> m_ommByteOffsets;
    std::vector<int> m_ommTriangleIndices;      // Per baked triangle: a micromap index or a special index.
    std::map<std::wstring, int> m_ommEntries;   // Bake key -> first triangle entry, or -1.
    std::map<std::wstring, std::shared_ptr<OpacityMicromap::AlphaImage>> m_ommAlpha; // Decoded mip-0 alpha.
    std::shared_ptr<OpacityMicromap::AlphaImage> LoadOpacityAlpha(const GltfTextureSource& texture, bool wrap);

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
    HRESULT SetDebugUI(IDebugUI* debugUI) { m_pDebugUI = debugUI; return S_OK; }
    HRESULT SetSettings(Settings* settings) { m_settings = settings; return S_OK; }

    HRESULT InitDevice();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    void UninitDevice();

    RenderPlatform* GetPlatformBase() { return m_platform; }

    RenderData& GetRenderData() { return m_renderData; }
    const SceneRenderSettings& GetSceneSettings() const { return m_renderData; }
    const std::list<RenderUnit*>& GetRenderUnits() const { return m_renderUnits; }
    void ApplySceneSettings(const SceneRenderSettings& settings);

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
    HRESULT LoadGltfTexture(const GltfTextureSource& source, LoadedTexture** texture, int* textureSlot = nullptr);
    LoadedTexture* FindLoadedGltfTexture(const GltfTextureSource& source) const;
    // Loads a non-base-color material map and stores its slot in the shader material.
    HRESULT BindMaterialTexture(const GltfTextureSource& source, MaterialTextureRole role, ShaderMaterial& shaderMaterial);
    HRESULT BindMaterialTextures(const MaterialTextureSources& sources, ShaderMaterial& shaderMaterial);
    // Bakes opacity micromaps for the leaf sprite drawn with an alpha-masked material (after its textures are bound)
    // and an atlas rect, and returns its first entry for RenderData::instanceOpacityMicromap, or -1 when it can't:
    // no DXR tier 1.2, no alpha mask, or a base-color map that is not a memory-backed PNG or JPEG.
    // Identical requests share one entry.
    int RegisterLeafOpacity(const ShaderMaterial& material, const GltfTextureSource* baseColor, XMFLOAT4 atlasRect);
    // Hands the scene's baked micromaps to the platform; call after the scene's objects initialize their graphics.
    HRESULT FinalizeOpacityMicromaps();
    HRESULT CreateMaterial(const wchar_t* name, const GltfTextureSource* textureSource,
                           const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename,
                           const wchar_t* shadowVertexShaderFilename, const wchar_t* shadowPixelShaderFilename,
                           ShaderMaterial& shaderMaterial, StockRenderState state, Material** newMaterial);
    HRESULT CreateMesh(const wchar_t* name, D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer,
        const GeometryBufferData::BufferOffsets* bufferIndices, InputLayouts inputLayout, Mesh** newMesh);

    HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);
    HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances,
        UINT objectTransformSlot = UINT_MAX);
    HRESULT GetInstanceIndex(WorldObject* object, UINT&);

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, SceneRoot& scene);
    virtual HRESULT UninitGameLevelGraphics();

    HRESULT BeginNewFrame();
    HRESULT EndFrame();

    void Render(bool oculus, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp,
                bool m_renderToSharedTexture, float* clearColor);

    HRESULT DrawFrameStats();
    HRESULT RenderShadowMap();

    HRESULT ChangePlatform(int platform);
};

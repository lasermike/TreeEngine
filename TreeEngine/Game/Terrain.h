#pragma once
#include "WorldObject.h"
#include "TerrainPackage.h"
#include "RoadNetwork.h"
#include "RoadMesh.h"
#include <array>
#include <condition_variable>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct RenderUnit;
struct GltfAsset;
struct GltfTextureSource;

// A heightfield terrain drawn from a terrain package (TerrainPackage.h), CDLOD style: each frame a quadtree walk from
// the root chooses nodes by distance, finer near the camera, and each chosen node is one draw of a shared 64 x 64-quad
// grid, with skirts hiding the cracks where levels meet. A node's vertices (world space) live in a slot of a pool the
// terrain reserves in the shared vertex buffer; nodes load into free or least recently used slots and upload through
// the render frame. A node splits only when its four children are loaded, so its coarser self draws until then.
//
// Streaming: a loader thread reads blocks (through the package's IAssetReader) and builds their vertices, best first;
// each frame the terrain uploads up to UploadBudget finished nodes, then asks for what this frame's selection lacked
// (children it wanted to split into) and what it may want soon (children of nodes within PrefetchRange of splitting).
// The first frame after loading, and every frame with RenderData::terrainStreaming off, loads synchronously instead.
// A material layer's tiling and coverage (SceneData::TerrainLayerDefinition); its material is the params' material
// slot of the same index.
struct TerrainLayerParams
{
    float tile = 4;
    float biplanar = 0;
    XMFLOAT3 height = XMFLOAT3(-1e9f, 1e9f, 0);    // Lowest, highest, blend.
    XMFLOAT3 slope = XMFLOAT3(0, 90, 0);
    XMFLOAT3 noise = XMFLOAT3(0, 0, 0);            // Height shift, slope shift, cell size.
};

class TerrainParams : public WorldObjectParams
{
public:
    TerrainParams() : WorldObjectParams(TerrainGeneratorType) {}
    std::vector<TerrainLayerParams> layers;     // Empty: the single material in slot 0.
    std::wstring packageDirectory;  // Resolved package directory.
    std::string source;             // As authored, for saving.
    std::wstring roadsFile;         // Resolved roads file (empty for none), and
    std::string roads;              // as authored, for saving.
    // The road surfaces' materials and maps, one per Roads::Part (asphalt, gravel, yellow and white markings).
    std::vector<ShaderMaterial> roadMaterials;
    std::vector<MaterialTextureSources> roadMaterialTextures;
    // Loads a roadside prop model (Models/Roadside/<name>.glb), asked once for each model the roads file's props
    // use; null (or a null result, for a model that isn't installed) draws none of that model's props.
    std::function<std::shared_ptr<GltfAsset>(Roads::PropModel)> loadPropModel;
    std::array<std::shared_ptr<GltfAsset>, static_cast<size_t>(Roads::PropModel::Count)> propModels;   // As loaded.
    float lodRange = 2.0f;          // A node splits within lodRange x its size of the camera.
    UINT slots = 512;               // Vertex pool size, in nodes.
};

class Terrain : public WorldObject
{
public:
    static constexpr UINT GridVertices = TerrainFormat::NodeVertices * TerrainFormat::NodeVertices;
    static constexpr UINT VerticesPerNode = GridVertices + 4 * TerrainFormat::NodeVertices;   // Grid, then skirts.
    static constexpr float Hysteresis = 0.1f;   // A split node stays split out to (1 + Hysteresis) x its range.
    static constexpr float PrefetchRange = 1.5f; // Children load ahead within this multiple of a node's range.
    static constexpr UINT UploadBudget = 32;    // Streamed nodes uploaded per frame at most (about 7 MB).

    explicit Terrain(TerrainParams* params);
    ~Terrain() override;
    ObjectType GetObjectType() override { return ObjectType_Terrain; }
    // False with Error() set when the package can't be opened.
    bool IsOpen() const { return m_open; }
    const std::string& Error() const { return m_package.Error(); }
    // The roads file's problem, when it has one (the terrain still opens; the scene load reports it).
    const std::string& RoadsError() const { return m_roadsError; }
    const Roads::Network& RoadNetwork() const { return m_roads; }
    const TerrainPackage& Package() const { return m_package; }

    void AppendGeometry(GeometryBufferData& geometryData) override;
    HRESULT InitGraphics(RenderManager& renderManager) override;
    HRESULT CleanUpDeviceObjects() override;
    HRESULT ComputeConstants(IRenderFrame* frame) override;
    // One instance for the nodes, one per road surface part, and one per prop part per prop.
    // Then a slot per street name holding its sign-text atlas region (not drawn).
    unsigned int GetNumInstances() override { return 1 + RoadParts() + PropInstances() + SignNames(); }
    unsigned int GetMaxInstances() override { return 1 + RoadParts() + PropInstances() + SignNames(); }
    // Each drawn node is a ray-tracing geometry of its own; so is each road surface part and prop instance.
    unsigned int GetMaxDrawnInstances() override { return Params().slots + RoadParts() + PropInstances(); }
    // The props drawn of a model (Roads::PropModel), for tests.
    UINT PropCount(Roads::PropModel model) const { return static_cast<UINT>(m_propWorlds[static_cast<size_t>(model)].size()); }
    // The distinct street names on its signs, in atlas order (for tests).
    const std::vector<std::string>& SignNamesList() const { return m_signNames; }
    // Editing props (Game's debug UI). A prop is its index in RoadNetwork().props. PickProp: the nearest drawn prop a
    // world-space ray (unit direction) hits within its model's bounds, or -1. SetProp moves one (package x, z; it
    // stands on the ground there) and turns it (degrees clockwise from north); RemoveProp hides it. Both mark it
    // edited until SaveProps writes the edits into the roads file: the runtime copy, and the source file the build
    // copied it from when it's found above the executable (Game\Resources\<roads>); `message` says where.
    int PickProp(const XMFLOAT3& origin, const XMFLOAT3& direction, float* distance = nullptr);
    // Where a world-space ray (unit direction) first meets the terrain's surface within `reach`: its distance, or a
    // negative value when it doesn't.
    float GroundHit(const XMFLOAT3& origin, const XMFLOAT3& direction, float reach);
    bool PropBounds(int prop, XMFLOAT3 bounds[2]) const;     // World axis-aligned bounds of a drawn prop.
    void SetProp(int prop, const XMFLOAT2& point, float heading);
    void RemoveProp(int prop);
    bool PropEdited(int prop) const { return m_propEdits.count(prop) != 0; }
    size_t PropEdits() const { return m_propEdits.size(); }
    bool SaveProps(std::string& message, const std::vector<std::wstring>* files = nullptr);    // files: instead (tests).
    // Props left out because their model isn't installed.
    UINT PropsWithoutModel() const { return m_propsWithoutModel; }

    // The terrain's height at world (x, z) from the package's finest level, bilinear between samples; false outside
    // the terrain or when the block can't be read. Reads the package (not thread safe with itself).
    bool HeightAt(float x, float z, float& height);

    // This frame's selection, for profiling and tests.
    UINT SelectedNodes() const { return static_cast<UINT>(m_selected.size()); }
    UINT ResidentNodes() const { return static_cast<UINT>(m_resident.size()); }
    UINT LoadedThisFrame() const { return m_loadedThisFrame; }
    UINT MissingNodes() const { return m_missing; }      // Splits this frame held back by children not yet loaded.
    UINT PendingNodes() const { return m_pending; }      // Nodes queued or loading.

private:
    struct Node { int level, x, z; };
    struct Slot
    {
        uint32_t node = UINT32_MAX;     // Package node index, or UINT32_MAX when free.
        UINT lastUsed = 0;              // Frame the node was last needed (drawn or split).
        UINT lastPrefetched = 0;        // Frame it was last wanted ahead of need.
    };

    struct LoadRequest
    {
        Node node;
        float priority;                 // Lower loads sooner.
        bool prefetch;                  // Wanted ahead of need: it may only take slots nothing used lately.
    };
    struct LoadResult
    {
        Node node;
        bool prefetch = false;
        bool ok = false;
        std::vector<SimpleVertex> vertices;
    };

    TerrainParams& Params() { return static_cast<TerrainParams&>(*_params); }
    const TerrainParams& Params() const { return static_cast<const TerrainParams&>(*_params); }
    static uint32_t Index(const Node& node) { return static_cast<uint32_t>(TerrainPackage::NodeIndex(node.level, node.x, node.z)); }
    void NodeBounds(const Node& node, XMFLOAT3 bounds[2]) const;
    float Distance(const Node& node, const XMFLOAT3& eye) const;
    // Whether a node is resident (marking it needed or prefetched), loading it now when synchronous (needed nodes
    // only), else asking the loader for it.
    bool Ensure(const Node& node, float priority, bool prefetch, IRenderFrame* frame);
    // Puts a node's vertices in a free slot, else evicts the least recently used one. Nodes needed this frame (and,
    // while streaming, last frame) are kept; a needed node may evict one only prefetched lately, a prefetched one not.
    bool Place(const Node& node, const SimpleVertex* vertices, bool prefetch, IRenderFrame* frame);
    void Select(const Node& node, const XMFLOAT3& eye, IRenderFrame* frame);
    void BuildVertices(const Node& node, const uint16_t* block, SimpleVertex* vertices) const;
    void StartLoader();
    void StopLoader();
    void LoaderMain();
    void ApplyLoaded(IRenderFrame* frame);
    void PublishRequests();

    TerrainPackage m_package;
    bool m_open = false;
    Roads::Network m_roads;
    std::string m_roadsError;
    std::wstring m_roadsFile;           // The roads file the terrain loaded, and its CRS origin (for saving props).
    double m_roadsOriginX = 0, m_roadsOriginY = 0;
    // Road surfaces, built when the roads load: a static mesh per non-empty part, drawn like a model's.
    Roads::Mesh m_roadMesh;
    struct RoadPart
    {
        Roads::Part part;
        GeometryBufferData::BufferOffsets offsets = {};
        RenderUnit* renderUnit = nullptr;
    };
    std::vector<RoadPart> m_roadParts;
    UINT RoadParts() const { return static_cast<UINT>(m_roadParts.size()); }
    // Roadside props: each one's transform (turned to its heading, on the ground) by model, and a batch per primitive
    // and material of each model in use, drawn instanced once per part using them per prop of the model (one render
    // unit each, as a glTF model groups its parts).
    static constexpr size_t PropModels = static_cast<size_t>(Roads::PropModel::Count);
    static constexpr float PropHeight = 3.5f;   // Meters: the tallest prop, which the terrain's bounds reach above it.
    std::array<std::vector<XMFLOAT4X4>, PropModels> m_propWorlds;
    struct PropBatch
    {
        size_t model = 0, primitive = 0, material = 0;
        std::vector<XMFLOAT4X4> transforms;     // The transforms within the model of the parts drawing these.
        int blade = -1;                         // A street-name blade's face or edge: 0 lower, 1 upper (else -1),
        bool face = false;                      // stretched to its name; a face also shows the name.
        RenderUnit* renderUnit = nullptr;
    };
    // Street names: the distinct names on street-name signs, each sign's lower and upper blade name (indices, by
    // the sign's place in its model's transforms), and the atlas they're drawn in (SignText.h).
    std::vector<std::string> m_signNames;
    std::vector<std::array<UINT, 2>> m_signBlades;
    std::shared_ptr<GltfTextureSource> m_signTexture;
    std::vector<XMFLOAT4> m_signRegions;
    std::vector<float> m_signStretch;           // Per name: its blade's length over the model's (SignText::BladeLength).
    UINT SignNames() const { return static_cast<UINT>(m_signRegions.size()); }
    void BuildSignText();
    std::vector<PropBatch> m_propBatches;
    UINT m_propInstances = 0;
    UINT m_propsWithoutModel = 0;
    UINT PropInstances() const { return m_propInstances; }
    void PlaceProps();
    // Each prop's drawn instance (its model and index in m_propWorlds), or model -1 when it isn't drawn; and edits.
    struct PropRef { int model = -1; size_t index = 0; };
    std::vector<PropRef> m_propRefs;
    std::map<int, Roads::PropEdit> m_propEdits;
    bool PropWorld(const Roads::Prop& prop, XMFLOAT4X4& world);
    // A drawn prop's bounds in its own frame: its model's, with a street-name sign's blades as long as they're drawn.
    void PropLocalBounds(const PropRef& ref, XMFLOAT3& low, XMFLOAT3& high) const;
    std::vector<DebugPolyline> m_roadOverlay;   // Built on first use: the centerlines on the ground, in world space.
    void LoadRoads(const std::wstring& file);
    void AddRoadOverlay(RenderData& renderData);
    XMFLOAT3 m_offset = {};             // The authored position, added to every vertex.
    GeometryBufferData::BufferOffsets m_offsets = {};   // Index pattern, and the pool's first vertex and node size.
    RenderUnit* m_renderUnit = nullptr;

    std::vector<Slot> m_slots;
    std::vector<UINT> m_freeSlots;
    std::unordered_map<uint32_t, UINT> m_resident;     // Node index -> slot.
    std::unordered_set<uint32_t> m_split, m_wasSplit;  // Nodes split this frame and last, for hysteresis.
    std::vector<std::pair<Node, UINT>> m_selected;     // This frame's nodes and their slots.
    UINT m_frame = 0;
    UINT m_loadedThisFrame = 0;
    UINT m_missing = 0;
    UINT m_pending = 0;
    bool m_synchronous = false;                         // This frame loads what it needs at once.
    std::vector<LoadRequest> m_wanted;                  // This frame's requests.
    std::unordered_map<uint32_t, size_t> m_wantedIndex;     // Node -> its entry in m_wanted.

    // The loader thread. Under m_loadLock: its queue (best last), what it is loading, and what it has finished.
    std::thread m_loader;
    std::mutex m_loadLock;
    std::condition_variable m_loadWake;
    bool m_stopLoader = false;
    std::vector<LoadRequest> m_queue;
    std::unordered_set<uint32_t> m_loading;
    std::vector<LoadResult> m_loaded;
    std::vector<std::vector<SimpleVertex>> m_spareVertices;
    std::vector<uint16_t> m_block;
    std::vector<SimpleVertex> m_vertices;
    std::unordered_map<uint64_t, std::vector<uint16_t>> m_heightBlocks;     // HeightAt's cache of finest blocks.
    // A unique address per node, its DXR owner: a node's BLAS stays valid while the node keeps its slot.
    std::unordered_map<uint32_t, char> m_nodeOwners;
};

#pragma once
#include "WorldObject.h"
#include "TerrainPackage.h"
#include "RoadNetwork.h"
#include "RoadMesh.h"
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct RenderUnit;
struct GltfAsset;

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
    // The roadside prop models by Roads::PropModel (Models/Roadside/<name>.glb); a null one draws nothing.
    std::array<std::shared_ptr<GltfAsset>, static_cast<size_t>(Roads::PropModel::Count)> propModels;
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
    unsigned int GetNumInstances() override { return 1 + RoadParts() + PropInstances(); }
    unsigned int GetMaxInstances() override { return 1 + RoadParts() + PropInstances(); }
    // Each drawn node is a ray-tracing geometry of its own; so is each road surface part and prop instance.
    unsigned int GetMaxDrawnInstances() override { return Params().slots + RoadParts() + PropInstances(); }
    // The props drawn of a model (Roads::PropModel), for tests.
    UINT PropCount(Roads::PropModel model) const { return static_cast<UINT>(m_propWorlds[static_cast<size_t>(model)].size()); }

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
    // Roadside props: each one's transform (turned to its heading, on the ground) by model, and a batch per part
    // (primitive and material) of each model in use, drawn instanced once per prop of the model, like a glTF model's.
    static constexpr size_t PropModels = static_cast<size_t>(Roads::PropModel::Count);
    static constexpr float PropHeight = 3.5f;   // Meters: the tallest prop, which the terrain's bounds reach above it.
    std::array<std::vector<XMFLOAT4X4>, PropModels> m_propWorlds;
    struct PropBatch
    {
        size_t model = 0, primitive = 0, material = 0;
        XMFLOAT4X4 transform;           // The part's transform within its model.
        RenderUnit* renderUnit = nullptr;
    };
    std::vector<PropBatch> m_propBatches;
    UINT m_propInstances = 0;
    UINT PropInstances() const { return m_propInstances; }
    void PlaceProps();
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

#pragma once
#include "WorldObject.h"
#include "TerrainPackage.h"
#include <unordered_map>
#include <unordered_set>

struct RenderUnit;

// A heightfield terrain drawn from a terrain package (TerrainPackage.h), CDLOD style: each frame a quadtree walk from
// the root chooses nodes by distance, finer near the camera, and each chosen node is one draw of a shared 64 x 64-quad
// grid, with skirts hiding the cracks where levels meet. A node's vertices (world space) live in a slot of a pool the
// terrain reserves in the shared vertex buffer; nodes load into free or least recently used slots and upload through
// the render frame. A node splits only when its four children are loaded, so its coarser self draws until then.
class TerrainParams : public WorldObjectParams
{
public:
    TerrainParams() : WorldObjectParams(TerrainGeneratorType) {}
    std::wstring packageDirectory;  // Resolved package directory.
    std::string source;             // As authored, for saving.
    float lodRange = 2.0f;          // A node splits within lodRange x its size of the camera.
    UINT slots = 512;               // Vertex pool size, in nodes.
};

class Terrain : public WorldObject
{
public:
    static constexpr UINT GridVertices = TerrainFormat::NodeVertices * TerrainFormat::NodeVertices;
    static constexpr UINT VerticesPerNode = GridVertices + 4 * TerrainFormat::NodeVertices;   // Grid, then skirts.
    static constexpr float Hysteresis = 0.1f;   // A split node stays split out to (1 + Hysteresis) x its range.

    explicit Terrain(TerrainParams* params);
    ObjectType GetObjectType() override { return ObjectType_Terrain; }
    // False with Error() set when the package can't be opened.
    bool IsOpen() const { return m_open; }
    const std::string& Error() const { return m_package.Error(); }
    const TerrainPackage& Package() const { return m_package; }

    void AppendGeometry(GeometryBufferData& geometryData) override;
    HRESULT InitGraphics(RenderManager& renderManager) override;
    HRESULT CleanUpDeviceObjects() override;
    HRESULT ComputeConstants(IRenderFrame* frame) override;
    unsigned int GetNumInstances() override { return 1; }
    unsigned int GetMaxInstances() override { return 1; }
    // Each drawn node is a ray-tracing geometry of its own.
    unsigned int GetMaxDrawnInstances() override { return Params().slots; }

    // The terrain's height at world (x, z) from the package's finest level, bilinear between samples; false outside
    // the terrain or when the block can't be read. Reads the package (not thread safe with itself).
    bool HeightAt(float x, float z, float& height);

    // This frame's selection, for profiling and tests.
    UINT SelectedNodes() const { return static_cast<UINT>(m_selected.size()); }
    UINT ResidentNodes() const { return static_cast<UINT>(m_resident.size()); }
    UINT LoadedThisFrame() const { return m_loadedThisFrame; }

private:
    struct Node { int level, x, z; };
    struct Slot
    {
        uint32_t node = UINT32_MAX;     // Package node index, or UINT32_MAX when free.
        UINT lastUsed = 0;              // Frame the node was last selected or split.
    };

    TerrainParams& Params() { return static_cast<TerrainParams&>(*_params); }
    void NodeBounds(const Node& node, XMFLOAT3 bounds[2]) const;
    bool Ensure(const Node& node, IRenderFrame* frame);
    void Select(const Node& node, const XMFLOAT3& eye, IRenderFrame* frame);
    void BuildVertices(const Node& node, const uint16_t* block, SimpleVertex* vertices) const;

    TerrainPackage m_package;
    bool m_open = false;
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
    std::vector<uint16_t> m_block;
    std::vector<SimpleVertex> m_vertices;
    std::unordered_map<uint64_t, std::vector<uint16_t>> m_heightBlocks;     // HeightAt's cache of finest blocks.
    // A unique address per node, its DXR owner: a node's BLAS stays valid while the node keeps its slot.
    std::unordered_map<uint32_t, char> m_nodeOwners;
};

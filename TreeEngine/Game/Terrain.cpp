#include "pch.h"
#include "Terrain.h"
#include "RenderManager.h"

using namespace TerrainFormat;

Terrain::Terrain(TerrainParams* params) : WorldObject(params)
{
    m_open = m_package.Open(params->packageDirectory);
    m_offset = params->position;
    m_block.resize(size_t(BlockSamples) * BlockSamples);
    m_vertices.resize(VerticesPerNode);
    if (m_open)
    {
        // Whole-terrain bounds until the first frame narrows them.
        const TerrainManifest& m = m_package.Manifest();
        const float half = static_cast<float>(m.worldSize / 2);
        _boundingBox[0] = XMFLOAT3(m_offset.x - half, m_offset.y + m.minHeight, m_offset.z - half);
        _boundingBox[1] = XMFLOAT3(m_offset.x + half, m_offset.y + m.maxHeight, m_offset.z + half);
    }
}

void Terrain::AppendGeometry(GeometryBufferData& geometryData)
{
    // The index pattern every node draws: the grid's quads, then each edge's skirt in both windings (a skirt hangs
    // below a crack and is seen from either side).
    m_offsets.IndexOffset = static_cast<UINT>(geometryData.indices.size());
    auto& indices = geometryData.indices;
    const UINT n = NodeVertices;
    for (UINT b = 0; b < NodeQuads; ++b)
        for (UINT a = 0; a < NodeQuads; ++a)
        {
            const UINT v = b * n + a;
            indices.insert(indices.end(), { v, v + n, v + 1, v + 1, v + n, v + n + 1 });
        }
    for (UINT edge = 0; edge < 4; ++edge)
        for (UINT k = 0; k < NodeQuads; ++k)
        {
            // Edges: -z (b = 0), +z (b = 64), -x (a = 0), +x (a = 64).
            const auto grid = [&](UINT i)
            {
                switch (edge)
                {
                case 0: return i;
                case 1: return NodeQuads * n + i;
                case 2: return i * n;
                default: return i * n + NodeQuads;
                }
            };
            const UINT t0 = grid(k), t1 = grid(k + 1);
            const UINT s0 = GridVertices + edge * n + k, s1 = s0 + 1;
            indices.insert(indices.end(), { t0, t1, s0, s0, t1, s1, t0, s0, t1, s0, s1, t1 });
        }
    m_offsets.IndexCount = static_cast<UINT>(geometryData.indices.size()) - m_offsets.IndexOffset;

    // The vertex pool, filled as nodes load.
    m_offsets.VertexOffset = static_cast<UINT>(geometryData.vertices.size());
    m_offsets.VertexCount = VerticesPerNode;
    geometryData.vertices.resize(geometryData.vertices.size() + size_t(Params().slots) * VerticesPerNode, SimpleVertex{});
}

HRESULT Terrain::InitGraphics(RenderManager& renderManager)
{
    HRR(CleanUpDeviceObjects());
    if (_params->materials.empty())
        HRR(AddDefaultPbrMaterial(*_params, XMFLOAT4(0.25f, 0.35f, 0.15f, 1), 0.9f, nullptr));
    if (_params->textureFilename.empty())
        _params->textureFilename.push_back(L"");

    wchar_t name[64];
    swprintf(name, 64, L"terrain%d", _id);
    ShaderMaterial shaderMaterial = _params->materials[0];
    shaderMaterial.ApplyTextureCoordScale(_params->textureCoordScale.x, _params->textureCoordScale.y);
    StockRenderState renderState;
    renderState.samplerState = StockSamplerStates::MinMagMipLinearUVWWrap;
    Material* material = nullptr;
    const auto* maps = _params->MaterialTextures(0);
    if (maps && !maps->Empty())
    {
        HRR(renderManager.BindMaterialTextures(*maps, shaderMaterial));
        HRR(renderManager.CreateMaterial(name, maps->roles[BaseColorTextureRole].get(), nullptr, nullptr, nullptr, nullptr,
            shaderMaterial, renderState, &material));
    }
    else
        HRR(renderManager.CreateMaterial(name, _params->textureFilename[0].empty() ? nullptr : _params->textureFilename[0].c_str(),
            nullptr, nullptr, nullptr, nullptr, shaderMaterial, renderState, &material));

    Mesh* mesh = nullptr;
    HRR(renderManager.CreateMesh(name, renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
        renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), &m_offsets, BASIC_INPUT_LAYOUT, &mesh));
    HRR(renderManager.ReserveRenderUnit(material, mesh, this, &m_renderUnit));
    m_renderUnit->pooledRayTracingGeometry = true;

    // A fresh vertex buffer: every slot is empty.
    m_slots.assign(Params().slots, Slot());
    m_freeSlots.clear();
    for (UINT slot = Params().slots; slot-- > 0;)
        m_freeSlots.push_back(slot);
    m_resident.clear();
    m_split.clear();
    m_wasSplit.clear();
    return S_OK;
}

HRESULT Terrain::CleanUpDeviceObjects()
{
    m_renderUnit = nullptr;
    m_resident.clear();
    m_slots.clear();
    m_freeSlots.clear();
    m_selected.clear();
    return WorldObject::CleanUpDeviceObjects();
}

void Terrain::NodeBounds(const Node& node, XMFLOAT3 bounds[2]) const
{
    const TerrainManifest& m = m_package.Manifest();
    const uint64_t index = TerrainPackage::NodeIndex(node.level, node.x, node.z);
    const double size = m.NodeSize(node.level), half = m.worldSize / 2;
    bounds[0] = XMFLOAT3(static_cast<float>(-half + node.x * size) + m_offset.x, m.Height(m_package.MinValue(index)) + m_offset.y,
        static_cast<float>(-half + node.z * size) + m_offset.z);
    bounds[1] = XMFLOAT3(static_cast<float>(-half + (node.x + 1) * size) + m_offset.x, m.Height(m_package.MaxValue(index)) + m_offset.y,
        static_cast<float>(-half + (node.z + 1) * size) + m_offset.z);
}

// World-space vertices for a node: the grid with normals from neighboring samples (the block's border reaches past
// the node, so edges match their neighbors), world-meter UVs, then each edge's skirt hanging below it.
void Terrain::BuildVertices(const Node& node, const uint16_t* block, SimpleVertex* vertices) const
{
    const TerrainManifest& m = m_package.Manifest();
    const float spacing = static_cast<float>(m.NodeSpacing(node.level));
    const double size = m.NodeSize(node.level), half = m.worldSize / 2;
    const float x0 = static_cast<float>(-half + node.x * size), z0 = static_cast<float>(-half + node.z * size);
    const auto height = [&](int a, int b) { return m.Height(block[size_t(b + 1) * BlockSamples + (a + 1)]); };
    for (int b = 0; b < NodeVertices; ++b)
        for (int a = 0; a < NodeVertices; ++a)
        {
            const float dx = (height(a + 1, b) - height(a - 1, b)) / (2 * spacing);
            const float dz = (height(a, b + 1) - height(a, b - 1)) / (2 * spacing);
            const float x = x0 + a * spacing, z = z0 + b * spacing;
            SimpleVertex& v = vertices[b * NodeVertices + a];
            v.Pos = XMFLOAT3(x + m_offset.x, height(a, b) + m_offset.y, z + m_offset.z);
            XMStoreFloat3(&v.Normal, XMVector3Normalize(XMVectorSet(-dx, 1, -dz, 0)));
            v.Tex = XMFLOAT2(x, -z);
            XMFLOAT3 tangent;
            XMStoreFloat3(&tangent, XMVector3Normalize(XMVectorSet(1, dx, 0, 0)));
            v.TangentU = XMFLOAT4(tangent.x, tangent.y, tangent.z, 1);
        }
    const float skirt = 3 * spacing + 1;
    for (int edge = 0; edge < 4; ++edge)
        for (int k = 0; k < NodeVertices; ++k)
        {
            const int a = edge == 2 ? 0 : edge == 3 ? NodeQuads : k;
            const int b = edge == 0 ? 0 : edge == 1 ? NodeQuads : k;
            SimpleVertex v = vertices[b * NodeVertices + a];
            v.Pos.y -= skirt;
            vertices[GridVertices + edge * NodeVertices + k] = v;
        }
}

// Makes a node resident, loading it into a free slot or the least recently used one not needed this frame.
bool Terrain::Ensure(const Node& node, IRenderFrame* frame)
{
    const uint32_t index = static_cast<uint32_t>(TerrainPackage::NodeIndex(node.level, node.x, node.z));
    const auto found = m_resident.find(index);
    if (found != m_resident.end())
    {
        m_slots[found->second].lastUsed = m_frame;
        return true;
    }
    UINT slot = UINT_MAX;
    if (!m_freeSlots.empty())
    {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
    }
    else
    {
        UINT oldest = m_frame;
        for (UINT s = 0; s < m_slots.size(); ++s)
            if (m_slots[s].lastUsed < oldest)
            {
                oldest = m_slots[s].lastUsed;
                slot = s;
            }
        if (slot == UINT_MAX)
            return false;
        m_resident.erase(m_slots[slot].node);
    }
    if (!m_package.ReadBlock(node.level, node.x, node.z, m_block.data()))
    {
        m_slots[slot] = Slot();
        m_freeSlots.push_back(slot);
        return false;
    }
    BuildVertices(node, m_block.data(), m_vertices.data());
    if (FAILED(frame->UploadVertices(m_offsets.VertexOffset + slot * VerticesPerNode, m_vertices.data(), VerticesPerNode)))
    {
        m_slots[slot] = Slot();
        m_freeSlots.push_back(slot);
        return false;
    }
    m_slots[slot].node = index;
    m_slots[slot].lastUsed = m_frame;
    m_resident.emplace(index, slot);
    ++m_loadedThisFrame;
    return true;
}

// CDLOD selection: split while the camera is within the node's range (further for a node split last frame), and only
// when all four children are resident; otherwise the node draws itself.
void Terrain::Select(const Node& node, const XMFLOAT3& eye, IRenderFrame* frame)
{
    if (!Ensure(node, frame))
        return;
    const uint32_t index = static_cast<uint32_t>(TerrainPackage::NodeIndex(node.level, node.x, node.z));
    bool split = false;
    if (node.level + 1 < m_package.Manifest().levels)
    {
        XMFLOAT3 bounds[2];
        NodeBounds(node, bounds);
        const float dx = std::max({ bounds[0].x - eye.x, 0.0f, eye.x - bounds[1].x });
        const float dy = std::max({ bounds[0].y - eye.y, 0.0f, eye.y - bounds[1].y });
        const float dz = std::max({ bounds[0].z - eye.z, 0.0f, eye.z - bounds[1].z });
        const float range = Params().lodRange * static_cast<float>(m_package.Manifest().NodeSize(node.level))
            * (m_wasSplit.count(index) ? 1 + Hysteresis : 1);
        if (dx * dx + dy * dy + dz * dz < range * range)
        {
            split = true;
            for (int child = 0; child < 4 && split; ++child)
                split = Ensure({ node.level + 1, 2 * node.x + (child & 1), 2 * node.z + (child >> 1) }, frame);
        }
    }
    if (split)
    {
        m_split.insert(index);
        for (int child = 0; child < 4; ++child)
            Select({ node.level + 1, 2 * node.x + (child & 1), 2 * node.z + (child >> 1) }, eye, frame);
    }
    else
        m_selected.emplace_back(node, m_resident.at(index));
}

HRESULT Terrain::ComputeConstants(IRenderFrame* frame)
{
    UINT instance = 0;
    HRR(frame->GetInstanceIndex(this, instance));
    InstancedData& data = frame->GetRenderData().instanceData[instance];
    XMStoreFloat4x4(&data.World, XMMatrixIdentity());
    data.InstanceOffset = data.InstanceOffsetPrev = instance;
    data.AtlasRegion = 0;
    data.PreviousSlot = instance;
    // Nodes draw through AddDraw, not the reservation's range.
    HRR(frame->SetInstances(m_renderUnit, this, instance, 0));
    m_selected.clear();
    m_loadedThisFrame = 0;
    if (!m_open || m_slots.empty())
        return S_OK;

    ++m_frame;
    std::swap(m_split, m_wasSplit);
    m_split.clear();
    XMFLOAT3 eye;
    XMStoreFloat3(&eye, frame->GetRenderData().eyePos);
    Select({ 0, 0, 0 }, eye, frame);

    XMVECTOR minimum = XMVectorReplicate(FLT_MAX), maximum = XMVectorReplicate(-FLT_MAX);
    for (const auto& selected : m_selected)
    {
        XMFLOAT3 bounds[2];
        NodeBounds(selected.first, bounds);
        const uint32_t node = m_slots[selected.second].node;
        HRR(frame->AddDraw(m_renderUnit, this, instance, 1, static_cast<INT>(m_offsets.VertexOffset + selected.second * VerticesPerNode),
            &m_nodeOwners[node], bounds));
        // The scene box (which reaches shadow casters toward the sun) takes the terrain near the camera only.
        if (bounds[1].x > eye.x - 512 && bounds[0].x < eye.x + 512 && bounds[1].z > eye.z - 512 && bounds[0].z < eye.z + 512)
        {
            minimum = XMVectorMin(minimum, XMLoadFloat3(&bounds[0]));
            maximum = XMVectorMax(maximum, XMLoadFloat3(&bounds[1]));
        }
    }
    if (XMVector3LessOrEqual(minimum, maximum))
    {
        XMStoreFloat3(&_boundingBox[0], minimum);
        XMStoreFloat3(&_boundingBox[1], maximum);
    }
    return S_OK;
}

bool Terrain::HeightAt(float x, float z, float& height)
{
    if (!m_open)
        return false;
    const TerrainManifest& m = m_package.Manifest();
    const int level = m.levels - 1;
    const int last = NodeQuads << level;
    const double u = (x - m_offset.x + m.worldSize / 2) / m.sampleSpacing, v = (z - m_offset.z + m.worldSize / 2) / m.sampleSpacing;
    if (!(u >= 0 && v >= 0 && u <= last && v <= last))
        return false;
    const int i0 = std::min(static_cast<int>(u), last - 1), j0 = std::min(static_cast<int>(v), last - 1);
    const int nx = i0 / NodeQuads, nz = j0 / NodeQuads;
    auto& block = m_heightBlocks[TerrainPackage::NodeIndex(level, nx, nz)];
    if (block.empty())
    {
        block.resize(size_t(BlockSamples) * BlockSamples);
        if (!m_package.ReadBlock(level, nx, nz, block.data()))
        {
            m_heightBlocks.erase(TerrainPackage::NodeIndex(level, nx, nz));
            return false;
        }
    }
    const int a = i0 - nx * NodeQuads + 1, b = j0 - nz * NodeQuads + 1;
    const float fu = static_cast<float>(u - i0), fv = static_cast<float>(v - j0);
    const auto at = [&](int da, int db) { return m.Height(block[size_t(b + db) * BlockSamples + a + da]); };
    height = (at(0, 0) * (1 - fu) + at(1, 0) * fu) * (1 - fv) + (at(0, 1) * (1 - fu) + at(1, 1) * fu) * fv + m_offset.y;
    return true;
}

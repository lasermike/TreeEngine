#include "pch.h"
#include "Terrain.h"
#include "GltfLoader.h"
#include "RenderManager.h"
#include "SceneDefinition.h"
#include "SignText.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

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
        if (!params->roadsFile.empty())
            LoadRoads(params->roadsFile);
    }
}

void Terrain::LoadRoads(const std::wstring& file)
{
    std::ifstream input(file, std::ios::binary);
    if (!input)
    {
        m_roadsError = "cannot read the roads file";
        return;
    }
    std::stringstream text;
    text << input.rdbuf();
    std::string crs;
    double originX = 0, originY = 0;
    if (!Roads::PackageFrame(m_package.Manifest().sourceJson, m_package.Manifest().worldSize, crs, originX, originY, m_roadsError))
        return;
    m_roadsFile = file;
    m_roadsOriginX = originX;
    m_roadsOriginY = originY;
    try
    {
        m_roads = Roads::Parse(text.str(), crs, originX, originY);
    }
    catch (const std::runtime_error& error)
    {
        m_roadsError = error.what();
        return;
    }
    // The surfaces, on the ground as the package has it (HeightAt reads the finest blocks it needs); trails in tiles of
    // their own, on each level's triangles.
    m_roadMesh = Roads::BuildMesh(m_roads, m_offset, [this](float x, float z, float& y) { return HeightAt(x, z, y); }, false);
    for (int part = 0; part < static_cast<int>(Roads::Part::Count); ++part)
        if (!m_roadMesh.parts[part].indices.empty())
            m_roadParts.push_back({ static_cast<Roads::Part>(part) });
    BuildTrailTiles();
    const size_t trail = static_cast<size_t>(Roads::Part::Trail);
    if (!m_trailTiles.empty() && Params().loadTrailMaterialTextures && trail < Params().roadMaterialTextures.size())
    {
        try
        {
            Params().roadMaterialTextures[trail] = Params().loadTrailMaterialTextures();
        }
        catch (const std::runtime_error& error)
        {
            m_roadsError = std::string("the trail material: ") + error.what();
            return;
        }
    }
    PlaceProps();
    m_heightBlocks.clear();     // HeightAt's cache: not needed again.
}

// Trails on each level's own triangles, a tile per node they cross, for the levels fine enough to show a trail. A
// level's heights are its blocks' samples, exactly what the terrain draws there.
void Terrain::BuildTrailTiles()
{
    const Roads::TrailBuilder builder(m_roads, m_offset);
    if (builder.Empty())
        return;
    float minX, minZ, maxX, maxZ;
    builder.Bounds(minX, minZ, maxX, maxZ);
    const TerrainManifest& m = m_package.Manifest();
    const double half = m.worldSize / 2;
    for (int level = m.levels - 1; level >= 0 && Roads::DrawsTrails(m.NodeSpacing(level), level == m.levels - 1); --level)
    {
        const double size = m.NodeSize(level), spacing = m.NodeSpacing(level);
        const int nodes = 1 << level;
        std::unordered_map<uint64_t, std::vector<uint16_t>> blocks;
        // A node's height at grid node (i, j) of this level (exact: queries land on grid nodes).
        const auto height = [&](float x, float z, float& y)
        {
            const long long i = std::llround((x - m_offset.x + half) / spacing), j = std::llround((z - m_offset.z + half) / spacing);
            const long long last = static_cast<long long>(NodeQuads) * nodes;
            if (i < 0 || j < 0 || i > last || j > last)
                return false;
            const int nx = static_cast<int>(std::min<long long>(i / NodeQuads, nodes - 1));
            const int nz = static_cast<int>(std::min<long long>(j / NodeQuads, nodes - 1));
            const uint64_t index = TerrainPackage::NodeIndex(level, nx, nz);
            auto& block = blocks[index];
            if (block.empty())
            {
                block.resize(size_t(BlockSamples) * BlockSamples);
                if (!m_package.ReadBlock(level, nx, nz, block.data()))
                {
                    blocks.erase(index);       // Not a block of zeros next time: read again, or fail again.
                    return false;
                }
            }
            const int a = static_cast<int>(i - nx * NodeQuads), b = static_cast<int>(j - nz * NodeQuads);
            y = m.Height(block[size_t(b + 1) * BlockSamples + a + 1]) + m_offset.y;
            return true;
        };
        Roads::GroundGrid grid;
        grid.spacing = static_cast<float>(spacing);
        grid.originX = static_cast<float>(m_offset.x - half);
        grid.originZ = static_cast<float>(m_offset.z - half);
        const int x0 = std::max(0, static_cast<int>(std::floor((minX - grid.originX) / size)));
        const int x1 = std::min(nodes - 1, static_cast<int>(std::floor((maxX - grid.originX) / size)));
        const int z0 = std::max(0, static_cast<int>(std::floor((minZ - grid.originZ) / size)));
        const int z1 = std::min(nodes - 1, static_cast<int>(std::floor((maxZ - grid.originZ) / size)));
        for (int nz = z0; nz <= z1; ++nz)
            for (int nx = x0; nx <= x1; ++nx)
            {
                TrailTile tile;
                tile.mesh = builder.Build(grid, nx * NodeQuads, nz * NodeQuads, nx * NodeQuads + NodeQuads - 1,
                    nz * NodeQuads + NodeQuads - 1, height);
                if (tile.mesh.indices.empty())
                    continue;
                XMVECTOR low = XMVectorReplicate(FLT_MAX), high = XMVectorReplicate(-FLT_MAX);
                for (const SimpleVertex& v : tile.mesh.vertices)
                {
                    low = XMVectorMin(low, XMLoadFloat3(&v.Pos));
                    high = XMVectorMax(high, XMLoadFloat3(&v.Pos));
                }
                XMStoreFloat3(&tile.bounds[0], low);
                XMStoreFloat3(&tile.bounds[1], high);
                m_trailTileOf[static_cast<uint32_t>(TerrainPackage::NodeIndex(level, nx, nz))] = m_trailTiles.size();
                m_trailTiles.push_back(std::move(tile));
            }
    }
}

// Each prop stands on the ground at its point (the package's finest heights), turned to its heading: its model's
// front (+z) faces that many degrees clockwise from north. Each model in use loads once; props off the terrain, or
// whose model isn't installed, are left out.
bool Terrain::PropWorld(const Roads::Prop& prop, XMFLOAT4X4& world)
{
    const float x = prop.point.x + m_offset.x, z = prop.point.y + m_offset.z;
    float y = 0;
    if (!HeightAt(x, z, y))
        return false;
    XMStoreFloat4x4(&world, XMMatrixRotationY(XMConvertToRadians(prop.heading)) * XMMatrixTranslation(x, y, z));
    return true;
}

void Terrain::PlaceProps()
{
    std::array<bool, PropModels> asked = {};
    m_propRefs.assign(m_roads.props.size(), PropRef());
    for (size_t p = 0; p < m_roads.props.size(); ++p)
    {
        const Roads::Prop& prop = m_roads.props[p];
        const size_t model = static_cast<size_t>(prop.model);
        if (!asked[model])
        {
            asked[model] = true;
            if (!Params().propModels[model] && Params().loadPropModel)
                Params().propModels[model] = Params().loadPropModel(prop.model);
        }
        if (!Params().propModels[model])
        {
            ++m_propsWithoutModel;
            continue;
        }
        XMFLOAT4X4 world;
        if (!PropWorld(prop, world))
            continue;
        m_propRefs[p] = { static_cast<int>(model), m_propWorlds[model].size() };
        m_propWorlds[model].push_back(world);
        if (prop.model == Roads::PropModel::StreetNameSign)
        {
            // "LOWER|UPPER": the lower blade names the road it runs along, the upper the other (one name: both).
            const size_t bar = prop.text.find('|');
            const std::string names[2] = { prop.text.substr(0, bar),
                bar == std::string::npos ? prop.text : prop.text.substr(bar + 1) };
            std::array<UINT, 2> blades = {};
            for (int b = 0; b < 2; ++b)
            {
                const auto found = std::find(m_signNames.begin(), m_signNames.end(), names[b]);
                blades[b] = static_cast<UINT>(found - m_signNames.begin());
                if (found == m_signNames.end())
                    m_signNames.push_back(names[b]);
            }
            m_signBlades.push_back(blades);
        }
    }
    for (size_t model = 0; model < PropModels; ++model)
    {
        if (m_propWorlds[model].empty())
            continue;
        const size_t first = m_propBatches.size();
        for (const GltfRenderPart& part : Params().propModels[model]->renderParts)
        {
            // Parts sharing a primitive and material (one mesh on several nodes) share a batch.
            auto batch = std::find_if(m_propBatches.begin() + first, m_propBatches.end(), [&](const PropBatch& b)
                { return b.primitive == part.primitiveIndex && b.material == part.materialIndex; });
            if (batch == m_propBatches.end())
            {
                m_propBatches.emplace_back();
                batch = m_propBatches.end() - 1;
                batch->model = model;
                batch->primitive = part.primitiveIndex;
                batch->material = part.materialIndex;
            }
            batch->transforms.push_back(part.transform);
            m_propInstances += static_cast<UINT>(m_propWorlds[model].size());
            if (model == static_cast<size_t>(Roads::PropModel::StreetNameSign))
            {
                const std::string& material = Params().propModels[model]->materials[part.materialIndex].name;
                // A blade's faces and edges stretch with its name; other materials (a bracket, say) stay as made.
                batch->blade = SignText::BladeMaterial(material, batch->face);
            }
        }
    }
    BuildSignText();
}

void Terrain::PropLocalBounds(const PropRef& ref, XMFLOAT3& low, XMFLOAT3& high) const
{
    const GltfAsset& asset = *Params().propModels[ref.model];
    low = asset.boundsMin;
    high = asset.boundsMax;
    if (ref.model == static_cast<int>(Roads::PropModel::StreetNameSign) && ref.index < m_signBlades.size() && SignNames())
    {
        // The lower blade runs along the model's x, the upper along z, each centered on the post.
        const float alongX = std::max(1.0f, m_signStretch[m_signBlades[ref.index][0]]);
        const float alongZ = std::max(1.0f, m_signStretch[m_signBlades[ref.index][1]]);
        low.x *= alongX;
        high.x *= alongX;
        low.z *= alongZ;
        high.z *= alongZ;
    }
}

float Terrain::GroundHit(const XMFLOAT3& origin, const XMFLOAT3& direction, float reach)
{
    // March half a meter at a time until the ray is below the ground, then halve the last step to a centimeter.
    const auto below = [&](float t)
    {
        float ground = 0;
        return HeightAt(origin.x + direction.x * t, origin.z + direction.z * t, ground) && origin.y + direction.y * t <= ground;
    };
    if (below(0))
        return 0;
    for (float t = 0.5f; t <= reach; t += 0.5f)
        if (below(t))
        {
            float low = t - 0.5f, high = t;
            while (high - low > 0.01f)
                (below((low + high) / 2) ? high : low) = (low + high) / 2;
            return high;
        }
    return -1;
}

int Terrain::PickProp(const XMFLOAT3& origin, const XMFLOAT3& direction, float* distance)
{
    int best = -1;
    float nearest = 1e30f;
    for (size_t p = 0; p < m_propRefs.size(); ++p)
    {
        const PropRef& ref = m_propRefs[p];
        if (ref.model < 0 || !Params().propModels[ref.model])
            continue;
        // The ray in the prop's own frame, against its model's bounds.
        const XMMATRIX world = XMLoadFloat4x4(&m_propWorlds[ref.model][ref.index]);
        XMVECTOR determinant;
        const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
        if (XMVectorGetX(determinant) == 0)
            continue;       // Removed: hidden at zero scale.
        XMFLOAT3 localOrigin, localDirection;
        XMStoreFloat3(&localOrigin, XMVector3TransformCoord(XMLoadFloat3(&origin), inverse));
        XMStoreFloat3(&localDirection, XMVector3TransformNormal(XMLoadFloat3(&direction), inverse));
        XMFLOAT3 low, high;
        PropLocalBounds(ref, low, high);
        const float t = Roads::RayBox(localOrigin, localDirection, low, high);
        if (t >= 0 && t < nearest)       // Rotations and translations keep distances.
        {
            nearest = t;
            best = static_cast<int>(p);
        }
    }
    // The nearest prop the ray hits counts only if the ground doesn't come first (a hillside hides what's behind
    // it); its foot stands on the ground, so the ground may come a little after the prop's bounds. Any prop further
    // on is behind that ground too.
    if (best >= 0)
    {
        const float ground = GroundHit(origin, direction, nearest);
        if (ground >= 0 && ground + 0.25f < nearest)
            best = -1;
    }
    if (distance)
        *distance = best >= 0 ? nearest : -1;
    return best;
}

bool Terrain::PropBounds(int prop, XMFLOAT3 bounds[2]) const
{
    if (prop < 0 || prop >= static_cast<int>(m_propRefs.size()) || m_propRefs[prop].model < 0)
        return false;
    const PropRef& ref = m_propRefs[prop];
    XMFLOAT3 lo, hi;
    PropLocalBounds(ref, lo, hi);
    const XMMATRIX world = XMLoadFloat4x4(&m_propWorlds[ref.model][ref.index]);
    XMVECTOR low = XMVectorReplicate(1e30f), high = XMVectorReplicate(-1e30f);
    for (int corner = 0; corner < 8; ++corner)
    {
        const XMVECTOR point = XMVector3TransformCoord(XMVectorSet(corner & 1 ? hi.x : lo.x,
            corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z, 1), world);
        low = XMVectorMin(low, point);
        high = XMVectorMax(high, point);
    }
    XMStoreFloat3(&bounds[0], low);
    XMStoreFloat3(&bounds[1], high);
    return true;
}

void Terrain::SetProp(int prop, const XMFLOAT2& point, float heading)
{
    if (prop < 0 || prop >= static_cast<int>(m_propRefs.size()) || m_propRefs[prop].model < 0)
        return;
    Roads::Prop& entry = m_roads.props[prop];
    const Roads::Prop before = entry;
    entry.point = point;
    entry.heading = std::fmod(std::fmod(heading, 360.0f) + 360.0f, 360.0f);
    XMFLOAT4X4 world;
    if (!PropWorld(entry, world))
    {
        entry = before;     // Off the terrain: it stays where it was.
        return;
    }
    const PropRef& ref = m_propRefs[prop];
    m_propWorlds[ref.model][ref.index] = world;
    Roads::PropEdit& change = m_propEdits[prop];
    change.id = entry.id;
    change.point = entry.point;
    change.heading = entry.heading;
}

void Terrain::RemoveProp(int prop)
{
    if (prop < 0 || prop >= static_cast<int>(m_propRefs.size()) || m_propRefs[prop].model < 0)
        return;
    const PropRef& ref = m_propRefs[prop];
    // Hidden at zero scale: the instance stays (instance counts are fixed once the terrain is drawn).
    XMStoreFloat4x4(&m_propWorlds[ref.model][ref.index], XMMatrixScaling(0, 0, 0));
    const Roads::Prop& entry = m_roads.props[prop];
    Roads::PropEdit& change = m_propEdits[prop];
    change.id = entry.id;
    change.point = entry.point;
    change.heading = entry.heading;
    change.removed = true;
}

bool Terrain::SaveProps(std::string& message, const std::vector<std::wstring>* only)
{
    if (m_propEdits.empty())
    {
        message = "No prop edits to save.";
        return true;
    }
    std::vector<Roads::PropEdit> edits;
    for (const auto& entry : m_propEdits)
        edits.push_back(entry.second);
    // The file the terrain loaded, and the source the build copied it from when that's what it is
    // (Roads::BuildSource); a roads file loaded from anywhere else is saved alone.
    std::vector<std::wstring> files = only ? *only : std::vector<std::wstring>{ m_roadsFile };
    wchar_t executable[MAX_PATH] = {};
    if (!only && GetModuleFileNameW(nullptr, executable, MAX_PATH))
    {
        std::wstring folder = executable;
        folder.resize(folder.find_last_of(L'\\'));
        const std::wstring source = Roads::BuildSource(m_roadsFile, SceneData::WidePath(Params().roads), folder,
            [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; });
        if (!source.empty())
            files.push_back(source);
    }
    // Every file's new text first (an edit a file can't take saves nothing), then each staged beside its file, and
    // only then each replaced, so a failure leaves the files as they were (or, past the first replacement, says which
    // were saved).
    std::vector<std::string> texts;
    for (const std::wstring& file : files)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            message = "Couldn't read " + SceneData::Utf8Path(file) + "; nothing was saved.";
            return false;
        }
        std::stringstream text;
        text << input.rdbuf();
        try
        {
            texts.push_back(Roads::UpdateProps(text.str(), edits, m_roadsOriginX, m_roadsOriginY));
        }
        catch (const std::runtime_error& error)
        {
            message = std::string(error.what()) + " (" + SceneData::Utf8Path(file) + "); nothing was saved.";
            return false;
        }
    }
    std::vector<std::wstring> staged;
    const auto discard = [&]()
    {
        for (const std::wstring& path : staged)
            DeleteFileW(path.c_str());
    };
    try
    {
        for (size_t i = 0; i < files.size(); ++i)
            staged.push_back(SceneData::StageFile(SceneData::Utf8Path(files[i]), texts[i]));
    }
    catch (const std::runtime_error& error)
    {
        discard();
        message = std::string(error.what()) + "; nothing was saved.";
        return false;
    }
    std::string saved;
    for (size_t i = 0; i < files.size(); ++i)
    {
        try
        {
            SceneData::PublishFile(staged[i], SceneData::Utf8Path(files[i]));
            staged[i].clear();
            saved += (saved.empty() ? "" : " and ") + SceneData::Utf8Path(files[i]);
        }
        catch (const std::runtime_error& error)
        {
            discard();
            message = std::string(error.what()) + (saved.empty() ? "; nothing was saved." : "; only " + saved + " was saved.");
            return false;
        }
    }
    message = "Saved " + std::to_string(edits.size()) + " prop edits to " + saved;
    if (files.size() == 1 && !only)
        message += " (no source copy to update: it was loaded from there, or Game\\Resources wasn't found above the executable)";
    m_propEdits.clear();
    return true;
}

// The street names drawn into an atlas texture (a BMP in memory, for the texture loader), a cell per name.
void Terrain::BuildSignText()
{
    if (m_signNames.empty())
        return;
    const SignText::Atlas atlas = SignText::Build(m_signNames);
    m_signRegions = atlas.regions;
    for (const std::string& name : m_signNames)
        m_signStretch.push_back(SignText::BladeLength(name) / SignText::ModelBladeLength);
    m_signTexture = std::make_shared<GltfTextureSource>();
    m_signTexture->encodedData = atlas.Bmp();
    m_signTexture->mimeType = "image/bmp";
    m_signTexture->colorSpace = 2;      // sRGB.
    m_signTexture->origin = GltfTextureSource::Origin::DataUri;
}

// The centerlines draped on the terrain, every 4 m and half a meter up so they clear the road bed: paved roads
// orange, gravel roads tan, driveways cyan.
void Terrain::AddRoadOverlay(RenderData& renderData)
{
    if (m_roadOverlay.empty())
    {
        for (const Roads::Line& line : m_roads.lines)
        {
            DebugPolyline polyline;
            polyline.lineWidth = line.kind == Roads::Kind::Road ? 3.0f : 2.0f;
            polyline.color = line.kind == Roads::Kind::Trail ? XMFLOAT4(0.55f, 1.0f, 0.25f, 1)
                : line.kind == Roads::Kind::Driveway ? XMFLOAT4(0.2f, 0.9f, 1.0f, 1)
                : line.surface == "gravel" ? XMFLOAT4(0.85f, 0.7f, 0.45f, 1) : XMFLOAT4(1.0f, 0.55f, 0.1f, 1);
            // A point off the terrain ends the polyline drawn so far; the next point on it starts another.
            const auto add = [&](float x, float z)
            {
                float y = 0;
                if (HeightAt(x + m_offset.x, z + m_offset.z, y))
                    polyline.points.push_back(XMFLOAT3(x + m_offset.x, y + 0.5f, z + m_offset.z));
                else if (!polyline.points.empty())
                {
                    if (polyline.points.size() >= 2)
                        m_roadOverlay.push_back(polyline);
                    polyline.points.clear();
                }
            };
            for (size_t i = 0; i + 1 < line.points.size(); ++i)
            {
                const XMFLOAT2 a = line.points[i], b = line.points[i + 1];
                const float length = std::hypot(b.x - a.x, b.y - a.y);
                const int steps = std::max(1, static_cast<int>(std::ceil(length / 4)));
                for (int s = 0; s < steps; ++s)
                    add(a.x + (b.x - a.x) * s / steps, a.y + (b.y - a.y) * s / steps);
            }
            add(line.points.back().x, line.points.back().y);
            if (polyline.points.size() >= 2)
                m_roadOverlay.push_back(std::move(polyline));
        }
    }
    static std::mutex overlayMutex;     // Terrains update in parallel.
    std::lock_guard<std::mutex> lock(overlayMutex);
    renderData.debugLines.insert(renderData.debugLines.end(), m_roadOverlay.begin(), m_roadOverlay.end());
}

Terrain::~Terrain()
{
    StopLoader();
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

    // Road surfaces, each part's indices relative to its first vertex.
    for (RoadPart& road : m_roadParts)
    {
        const Roads::MeshPart& part = m_roadMesh[road.part];
        road.offsets.VertexOffset = static_cast<UINT>(geometryData.vertices.size());
        road.offsets.VertexCount = static_cast<UINT>(part.vertices.size());
        road.offsets.IndexOffset = static_cast<UINT>(geometryData.indices.size());
        road.offsets.IndexCount = static_cast<UINT>(part.indices.size());
        geometryData.vertices.insert(geometryData.vertices.end(), part.vertices.begin(), part.vertices.end());
        geometryData.indices.insert(geometryData.indices.end(), part.indices.begin(), part.indices.end());
    }

    // Trail tiles, each part's indices relative to its first vertex.
    for (TrailTile& tile : m_trailTiles)
    {
        tile.offsets.VertexOffset = static_cast<UINT>(geometryData.vertices.size());
        tile.offsets.VertexCount = static_cast<UINT>(tile.mesh.vertices.size());
        tile.offsets.IndexOffset = static_cast<UINT>(geometryData.indices.size());
        tile.offsets.IndexCount = static_cast<UINT>(tile.mesh.indices.size());
        geometryData.vertices.insert(geometryData.vertices.end(), tile.mesh.vertices.begin(), tile.mesh.vertices.end());
        geometryData.indices.insert(geometryData.indices.end(), tile.mesh.indices.begin(), tile.mesh.indices.end());
    }

    // Prop models (each asset once, however many objects share it).
    for (size_t model = 0; model < PropModels; ++model)
        if (!m_propWorlds[model].empty())
            Params().propModels[model]->AppendGeometry(geometryData);
}

void Terrain::ApplyAntiTiling(ShaderMaterial& material) const
{
    const bool on = Params().antiTiling;
    material.tintTextures.z = on ? 1.0f : 0.0f;
    material.tintTextures.w = on ? Params().variation : 0.0f;
    material.opticalParams.z = on ? Params().variationSize : 0.0f;
}

HRESULT Terrain::SetAntiTiling(RenderManager& renderManager, bool on, float variation, float variationSize)
{
    Params().antiTiling = on;
    Params().variation = (std::max)(0.0f, (std::min)(1.0f, variation));
    Params().variationSize = (std::max)(1.0f, variationSize);
    // Raster and DXR read each material every frame; the layer table is uploaded again.
    for (Material* material : m_antiTiledMaterials)
        ApplyAntiTiling(material->m_shaderMaterial);
    if (m_layerCount)
        HRR(renderManager.UpdateTerrainLayers(m_firstLayer, m_layerCount,
            [this](TerrainLayerData& layer) { ApplyAntiTiling(layer.material); }));
    return S_OK;
}

HRESULT Terrain::InitGraphics(RenderManager& renderManager)
{
    HRR(CleanUpDeviceObjects());
    m_antiTiledMaterials.clear();
    m_firstLayer = m_layerCount = 0;
    if (_params->materials.empty())
        HRR(AddDefaultPbrMaterial(*_params, XMFLOAT4(0.25f, 0.35f, 0.15f, 1), 0.9f, nullptr));
    if (_params->textureFilename.empty())
        _params->textureFilename.push_back(L"");

    wchar_t name[64];
    swprintf(name, 64, L"terrain%d", _id);
    ShaderMaterial shaderMaterial = _params->materials[0];
    ApplyAntiTiling(shaderMaterial);
    shaderMaterial.ApplyTextureCoordScale(_params->textureCoordScale.x, _params->textureCoordScale.y);
    StockRenderState renderState;
    renderState.samplerState = StockSamplerStates::MinMagMipLinearUVWWrap;
    Material* material = nullptr;
    const auto* maps = _params->MaterialTextures(0);
    if (!Params().layers.empty())
    {
        // Layers: each a material of its own in the scene's layer table, maps by texture slot; the draw's material
        // only points at them (Shaders/Terrain.hlsli).
        const auto& layers = Params().layers;
        std::vector<TerrainLayerData> data(layers.size());
        for (size_t i = 0; i < layers.size(); ++i)
        {
            ShaderMaterial layerMaterial = _params->materials[i];
            layerMaterial.ApplyTextureCoordScale(1 / layers[i].tile, 1 / layers[i].tile);
            if (layers[i].biplanar > 0)
                layerMaterial.biplanarParams = XMFLOAT4(1, layers[i].biplanar, 0, 0);
            if (const auto* layerMaps = _params->MaterialTextures(i))
            {
                HRR(renderManager.BindMaterialTextures(*layerMaps, layerMaterial));
                if (layerMaps->roles[BaseColorTextureRole])
                {
                    LoadedTexture* texture = nullptr;
                    int slot = -1;
                    HRR(renderManager.LoadGltfTexture(*layerMaps->roles[BaseColorTextureRole], &texture, &slot));
                    data[i].textures = XMFLOAT4(static_cast<float>(slot), layerMaterial.specularColor.w, 0, 0);
                }
            }
            ApplyAntiTiling(layerMaterial);
            data[i].material = layerMaterial;
            data[i].height = XMFLOAT4(layers[i].height.x, layers[i].height.y, layers[i].height.z, 0);
            data[i].slope = XMFLOAT4(layers[i].slope.x, layers[i].slope.y, layers[i].slope.z, 0);
            data[i].noise = XMFLOAT4(layers[i].noise.x, layers[i].noise.y, layers[i].noise.z, 0);
        }
        const UINT first = renderManager.RegisterTerrainLayers(data.data(), static_cast<UINT>(data.size()));
        m_firstLayer = first;
        m_layerCount = static_cast<UINT>(data.size());
        shaderMaterial = data[0].material;
        shaderMaterial.flags.y = 0;
        shaderMaterial.normalTextures.x = shaderMaterial.metallicRoughnessTextures.x = -1;
        shaderMaterial.specularTextures.x = shaderMaterial.specularTextures.y = shaderMaterial.tintTextures.x = -1;
        shaderMaterial.biplanarParams = XMFLOAT4(0, 1, static_cast<float>(first + 1), static_cast<float>(data.size()));
        HRR(renderManager.CreateMaterial(name, static_cast<const wchar_t*>(nullptr), nullptr, nullptr, nullptr, nullptr,
            shaderMaterial, renderState, &material));
    }
    else if (maps && !maps->Empty())
    {
        HRR(renderManager.BindMaterialTextures(*maps, shaderMaterial));
        HRR(renderManager.CreateMaterial(name, maps->roles[BaseColorTextureRole].get(), nullptr, nullptr, nullptr, nullptr,
            shaderMaterial, renderState, &material));
    }
    else
        HRR(renderManager.CreateMaterial(name, _params->textureFilename[0].empty() ? nullptr : _params->textureFilename[0].c_str(),
            nullptr, nullptr, nullptr, nullptr, shaderMaterial, renderState, &material));

    m_antiTiledMaterials.push_back(material);

    Mesh* mesh = nullptr;
    HRR(renderManager.CreateMesh(name, renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
        renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), &m_offsets, BASIC_INPUT_LAYOUT, &mesh));
    HRR(renderManager.ReserveRenderUnit(material, mesh, this, &m_renderUnit));
    m_renderUnit->pooledRayTracingGeometry = true;

    // Road surfaces: static geometry (the DXR renderer keeps it in its static BLAS); then trail tiles, in their
    // material (the trail part's), drawn with the nodes the terrain selects, so a ray-tracing geometry of their own
    // each, like a node.
    std::vector<RoadPart> trailMaterial;
    if (!m_trailTiles.empty())
        trailMaterial.push_back({ Roads::Part::Trail });
    Material* trailSurface = nullptr;
    std::vector<RoadPart*> surfaces;
    for (RoadPart& road : m_roadParts)
        surfaces.push_back(&road);
    for (RoadPart& road : trailMaterial)
        surfaces.push_back(&road);
    for (RoadPart* part : surfaces)
    {
        RoadPart& road = *part;
        const size_t index = static_cast<size_t>(road.part);
        ShaderMaterial roadMaterial = index < Params().roadMaterials.size() ? Params().roadMaterials[index] : _params->materials[0];
        if (road.part != Roads::Part::YellowLine && road.part != Roads::Part::WhiteLine)
            ApplyAntiTiling(roadMaterial);      // The surfaces, not the paint.
        wchar_t roadName[64];
        swprintf(roadName, 64, L"terrain%d-road%zu", _id, index);
        Material* surface = nullptr;
        StockRenderState roadState;
        roadState.samplerState = StockSamplerStates::MinMagMipLinearUVWWrap;
        const MaterialTextureSources* roadMaps = index < Params().roadMaterialTextures.size() ? &Params().roadMaterialTextures[index] : nullptr;
        if (roadMaps && !roadMaps->Empty())
        {
            HRR(renderManager.BindMaterialTextures(*roadMaps, roadMaterial));
            HRR(renderManager.CreateMaterial(roadName, roadMaps->roles[BaseColorTextureRole].get(), nullptr, nullptr, nullptr, nullptr,
                roadMaterial, roadState, &surface));
        }
        else
            HRR(renderManager.CreateMaterial(roadName, static_cast<const wchar_t*>(nullptr), nullptr, nullptr, nullptr, nullptr,
                roadMaterial, roadState, &surface));
        if (road.part != Roads::Part::YellowLine && road.part != Roads::Part::WhiteLine)
            m_antiTiledMaterials.push_back(surface);
        if (road.part == Roads::Part::Trail && part == surfaces.back() && !trailMaterial.empty())
        {
            trailSurface = surface;
            continue;
        }
        Mesh* roadMesh = nullptr;
        HRR(renderManager.CreateMesh(roadName, renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), &road.offsets, BASIC_INPUT_LAYOUT, &roadMesh));
        HRR(renderManager.ReserveRenderUnit(surface, roadMesh, this, &road.renderUnit));
        road.renderUnit->staticRayTracingGeometry = true;
        road.renderUnit->castsShadows = false;     // A ribbon 6 to 12 cm above the ground casts nothing worth drawing.
    }
    for (size_t t = 0; t < m_trailTiles.size(); ++t)
    {
        TrailTile& tile = m_trailTiles[t];
        wchar_t tileName[64];
        swprintf(tileName, 64, L"terrain%d-trail%zu", _id, t);
        Mesh* tileMesh = nullptr;
        HRR(renderManager.CreateMesh(tileName, renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), &tile.offsets, BASIC_INPUT_LAYOUT, &tileMesh));
        HRR(renderManager.ReserveRenderUnit(trailSurface, tileMesh, this, &tile.renderUnit));
        tile.renderUnit->pooledRayTracingGeometry = true;     // World-space vertices, drawn when their node is.
        tile.renderUnit->castsShadows = false;
    }

    // Props: a batch per model part, its material and mesh named as a glTF model's (so they're shared with one).
    for (PropBatch& batch : m_propBatches)
    {
        const GltfAsset& asset = *Params().propModels[batch.model];
        const std::wstring assetName(asset.sourceFilename.begin(), asset.sourceFilename.end());
        const GltfMaterialData& imported = asset.materials[batch.material];
        const GltfTextureSource* texture = imported.textureIndex >= 0 ? &asset.textures[static_cast<size_t>(imported.textureIndex)] : nullptr;
        StockRenderState state;
        state.samplerState = imported.wrapTexture ? StockSamplerStates::MinMagMipLinearUVWWrap : StockSamplerStates::MinMagMipLinearUVWClamp;
        if (imported.alphaBlend)
        {
            state.blendState = PbrBlendState(imported.shaderMaterial);
            state.depthStencilState = StockDepthStencilStates::DepthLessThanNoZWriteNoStencil;
        }
        ShaderMaterial shaderMaterial = imported.shaderMaterial;
        std::wstring materialName = assetName + L"#material" + std::to_wstring(batch.material);
        if (batch.face && m_signTexture)
        {
            // A street-name blade's face: white times the names atlas, each sign's cell chosen by its instance's
            // atlas region (ComputeConstants).
            texture = m_signTexture.get();
            m_signTexture->key = L"terrain" + std::to_wstring(_id) + L"#sign-names";
            shaderMaterial.Diffuse = XMFLOAT4(1, 1, 1, 1);
            shaderMaterial.Ambient = XMFLOAT4(0.15f, 0.15f, 0.15f, 1);
            shaderMaterial.flags.y = 1;
            state.samplerState = StockSamplerStates::MinMagMipLinearUVWClamp;
            materialName = m_signTexture->key + std::to_wstring(batch.blade);
        }
        const std::pair<int, MaterialTextureRole> maps[] = {
            { imported.specularTextureIndex, SpecularStrengthTextureRole },
            { imported.metallicRoughnessTextureIndex, MetallicRoughnessTextureRole },
            { imported.normalTextureIndex, NormalTextureRole },
            { imported.specularColorTextureIndex, SpecularColorTextureRole } };
        for (const auto& map : maps)
            if (map.first >= 0)
                HRR(renderManager.BindMaterialTexture(asset.textures[map.first], map.second, shaderMaterial));
        Material* material = nullptr;
        HRR(renderManager.CreateMaterial(materialName.c_str(), texture,
            nullptr, nullptr, nullptr, nullptr, shaderMaterial, state, &material));
        Mesh* mesh = nullptr;
        HRR(renderManager.CreateMesh((assetName + L"#primitive" + std::to_wstring(batch.primitive)).c_str(),
            renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            &asset.primitives[batch.primitive].bufferOffsets, BASIC_INPUT_LAYOUT, &mesh));
        HRR(renderManager.ReserveRenderUnit(material, mesh, this, &batch.renderUnit));
        batch.renderUnit->staticRayTracingGeometry = true;     // They never move: the DXR renderer's static BLAS.
    }

    // A fresh vertex buffer: every slot is empty.
    m_slots.assign(Params().slots, Slot());
    m_freeSlots.clear();
    for (UINT slot = Params().slots; slot-- > 0;)
        m_freeSlots.push_back(slot);
    m_resident.clear();
    m_split.clear();
    m_wasSplit.clear();
    m_frame = 0;
    if (m_open)
        StartLoader();
    return S_OK;
}

HRESULT Terrain::CleanUpDeviceObjects()
{
    StopLoader();
    m_renderUnit = nullptr;
    for (RoadPart& road : m_roadParts)
        road.renderUnit = nullptr;
    for (PropBatch& batch : m_propBatches)
        batch.renderUnit = nullptr;
    for (TrailTile& tile : m_trailTiles)
        tile.renderUnit = nullptr;
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
            // The drop rides in the tangent's w (its sign is the bitangent's), so layers cover a skirt seen through a
            // crack as they cover its edge (Shaders/Terrain.hlsli).
            v.TangentU.w = 1 + skirt;
            vertices[GridVertices + edge * NodeVertices + k] = v;
        }
}

float Terrain::Distance(const Node& node, const XMFLOAT3& eye) const
{
    XMFLOAT3 bounds[2];
    NodeBounds(node, bounds);
    const float dx = std::max({ bounds[0].x - eye.x, 0.0f, eye.x - bounds[1].x });
    const float dy = std::max({ bounds[0].y - eye.y, 0.0f, eye.y - bounds[1].y });
    const float dz = std::max({ bounds[0].z - eye.z, 0.0f, eye.z - bounds[1].z });
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool Terrain::Ensure(const Node& node, float priority, bool prefetch, IRenderFrame* frame)
{
    const uint32_t index = Index(node);
    const auto found = m_resident.find(index);
    if (found != m_resident.end())
    {
        (prefetch ? m_slots[found->second].lastPrefetched : m_slots[found->second].lastUsed) = m_frame;
        return true;
    }
    if (m_synchronous)
    {
        if (prefetch || !m_package.ReadBlock(node.level, node.x, node.z, m_block.data()))
            return false;
        BuildVertices(node, m_block.data(), m_vertices.data());
        return Place(node, m_vertices.data(), false, frame);
    }
    const auto wanted = m_wantedIndex.emplace(index, m_wanted.size());
    if (wanted.second)
        m_wanted.push_back({ node, priority, prefetch });
    else
    {
        // Asked for again: keep the more urgent request.
        LoadRequest& request = m_wanted[wanted.first->second];
        request.priority = std::min(request.priority, priority);
        request.prefetch = request.prefetch && prefetch;
    }
    return false;
}

bool Terrain::Place(const Node& node, const SimpleVertex* vertices, bool prefetch, IRenderFrame* frame)
{
    const uint32_t index = Index(node);
    UINT slot = UINT_MAX;
    if (!m_freeSlots.empty())
    {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
    }
    else
    {
        // Slots needed since `keep` stay; of the rest, the least recently needed or prefetched goes.
        const UINT keep = m_synchronous ? m_frame : m_frame - 1;
        UINT oldest = UINT_MAX;
        for (UINT s = 0; s < m_slots.size(); ++s)
        {
            const UINT recent = std::max(m_slots[s].lastUsed, m_slots[s].lastPrefetched);
            if (m_slots[s].lastUsed < keep && (!prefetch || recent < keep) && recent < oldest)
            {
                oldest = recent;
                slot = s;
            }
        }
        if (slot == UINT_MAX)
            return false;
        m_resident.erase(m_slots[slot].node);
    }
    if (FAILED(frame->UploadVertices(m_offsets.VertexOffset + slot * VerticesPerNode, vertices, VerticesPerNode)))
    {
        m_slots[slot] = Slot();
        m_freeSlots.push_back(slot);
        return false;
    }
    m_slots[slot].node = index;
    m_slots[slot].lastUsed = prefetch ? 0 : m_frame;
    m_slots[slot].lastPrefetched = prefetch ? m_frame : 0;
    m_resident.emplace(index, slot);
    ++m_loadedThisFrame;
    return true;
}

// CDLOD selection: split while the camera is within the node's range (further for a node split last frame), and only
// when all four children are resident; otherwise the node draws itself. Children wanted for a split load first (by
// how deep inside its range the camera is), then those near their parent's range.
void Terrain::Select(const Node& node, const XMFLOAT3& eye, IRenderFrame* frame)
{
    const uint32_t index = Index(node);
    bool split = false;
    if (node.level + 1 < m_package.Manifest().levels)
    {
        const float distance = Distance(node, eye);
        const float range = Params().lodRange * static_cast<float>(m_package.Manifest().NodeSize(node.level));
        const float splitRange = range * (m_wasSplit.count(index) ? 1 + Hysteresis : 1);
        if (distance < splitRange)
        {
            split = true;
            for (int child = 0; child < 4; ++child)
                split = Ensure({ node.level + 1, 2 * node.x + (child & 1), 2 * node.z + (child >> 1) }, distance / range, false,
                    frame) && split;
            m_missing += split ? 0 : 1;
        }
        else if (distance < range * PrefetchRange)
        {
            for (int child = 0; child < 4; ++child)
                Ensure({ node.level + 1, 2 * node.x + (child & 1), 2 * node.z + (child >> 1) }, 1 + distance / range, true, frame);
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
    // Road surfaces: an instance each, in world space.
    for (UINT r = 0; r < RoadParts(); ++r)
    {
        const UINT slot = instance + 1 + r;
        InstancedData& roadData = frame->GetRenderData().instanceData[slot];
        XMStoreFloat4x4(&roadData.World, XMMatrixIdentity());
        roadData.InstanceOffset = roadData.InstanceOffsetPrev = slot;
        roadData.AtlasRegion = 0;
        roadData.PreviousSlot = slot;
        if (m_roadParts[r].renderUnit)
            HRR(frame->SetInstances(m_roadParts[r].renderUnit, this, slot, 1));
    }
    // Props: each batch an instance per part per prop of its model, the part's transform then the prop's.
    UINT slot = instance + 1 + RoadParts();
    const UINT regions = slot + PropInstances();
    for (const PropBatch& batch : m_propBatches)
    {
        const auto& worlds = m_propWorlds[batch.model];
        const UINT first = slot;
        for (const XMFLOAT4X4& transform : batch.transforms)
        {
            const XMMATRIX part = XMLoadFloat4x4(&transform);
            for (const XMFLOAT4X4& world : worlds)
            {
                InstancedData& propData = frame->GetRenderData().instanceData[slot];
                // A blade face shows its sign's name (one plus the slot holding that name's atlas region), on a
                // blade stretched along its length (the model's x for the lower one, z for the upper) to fit it.
                const size_t sign = static_cast<size_t>(&world - worlds.data());
                const bool named = batch.blade >= 0 && sign < m_signBlades.size() && SignNames();
                XMMATRIX stretch = XMMatrixIdentity();
                if (named)
                {
                    const float s = m_signStretch[m_signBlades[sign][batch.blade]];
                    stretch = batch.blade == 0 ? XMMatrixScaling(s, 1, 1) : XMMatrixScaling(1, 1, s);
                }
                XMStoreFloat4x4(&propData.World, part * stretch * XMLoadFloat4x4(&world));
                propData.InstanceOffset = propData.InstanceOffsetPrev = slot;
                propData.AtlasRegion = named && batch.face ? 1 + regions + m_signBlades[sign][batch.blade] : 0;
                propData.PreviousSlot = slot;
                ++slot;
            }
        }
        if (batch.renderUnit)
            HRR(frame->SetInstances(batch.renderUnit, this, first, slot - first));
    }
    // The street names' atlas regions, a slot each (World row 0: u offset, v offset, u scale, v scale).
    for (UINT n = 0; n < SignNames(); ++n)
    {
        InstancedData& regionData = frame->GetRenderData().instanceData[regions + n];
        XMStoreFloat4x4(&regionData.World, XMMatrixIdentity());
        regionData.World._11 = m_signRegions[n].x;
        regionData.World._12 = m_signRegions[n].y;
        regionData.World._13 = m_signRegions[n].z;
        regionData.World._14 = m_signRegions[n].w;
        regionData.InstanceOffset = regionData.InstanceOffsetPrev = regions + n;
        regionData.AtlasRegion = 0;
        regionData.PreviousSlot = regions + n;
    }
    m_selected.clear();
    m_loadedThisFrame = m_missing = 0;
    if (!m_open || m_slots.empty())
        return S_OK;
    if (frame->GetRenderData().showRoads && !m_roads.lines.empty())
        AddRoadOverlay(frame->GetRenderData());

    ++m_frame;
    // The first frame draws everything it needs; later ones stream unless streaming is off.
    m_synchronous = m_frame == 1 || !frame->GetRenderData().terrainStreaming;
    ApplyLoaded(frame);
    std::swap(m_split, m_wasSplit);
    m_split.clear();
    m_wanted.clear();
    m_wantedIndex.clear();
    XMFLOAT3 eye;
    XMStoreFloat3(&eye, frame->GetRenderData().eyePos);
    if (Ensure({ 0, 0, 0 }, 0, false, frame))
        Select({ 0, 0, 0 }, eye, frame);
    PublishRequests();

    XMVECTOR minimum = XMVectorReplicate(FLT_MAX), maximum = XMVectorReplicate(-FLT_MAX);
    for (const auto& selected : m_selected)
    {
        XMFLOAT3 bounds[2];
        NodeBounds(selected.first, bounds);
        const uint32_t node = m_slots[selected.second].node;
        HRR(frame->AddDraw(m_renderUnit, this, instance, 1, static_cast<INT>(m_offsets.VertexOffset + selected.second * VerticesPerNode),
            &m_nodeOwners[node], bounds));
        // The node's trails, on its own triangles.
        const auto tile = m_trailTileOf.find(Index(selected.first));
        if (tile != m_trailTileOf.end() && m_trailTiles[tile->second].renderUnit)
        {
            TrailTile& trail = m_trailTiles[tile->second];
            HRR(frame->AddDraw(trail.renderUnit, this, instance, 1, static_cast<INT>(trail.offsets.VertexOffset), &trail.owner, trail.bounds));
        }
        // The scene box (which reaches shadow casters toward the sun) takes the terrain near the camera only.
        if (bounds[1].x > eye.x - 512 && bounds[0].x < eye.x + 512 && bounds[1].z > eye.z - 512 && bounds[0].z < eye.z + 512)
        {
            minimum = XMVectorMin(minimum, XMLoadFloat3(&bounds[0]));
            maximum = XMVectorMax(maximum, XMLoadFloat3(&bounds[1]));
        }
    }
    if (XMVector3LessOrEqual(minimum, maximum))
    {
        if (m_propInstances)
            maximum = XMVectorAdd(maximum, XMVectorSet(0, PropHeight, 0, 0));     // Signs stand above the ground.
        XMStoreFloat3(&_boundingBox[0], minimum);
        XMStoreFloat3(&_boundingBox[1], maximum);
    }
    return S_OK;
}

void Terrain::StartLoader()
{
    StopLoader();
    m_stopLoader = false;
    m_loader = std::thread([this]() { LoaderMain(); });
}

void Terrain::StopLoader()
{
    if (m_loader.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(m_loadLock);
            m_stopLoader = true;
        }
        m_loadWake.notify_all();
        m_loader.join();
    }
    std::lock_guard<std::mutex> lock(m_loadLock);
    m_queue.clear();
    m_loading.clear();
    m_loaded.clear();
}

// The loader thread: takes the best queued node, reads and builds it, and hands it back.
void Terrain::LoaderMain()
{
    std::vector<uint16_t> block(size_t(BlockSamples) * BlockSamples);
    for (;;)
    {
        LoadResult result;
        {
            std::unique_lock<std::mutex> lock(m_loadLock);
            m_loadWake.wait(lock, [this]() { return m_stopLoader || !m_queue.empty(); });
            if (m_stopLoader)
                return;
            result.node = m_queue.back().node;
            result.prefetch = m_queue.back().prefetch;
            m_queue.pop_back();
            m_loading.insert(Index(result.node));
            if (!m_spareVertices.empty())
            {
                result.vertices = std::move(m_spareVertices.back());
                m_spareVertices.pop_back();
            }
        }
        result.vertices.resize(VerticesPerNode);
        result.ok = m_package.ReadBlock(result.node.level, result.node.x, result.node.z, block.data());
        if (result.ok)
            BuildVertices(result.node, block.data(), result.vertices.data());
        std::lock_guard<std::mutex> lock(m_loadLock);
        m_loading.erase(Index(result.node));
        m_loaded.push_back(std::move(result));
    }
}

// Uploads nodes the loader finished, up to the frame's budget (see Place for the slots they may take). A node that
// finds no slot is dropped; the selection asks for it again if it still wants it.
void Terrain::ApplyLoaded(IRenderFrame* frame)
{
    std::vector<LoadResult> loaded;
    {
        std::lock_guard<std::mutex> lock(m_loadLock);
        loaded.swap(m_loaded);
    }
    size_t applied = 0;
    for (; applied < loaded.size() && m_loadedThisFrame < UploadBudget; ++applied)
    {
        const LoadResult& result = loaded[applied];
        if (result.ok && !m_resident.count(Index(result.node)))
            Place(result.node, result.vertices.data(), result.prefetch, frame);
    }
    std::lock_guard<std::mutex> lock(m_loadLock);
    // Over budget: back to the front of the finished list, for the next frame.
    m_loaded.insert(m_loaded.begin(), std::make_move_iterator(loaded.begin() + applied), std::make_move_iterator(loaded.end()));
    for (size_t i = 0; i < applied; ++i)
        m_spareVertices.push_back(std::move(loaded[i].vertices));
}

// Replaces the loader's queue with this frame's requests (best last), leaving out nodes loading or finished, and
// those no slot could take next frame (see Place): needed nodes up to the slots not needed lately, prefetches up to
// those neither needed nor prefetched lately, after the needed ones.
void Terrain::PublishRequests()
{
    // Needed before prefetched, then by priority; the best ends up last.
    std::sort(m_wanted.begin(), m_wanted.end(), [](const LoadRequest& a, const LoadRequest& b)
    {
        return a.prefetch != b.prefetch ? a.prefetch : a.priority > b.priority;
    });
    size_t forNeeded = m_freeSlots.size(), forPrefetch = m_freeSlots.size();
    for (const Slot& slot : m_slots)
        if (slot.node != UINT32_MAX && slot.lastUsed < m_frame)
        {
            ++forNeeded;
            forPrefetch += std::max(slot.lastUsed, slot.lastPrefetched) < m_frame ? 1 : 0;
        }
    {
        std::lock_guard<std::mutex> lock(m_loadLock);
        std::unordered_set<uint32_t> finished;
        for (const LoadResult& result : m_loaded)
            finished.insert(Index(result.node));
        std::vector<LoadRequest> queue;
        size_t needed = 0, prefetched = 0;
        for (auto request = m_wanted.rbegin(); request != m_wanted.rend(); ++request)
        {
            const uint32_t index = Index(request->node);
            if (m_loading.count(index) || finished.count(index))
                continue;
            if (request->prefetch ? needed + ++prefetched > forPrefetch : ++needed > forNeeded)
                continue;
            queue.push_back(*request);
        }
        m_queue.assign(queue.rbegin(), queue.rend());
        m_pending = static_cast<UINT>(m_queue.size() + m_loading.size() + m_loaded.size());
    }
    if (!m_queue.empty())
        m_loadWake.notify_one();
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

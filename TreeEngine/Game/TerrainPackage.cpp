#include "pch.h"
#include "TerrainPackage.h"
#include "ThirdParty/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace TerrainFormat;

namespace
{
    std::string ReadText(const std::wstring& path)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file)
            return {};
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::wstring Join(const std::wstring& directory, const std::string& name)
    {
        std::wstring result = directory;
        if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
            result += L'\\';
        return result + std::wstring(name.begin(), name.end());
    }
}

int TerrainPackage::LevelsFor(int samples)
{
    int levels = 1;
    while (NodeQuads * (1ll << (levels - 1)) + 1 < samples && levels < 16)
        ++levels;
    return levels;
}

uint64_t TerrainPackage::NodeCount(int levels)
{
    uint64_t count = 0;
    for (int level = 0; level < levels; ++level)
        count += 1ull << (2 * level);
    return count;
}

uint64_t TerrainPackage::NodeIndex(int level, int x, int z)
{
    return NodeCount(level) + uint64_t(z) * (1ull << level) + uint64_t(x);
}

std::string TerrainPackage::ManifestJson(const TerrainManifest& m)
{
    nlohmann::ordered_json json;
    json["format"] = "TreeTerrain";
    json["version"] = m.version;
    json["worldSize"] = m.worldSize;
    json["sampleSpacing"] = m.sampleSpacing;
    json["levels"] = m.levels;
    json["nodeQuads"] = NodeQuads;
    json["blockSamples"] = BlockSamples;
    json["blockStride"] = BlockStride;
    json["heightOffset"] = m.heightOffset;
    json["heightScale"] = m.heightScale;
    json["minHeight"] = m.minHeight;
    json["maxHeight"] = m.maxHeight;
    json["nodesFile"] = m.nodesFile;
    json["boundsFile"] = m.boundsFile;
    json["source"] = nlohmann::ordered_json::parse(m.sourceJson.empty() ? "{}" : m.sourceJson, nullptr, false);
    if (json["source"].is_discarded())
        json["source"] = nlohmann::ordered_json::object();
    return json.dump(2) + "\n";
}

bool TerrainPackage::ParseManifest(const std::string& text, TerrainManifest& m, std::string& error)
{
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "manifest.json is not a JSON object";
        return false;
    }
    if (json.value("format", std::string()) != "TreeTerrain")
    {
        error = "manifest.json is not a TreeTerrain manifest";
        return false;
    }
    m.version = json.value("version", 0);
    if (m.version != Version)
    {
        error = "unsupported terrain package version " + std::to_string(m.version);
        return false;
    }
    if (json.value("nodeQuads", 0) != NodeQuads || json.value("blockSamples", 0) != BlockSamples
        || json.value("blockStride", size_t(0)) != BlockStride)
    {
        error = "terrain package block layout differs from this build's";
        return false;
    }
    m.worldSize = json.value("worldSize", 0.0);
    m.sampleSpacing = json.value("sampleSpacing", 0.0);
    m.levels = json.value("levels", 0);
    m.heightOffset = json.value("heightOffset", 0.0f);
    m.heightScale = json.value("heightScale", 0.0f);
    m.minHeight = json.value("minHeight", 0.0f);
    m.maxHeight = json.value("maxHeight", 0.0f);
    m.nodesFile = json.value("nodesFile", std::string("nodes.bin"));
    m.boundsFile = json.value("boundsFile", std::string("bounds.bin"));
    m.sourceJson = json.contains("source") ? json["source"].dump() : "{}";
    if (!(m.worldSize > 0) || !(m.sampleSpacing > 0) || m.levels < 1 || m.levels > 16 || !(m.heightScale > 0)
        || std::fabs(m.worldSize - m.sampleSpacing * NodeQuads * double(1ll << (m.levels - 1))) > 1e-6 * m.worldSize
        || m.nodesFile.empty() || m.nodesFile.find_first_of("/\\:") != std::string::npos
        || m.boundsFile.empty() || m.boundsFile.find_first_of("/\\:") != std::string::npos)
    {
        error = "manifest.json has an inconsistent size, spacing, level count, height scale or file names";
        return false;
    }
    return true;
}

bool TerrainPackage::Open(const std::wstring& directory)
{
    Close();
    m_error.clear();
    const std::string text = ReadText(Join(directory, "manifest.json"));
    if (text.empty())
    {
        m_error = "cannot read manifest.json";
        return false;
    }
    if (!ParseManifest(text, m_manifest, m_error))
        return false;
    if (!m_nodes.Open(Join(directory, m_manifest.nodesFile)))
    {
        m_error = "cannot open " + m_manifest.nodesFile;
        return false;
    }
    if (m_nodes.Size() < NodeCount(m_manifest.levels) * BlockStride)
    {
        Close();
        m_error = m_manifest.nodesFile + " is shorter than its node count needs";
        return false;
    }
    m_bounds.resize(size_t(2 * NodeCount(m_manifest.levels)));
    std::ifstream bounds(Join(directory, m_manifest.boundsFile).c_str(), std::ios::binary);
    if (!bounds.read(reinterpret_cast<char*>(m_bounds.data()), std::streamsize(m_bounds.size() * sizeof(uint16_t))))
    {
        Close();
        m_error = "cannot read " + m_manifest.boundsFile;
        return false;
    }
    return true;
}

void TerrainPackage::Close()
{
    m_nodes.Close();
    m_bounds.clear();
}

bool TerrainPackage::ReadBlock(int level, int x, int z, uint16_t* samples) const
{
    if (!m_nodes.IsOpen() || level < 0 || level >= m_manifest.levels || x < 0 || z < 0 || x >= (1 << level) || z >= (1 << level))
        return false;
    return m_nodes.Read(NodeIndex(level, x, z) * BlockStride, samples, BlockBytes);
}

bool TerrainPackage::Write(const std::wstring& directory, const std::vector<float>& heights, int samples,
    double sampleSpacing, const std::string& sourceJson, std::string& error)
{
    const int levels = LevelsFor(samples);
    if (samples < 2 || NodeQuads * (1ll << (levels - 1)) + 1 != samples || heights.size() != size_t(samples) * samples
        || !(sampleSpacing > 0))
    {
        error = "heights must be (64 * 2^n + 1) samples per side, with a positive spacing";
        return false;
    }
    TerrainManifest m;
    m.worldSize = sampleSpacing * (samples - 1);
    m.sampleSpacing = sampleSpacing;
    m.levels = levels;
    m.sourceJson = sourceJson;
    const auto range = std::minmax_element(heights.begin(), heights.end());
    m.minHeight = *range.first;
    m.maxHeight = *range.second;
    m.heightOffset = m.minHeight;
    m.heightScale = std::max((m.maxHeight - m.minHeight) / 65535.0f, 1e-6f);

    CreateDirectoryW(directory.c_str(), nullptr);
    FILE* nodes = nullptr;
    if (_wfopen_s(&nodes, Join(directory, m.nodesFile).c_str(), L"wb") != 0 || !nodes)
    {
        error = "cannot create nodes.bin";
        return false;
    }
    std::vector<uint8_t> block(BlockStride, 0);
    uint16_t* values = reinterpret_cast<uint16_t*>(block.data());
    bool ok = true;
    for (int level = 0; level < levels && ok; ++level)
    {
        const int64_t step = 1ll << (levels - 1 - level);      // Finest samples per node sample.
        const int side = 1 << level;
        for (int nz = 0; nz < side && ok; ++nz)
            for (int nx = 0; nx < side && ok; ++nx)
            {
                for (int b = 0; b < BlockSamples; ++b)
                {
                    const int64_t j = std::min<int64_t>(std::max<int64_t>((int64_t(nz) * NodeQuads + b - 1) * step, 0), samples - 1);
                    for (int a = 0; a < BlockSamples; ++a)
                    {
                        const int64_t i = std::min<int64_t>(std::max<int64_t>((int64_t(nx) * NodeQuads + a - 1) * step, 0), samples - 1);
                        const float value = (heights[size_t(j) * samples + size_t(i)] - m.heightOffset) / m.heightScale;
                        values[b * BlockSamples + a] = uint16_t(std::min(65535.0f, std::max(0.0f, std::round(value))));
                    }
                }
                ok = fwrite(block.data(), 1, BlockStride, nodes) == BlockStride;
            }
    }
    ok = (fclose(nodes) == 0) && ok;
    if (!ok)
    {
        error = "writing nodes.bin failed";
        return false;
    }
    // Height bounds: each finest-level node's samples, then each level up from its four children.
    std::vector<uint16_t> bounds(size_t(2 * NodeCount(levels)));
    const auto quantize = [&](float height) { return (height - m.heightOffset) / m.heightScale; };
    for (int level = levels - 1; level >= 0; --level)
    {
        const int side = 1 << level;
        for (int nz = 0; nz < side; ++nz)
            for (int nx = 0; nx < side; ++nx)
            {
                uint16_t* range = &bounds[size_t(2 * NodeIndex(level, nx, nz))];
                if (level == levels - 1)
                {
                    float low = FLT_MAX, high = -FLT_MAX;
                    for (int j = nz * NodeQuads; j <= (nz + 1) * NodeQuads; ++j)
                        for (int i = nx * NodeQuads; i <= (nx + 1) * NodeQuads; ++i)
                        {
                            low = std::min(low, heights[size_t(j) * samples + i]);
                            high = std::max(high, heights[size_t(j) * samples + i]);
                        }
                    range[0] = uint16_t(std::min(65535.0f, std::max(0.0f, std::floor(quantize(low)))));
                    range[1] = uint16_t(std::min(65535.0f, std::max(0.0f, std::ceil(quantize(high)))));
                    continue;
                }
                range[0] = 65535;
                range[1] = 0;
                for (int child = 0; child < 4; ++child)
                {
                    const uint16_t* c = &bounds[size_t(2 * NodeIndex(level + 1, 2 * nx + (child & 1), 2 * nz + (child >> 1)))];
                    range[0] = std::min(range[0], c[0]);
                    range[1] = std::max(range[1], c[1]);
                }
            }
    }
    std::ofstream boundsFile(Join(directory, m.boundsFile).c_str(), std::ios::binary);
    boundsFile.write(reinterpret_cast<const char*>(bounds.data()), std::streamsize(bounds.size() * sizeof(uint16_t)));
    if (!boundsFile)
    {
        error = "writing bounds.bin failed";
        return false;
    }
    boundsFile.close();

    std::ofstream manifest(Join(directory, "manifest.json").c_str(), std::ios::binary);
    manifest << ManifestJson(m);
    if (!manifest)
    {
        error = "writing manifest.json failed";
        return false;
    }
    return true;
}

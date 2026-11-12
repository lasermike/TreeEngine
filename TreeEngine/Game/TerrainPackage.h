#pragma once

// Terrain packages: a heightfield split into a quadtree of fixed-size height blocks, one per node, each readable on
// its own so nodes can stream in (file reads now; DirectStorage later reads the same blocks). Written by
// Tools/terrain/TerrainTool (procedural maps, 16-bit PNG/RAW, and GIS data through import_heightmap.py) and read by
// the Terrain object. See Tools/terrain/README.md for the format.
//
// A package is a directory holding manifest.json and nodes.bin. The world is a square of worldSize meters centered on
// the origin (x and z from -worldSize/2 to +worldSize/2). Level 0 is the root node covering it all; level k has 2^k x
// 2^k nodes, each covering worldSize / 2^k meters with NodeQuads x NodeQuads quads, so the finest level samples every
// sampleSpacing meters. A node's block holds BlockSamples x BlockSamples heights: its (NodeQuads + 1)^2 vertices plus
// a one-sample border on every side for normals, row-major from -x/-z, sampled from the finest grid (clamped at the
// world's edge). Heights are uint16: height = heightOffset + value * heightScale. bounds.bin holds each node's lowest
// and highest height (two uint16 per node, in node order), over the finest samples in its area, so they bound every
// level's geometry there.

#include "AssetReader.h"
#include <cstdint>
#include <string>
#include <vector>

namespace TerrainFormat
{
    constexpr int NodeQuads = 64;                                   // Quads per node side.
    constexpr int NodeVertices = NodeQuads + 1;                     // Vertices per node side.
    constexpr int BlockSamples = NodeQuads + 3;                     // Vertices plus a one-sample border each side.
    constexpr size_t BlockBytes = size_t(BlockSamples) * BlockSamples * sizeof(uint16_t);
    constexpr size_t BlockStride = (BlockBytes + 511) / 512 * 512;  // Blocks start on 512-byte boundaries.
    constexpr int Version = 1;
}

struct TerrainManifest
{
    int version = TerrainFormat::Version;
    double worldSize = 0;           // Meters per side.
    double sampleSpacing = 1;       // Meters between the finest level's samples.
    int levels = 0;                 // Quadtree levels; the finest is levels - 1.
    float heightOffset = 0;         // Height of value 0, in meters.
    float heightScale = 1;          // Meters per unit.
    float minHeight = 0, maxHeight = 0;
    std::string nodesFile = "nodes.bin";
    std::string boundsFile = "bounds.bin";
    std::string sourceJson = "{}";  // Where the heights came from (generator settings, GIS source, CRS, bounds).

    double NodeSize(int level) const { return worldSize / double(1ll << level); }
    double NodeSpacing(int level) const { return NodeSize(level) / TerrainFormat::NodeQuads; }
    float Height(uint16_t value) const { return heightOffset + value * heightScale; }
};

class TerrainPackage
{
public:
    ~TerrainPackage() { Close(); }

    // Opens a package directory; returns false with Error() set on failure.
    bool Open(const std::wstring& directory);
    void Close();
    const TerrainManifest& Manifest() const { return m_manifest; }
    const std::string& Error() const { return m_error; }

    // Blocks are stored level by level from the root, row-major (z, then x) within a level.
    static uint64_t NodeIndex(int level, int x, int z);
    static uint64_t NodeCount(int levels);
    // Reads one node's block (BlockSamples^2 values) through the package's asset reader. Safe from several threads.
    bool ReadBlock(int level, int x, int z, uint16_t* samples) const;
    // A node's height range in package units (Manifest().Height converts), read at Open.
    uint16_t MinValue(uint64_t node) const { return m_bounds[2 * node]; }
    uint16_t MaxValue(uint64_t node) const { return m_bounds[2 * node + 1]; }

    // Writes a package from the finest grid: samples x samples heights (row-major from -x/-z), where
    // samples = NodeQuads * 2^(levels - 1) + 1. Returns false with an error message on failure.
    static bool Write(const std::wstring& directory, const std::vector<float>& heights, int samples, double sampleSpacing,
        const std::string& sourceJson, std::string& error);
    // The level count whose finest grid covers at least `samples` samples per side.
    static int LevelsFor(int samples);

    static bool ParseManifest(const std::string& text, TerrainManifest& manifest, std::string& error);
    static std::string ManifestJson(const TerrainManifest& manifest);

private:
    TerrainManifest m_manifest;
    std::string m_error;
    FileAssetReader m_nodes;
    std::vector<uint16_t> m_bounds;
};

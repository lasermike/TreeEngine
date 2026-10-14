#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GltfTextureSource
{
    std::wstring key;
    std::wstring filename;
    std::vector<uint8_t> encodedData;
    std::string mimeType;

    bool IsMemoryBacked() const { return !encodedData.empty(); }
};

struct GltfMaterialData
{
    ShaderMaterial shaderMaterial;
    int textureIndex = -1;
    bool doubleSided = false;
    bool alphaBlend = false;
    bool wrapTexture = true;
};

struct GltfPrimitiveData
{
    std::vector<SimpleVertex> vertices;
    std::vector<UINT> indices;
    GeometryBufferData::BufferOffsets bufferOffsets = {};
};

struct GltfRenderPart
{
    size_t primitiveIndex = 0;
    size_t materialIndex = 0;
    XMFLOAT4X4 transform;
};

struct GltfAsset
{
    std::string sourceFilename;
    std::vector<GltfTextureSource> textures;
    std::vector<GltfMaterialData> materials;
    std::vector<GltfPrimitiveData> primitives;
    std::vector<GltfRenderPart> renderParts;
    XMFLOAT3 boundsMin = {};
    XMFLOAT3 boundsMax = {};

    void AppendGeometry(GeometryBufferData& geometryData);
};

std::shared_ptr<GltfAsset> LoadGltfAsset(const std::string& filename);

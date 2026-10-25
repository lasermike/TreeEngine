#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GltfTextureSource
{
    enum class Origin { ExternalFile, DataUri, BufferView };
    std::wstring key;
    std::wstring filename;
    std::string name;
    std::vector<uint8_t> encodedData;
    std::string mimeType;
    int colorSpace = 0; // 0 = existing behavior, 1 = linear data, 2 = sRGB color
    Origin origin = Origin::ExternalFile;

    bool IsMemoryBacked() const { return !encodedData.empty(); }
};

struct GltfMaterialData
{
    std::string name;
    ShaderMaterial shaderMaterial;
    int textureIndex = -1;
    int specularTextureIndex = -1;
    int specularColorTextureIndex = -1;
    int normalTextureIndex = -1;
    int metallicRoughnessTextureIndex = -1;
    bool doubleSided = false;
    bool alphaBlend = false;
    bool wrapTexture = true;
    bool unlit = false;
    bool alphaMask = false;
    float alphaCutoff = 0.5f;
};

struct GltfPrimitiveData
{
    std::vector<SimpleVertex> vertices;
    std::vector<UINT> indices;
    GeometryBufferData::BufferOffsets bufferOffsets = {};
    size_t sourceVertexCount = 0;
    size_t sourceIndexCount = 0;
    bool hasNormals = false;
    bool hasTexcoords = false;
    bool hasTangents = false;
};

struct GltfRenderPart
{
    size_t meshIndex = 0;
    size_t nodeIndex = 0;
    size_t sourcePrimitiveIndex = 0;
    std::string nodeName;
    size_t primitiveIndex = 0;
    size_t materialIndex = 0;
    XMFLOAT4X4 transform;
};

struct GltfMeshData
{
    std::string name;
    size_t sourceIndex = 0;
};

struct GltfAsset
{
    std::string sourceFilename;
    std::vector<GltfTextureSource> textures;
    std::vector<GltfMaterialData> materials;
    std::vector<GltfPrimitiveData> primitives;
    std::vector<GltfMeshData> meshes;
    std::vector<GltfRenderPart> renderParts;
    XMFLOAT3 boundsMin = {};
    XMFLOAT3 boundsMax = {};

    void AppendGeometry(GeometryBufferData& geometryData);
};

std::shared_ptr<GltfAsset> LoadGltfAsset(const std::string& filename);

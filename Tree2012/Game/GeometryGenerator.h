
#pragma once
//***************************************************************************************
// GeometryGenerator.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Defines a static class for procedurally generating the geometry of 
// common mathematical objects.
//
// All triangles are generated "outward" facing.  If you want "inward" 
// facing triangles (for example, if you want to place the camera inside
// a sphere to simulate a sky), you will need to:
//   1. Change the Direct3D cull mode or manually reverse the winding order.
//   2. Invert the normal.
//   3. Update the texture coordinates and tangent vectors.
//***************************************************************************************

#include "pch.h"
#include <directxmath.h>
#include <vector>

enum PrimitiveType
{
    PrimitiveType_Box = 0,
    PrimitiveType_Cylinder,
    PrimitiveType_CylinderLD,
    PrimitiveType_CylinderHD,
    PrimitiveType_FSQuad,
    PrimitiveType_SkinnedCylinder
};

struct SimpleVertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 Tex;
    XMFLOAT3 TangentU;
};

struct SkinnedVertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 Tex;
    XMFLOAT3 TangentU;
    XMFLOAT3 InstanceWeights;
};

struct GeometryBufferData
{
    struct BufferOffsets
    {
        UINT VertexOffset;
        UINT VertexCount;
        UINT IndexOffset;
        UINT IndexCount;
    };

    BufferOffsets boxIndices;
    BufferOffsets cylinderIndices;
    BufferOffsets cylinderLDIndices;
    BufferOffsets cylinderHDIndices;
    BufferOffsets fsQuadIndices;
    BufferOffsets skinnedCylinderIndices;

    std::vector<SimpleVertex> vertices;
    std::vector<UINT> indices;

    std::vector<SkinnedVertex> skinnedVertices;
    std::vector<UINT> skinnedIndices;


    GeometryBufferData() : vertices(), indices()
    {
        ZeroMemory(&boxIndices, sizeof(BufferOffsets));
        ZeroMemory(&cylinderIndices, sizeof(BufferOffsets));
        ZeroMemory(&cylinderLDIndices, sizeof(BufferOffsets));
        ZeroMemory(&fsQuadIndices, sizeof(BufferOffsets));
    }

    void Release()
    {
        vertices.clear();
        indices.clear();
        skinnedVertices.clear();
        skinnedIndices.clear();
    }

    const BufferOffsets* const GetBufferOffsets(PrimitiveType primType)
    {
        BufferOffsets* pBufferOffsets = nullptr;
        switch (primType)
        {
        case PrimitiveType_Cylinder:
            pBufferOffsets = &this->cylinderIndices;
            break;
        case PrimitiveType_CylinderLD:
            pBufferOffsets = &this->cylinderLDIndices;
            break;
        case PrimitiveType_CylinderHD:
            pBufferOffsets = &this->cylinderHDIndices;
            break;
        case PrimitiveType_FSQuad:
            pBufferOffsets = &this->fsQuadIndices;
            break;
        case PrimitiveType_SkinnedCylinder:
            pBufferOffsets = &this->skinnedCylinderIndices;
            break;
        case PrimitiveType_Box:
        default:
            pBufferOffsets = &this->boxIndices;
            break;
        }

        return pBufferOffsets;
    }
};

class GeometryGenerator
{
public:
    struct Vertex
    {
        Vertex() {}
        Vertex(const XMFLOAT3& p, const XMFLOAT3& n, const XMFLOAT3& t, const XMFLOAT2& uv, const XMFLOAT3& instanceWeights)
            : Position(p), Normal(n), TangentU(t), TexC(uv), InstanceWeights(instanceWeights) {}
        Vertex(
            float px, float py, float pz,
            float nx, float ny, float nz,
            float tx, float ty, float tz,
            float u, float v)
            : Position(px, py, pz), Normal(nx, ny, nz),
            TangentU(tx, ty, tz), TexC(u, v),
            InstanceWeights(1.0f, 0, 1.0f) {}

        Vertex(
            float px, float py, float pz,
            float nx, float ny, float nz,
            float tx, float ty, float tz,
            float u, float v,
            float w1, float w2, float w3)
            : Position(px, py, pz), Normal(nx, ny, nz),
            TangentU(tx, ty, tz), TexC(u, v),
            InstanceWeights(w1, w2, w3) {}

        XMFLOAT3 Position;
        XMFLOAT3 Normal;
        XMFLOAT3 TangentU;
        XMFLOAT2 TexC;
        XMFLOAT3 InstanceWeights; // For skinned, weight of which branch matrix to use in BranchData
    };

    struct MeshData
    {
        std::vector<Vertex> Vertices;
        std::vector<UINT> Indices;
        XMFLOAT3 BoundingBoxMin;
        XMFLOAT3 BoundingBoxMax;
    };


    void GeometryGenerator::BuildGeometryBuffers(GeometryBufferData& data);

    ///<summary>
    /// Creates a box centered at the origin with the given dimensions.
    ///</summary>
    void CreateBox(float width, float height, float depth, MeshData& meshData);

    ///<summary>
    /// Creates a sphere centered at the origin with the given radius.  The
    /// slices and stacks parameters control the degree of tessellation.
    ///</summary>
    void CreateSphere(float radius, UINT sliceCount, UINT stackCount, MeshData& meshData);

    ///<summary>
    /// Creates a geosphere centered at the origin with the given radius.  The
    /// depth controls the level of tessellation.
    ///</summary>
    void CreateGeosphere(float radius, UINT numSubdivisions, MeshData& meshData);

    ///<summary>
    /// Creates a cylinder parallel to the y-axis, and centered about the origin.  
    /// The bottom and top radius can vary to form various cone shapes rather than true
    // cylinders.  The slices and stacks parameters control the degree of tessellation.
    ///</summary>
    void CreateCylinder(float bottomRadius, float topRadius, float height, UINT sliceCount, UINT stackCount, bool buildTop, bool buildBottom, MeshData& meshData);

    ///<summary>
    /// Creates an mxn grid in the xz-plane with m rows and n columns, centered
    /// at the origin with the specified width and depth.
    ///</summary>
    void CreateGrid(float width, float depth, UINT m, UINT n, MeshData& meshData);

    ///<summary>
    /// Creates a quad covering the screen in NDC coordinates.  This is useful for
    /// postprocessing effects.
    ///</summary>
    void CreateFullscreenQuad(MeshData& meshData);

private:
    void Subdivide(MeshData& meshData);
    void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, UINT sliceCount, UINT stackCount, MeshData& meshData);
    void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, UINT sliceCount, UINT stackCount, MeshData& meshData);
};


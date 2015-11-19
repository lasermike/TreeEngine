
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
	PrimitiveType_FSQuad,
};

struct SimpleVertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 Tex;
	XMFLOAT3 TangentU;
};

struct GeometryBufferData
{
	struct BufferIndices
	{
		UINT VertexOffset;
		UINT VertexCount;
		UINT IndexOffset;
		UINT IndexCount;
	};

	BufferIndices boxIndices;
	BufferIndices cylinderIndices;
	BufferIndices cylinderLDIndices;
	BufferIndices fsQuadIndices;

	std::vector<SimpleVertex> vertices;
	std::vector<UINT> indices;

	GeometryBufferData() : vertices(), indices()
	{
		ZeroMemory(&boxIndices, sizeof(BufferIndices));
		ZeroMemory(&cylinderIndices, sizeof(BufferIndices));
		ZeroMemory(&cylinderLDIndices, sizeof(BufferIndices));
		ZeroMemory(&fsQuadIndices, sizeof(BufferIndices));
	}

	void Release()
	{
		vertices.clear();
		indices.clear();
	}

	const BufferIndices* const GetBufferIndices(PrimitiveType primType)
	{
		BufferIndices* pBufferIndices = nullptr;
		switch (primType)
		{
		case PrimitiveType_Cylinder:
			pBufferIndices = &this->cylinderIndices;
			break;
		case PrimitiveType_CylinderLD:
			pBufferIndices = &this->cylinderLDIndices;
			break;
		case PrimitiveType_FSQuad:
			pBufferIndices = &this->fsQuadIndices;
			break;
		case PrimitiveType_Box:
		default:
			pBufferIndices = &this->boxIndices;
			break;
		}

		return pBufferIndices;
	}
};

class GeometryGenerator
{
public:
	struct Vertex
	{
		Vertex(){}
		Vertex(const XMFLOAT3& p, const XMFLOAT3& n, const XMFLOAT3& t, const XMFLOAT2& uv)
			: Position(p), Normal(n), TangentU(t), TexC(uv){}
		Vertex(
			float px, float py, float pz, 
			float nx, float ny, float nz,
			float tx, float ty, float tz,
			float u, float v)
			: Position(px,py,pz), Normal(nx,ny,nz),
			  TangentU(tx, ty, tz), TexC(u,v){}

		XMFLOAT3 Position;
		XMFLOAT3 Normal;
		XMFLOAT3 TangentU;
		XMFLOAT2 TexC;
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


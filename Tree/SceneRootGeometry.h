#pragma once
#include "CommonStuff.h"
#include "Materials.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>

class SceneRootGeometry // : public WorldObject
{
	ID3D11VertexShader*                 _pVertexShader;
	ID3D11PixelShader*                  _pPixelShader;
	ID3D11InputLayout*                  _pVertexLayout;
	ID3D11Buffer*                       _pVertexBuffer;
	ID3D11Buffer*                       _pIndexBuffer;
	ID3D11Buffer*						_pInstancedBuffer;

	ID3D11Buffer*                       _pCBChangesEveryFrame;
	Material							_trunkMaterial;
	DirectionalLight					_light;  // Doesn't belong here, will move later

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

	std::list<WorldObject*>					_children;

public:
	SceneRootGeometry();
	~SceneRootGeometry();

	virtual void Create(ModelGenerator* generator);
	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float time);

	//virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView) { return E_NOTIMPL;  }
	//ID3D11Buffer* GetInstanceBuffer() { return nullptr; }

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}


};


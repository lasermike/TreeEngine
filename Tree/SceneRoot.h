#pragma once
#include "CommonStuff.h"
#include "Materials.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>

class SceneRoot 
{
	ID3D11VertexShader*                 _pVertexShader;
	ID3D11PixelShader*                  _pPixelShader;
	ID3D11InputLayout*                  _pVertexLayout;
	ID3D11Buffer*                       _pVertexBuffer;
	ID3D11Buffer*                       _pIndexBuffer;
	ID3D11Buffer*						_pInstancedBuffer;

	ID3D11Buffer*                       _pCBNeverChanges;
	ID3D11Buffer*                       _pCBChangesEveryFrame;
	DirectionalLight					_light;  // Doesn't belong here, will move later

	XMMATRIX                            _View;
	XMVECTOR							_eyePos;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

	std::list<WorldObject*>					_children;

public:
	SceneRoot();
	~SceneRoot();

	virtual void Create(ModelGenerator* generator);
	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time);

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}


};


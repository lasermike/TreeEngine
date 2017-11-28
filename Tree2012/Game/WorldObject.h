#pragma once
#include "pch.h"
#include "Model.h"
#include "RenderData.h"
#include <GeometryGenerator.h>
#include <memory>

class RenderManager;

interface IRenderFrame;

enum GeneratorType
{
    NullGeneratorType,
    PrimitiveGeneratorType,
    FixedTreeGeneratorType,
    LSystemGeneratorType,
    GraphGeneratorType,
    FSGraphGeneratorType,
};

enum ObjectType
{
    WorldObjectType,
    PrimitiveObjectType,
    TreeType,
    GraphType,
};

enum Extent
{
    TOP = 0,
    LEFT,
    RIGHT,
    BOTTOM,
    NUM_EXTENTS
};

class WorldObjectParams
{
public:
    WorldObjectParams(GeneratorType genType) :
        position(0, 0, 0),
        scale(1, 1, 1),
        depthLOD(4),
        generatorType(genType),
        _animationSpeed(1.0f),
        primitiveType(PrimitiveType_Box)
    {
        XMStoreFloat4(&rotation, XMQuaternionIdentity());
    }

    virtual ~WorldObjectParams() { }

    XMFLOAT3 position;
    XMFLOAT3 scale;
    XMFLOAT4 rotation; // Quaternion
    GeneratorType generatorType;
    float _animationSpeed;
    int depthLOD;
    PrimitiveType primitiveType;
    std::vector<std::wstring> textureFilename;
    std::vector<ShaderMaterial> materials;
};

template<typename T>
class WorldObjectParameters : public WorldObjectParams
{
private:
    T generatorParameters;

public:
    WorldObjectParameters(GeneratorType genType) : WorldObjectParams(genType) { }

    WorldObjectParameters(WorldObjectParameters<T>& src) : WorldObjectParams(src)
    {
        generatorParameters = src.generatorParameters;
    }

    T& GetGeneratorParameters()
    {
        return generatorParameters;
    }
};

class WorldObject
{
protected:

    bool        _drawInstanced;

    unique_ptr<WorldObjectParams> _params;
    XMFLOAT3    _position;
    XMFLOAT4    _rotation; // Quaternion
    XMFLOAT3    _scale;

    XMFLOAT3    _boundingBox[2];
    XMFLOAT3    _extents[4];

    float CalcTime(float time) { return time * _params->_animationSpeed; }
public:

    WorldObject(WorldObjectParams* pParams);
    virtual ~WorldObject(void);

    virtual ObjectType GetObjectType() { return WorldObjectType; }
    WorldObjectParams& GetParams() { return *_params; }
    template <class T> WorldObjectParameters<T>& GetParams() { return *(WorldObjectParameters<T>*)_params.get(); }

    virtual void Create(ModelGenerator* /*generator*/) { }

    virtual HRESULT InitGraphics(RenderManager& renderManager);
    virtual HRESULT CleanUpDeviceObjects();

    virtual HRESULT ComputeConstants(IRenderFrame* pFrame);
    virtual unsigned int GetNumInstances() { return 0; }
    virtual unsigned int GetMaxInstances() { return 0; }
    XMFLOAT3* GetBoundingBox() { return _boundingBox; }
    XMVECTOR GetExtents(Extent extent);
    XMVECTOR GetPosition() { return XMLoadFloat3(&_position); }
    XMVECTOR GetRotation() { return XMLoadFloat4(&_rotation); }

    void SetPosition(XMVECTOR pos) { return XMStoreFloat3(&_position, pos); }
    void SetRotation(XMVECTOR rot) { return XMStoreFloat4(&_rotation, rot); }

};


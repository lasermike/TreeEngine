#pragma once
#include "Tree.h"
#include "TreeModelGenerator.h"

struct GraphParams
{
	std::vector<XMFLOAT2> points;

	GraphParams() { };
};


class GraphModelGenerator : public TreeModelGenerator
{
	GraphParams _params;

public:
	GraphModelGenerator(GraphParams& params) : _params(params) { }
	TreeModel* Create();

protected:
	void CreateGraph(std::vector<XMFLOAT2>& points);
};


/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */

class FSGraphModel : public Model
{
public:
	FSGraphModel(void) { }
	~FSGraphModel(void) { }
};


class FSGraphModelGenerator : public ModelGenerator
{
	GraphParams _params;

public:
	FSGraphModelGenerator(GraphParams& params) : _params(params) { }
	FSGraphModel* Create();

protected:
	//void CreateGraph(std::vector<XMFLOAT2>& points);
};

class FSGraph : public WorldObject
{
	FSGraphModel*	_model;
	RenderUnit*		m_renderUnit;

public:
	FSGraph(WorldObjectParams* wop);
	~FSGraph();
	virtual ObjectType GetObjectType() { return PrimitiveObjectType; }

	//virtual void Create(ModelGenerator* generator) { return Create((PrimitiveModelGenerator*)generator); }
	virtual void Create(FSGraphModelGenerator* generator);
	virtual HRESULT InitGraphics(RenderManager& renderManager);

	virtual HRESULT ComputeConstants(IRenderFrame* pFrame) override;

	virtual unsigned int GetNumInstances() { return 1; }
	virtual unsigned int GetMaxInstances() { return 1; }

};


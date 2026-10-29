#include "pch.h"
#include "Graph.h"
#include "Tree.h"
#include "RenderManager.h"

TreeModel* GraphModelGenerator::Create()
{
	_model = new TreeModel();

	Branch* parent = nullptr;

	for (UINT i = 1; i < _params.points.size(); i++)
	{
		XMFLOAT4 start = XMFLOAT4(_params.points[i - 1].x, _params.points[i - 1].y, 0, 0);
		XMFLOAT4 end = XMFLOAT4(_params.points[i].x, _params.points[i].y, 0, 0);

		const float thickness = 0.005f;
		Branch* child = AddBranch(parent, start, end, Stick, thickness);

		if (i == 1)
		{
			_model->trunk = child;
		}

		parent = child;
	}

	return _model;
}

void GraphModelGenerator::CreateGraph(std::vector<XMFLOAT2>& /*points*/) { }

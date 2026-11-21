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

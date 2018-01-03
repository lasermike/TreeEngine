#pragma once

#include "Model.h"
#include "TreeModelGenerator.h"
#include "DynamicProperties.h"

struct Rule
{
    string input;
    string output;

    Rule(char* in, char* out) : input(in), output(out) { }
};

struct LSystemParams;

typedef float (*LengthFunc)(LSystemParams* params, float cmdParam);

struct LSystemParams
{
    LSystemParams();

    int _numIterations;
    float _angle;
    float _segmentLength;
    string _constants;
    float thickness;
    string _axiom;
    XMFLOAT3 _initialDirection;
    vector<Rule> _rules;

    LengthFunc SegmentLength;
};

struct BuildState
{
    XMVECTOR pos;
    XMMATRIX matDir;
    Branch* branch;

    BuildState()
    {
        ZeroMemory(this, sizeof(BuildState));
    }
};

class LSystemModelGenerator : public TreeModelGenerator
{
    LSystemParams _params;

public:
    LSystemModelGenerator(LSystemParams& params);
    TreeModel* Create();

protected:
    void CreateSkeleton(string& cmd);
};

#pragma once

#include "Model.h"
#include "TreeModelGenerator.h"
#include "DynamicProperties.h"

struct Rule
{
    string input;
    string output;
    int numIterations;

    Rule(char* in, char* out) : input(in), output(out), numIterations(0) { }
    Rule(char* in, int numIterations, char* out) : input(in), numIterations(numIterations), output(out) { }

    bool SatisfiesCondition(int numInterations);

};

struct LSystemParams;

typedef double (*LengthFunc)(LSystemParams* params, double cmdParam);

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
    float thickness;

    BuildState()
    {
        ZeroMemory(this, sizeof(BuildState));
        thickness = 1.0f;
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

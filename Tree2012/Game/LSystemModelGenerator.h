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


//class GeneratorParams
//{
//public:
//	virtual ~GeneratorParams() { }
//};
//
//template<typename T>
//class GeneratorParameters : public GeneratorParams
//{
//private:
//    T data;
//public:
//    virtual ~GeneratorParameters () { }
//
//    GeneratorParameters(T d)
//    {
//        data = d;
//    }
//
//    T GetValue()
//    {
//        return data;
//    }
//};


struct LSystemParams
{
	int _numIterations;
	float _angle;
	float _segmentLength;
	string _constants;
	float thickness;
	string _axiom;
	vector<Rule> _rules;

	LSystemParams() : _numIterations(0), _angle(0), _constants(), _axiom(), _rules() { };
};


struct BuildState
{
	XMFLOAT4 pos;
	XMFLOAT4 dir;
	Branch* branch;

	BuildState()
	{
		ZeroMemory(this, sizeof(BuildState));
	}
};

class LSystemModelGenerator : public TreeModelGenerator
{
	LSystemParams _params;
	TreeModel*	_model;

public:
	LSystemModelGenerator(LSystemParams& params);
	TreeModel* Create();

protected:
	void CreateSkeleton(string& cmd);
	Branch* AddBranch(Branch* parent, XMFLOAT4& start, XMFLOAT4& end);
};

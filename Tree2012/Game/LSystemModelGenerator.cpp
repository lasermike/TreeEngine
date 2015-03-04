#include "pch.h"
#include "LSystemModelGenerator.h"
#include <stack>

LSystemModelGenerator::LSystemModelGenerator(LSystemParams& params) : _model(nullptr), _params(params)
{
}

void replaceAll( string &s, const string &search, const string &replace ) {
    for( size_t pos = 0; ; pos += replace.length() ) {
        // Locate the substring to replace
        pos = s.find( search, pos );
        if( pos == string::npos ) break;
        // Replace by erasing and inserting
        s.erase( pos, search.length() );
        s.insert( pos, replace );
    }
}

TreeModel* LSystemModelGenerator::Create()
{
	_model = new TreeModel();

	string axiom = _params._axiom;
	for (int i = 0; i < _params._numIterations; i++)
	{
		for (auto r = _params._rules.begin(); r != _params._rules.end(); r++)
		{
			replaceAll(axiom, (*r).input, (*r).output);
		}
	}

	OutputDebugStringA(axiom.c_str());
	
	CreateSkeleton(axiom);

	return _model;
}

void LSystemModelGenerator::CreateSkeleton(string& axiom)
{
	BuildState initialState;
	initialState.pos = XMFLOAT4(0,0,0,1);
	initialState.dir = XMFLOAT4(0,1,0,1);

	int id = _model->treeData.numBranches++;
	Branch* child = &_model->treeData.pBranches[id];

	_model->trunk = child;
	_model->trunk->id = id;
	_model->trunk->parent = -1;
	_model->trunk->start = initialState.pos;
	XMStoreFloat4(&_model->trunk->end, XMLoadFloat4(&initialState.pos) + XMLoadFloat4(&initialState.dir) * _params._segmentLength);
	_model->trunk->thickness = 0.05f;
	_model->trunk->depth = 0;
	initialState.branch = _model->trunk;

	BuildState previousState = initialState, currentState;

	XMFLOAT4 axis(0,0,1,1);
	XMMATRIX rotateLeftMat = XMMatrixRotationAxis(XMLoadFloat4(&axis), _params._angle);
	XMMATRIX rotateRightMat = XMMatrixRotationAxis(XMLoadFloat4(&axis), -_params._angle);

	stack<BuildState> stateStack;

	for (auto c = axiom.begin(); c != axiom.end(); c++)
	{
		currentState = previousState;

		char cmd = *c;
		switch (cmd)
		{
		case 'A':
		case 'B':
			XMStoreFloat4(&currentState.pos, XMLoadFloat4(&currentState.pos) + XMLoadFloat4(&currentState.dir) * _params._segmentLength);
			currentState.branch = AddBranch(previousState.branch, previousState.pos, currentState.pos); 
			break;
		case '[':
			stateStack.push(currentState);
			XMStoreFloat4(&currentState.dir, XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateLeftMat));
			break;
		case ']':
			currentState = stateStack.top();
			stateStack.pop();
			XMStoreFloat4(&currentState.dir, XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateRightMat));
			break;
		}
		previousState = currentState;
	}

}

Branch* LSystemModelGenerator::AddBranch(Branch* parent, XMFLOAT4& start, XMFLOAT4& end)
{
	int id = _model->treeData.numBranches++;
	Branch* child = &_model->treeData.pBranches[id];

	child->id = id;
	parent->SetChild(parent->numChildren, child->id);
	parent->numChildren++;
	child->parent = parent->id;
	child->start = start;
	child->end = end;
	child->thickness = parent->thickness;
	child->depth = parent->depth + 1;

	return child;
}
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
	_model->trunk->thickness = _params.thickness;
	_model->trunk->depth = 0;
	initialState.branch = _model->trunk;

	BuildState previousState = initialState, currentState;

	XMFLOAT4 zAxis(0,0,1,1);
	XMMATRIX rotateZPosMat = XMMatrixRotationAxis(XMLoadFloat4(&zAxis), _params._angle);
	XMMATRIX rotateZNegMat = XMMatrixRotationAxis(XMLoadFloat4(&zAxis), -_params._angle);

	XMFLOAT4 xAxis(1,0,0,1);
	XMMATRIX rotateXPosMat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), _params._angle);
	XMMATRIX rotateXNegMat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), _params._angle);
	XMMATRIX rotate180Mat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), XM_PI);

	XMFLOAT4 yAxis(0,1,0,1);
	XMMATRIX rotateYPosMat = XMMatrixRotationAxis(XMLoadFloat4(&yAxis), _params._angle);
	XMMATRIX rotateYNegMat = XMMatrixRotationAxis(XMLoadFloat4(&yAxis), _params._angle);


	stack<BuildState> stateStack;

	int pos = 0;
	string done;
	for (auto c = axiom.begin(); c != axiom.end(); c++)
	{
		currentState = previousState;

		char cmd = *c;
		switch (cmd)
		{
		case '&':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateYPosMat)));
			break;
		case '^':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateYNegMat)));
			break;
		case '<':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateXPosMat)));
			break;
		case '>':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateXNegMat)));
			break;
		case '+':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateZPosMat)));
			break;
		case '-':
			XMStoreFloat4(&currentState.dir, XMVector4Normalize(XMVector4Transform(XMLoadFloat4(&currentState.dir), rotateZNegMat)));
			break;
		case '|':
			XMStoreFloat4(&currentState.dir, XMVector4Transform(XMLoadFloat4(&currentState.dir), rotate180Mat));
			break; 
		case 'A':
		case 'B':
		case 'F':
			XMStoreFloat4(&currentState.pos, XMLoadFloat4(&currentState.pos) + XMLoadFloat4(&currentState.dir) * _params._segmentLength);
			currentState.branch = AddBranch(previousState.branch, previousState.pos, currentState.pos); 
			break;
		case '[':
			stateStack.push(currentState);
			break;
		case ']':
			currentState = stateStack.top();
			stateStack.pop();
			break;
		case 'C':
			c++;
			// TODO color
			break;
		case 'X':
		case ' ':
			break; // noop
		}
		previousState = currentState;
		pos++;
		done.push_back(cmd);
	}

}

Branch* LSystemModelGenerator::AddBranch(Branch* parent, XMFLOAT4& start, XMFLOAT4& end)
{
	int id = _model->treeData.numBranches++;
	Branch* child = &_model->treeData.pBranches[id];

	child->id = id;
	parent->AddChild(child->id);
	///parent->numChildren++;
	child->parent = parent->id;
	child->start = start;
	child->end = end;
	//child->numChildren = 0;
	child->thickness = parent->thickness;
	child->depth = parent->depth + 1;

	if (child->depth > _model->treeData.numLevels)
		_model->treeData.numLevels = child->depth;

	return child;
}
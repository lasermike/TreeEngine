#include "pch.h"
#include "LSystemModelGenerator.h"
#include <stack>

LSystemModelGenerator::LSystemModelGenerator(LSystemParams& params) : TreeModelGenerator(), _params(params)
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
	OutputDebugStringA("\n");
	CreateSkeleton2(axiom);

	return _model;
}

void LSystemModelGenerator::CreateSkeleton2(string& axiom)
{
	BuildState initialState;
	initialState.pos = XMVectorSet(0, 0, 0, 1);
	initialState.dir = XMQuaternionRotationAxis(XMVectorSet(0, 1.0f, 0, 0), XM_PI);

	// Create trunk
	int id = _model->treeData.numBranches++;
	Branch* child = &_model->treeData.pBranches[id];

	_model->trunk = child;
	_model->trunk->id = id;
	_model->trunk->parent = -1;
	XMStoreFloat4(&_model->trunk->start, initialState.pos);


	XMVECTOR axis;  float angle;
	XMQuaternionToAxisAngle(&axis, &angle, initialState.dir);
	XMStoreFloat4(&_model->trunk->end, initialState.pos + axis * _params._segmentLength);
	_model->trunk->thickness = _params.thickness;
	_model->trunk->depth = 0;
	initialState.branch = _model->trunk;

	BuildState currentState = initialState;
	currentState.pos = XMLoadFloat4(&_model->trunk->end);

	XMFLOAT3 zAxis(0, 0, 1);
	XMVECTOR zQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), _params._angle);
	XMVECTOR zQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), -_params._angle);
	XMMATRIX rotateZPosMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), _params._angle);
	XMMATRIX rotateZNegMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), -_params._angle);

	XMFLOAT3 xAxis(1, 0, 0);
	XMVECTOR xVec = XMLoadFloat3(&xAxis);
	XMVECTOR xQuadPos = XMQuaternionRotationAxis(xVec, _params._angle);
	XMVECTOR xQuadNeg = XMQuaternionRotationAxis(xVec, -_params._angle);
	XMVECTOR xQuad180 = XMQuaternionRotationAxis(xVec, XM_PI);
	XMMATRIX rotateXPosMat = XMMatrixRotationNormal(xVec, _params._angle);
	XMMATRIX rotateXNegMat = XMMatrixRotationNormal(xVec, - _params._angle);
	XMMATRIX rotate180Mat = XMMatrixRotationNormal(xVec, XM_PI);

	XMFLOAT3 yAxis(0, 1, 0);
	XMVECTOR yVec = XMLoadFloat3(&yAxis);
	XMVECTOR yQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), _params._angle);
	XMVECTOR yQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), -_params._angle);
	XMMATRIX rotateYPosMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), _params._angle);
	XMMATRIX rotateYNegMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), - _params._angle);

	stack<BuildState> stateStack;

	XMFLOAT4 tmpPrev, tmpNext;
	XMVECTOR prevPos;
	int pos = 0;
	string done, unknown;
	for (auto c = axiom.begin(); c != axiom.end(); c++)
	{
		//currentState = previousState;

		char cmd = *c;
		switch (cmd)
		{
		case '&':
			currentState.dir = XMQuaternionMultiply(currentState.dir, yQuadPos);
			break;
		case '^':
			currentState.dir = XMQuaternionMultiply(currentState.dir, yQuadNeg);
			break;
		case '<':
		case '\\':
			currentState.dir = XMQuaternionMultiply(currentState.dir, xQuadPos);
			break;
		case '>':
		case '/':
			currentState.dir = XMQuaternionMultiply(currentState.dir, xQuadNeg);
			break;
		case '+':
			currentState.dir = XMQuaternionMultiply(currentState.dir, zQuadPos);
			break;
		case '-':
			currentState.dir = XMQuaternionMultiply(currentState.dir, zQuadNeg);
			break;
		case '|':
			currentState.dir = XMQuaternionMultiply(currentState.dir, xQuad180);
			break;
		case 'F':
		case 'L':
			//case 'A':
			//case 'S':
			prevPos = currentState.pos;
			
			axis = XMVector3TransformNormal(yVec, XMMatrixRotationQuaternion(currentState.dir));
			currentState.pos = currentState.pos + axis * _params._segmentLength * (cmd == 'L' ? 0.5f : 1.0f);

			XMStoreFloat4(&tmpPrev, prevPos);
			XMStoreFloat4(&tmpNext, currentState.pos);
			
			currentState.branch = AddBranch(currentState.branch, tmpPrev, tmpNext, (cmd == 'L' ? Leaf : Stick ), _params.thickness);
			break;
		case '[':
			stateStack.push(currentState);
			break;
		case ']':
			currentState = stateStack.top();
			stateStack.pop();
			break;
		case 'C':
			// TODO color
			c++;
			break;
		case 'X':
		case ' ':
			break; // noop
		default: 
			unknown.push_back(cmd);
			break;
		}
		
		//previousState = currentState;
		pos++;
		done.push_back(cmd);
	}

	LOG(unknown.c_str());
}

void LSystemModelGenerator::CreateSkeleton(string& axiom)
{
	//BuildState initialState;
	//initialState.pos = XMFLOAT4(0, 0, 0, 1);
	//initialState.dir = XMFLOAT3(0, 1, 0);

	//int id = _model->treeData.numBranches++;
	//Branch* child = &_model->treeData.pBranches[id];

	//_model->trunk = child;
	//_model->trunk->id = id;
	//_model->trunk->parent = -1;
	//_model->trunk->start = initialState.pos;
	//XMStoreFloat4(&_model->trunk->end, XMLoadFloat4(&initialState.pos) + XMLoadFloat3(&initialState.dir) * _params._segmentLength);
	//_model->trunk->thickness = _params.thickness;
	//_model->trunk->depth = 0;
	//initialState.branch = _model->trunk;

	//BuildState previousState = initialState, currentState;

	//XMFLOAT4 zAxis(0, 0, 1, 1);
	//XMMATRIX rotateZPosMat = XMMatrixRotationAxis(XMLoadFloat4(&zAxis), _params._angle);
	//XMMATRIX rotateZNegMat = XMMatrixRotationAxis(XMLoadFloat4(&zAxis), -_params._angle);

	//XMFLOAT4 xAxis(1, 0, 0, 1);
	//XMMATRIX rotateXPosMat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), _params._angle);
	//XMMATRIX rotateXNegMat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), _params._angle);
	//XMMATRIX rotate180Mat = XMMatrixRotationAxis(XMLoadFloat4(&xAxis), XM_PI);

	//XMFLOAT4 yAxis(0, 1, 0, 1);
	//XMMATRIX rotateYPosMat = XMMatrixRotationAxis(XMLoadFloat4(&yAxis), _params._angle);
	//XMMATRIX rotateYNegMat = XMMatrixRotationAxis(XMLoadFloat4(&yAxis), _params._angle);


	//stack<BuildState> stateStack;

	//int pos = 0;
	//string done;
	//for (auto c = axiom.begin(); c != axiom.end(); c++)
	//{
	//	currentState = previousState;

	//	char cmd = *c;
	//	switch (cmd)
	//	{
	//	case '&':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateYPosMat)));
	//		break;
	//	case '^':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateYNegMat)));
	//		break;
	//	case '<':
	//	case '\\':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateXPosMat)));
	//		break;
	//	case '>':
	//	case '/':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateXNegMat)));
	//		break;
	//	case '+':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateZPosMat)));
	//		break;
	//	case '-':
	//		XMStoreFloat3(&currentState.dir, XMVector4Normalize(XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotateZNegMat)));
	//		break;
	//	case '|':
	//		XMStoreFloat3(&currentState.dir, XMVector3TransformNormal(XMLoadFloat3(&currentState.dir), rotate180Mat));
	//		break;
	//		//case 'A':
	//		//case 'B':
	//	case 'f':
	//		XMStoreFloat4(&currentState.pos, XMLoadFloat4(&currentState.pos) + XMLoadFloat3(&currentState.dir) * _params._segmentLength * 0.2f);
	//		currentState.branch = AddBranch(previousState.branch, previousState.pos, currentState.pos, Leaf);
	//		break;
	//	case 'F':
	//		XMStoreFloat4(&currentState.pos, XMLoadFloat4(&currentState.pos) + XMLoadFloat3(&currentState.dir) * _params._segmentLength);
	//		currentState.branch = AddBranch(previousState.branch, previousState.pos, currentState.pos, Stick);
	//		break;
	//	case '[':
	//		stateStack.push(currentState);
	//		break;
	//	case ']':
	//		currentState = stateStack.top();
	//		stateStack.pop();
	//		break;
	//	case 'C':
	//		c++;
	//		// TODO color
	//		break;
	//	case 'X':
	//	case ' ':
	//		break; // noop
	//	}
	//	previousState = currentState;
	//	pos++;
	//	done.push_back(cmd);
	//}

}


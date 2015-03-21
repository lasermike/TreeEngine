#include "pch.h"
#include "GameLoader.h"
#include "Tree.h"
#include "SceneRoot.h"
#include "LSystemModelGenerator.h"

void GameLoader::Load(char* name, SceneRoot* pScene)
{
	//WorldObjectParameters<LSystemParams>* params1 = new WorldObjectParameters<LSystemParams>();

	//params1->position = XMFLOAT3(1.3f, 0, 1.3f);
	//params1->generatorType = LSystemGeneratorType;
	//params1->_animationSpeed = 10.0f;
	//params1->depthLOD = INT_MAX;
	//params1->GetGeneratorParameters()._axiom = "A";
	//params1->GetGeneratorParameters()._rules.push_back(Rule("B", "BB"));
	//params1->GetGeneratorParameters()._rules.push_back(Rule("A", "B[A]A"));
	//params1->GetGeneratorParameters()._angle = XM_PIDIV4;
	//params1->GetGeneratorParameters()._numIterations = 6;
	//params1->GetGeneratorParameters()._segmentLength = .08f;

	//// Init trees and other world objects
	//_trees.push_back(new Tree(params1));
	//_pScene->AddChild((*_trees.rbegin()));

	//WorldObjectParams* params2 = new WorldObjectParams();
	//params2->position = XMFLOAT3(1.3f,0,-1.3f);
	//_trees.push_back(new Tree(params2));
	//_pScene->AddChild((*_trees.rbegin()));

	//WorldObjectParams* params3 = new WorldObjectParams();
	//params3->position = XMFLOAT3(-1.3f,0,1.3f);
	//_trees.push_back(new Tree(params3));
	//_pScene->AddChild((*_trees.rbegin()));

	//WorldObjectParams* params4 = new WorldObjectParams();
	//params4->position = XMFLOAT3(0,0,0);
	//params4->scale = XMFLOAT3(30, .01f, 30);
	//_pPlane = new Primitive(params4);
	//_pScene->AddChild(_pPlane);

	//// Init lights
	//_renderData.dirLights[0].Ambient  = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	//_renderData.dirLights[0].Diffuse  = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
	//_renderData.dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	//_renderData.dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	//_renderData.time = 0;

	//// Camera
	//XMFLOAT3 bounds[] = 
	//{
	//	XMFLOAT3(-8,1,-8),
	//	XMFLOAT3(8,2,8)
	//};

	//_camera = new XSF::OrbitCamera();
	////_camera->SetFocusPosition(XMVectorSet(0, 1.0f, 0, 1));
	//_camera->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));

}

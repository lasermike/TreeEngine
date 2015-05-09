#include "pch.h"
#include "GameLoader.h"
#include "Tree.h"
#include "Primitive.h"
#include "SceneRoot.h"
#include "LSystemModelGenerator.h"
#include "orbitcamera.h"
#include <time.h>

GameLoader::GameLoader()
{
	_currentSeed = 0;
}

void GameLoader::Load(char* /*name*/, SceneRoot* pScene, RenderData* pRenderData, XSF::OrbitCamera* pCamera)
{
	LoadTrees(pScene, pRenderData, pCamera);
//	LoadTestBlock(pScene, pRenderData, pCamera);
}

void GameLoader::LoadTestBlock(SceneRoot* pScene, RenderData* pRenderData, XSF::OrbitCamera* pCamera)
{
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0,0,0);
	params4->scale = XMFLOAT3(1, 1.5, 2);
	XMStoreFloat4(&params4->rotation, XMQuaternionRotationAxis(XMVectorSet(.7f, .7f, .7f, 1), XM_PIDIV2));
	params4->primitiveType = PrimitiveType_Box;
	pScene->AddChild(new Primitive(params4));

	// Init lights
	pRenderData->dirLights[0].Ambient  = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pRenderData->dirLights[0].Diffuse  = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
	pRenderData->dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	pRenderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	pRenderData->time = 0;

	// Camera
	const float maxBound = 3.0f;
	XMFLOAT3 bounds[] = 
	{
		XMFLOAT3(-maxBound,-maxBound,-maxBound),
		XMFLOAT3(maxBound,maxBound,maxBound)
	};
	pCamera->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));

	pCamera->SetHeading(2.48f);
	//pCamera->SetFocusPosition(XMVectorSet(0, 1.1f, 0, 1));

}

void GameLoader::LoadTrees(SceneRoot* pScene, RenderData* pRenderData, XSF::OrbitCamera* pCamera)
{
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0,0,0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	pScene->AddChild(new Primitive(params4));

	WorldObjectParams* params3 = new WorldObjectParams(FixedTreeGeneratorType);
	params3->position = XMFLOAT3(-1.3f, 0.5f,1.3f);
	pScene->AddChild(new Tree(params3));

	WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params2->position = XMFLOAT3(1.3f, .5f, -1.3f);
	params2->_animationSpeed = 0.0f;
	params2->depthLOD = 3;
	params2->GetGeneratorParameters()._axiom = "F";	
	params2->GetGeneratorParameters()._constants = "";	
	params2->GetGeneratorParameters()._rules.push_back(Rule("F", "F [- & < F][ < + + & F ] | | F [ - - & > F ][+ & F ]"));
	params2->GetGeneratorParameters()._angle = 0.383972f;
	params2->GetGeneratorParameters()._numIterations = 4;
	params2->GetGeneratorParameters()._segmentLength = .28f;
	params2->GetGeneratorParameters().thickness = .020f;	
	pScene->AddChild(new Tree(params2));

	WorldObjectParameters<LSystemParams>* params5 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params5->position = XMFLOAT3(-1.3f, .5f,-1.3f);
	params5->_animationSpeed = 0.0f;
	params5->depthLOD = 3;
	params5->GetGeneratorParameters()._axiom = "F";	
	params5->GetGeneratorParameters()._constants = "";	
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "F [ & + F] F [ - > F][- > F][& F]"));
	params5->GetGeneratorParameters()._angle = 0.383972f;
	params5->GetGeneratorParameters()._numIterations = 4;
	params5->GetGeneratorParameters()._segmentLength = .28f;
	params5->GetGeneratorParameters().thickness = .020f;	
	pScene->AddChild(new Tree(params5));



	// Init lights
	pRenderData->dirLights[0].Ambient  = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pRenderData->dirLights[0].Diffuse  = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
	pRenderData->dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	pRenderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	pRenderData->time = 0;

	// Camera
	const float maxBound = 7.0f;
	XMFLOAT3 bounds[] = 
	{
		XMFLOAT3(-maxBound,1,-maxBound),
		XMFLOAT3(maxBound,2,maxBound)
	};

	pCamera->SetHeading(-2.48f);
	//_camera->SetFocusPosition(XMVectorSet(0, 1.0f, 0, 1));
	pCamera->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
}

void GameLoader::Regenerate(SceneRoot* pScene)
{
	if ((unsigned int)_currentSeed + 1 > _seeds.size())
	{
		_seeds.push_back(0); //(unsigned int)time(NULL));
	}

	int treeNum = 0;
	for (auto t = pScene->Children().begin(); t != pScene->Children().end(); t++)
	{
		treeNum++;

		GeneratorType genType = (*t)->GetParams().generatorType;
		if (genType == LSystemGeneratorType)
		{
			WorldObjectParameters<LSystemParams>& wop = (*t)->GetParams<LSystemParams>();

			LSystemModelGenerator generater(wop.GetGeneratorParameters()); // TODO
			(*t)->Create(&generater);
		}
		else if (genType == FixedTreeGeneratorType)
		{
			FixedTreeModelGenerator generator(_seeds[_currentSeed] * treeNum);
			(*t)->Create(&generator);
		}
		else if (genType == PrimitiveGeneratorType)
		{
			PrimitiveModelGenerator planeGen((*t)->GetParams().primitiveType);
			(*t)->Create(&planeGen);
		}
		else
		{
			ASSERT(0);
		}
	}

}

	// Init trees and other world objects
	//WorldObjectParameters<LSystemParams>* params1 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	//params1->position = XMFLOAT3(1.3f, 0, 1.3f);
	//params1->_animationSpeed = 10.0f;
	//params1->depthLOD = INT_MAX;
	//params1->GetGeneratorParameters()._axiom = "A";
	//params1->GetGeneratorParameters()._rules.push_back(Rule("B", "BB"));
	//params1->GetGeneratorParameters()._rules.push_back(Rule("A", "B[+A]-A"));
	//params1->GetGeneratorParameters()._angle = XM_PIDIV4;
	//params1->GetGeneratorParameters()._numIterations = 6;
	//params1->GetGeneratorParameters()._segmentLength = .08f;
	//params1->GetGeneratorParameters().thickness = .05f;	
	//pScene->AddChild(new Tree(params1));

	//params1->GetGeneratorParameters()._rules.push_back(Rule("F", "C0FF-[C1-F+F+F]+"));
	//params1->GetGeneratorParameters()._angle = 0.3839724f;



	//WorldObjectParameters<LSystemParams>* params1 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	//params1->position = XMFLOAT3(1.3f, .2f, 1.3f);
	//params1->_animationSpeed = 10.0f;
	//params1->depthLOD = INT_MAX;
	//params1->GetGeneratorParameters()._axiom = "FX";
	//params1->GetGeneratorParameters()._rules.push_back(Rule("F", "C0FF-[C1-F+F]+[C2+F-F]"));
	//params1->GetGeneratorParameters()._rules.push_back(Rule("X", "C0FF+[C1+F]+[C3-F]"));
	//params1->GetGeneratorParameters()._angle = 0.3839724f;
	//params1->GetGeneratorParameters()._numIterations = 4;
	//params1->GetGeneratorParameters()._segmentLength = .08f;
	//params1->GetGeneratorParameters().thickness = .02f;	
	//pScene->AddChild(new Tree(params1));

	//WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	//params2->position = XMFLOAT3(1.3f, 0,-1.3f);
	//params2->_animationSpeed = 20.0f;
	//params2->depthLOD = INT_MAX;
	//params2->GetGeneratorParameters()._axiom = "X";	
	//params2->GetGeneratorParameters()._constants = "X";	
	//params2->GetGeneratorParameters()._rules.push_back(Rule("X", "C0F-[C2[X]+C3X]+C1F[C3+FX]-X"));
	//params2->GetGeneratorParameters()._rules.push_back(Rule("F", "FF"));
	//params2->GetGeneratorParameters()._angle = 0.436332f;
	//params2->GetGeneratorParameters()._numIterations = 5;
	//params2->GetGeneratorParameters()._segmentLength = .035f;
	//params2->GetGeneratorParameters().thickness = .020f;	
	//pScene->AddChild(new Tree(params2));


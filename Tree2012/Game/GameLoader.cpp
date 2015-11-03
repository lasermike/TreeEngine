#include "pch.h"
#include "GameLoader.h"
#include "Tree.h"
#include "Primitive.h"
#include "SceneRoot.h"
#include "LSystemModelGenerator.h"
#include "orbitcamera.h"
#include <time.h>
#include "Player.h"

GameLoader::GameLoader()
{
	_currentSeed = 0;
}

void GameLoader::Load(char* /*name*/, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
{
	LoadGraph(pScene, pRenderData, pPlayer);
//	LoadTrees(pScene, pRenderData, pPlayer);
//	LoadTestBlock(pScene, pRenderData, pCamera);
}


struct GraphParams
{
	std::vector<XMFLOAT2> points;

	GraphParams()  { };
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

TreeModel* GraphModelGenerator::Create()
{ 
	_model = new TreeModel();

	Branch* parent = nullptr;

	for (UINT i = 1; i < _params.points.size(); i++)
	{
		XMFLOAT4 start = XMFLOAT4(_params.points[i - 1].x, _params.points[i - 1].y, 0, 0);
		XMFLOAT4 end = XMFLOAT4(_params.points[i].x, _params.points[i].y, 0, 0);

		Branch* child = AddBranch(parent, start, end, Stick, .020f);

		if (i == 1)
		{
			_model->trunk = child;
		}

		parent = child;
	}

	return _model; 
}

void GraphModelGenerator::CreateGraph(std::vector<XMFLOAT2>& points) { }


void GameLoader::LoadGraph(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
{
	// Graph
	WorldObjectParameters<GraphParams>* graphParams = new WorldObjectParameters<GraphParams>(GraphGeneratorType);
	graphParams->position = XMFLOAT3(1.3f, .5f, -1.3f);
	graphParams->_animationSpeed = 0.0f;
	graphParams->depthLOD = 1;
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(0, 0));
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(1, 1));
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(2, 1.5f));
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(3, 2.5f));
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(4, 1));
	graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(5, 0));
	pScene->AddChild(new Tree(graphParams));


	// "Ground" (temporary)
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	pScene->AddChild(new Primitive(params4));

	// Init lights
	pRenderData->dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pRenderData->dirLights[0].Diffuse = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
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

	if (pPlayer && pPlayer->GetOrbitCamera())
	{
		pPlayer->GetOrbitCamera()->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
		pPlayer->GetOrbitCamera()->SetHeading(2.48f);
		//pCamera->SetFocusPosition(XMVectorSet(0, 1.1f, 0, 1));
	}
}


void GameLoader::LoadTestBlock(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
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

    if (pPlayer && pPlayer->GetOrbitCamera())
    {
        pPlayer->GetOrbitCamera()->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
	    pPlayer->GetOrbitCamera()->SetHeading(2.48f);
	    //pCamera->SetFocusPosition(XMVectorSet(0, 1.1f, 0, 1));
    }
}

void GameLoader::LoadTrees(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
{
	WorldObjectParams* params3 = new WorldObjectParams(FixedTreeGeneratorType);
	params3->depthLOD = 2;
	params3->position = XMFLOAT3(-1.3f, 0.5f,1.3f);
	pScene->AddChild(new Tree(params3));

	WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params2->position = XMFLOAT3(1.3f, .5f, -1.3f);
	params2->_animationSpeed = 0.0f;
	params2->depthLOD = 1;
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
	params5->depthLOD = 1;
	params5->GetGeneratorParameters()._axiom = "F";	
	params5->GetGeneratorParameters()._constants = "";	
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "F [ & + F] F [ - > F][- > F][& F]"));
	params5->GetGeneratorParameters()._angle = 0.383972f;
	params5->GetGeneratorParameters()._numIterations = 4;
	params5->GetGeneratorParameters()._segmentLength = .28f;
	params5->GetGeneratorParameters().thickness = .020f;	
	pScene->AddChild(new Tree(params5));

	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0,0,0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	pScene->AddChild(new Primitive(params4));

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

    if (pPlayer && pPlayer->GetOrbitCamera())
	{
		pPlayer->GetOrbitCamera()->SetHeading(-2.48f);
		//_camera->SetFocusPosition(XMVectorSet(0, 1.0f, 0, 1));
		pPlayer->GetOrbitCamera()->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
	}
}

void GameLoader::LoadTrees2(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
{
	WorldObjectParameters<LSystemParams>* params5 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params5->position = XMFLOAT3(0, .5f, 0);
	params5->_animationSpeed = 50.0f;
	params5->depthLOD = 1;
	params5->GetGeneratorParameters()._constants = "";
	params5->GetGeneratorParameters()._axiom = "X";
	params5->GetGeneratorParameters()._rules.push_back(Rule("X", "F[+X][-X]FX"));
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "FF"));
	params5->GetGeneratorParameters()._angle = 0.47996554429844063365401496133436f;
	params5->GetGeneratorParameters()._numIterations = 5;
	params5->GetGeneratorParameters()._segmentLength = .05f;
	params5->GetGeneratorParameters().thickness = .01f;
	pScene->AddChild(new Tree(params5));

	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	pScene->AddChild(new Primitive(params4));

	// Init lights
	pRenderData->dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pRenderData->dirLights[0].Diffuse = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
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

	if (pPlayer && pPlayer->GetOrbitCamera())
	{
		pPlayer->GetOrbitCamera()->SetHeading(-2.48f);
		//_camera->SetFocusPosition(XMVectorSet(0, 1.0f, 0, 1));
		pPlayer->GetOrbitCamera()->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
	}
}
void GameLoader::LoadTrees3(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer)
{
	// ?:A 
	// p1 :A?[&FL!A]/////’[&FL!A]///////’[&FL!A] 
	// p2 :F? S ///// F 
	// p3 :S? FL 
	// p4 :L? [’’’??{-f+f+f-|-f+f+f}]

	// INCOMPLETE TREE!
	WorldObjectParameters<LSystemParams>* params5 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params5->position = XMFLOAT3(0, 2.0f, 0);
	params5->_animationSpeed = 15.0f;
	params5->depthLOD = 1;
	params5->GetGeneratorParameters()._constants = "";
	params5->GetGeneratorParameters()._axiom = "A";
	params5->GetGeneratorParameters()._rules.push_back(Rule("A", "[&&&FL!A]/////’[&&&FL!A]///////’[&FL!A]"));
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "S/////F"));
	params5->GetGeneratorParameters()._rules.push_back(Rule("S", "FL"));
	//params5->GetGeneratorParameters()._rules.push_back(Rule("L", "[’’’^^f]"));
	//params5->GetGeneratorParameters()._rules.push_back(Rule("L", "[’’’??{-f+f+f-|-f+f+f}]"));
	params5->GetGeneratorParameters()._angle = 0.1963495f;
	params5->GetGeneratorParameters()._numIterations = 1;
	params5->GetGeneratorParameters()._segmentLength = .1f;
	params5->GetGeneratorParameters().thickness = .01f;
	pScene->AddChild(new Tree(params5));

	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	pScene->AddChild(new Primitive(params4));

	// Init lights
	pRenderData->dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pRenderData->dirLights[0].Diffuse = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
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

	if (pPlayer && pPlayer->GetOrbitCamera())
	{
		pPlayer->GetOrbitCamera()->SetHeading(-2.48f);
		//_camera->SetFocusPosition(XMVectorSet(0, 1.0f, 0, 1));
		pPlayer->GetOrbitCamera()->FocusOnBoundingBox(bounds, ARRAYSIZE(bounds));
	}
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
		else if (genType == GraphGeneratorType)
		{
			WorldObjectParameters<GraphParams>& wop = (*t)->GetParams<GraphParams>();
			GraphModelGenerator graphGen(wop.GetGeneratorParameters());
			(*t)->Create(&graphGen);
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


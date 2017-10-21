#include "pch.h"
#include "GameLoader.h"
#include "Tree.h"
#include "Primitive.h"
#include "SceneRoot.h"
#include "LSystemModelGenerator.h"
#include "orbitcamera.h"
#include <time.h>
#include "Player.h"
#include "Graph.h"
#include <directxcolors.h>

void GameData::ResetToDefaults()
{
	useShadowMaps = true;
	useAlphaBlendedRenderTarget = true;
	clearColor = Colors::SkyBlue;
}


GameLoader::GameLoader()
{
	_currentSeed = 0;
}

void GameLoader::Load(int sceneNum, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData)
{
	switch (sceneNum)
	{
    case 0:
    {
        LoadNewTrees(pScene, pRenderData, pPlayer, gameData);
        break;
    }
    case 1:
	{
		LoadTrees(pScene, pRenderData, pPlayer, gameData);
		break;
	}
	case 4:
	{
		LoadGraph(pScene, pRenderData, pPlayer, gameData);
		break;
	}
	case 2:
	{
		LoadTestBlock(pScene, pRenderData, pPlayer, gameData);
		break;
	}
	case 3:
	{
		LoadFSGraph(pScene, pRenderData, pPlayer, gameData);
		break;
	}
	default:
		ASSERT(false);
	}
}

void GameLoader::Load(char* /*name*/, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData)
{
//	Load(0, pScene, pRenderData, pPlayer, gameData);
//	LoadGraph(pScene, pRenderData, pPlayer, gameData);
//	LoadTrees(pScene, pRenderData, pPlayer, gameData);
//	LoadTestBlock(pScene, pRenderData, pCamera, gameData);
}

HRESULT LoadGraphPoints(std::vector<XMFLOAT2>& points, char* filename)
{
	// Find max values
	std::vector<XMFLOAT2> values;
	float maxX = 0; //, maxY = 0;
	ifstream infile(filename); // for example
	ASSERT(infile);
	string line;
	while (getline(infile, line))
	{
		stringstream strstr(line);
		string time, value1;
		getline(strstr, time, ',');
		getline(strstr, value1, ',');
		float x = (float)atof(time.c_str());
		float y = (float)atof(value1.c_str());
		values.push_back(XMFLOAT2(x, y));
		maxX = std::max(maxX, x);
		//maxY = std::max(maxY, y);
	}

	float xScale = 10.0f / maxX;
	float xDelta = .5f / values[1].x;

	for (UINT i = 1; i < values.size(); i++)
	{
		points.push_back(XMFLOAT2(values[i].x * xScale,
			values[i].y * xDelta));
	}

	return S_OK;
}

HRESULT CreateBufferOfGraphPoints(std::vector<float>& buffer, UINT& width, char* filename)
{
	// Find max values
	float maxTime = 0;
	UINT rows = 0;
	UINT columns = 0;

	ifstream infile(filename); // for example
	ASSERT(infile);

	// Determine number of rows and columns and max time value
	string line;
	while (getline(infile, line))   // Read row
	{
		stringstream strstr(line);
		string time;
		getline(strstr, time, ',');

		// Store time from column 0
		float timeFloat = (float)atof(time.c_str());
		buffer.push_back(timeFloat);

		// Find highest time value
		maxTime = std::max(maxTime, timeFloat);

		// Insert remaining columns
		string value;
		while (getline(strstr, value, ','))
		{
			float valueFloat = (float)atof(value.c_str());
			buffer.push_back(valueFloat);
		}
		rows++;
	}

	ASSERT(buffer.size() % rows == 0);
	columns = UINT(buffer.size() / rows);

	double timeScale = 1.0 / maxTime;
	double timeDelta = .1 / buffer[columns];

	for (UINT i = 0; i < buffer.size(); i++)
	{
		if (!(i % columns))  // Scale time
		{
			buffer[i] = float(buffer[i] * timeScale);
		}
		else   // Scale values
		{
			buffer[i] = float(buffer[i] * timeDelta);
		}
	}

	width = columns;

	return S_OK;
}

void GameLoader::LoadFSGraph(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
	// Graph params
	WorldObjectParameters<FSGraphParams>* graphParams = new WorldObjectParameters<FSGraphParams>(FSGraphGeneratorType);
	FSGraphParams& params = graphParams->GetGeneratorParameters();
	graphParams->depthLOD = -1;
	graphParams->_animationSpeed = 300.0f;
	graphParams->position = XMFLOAT3(0, .55f, 0);

	// Load graph points into buffer
	UINT width = 0;
	HR(CreateBufferOfGraphPoints(params.points, params.width, "graphdata.txt"));
	params.height = UINT(params.points.size() / params.width);

	scene->AddChild(new FSGraph(graphParams));

	// Init lights
	//m_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);
	renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	//renderData->dirLights[0].Direction = XMFLOAT3(0.0f, 0.0f, 1.0f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(5.0f, 1.5f, -8.2f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), 0));

	gameData->useShadowMaps = false;
	gameData->clearColor = Colors::White;
	//gameData->useAlphaBlendedRenderTarget = true;
}


void GameLoader::LoadGraph(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
	// Graph
	WorldObjectParameters<GraphParams>* graphParams = new WorldObjectParameters<GraphParams>(GraphGeneratorType);

	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(0, 0));
	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(1, 1));
	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(2, 1.5f));
	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(3, 2.5f));
	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(4, 1));
	//graphParams->GetGeneratorParameters().points.push_back(XMFLOAT2(5, 0));

	// Load graph points
	GraphParams& params = graphParams->GetGeneratorParameters();
	LoadGraphPoints(params.points, "graphdata.txt");

	// Graph params
	graphParams->depthLOD = -1;
	graphParams->_animationSpeed = 300.0f;
	graphParams->position = XMFLOAT3(0, .55f, 0);
	scene->AddChild(new Tree(graphParams));

	// "Ground" (temporary)
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0.0f, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	scene->AddChild(new Primitive(params4));

	// Init lights
	//m_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);
	renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	//renderData->dirLights[0].Direction = XMFLOAT3(0.0f, 0.0f, 1.0f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(5.0f, 1.5f, -8.2f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), 0));

	gameData->useShadowMaps = false;
}


void GameLoader::LoadTestBlock(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0,0,0);
	params4->scale = XMFLOAT3(1, 1.5, 2);
	XMStoreFloat4(&params4->rotation, XMQuaternionRotationAxis(XMVectorSet(.7f, .7f, .7f, 1), XM_PIDIV2));
	params4->primitiveType = PrimitiveType_Box;
	scene->AddChild(new Primitive(params4));

	// Init lights
	renderData->dirLights[0].Ambient  = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	renderData->dirLights[0].Diffuse  = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
}

void GameLoader::LoadNewTrees(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    //WorldObjectParams* params3 = new WorldObjectParams(FixedTreeGeneratorType);
    //params3->depthLOD = 2;
    //params3->position = XMFLOAT3(-1.3f, 0.5f, 1.3f);
    //scene->AddChild(new Tree(params3));

    WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params2->position = XMFLOAT3(0, .5f, 0);
    params2->_animationSpeed = 4.0f;
    params2->depthLOD = 1;
    params2->textureFilename.push_back(L"urchinskin.dds");

    ShaderMaterial trunkMaterial;
    trunkMaterial.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
    trunkMaterial.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    trunkMaterial.Specular = XMFLOAT4(0, .1f, .1f, 1.0);
    trunkMaterial.flags.y = 1; //useTextures  TODO
    params2->materials.push_back(trunkMaterial);

    ShaderMaterial leafMaterial;
    XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
    leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
    leafMaterial.flags.y = false; //useTextures  TODO
    params2->materials.push_back(leafMaterial);

    params2->GetGeneratorParameters()._axiom = "F";
    params2->GetGeneratorParameters()._constants = "";
    params2->GetGeneratorParameters()._rules.push_back(Rule("F", "F [z F][Z F][X y F z F][x Y F]")); //[ X Z Z Y F ] F [ - - Y x F ][Z Y F ]
    params2->GetGeneratorParameters()._angle = 0.383972f;
    params2->GetGeneratorParameters()._numIterations = 4;
    params2->GetGeneratorParameters()._segmentLength = .28f;
    params2->GetGeneratorParameters().thickness = .020f;
    scene->AddChild(new Tree(params2));

    // Ground
    WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
    params4->position = XMFLOAT3(0, 0, 0);
    params4->scale = XMFLOAT3(25, .01f, 25);
    params4->primitiveType = PrimitiveType_Cylinder;
    params4->textureFilename.push_back(L"undersea.dds");
    ShaderMaterial mat;
    mat.Ambient = XMFLOAT4(.3, .3, .3, 1);
    mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1);
    mat.Specular = XMFLOAT4(.3f, .3f, .3f, 4.0f);
    mat.Reflect = XMFLOAT4(0, 0, 0, 1);
    mat.flags.y = 1; //1 for textured; 
    params4->materials.push_back(mat);
    scene->AddChild(new Primitive(params4));

    // Init lights
    renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    renderData->dirLights[0].Direction = XMFLOAT3(-0.0, -0.7f, 0.7f);
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
}

void GameLoader::LoadTrees(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
	WorldObjectParams* params3 = new WorldObjectParams(FixedTreeGeneratorType);
	params3->depthLOD = 2;
	params3->position = XMFLOAT3(-1.3f, 0.5f,1.3f);
	scene->AddChild(new Tree(params3));

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
	scene->AddChild(new Tree(params2));

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
	scene->AddChild(new Tree(params5));

    // Ground
	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0,0,0);
	params4->scale = XMFLOAT3(25, .01f, 25);
	params4->primitiveType = PrimitiveType_Cylinder;
	scene->AddChild(new Primitive(params4));

    WorldObjectParams* params7 = new WorldObjectParams(PrimitiveGeneratorType);
    params7->position = XMFLOAT3(0, 0, 0);
    params7->scale = XMFLOAT3(.4f, .4f, .4f);
    //XMStoreFloat4(&params5->rotation, XMQuaternionRotationAxis(XMVectorSet(.7f, .7f, .7f, 1), XM_PIDIV2));
    params7->primitiveType = PrimitiveType_Box;
    scene->AddChild(new Primitive(params7));

	// Init lights
	renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
}

void GameLoader::LoadTrees2(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
	WorldObjectParameters<LSystemParams>* params5 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
	params5->position = XMFLOAT3(0, .5f, 0);
	params5->_animationSpeed = 50.0f;
	params5->depthLOD = 1;
	params5->GetGeneratorParameters()._constants = "";
	params5->GetGeneratorParameters()._axiom = "M";
	params5->GetGeneratorParameters()._rules.push_back(Rule("M", "F[+ ][- ]F "));
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "FF"));
	params5->GetGeneratorParameters()._angle = 0.47996554429844063365401496133436f;
	params5->GetGeneratorParameters()._numIterations = 5;
	params5->GetGeneratorParameters()._segmentLength = .05f;
	params5->GetGeneratorParameters().thickness = .01f;
	scene->AddChild(new Tree(params5));

	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	scene->AddChild(new Primitive(params4));

	// Init lights
	renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
}
void GameLoader::LoadTrees3(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
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
	params5->GetGeneratorParameters()._rules.push_back(Rule("A", "+++&&&[/FLA]---&&&[/FLA]+++&&&[/FLA]"));
	//params5->GetGeneratorParameters()._rules.push_back(Rule("A", "[&FL!A]/////’[&FL!A]///////’[&FL!A]"));
	params5->GetGeneratorParameters()._rules.push_back(Rule("F", "S----F"));
	params5->GetGeneratorParameters()._rules.push_back(Rule("S", "FL"));
	////params5->GetGeneratorParameters()._rules.push_back(Rule("L", "[’’’^^f]"));
	////params5->GetGeneratorParameters()._rules.push_back(Rule("L", "[’’’??{-f+f+f-|-f+f+f}]"));
	params5->GetGeneratorParameters()._angle = 0.1963495f;
	params5->GetGeneratorParameters()._numIterations = 1;
	params5->GetGeneratorParameters()._segmentLength = .1f;
	params5->GetGeneratorParameters().thickness = .01f;
	scene->AddChild(new Tree(params5));

	WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
	params4->position = XMFLOAT3(0, 0, 0);
	params4->scale = XMFLOAT3(30, .01f, 30);
	params4->primitiveType = PrimitiveType_Cylinder;
	scene->AddChild(new Primitive(params4));

	// Init lights
	renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	renderData->time = 0;

	// Camera
	player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
	player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
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
		else if (genType == FSGraphGeneratorType)
		{
			WorldObjectParameters<FSGraphParams>& wop = (*t)->GetParams<FSGraphParams>();
			FSGraphModelGenerator graphGen(wop.GetGeneratorParameters());
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


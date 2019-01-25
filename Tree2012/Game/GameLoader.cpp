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

void LoadFSGraph(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
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
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(5.0f, 1.5f, -8.2f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), 0));

    gameData->useShadowMaps = false;
    gameData->clearColor = Colors::White;
    //gameData->useAlphaBlendedRenderTarget = true;
}


void LoadGraph(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    // Graph
    WorldObjectParameters<GraphParams>* graphParams = new WorldObjectParameters<GraphParams>(GraphGeneratorType);

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
    params4->primitiveType = PrimitiveType_CylinderHD;
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


void LoadTestBlock(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
    params4->position = XMFLOAT3(0, 0, 0);
    params4->scale = XMFLOAT3(1, 1.5, 2);
    XMStoreFloat4(&params4->rotation, XMQuaternionRotationAxis(XMVectorSet(.7f, .7f, .7f, 1), XM_PIDIV2));
    params4->primitiveType = PrimitiveType_Box;
    scene->AddChild(new Primitive(params4));

    // Init lights
    renderData->dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
    renderData->dirLights[0].Diffuse = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
    renderData->dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
    renderData->dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(-4.0f, 1.5f, -4.0f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
}


double SegLengthPlusRand(LSystemParams* params, double cmdParam)
{
    double len = rand() / (float)RAND_MAX * 0.2 + params->_segmentLength;
    return len;
}

void LoadSeaScene(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params2->_animationSpeed = 4.0f;
    params2->depthLOD = 1;
    params2->textureFilename.push_back(L"urchinskin.dds");

    ShaderMaterial trunkMaterial;
    trunkMaterial.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
    trunkMaterial.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    trunkMaterial.Specular = XMFLOAT4(0.1f, .1f, .1f, 1.0f);
    trunkMaterial.flags.y = 1; //useTextures  TODO
    params2->materials.push_back(trunkMaterial);

    ShaderMaterial leafMaterial;
    XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
    leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
    leafMaterial.flags.y = false; //useTextures  TODO
    params2->materials.push_back(leafMaterial);

    params2->GetGeneratorParameters()._axiom = "F Z F";
    params2->GetGeneratorParameters()._rules.push_back(Rule("F", "F [z F][Z F][X y F z F][x Y F]"));
    params2->GetGeneratorParameters()._constants = "";
    params2->GetGeneratorParameters()._angle = 0.453972f;
    params2->GetGeneratorParameters()._numIterations = 4;
    params2->GetGeneratorParameters()._segmentLength = .28f;
    params2->GetGeneratorParameters().thickness = .020f;
    params2->GetGeneratorParameters().SegmentLength = SegLengthPlusRand;

    params2->position = XMFLOAT3(-2.2f, .5f, 1.0f);
    scene->AddChild(new Tree(params2));

    WorldObjectParameters<LSystemParams>* params2A = new WorldObjectParameters<LSystemParams>(*params2);
    params2A->position = XMFLOAT3(0.5f, .5f, 0.0f);
    XMStoreFloat4(&params2A->rotation, XMQuaternionRotationNormal(XMVectorSet(0, 1, 0, 0), 1.0f));
    scene->AddChild(new Tree(params2A));

    WorldObjectParameters<LSystemParams>* params2B = new WorldObjectParameters<LSystemParams>(*params2);
    params2B->position = XMFLOAT3(3.0f, .5f, 1.0f);
    XMStoreFloat4(&params2B->rotation, XMQuaternionRotationNormal(XMVectorSet(0, 1, 0, 0), 2.0f));
    scene->AddChild(new Tree(params2B));

    // Ground
    WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
    params4->position = XMFLOAT3(0, 0, 0);
    params4->scale = XMFLOAT3(25, .01f, 25);
    params4->primitiveType = PrimitiveType_CylinderHD;
    params4->textureFilename.push_back(L"undersea.dds");
    ShaderMaterial mat;
    mat.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
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

void LoadCurvesScene(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    ShaderMaterial trunkMaterial;
    trunkMaterial.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
    trunkMaterial.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    trunkMaterial.Specular = XMFLOAT4(0.1f, .1f, .1f, 1.0f);
    trunkMaterial.flags.y = 1; //useTextures  TODO

    ShaderMaterial leafMaterial;
    XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
    leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
    leafMaterial.flags.y = false; //useTextures  TODO

    // Koch curve
    WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params2->depthLOD = 1;
    params2->textureFilename.push_back(L"urchinskin.dds");
    params2->_animationSpeed = 150.0f;
    params2->position = XMFLOAT3(-1, 1, 2);
    params2->scale = XMFLOAT3(3, 3, 3);

    XMStoreFloat4(&params2->rotation, XMQuaternionRotationNormal(XMVectorSet(0, 1, 0, 0), XM_PIDIV2));
    params2->GetGeneratorParameters()._axiom = "F(0.1) x F(0.1) x F(0.1) x F(0.1) x F(0.1) x F(0.1) x";
    params2->GetGeneratorParameters()._rules.push_back(Rule("F(a)", "F(a*0.33) X F(a*0.33) x x F(a*0.33) X F(a*0.33)"));
    params2->GetGeneratorParameters()._constants = "";
    params2->GetGeneratorParameters()._angle = 1.047198f;
    params2->GetGeneratorParameters()._numIterations = 3;
    params2->GetGeneratorParameters()._segmentLength = 6.0f;
    params2->GetGeneratorParameters().thickness = .010f;
    params2->materials.push_back(trunkMaterial);
    params2->materials.push_back(leafMaterial);
    scene->AddChild(new Tree(params2));

    // Hilbert curve
    WorldObjectParameters<LSystemParams>* params3 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params3->depthLOD = 1;
    params3->textureFilename.push_back(L"urchinskin.dds");
    params3->GetGeneratorParameters()._constants = "";
    params3->GetGeneratorParameters()._angle = XM_PI / 2.0f;
    params3->GetGeneratorParameters()._segmentLength = 0.1f; //  0.05f;
    params3->GetGeneratorParameters().thickness = .010f;
    params3->GetGeneratorParameters()._initialDirection = XMFLOAT3(1.0f, 0.0, 0.0);
    params3->GetGeneratorParameters()._axiom = "A";
    params3->GetGeneratorParameters()._rules.push_back(Rule("A", "YXAFYXAFAzFYxxAFAyFZxxAFAzFxAzx"));
    params3->GetGeneratorParameters()._numIterations = 1;
    params3->position = XMFLOAT3(0, 1.0, 0);
    params3->_animationSpeed = 2.0f;

    params3->materials.push_back(trunkMaterial);
    params3->materials.push_back(leafMaterial);

    scene->AddChild(new Tree(params3));

    WorldObjectParameters<LSystemParams>* params3A = new WorldObjectParameters<LSystemParams>(*params3);
    params3A->position = XMFLOAT3(-1, 1.0, 0);
    params3A->GetGeneratorParameters()._numIterations = 2;
    params3A->_animationSpeed = 15.0f;
    scene->AddChild(new Tree(params3A));

    WorldObjectParameters<LSystemParams>* params3B = new WorldObjectParameters<LSystemParams>(*params3);
    params3B->position = XMFLOAT3(-2, 1.0, 0);
    params3B->GetGeneratorParameters()._numIterations = 3;
    params3B->_animationSpeed = 30.0f;
    scene->AddChild(new Tree(params3B));

    WorldObjectParameters<LSystemParams>* params3C = new WorldObjectParameters<LSystemParams>(*params3);
    params3C->position = XMFLOAT3(-4, 1.0, 0);
    params3C->GetGeneratorParameters()._numIterations = 4;
    params3C->_animationSpeed = 100.0f;
    scene->AddChild(new Tree(params3C));

    // Ground
    WorldObjectParams* params4 = new WorldObjectParams(PrimitiveGeneratorType);
    params4->position = XMFLOAT3(0, 0, 0);
    params4->scale = XMFLOAT3(25, .01f, 25);
    params4->primitiveType = PrimitiveType_CylinderHD;
    //params4->textureFilename.push_back(L"undersea.dds");
    ShaderMaterial mat;
    mat.Ambient = XMFLOAT4(.9f, .9f, .9f, 1.0f);
    mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1);
    mat.Specular = XMFLOAT4(.3f, .3f, .3f, 4.0f);
    mat.Reflect = XMFLOAT4(0, 0, 0, 1);
    mat.flags.y = 0; //1 for textured; 
    params4->materials.push_back(mat);
    scene->AddChild(new Primitive(params4));

    // Init lights 
    renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    XMStoreFloat3(&renderData->dirLights[0].Direction, XMVector3Normalize(XMVectorSet(-0.7f, -0.7f, 0.7f, 0.0f)));
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(0.0f, 1.5f, -6.0f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), 0));

    gameData->clearColor = Colors::White;
}

float SegLengthParam(LSystemParams* params, float cmdParam)
{
    float len = cmdParam;
    return len;
}


void LoadXmasTree(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    ShaderMaterial trunkMaterial;
    trunkMaterial.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
    trunkMaterial.Diffuse = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
    trunkMaterial.Specular = XMFLOAT4(0.1f, .1f, .1f, 1.0f);
    trunkMaterial.flags.y = 0; //useTextures  TODO

    ShaderMaterial leafMaterial;
    XMStoreFloat4(&leafMaterial.Diffuse, Colors::White);
    leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
    leafMaterial.flags.y = true; //useTextures  TODO

    WorldObjectParameters<LSystemParams>* params2 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params2->textureFilename.push_back(L"Bark_0005_diffuse.dds");
    params2->textureFilename.push_back(L"FirBranchWithNeedles.dds");

    params2->position = XMFLOAT3(0, .5f, -2.0f);
    params2->GetGeneratorParameters()._angle = XM_2PI;
    params2->GetGeneratorParameters()._numIterations = 10;
    params2->_animationSpeed = 15.0f;
    params2->depthLOD = 1;
    params2->GetGeneratorParameters()._segmentLength = .01; // 0.5f;
    params2->GetGeneratorParameters().thickness = .08f;
    params2->GetGeneratorParameters()._constants = "";

    params2->GetGeneratorParameters()._axiom = "F(50) T(100)";

    params2->GetGeneratorParameters()._rules.push_back(Rule("T(t)", "[Y(t * ?) z(0.2) !(.006 * t) B(t * .5)] !(t * 0.01) F(t * 0.1) "
                                                                    "[Y(t * ?) z(0.2) !(.006 * t) B(t * .5)] !(t * 0.01) F(t * 0.1) "
                                                                    "[Y(t * ?) z(0.2) !(.006 * t) B(t * .5)] !(t * 0.01) F(t * 0.1) T(t * 0.8) "));
    params2->GetGeneratorParameters()._rules.push_back(Rule("B(b)", "!(.002 * b) F(b * .2) [!(.02) $(1) x(0.1) F(b * 0.5)] [!(.02) $(1) x(-0.1) F(b * 0.5)] B(b * 0.95) "));

    params2->materials.push_back(trunkMaterial);
    params2->materials.push_back(leafMaterial);

    params2->meshes.push_back(PrimitiveType_SkinnedCylinder);

    scene->AddChild(new Tree(params2));

    // Init lights 
    renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    XMStoreFloat3(&renderData->dirLights[0].Direction, XMVector3Normalize(XMVectorSet(-0.7f, -0.7f, 0.7f, 0.0f)));
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(0.0f, 1.5f, -6.0f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), 0));

    gameData->clearColor = Colors::White;
}


void LoadTestTree(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    ShaderMaterial trunkMaterial;
    trunkMaterial.Ambient = XMFLOAT4(.3f, .3f, .3f, 1.0f);
    trunkMaterial.Diffuse = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
    trunkMaterial.Specular = XMFLOAT4(0.1f, .1f, .1f, 1.0f);
    trunkMaterial.flags.y = 0; //useTextures  TODO

    ShaderMaterial leafMaterial;
    XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
    leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
    leafMaterial.flags.y = false; //useTextures  TODO

    WorldObjectParameters<LSystemParams>* params3 = new WorldObjectParameters<LSystemParams>(LSystemGeneratorType);
    params3->depthLOD = 1;
    //params3->textureFilename.push_back(L"urchinskin.dds");
    params3->GetGeneratorParameters()._constants = "";
    params3->GetGeneratorParameters()._angle = 1.0; // XM_PI / 4.0f;
    params3->GetGeneratorParameters()._segmentLength = .001; // 0.5f;
    params3->GetGeneratorParameters().thickness = .01f;
    params3->GetGeneratorParameters()._numIterations = 6;
    params3->position = XMFLOAT3(0, 1.0, 0);
    params3->_animationSpeed = 5.0f;
    params3->GetGeneratorParameters()._axiom = "F(200) /(0.785398) A";
    params3->GetGeneratorParameters()._rules.push_back(Rule("A", "F(50)[&(0.33074)F(50)A]/(1.653525) [&(0.33074)F(50)A]/(2.31483)[&(0.33074)F(50)A]"));
    params3->GetGeneratorParameters()._rules.push_back(Rule("F(l)", "F(l*1.309)"));

    //    params3->GetGeneratorParameters()._rules.push_back(Rule("!(w)", "!(w*1.732)"));

    //params3->GetGeneratorParameters()._axiom = "G(1.0)";
    //params3->GetGeneratorParameters()._rules.push_back(Rule("G(a)", "F(a) [ Z(a * 0.5) F(a * 0.5) G(a * 0.5)]")); //[zF]

    //params3->GetGeneratorParameters()._axiom = "F(200) z(0.785398) A";
    //params3->GetGeneratorParameters()._rules.push_back(Rule("A", "F(50)[&(0.33074)F(50)A]Z(1.653525) [&(0.33074)F(50)A]/(2.31483)[&(0.33074)F(50)A]"));
    //params3->GetGeneratorParameters()._rules.push_back(Rule("F(l)", "F(l*1.109)"));


    params3->materials.push_back(trunkMaterial);
    params3->materials.push_back(leafMaterial);

    params3->meshes.push_back(PrimitiveType_SkinnedCylinder);

    scene->AddChild(new Tree(params3));


    // Init lights 
    renderData->dirLights[0].Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    renderData->dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    renderData->dirLights[0].Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    XMStoreFloat3(&renderData->dirLights[0].Direction, XMVector3Normalize(XMVectorSet(-0.7f, -0.7f, 0.7f, 0.0f)));
    renderData->time = 0;

    // Camera
    player->SetPosition(XMLoadFloat3(&XMFLOAT3(0.0f, 1.5f, -6.0f)));
    player->SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), 0));

    gameData->clearColor = Colors::White;
}

void LoadTrees(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
{
    WorldObjectParams* params3 = new WorldObjectParams(FixedTreeGeneratorType);
    params3->depthLOD = 2;
    params3->position = XMFLOAT3(-1.3f, 0.5f, 1.3f);
    params3->meshes.push_back(PrimitiveType_SkinnedCylinder);

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
    params5->position = XMFLOAT3(-1.3f, .5f, -1.3f);
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
    params4->position = XMFLOAT3(0, 0, 0);
    params4->scale = XMFLOAT3(25, .01f, 25);
    params4->primitiveType = PrimitiveType_CylinderHD;
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

void LoadTrees2(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
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
    params4->primitiveType = PrimitiveType_CylinderHD;
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
void LoadTrees3(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData)
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
    params4->primitiveType = PrimitiveType_CylinderHD;
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

int GameLoader::GetNumScenes()
{ 
    return 7;
}


void GameLoader::Load(int sceneNum, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData)
{
    switch (sceneNum)
    {
    case 0:
        LoadXmasTree(pScene, pRenderData, pPlayer, gameData);
        break;
    case 1:
        LoadTestTree(pScene, pRenderData, pPlayer, gameData);
        break;
    case 2:
        LoadSeaScene(pScene, pRenderData, pPlayer, gameData);
        break;
    case 3:
        LoadCurvesScene(pScene, pRenderData, pPlayer, gameData);
        break;
    case 4:
        LoadTrees(pScene, pRenderData, pPlayer, gameData);
        break;
    case 5:
        LoadFSGraph(pScene, pRenderData, pPlayer, gameData);
        break;
    case 6:
        LoadTestBlock(pScene, pRenderData, pPlayer, gameData);
        break;
    case 7:
        LoadGraph(pScene, pRenderData, pPlayer, gameData);
        break;
    default:
        ASSERT(false);
    }
}

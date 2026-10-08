#include "pch.h"
#include "Game.h"
#include "StockRenderStates.h"
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include "GameLoader.h"
#include "RenderManager.h"
#include "InputManager.h"
#include "ThreadPool.h"
#include <cmath>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "implot.h"

using namespace DirectX;

#ifndef _XBOX_ONE
#ifndef WIN32
using namespace Windows::Graphics::Display;
#endif 
#endif 

#define D3D_DEBUG_INFO

extern Game* g_game;


Game::Game(IInputManager* inputMgr) : m_inputMgr(inputMgr)
{
    m_needsResize = false;
    m_nextScreenWidth = 0;
    m_nextScreenHeight = 0;
    m_renderToSharedTexture = false;
    m_settings.m_paused = false;
    m_settings.m_wireframe = false;
    m_showHelp = false;
    m_is12Driver = true;
    m_rotateLights = false;
    m_timeStart = 0;
    m_resetTree = true;
    m_settings.m_showShadowBuffer = false;
    m_settings.m_enablePostProcessing = true;
    m_settings.m_showPerfGraph = false;
    m_settings.m_showDebugUI = false;
    m_advanceScene = 0;
    m_advanceSceneAmount = 0;
    m_currentScene = 0;
    m_reloadDevice = false;
    m_renderPlatformDLL = nullptr;
    m_bDebugUIKeyCaptured = false;

    m_player = nullptr;
    m_threadPool = nullptr;
    assert(m_inputMgr);

    std::fill(std::begin(m_timeStamps), std::end(m_timeStamps), 0.0f);
    std::fill(std::begin(m_frameDurations), std::end(m_frameDurations), 0.0f);
    
}

void Game::UpdateViewMatrix()
{
}

Game::~Game()
{
    Cleanup();
    SafeDelete(&m_pScene);
    SafeDelete(&m_threadPool);
    SafeDelete(&m_player);
}


HRESULT Game::Initialize(bool renderToSharedTexture)
{
    m_renderToSharedTexture = renderToSharedTexture;


    // Rendering defaults
    m_renderManager.GetRenderData().frame = 0;
    m_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
    m_renderManager.GetRenderData().projectionData.nearClippingPlane = .2f;
    m_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;
    m_renderManager.GetRenderData().clearColor = XMFLOAT4(0, 0, 0, 0);

    GameCommon::SetContentFileRoot();

    // Init vertex/index buffer
    m_pScene = new SceneRoot();

    // Init scene bounds.
    // Estimatation.    
    // Ideally would loop through all world space vertices
    m_renderManager.GetRenderData().mSceneBounds.Center = XMFLOAT3(0.0f, 3.0f, 0.0f);
    m_renderManager.GetRenderData().mSceneBounds.Radius = 5; //sqrtf(5.0f*5.0f + 5.0f*5.0f);

    // Create player
    WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
    //playerParams->position = XMFLOAT3(-4.0f, 1.5f, -4.0f);
    XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
    m_player = new Player(playerParams);

    HRR(ReloadDevice());

    if (!m_renderTestMode) m_currentScene = 8; // LoadAITree
    //m_currentScene = 2;  // SeaScene
    //m_currentScene = 1; // Test tree
    //m_currentScene = 7; // simple box and cylindar

    SceneRenderSettings sceneSettings = m_renderManager.GetSceneSettings();
    if (m_renderTestMode)
    {
        HRR(m_loader.Load(m_renderTestScene.c_str(), m_pScene, &sceneSettings, m_player, &m_gameData));
    }
    else
    {
        HRR(m_loader.Load(m_currentScene, m_pScene, &sceneSettings, m_player, &m_gameData));
    }
    m_renderManager.ApplySceneSettings(sceneSettings);

    m_needsResize = true;

    // Create thread pool
    //may return 0 when not able to detect
    unsigned concurentThreadsSupported = std::thread::hardware_concurrency();
    // Keep render-test object updates ordered so shared instance data is repeatable.
    m_threadPool = new ThreadPool(m_renderTestMode ? 1 : (concurentThreadsSupported ? concurentThreadsSupported : 1));

    return S_OK;
}


HRESULT Game::Cleanup()
{
    if (m_pScene)
    {
        m_pScene->CleanUpDeviceObjects();
        SafeDelete(&m_pScene);
    }

    m_renderManager.UninitDevice();

    return S_OK;
}

HRESULT Game::ReloadDevice()
{
    HRESULT hr = S_OK;
    bool firstTimeLoad = m_renderPlatformDLL == nullptr;

    // Clear old stuff
    if (m_renderPlatformDLL)
    {
        if (m_pScene)
        {
            m_pScene->CleanUpDeviceObjects();
        }

        m_renderManager.UninitGameLevelGraphics();

        m_renderManager.UninitDevice();

#if defined(TREENGINE_WIN32)
        m_renderManager.GetPlatform()->SetWindow(nullptr);
#else
        m_renderManager.GetPlatform()->SetWindow(nullptr, m_logicalDpi);
#endif

        FreeLibrary(m_renderPlatformDLL);
        m_renderPlatformDLL = nullptr;

    }

    const wchar_t* dllFilename =
#if defined(_TREE_CLASSIC)
        L"RenderPlatform12.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#elif defined(TREE_XBOX)
        L"RenderPlatform12Xbox.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#else
        m_is12Driver ? L"RenderPlatform12UWP.dll" : L"RenderPlatform11UWP.dll";
        m_renderPlatformDLL = ::LoadPackagedLibrary(dllFilename, 0);
#endif

    assert(m_renderPlatformDLL != nullptr);

    m_renderManager.SetDebugUI(this);
    m_renderManager.SetSettings(&m_settings);

    m_renderManager.SetPlatform(m_renderPlatformDLL); // Set this last

#if defined(TREENGINE_WIN32)
    m_renderManager.GetPlatform()->SetWindow(m_hwnd);
#else
    m_renderManager.GetPlatform()->SetWindow(m_window.Get(), m_logicalDpi);
#endif


    m_renderManager.InitDevice();

    m_needsResize = true;

    if (m_pScene)
    {
        if (m_needsResize)
        {
            m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
            m_needsResize = false;
        }

        // Init render manager
        hr = m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps);
        assert(SUCCEEDED(hr));

        // Init new stuff
        hr = m_pScene->InitGraphics(m_renderManager);
        assert(SUCCEEDED(hr));
    }

    return hr;
}

HRESULT Game::Regenerate()
{
    HRESULT hr = S_OK;

    // Clear old stuff
    m_pScene->CleanUpDeviceObjects();
    m_renderManager.UninitGameLevelGraphics();

    m_loader.Regenerate(m_pScene);

    // Init render manager
    HRR(m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps));

    // Init new stuff
    HRR(m_pScene->InitGraphics(m_renderManager));
    return S_OK;
}

void Game::ConfigureRenderTest(const std::string& sceneName, bool rayTracing, bool postProcessing)
{
    m_renderTestMode = true;
    m_renderTestScene = sceneName;
    m_settings.m_dxrEnabled = rayTracing;
    m_settings.m_enablePostProcessing = postProcessing;
    m_settings.m_showDebugUI = false;
    m_settings.m_showPerfGraph = false;
    m_settings.m_showShadowBuffer = false;
    m_settings.m_wireframe = false;
    m_rotateLights = false;
    m_showHelp = false;
}

HRESULT Game::RenderTestFrame(double simulationTime, const std::wstring& captureFilename)
{
    if (!m_renderTestMode || !std::isfinite(simulationTime) || simulationTime < 0) return E_INVALIDARG;
    if (m_needsResize)
    {
        HRR(m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, false));
        m_needsResize = false;
    }
    if (m_resetTree)
    {
        HRR(Regenerate());
        m_resetTree = false;
    }

    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Render regression frame");
    auto& renderData = m_renderManager.GetRenderData();
    renderData.time = static_cast<float>(simulationTime);
    renderData.frame++;
    m_timeCurrent = simulationTime;
    XMStoreFloat4x4(&renderData.view, m_player->GetViewMatrix());
    renderData.eyePos = m_player->GetEyePosition();
    HRR(m_renderManager.BeginNewFrame());
    HRR(m_pScene->Update(m_renderManager, *m_threadPool, true));
    HRR(m_renderManager.EndFrame());
    m_settings.m_captureFilename = captureFilename;
    m_settings.m_captureResult = captureFilename.empty() ? S_OK : E_PENDING;
    ComputeGPU();
    Render(false);
    return m_settings.m_captureResult;
}

void Game::Update(DX::StepTimer const& timer)
{
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"Update");

    // Update frame duration circular array
    m_timeStamps[m_timestampIndex] = timer.GetTotalSeconds();

    float frameDuration = static_cast<float>(timer.GetElapsedSeconds() * 1000.0); // Convert to milliseconds
    m_frameDurations[m_timestampIndex] = frameDuration;
    m_timestampIndex = (m_timestampIndex + 1) % FRAME_DURATION_HISTORY_SIZE;

    // Reset stats
    for (int i = 0; i < MAX_FRAME_STAT; i++)
    {
        m_renderManager.GetRenderData().frameStats[i].stat = 0;
    }

    m_renderManager.GetRenderData().frame++;

    if (m_reloadDevice)
    {
        HR(ReloadDevice());
        m_reloadDevice = false;
    }

    m_renderManager.GetRenderData().frameStats[DRIVER_12_STAT].stat = m_is12Driver;

    if (m_needsResize)
    {
        m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
        m_needsResize = false;
    }

    if (m_advanceScene)
    {
        int nextSceneIndex = (m_currentScene + m_advanceSceneAmount) % m_loader.GetNumScenes();
        if (nextSceneIndex < 0)
        {
            nextSceneIndex += m_loader.GetNumScenes();
        }
        m_advanceScene = false;
        m_advanceSceneAmount = 0;

        // Validate and build the next scene before discarding the current one.
        auto nextScene = std::make_unique<SceneRoot>();
        SceneRenderSettings nextSceneSettings = m_renderManager.GetSceneSettings();
        GameData nextGameData;
        const XMVECTOR oldPosition = m_player->GetPosition();
        const XMVECTOR oldRotation = m_player->GetRotation();
        if (SUCCEEDED(m_loader.Load(nextSceneIndex, nextScene.get(), &nextSceneSettings, m_player, &nextGameData)))
        {
            m_pScene->DeleteAllChildren();
            m_renderManager.UninitGameLevelGraphics();
            delete m_pScene;
            m_pScene = nextScene.release();
            m_currentScene = nextSceneIndex;
            m_gameData = nextGameData;
            m_renderManager.ApplySceneSettings(nextSceneSettings);
            m_renderManager.GetRenderData().time = 0;
            m_timeStart = 0;
            m_settings.m_buffersInUse = 0;
            m_resetTree = true;
            m_needsResize = true;
        }
        else
        {
            m_player->SetPosition(oldPosition);
            m_player->SetRotation(oldRotation);
        }
    }

    // Rebuild tree if necessary
    if (m_resetTree)
    {
        HR(Regenerate());
        m_resetTree = false;
    }

    // Update our time
    if (m_timeStart == 0)
    {
        m_timeStart = timer.GetTotalSeconds();
        m_timeCurrent = 0;
    }
    else if (!m_settings.m_paused)
    {
        m_timeCurrent += timer.GetElapsedSeconds();
    }
    m_renderManager.GetRenderData().time = (float)m_timeCurrent;


    if (!m_settings.m_paused && m_rotateLights)
    {
        // Light rotation
        XMVECTOR quat = XMQuaternionRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 1.f), 0.05f); //m_timeCurrent / 1000

        XMVECTOR vec = XMLoadFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction);
        vec = XMVector3Rotate(vec, quat);
        XMStoreFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction, vec);
    }

    HR(m_renderManager.BeginNewFrame());

    // Compute per-frame values
    HR(m_pScene->Update(m_renderManager, *m_threadPool));

    m_player->Update(timer, &m_renderManager.GetRenderData());

    HR(m_renderManager.EndFrame());

}


#define STR(str) #str

const char s_szObjectTypeNames[ObjectType_MAX + 1][32] =
{
    STR(ObjectType_World),
    STR(ObjectType_Primitive),
    STR(ObjectType_Tree),
    STR(ObjectType_Graph),
};

const char s_szPrimitiveTypeNames[PrimitiveType_MAX + 1][32] =
{
    STR(PrimitiveType_Box),
    STR(PrimitiveType_Cylinder),
    STR(PrimitiveType_CylinderLD),
    STR(PrimitiveType_CylinderHD),
    STR(PrimitiveType_FSQuad),
    STR(PrimitiveType_SkinnedCylinder),
    STR(PrimitiveType_Sprite),
};

int Game::RuleTextEditCallback(ImGuiInputTextCallbackData* data)
{
    if (g_game)
    {
        g_game->m_resetTree = true;

        *((string*)data->UserData) = data->Buf;
    }
    return 0;
}



HRESULT Game::UpdateDebugUI(ImGuiContext* imGuiContext)
{
    if (!m_settings.m_showDebugUI)
    {
        return S_OK;
    }

    // Perf overlay
    if (m_settings.m_showPerfGraph)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 window_size = ImVec2(viewport->Size.x, 250.0f);  // 150 px tall
        ImVec2 window_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - window_size.y - 10.0f);

        ImGui::SetNextWindowPos(window_pos);
        ImGui::SetNextWindowSize(window_size);
        ImGui::SetNextWindowBgAlpha(0.0); // Optional: translucent background


        ImVec4 transparent = ImVec4(0, 0, 0, 0); // RGBA with 0 alpha
        ImPlot::GetStyle().Colors[ImPlotCol_PlotBg] = transparent;
        ImPlot::GetStyle().Colors[ImPlotCol_PlotBorder] = transparent;
        ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = transparent; // Optional
        ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] = transparent; // Optional

        ImPlot::GetStyle().Colors[ImPlotCol_Line] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        ImPlot::GetStyle().LineWeight = 2.0f;

        if (ImGui::Begin("TransparentPlot", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground))
        {
            if (ImPlot::BeginPlot("##Performance"), ImVec2(0, 0), ImPlotFlags_CanvasOnly)
            {
                ImGui::SetNextWindowBgAlpha(0.0f); // Optional: translucent background
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, 60.0);
                ImPlot::SetupAxes("Seconds", "MS", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("Frame Duration", m_timeStamps, m_frameDurations, FRAME_DURATION_HISTORY_SIZE,
                    ImPlotLineFlags_None, m_timestampIndex);
                ImPlot::EndPlot();
            }

            ImGui::End();
        }
    }


    // Debug Settings 
    bool bOpen = true;
    if (!ImGui::Begin("Debug UI", &bOpen, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        //ImGui::End();
        return S_FALSE;
    }

    ImGuiIO& io = ImGui::GetIO();

    m_bDebugUIKeyCaptured = io.WantCaptureKeyboard;

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Basic info
        ImGui::Text("Simulation Time %.3f seconds", m_renderManager.GetRenderData().time);
        ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Separator();
        ImGui::Text("Skinned: %d \t Sticks: %d \t Leaves: %d", m_renderManager.GetRenderData().frameStats[WORLD_MATRIX_COMPUTED_STAT].stat,
            m_renderManager.GetRenderData().frameStats[NUM_STICKS_STAT].stat,
            m_renderManager.GetRenderData().frameStats[NUM_LEAVES_STAT].stat);
        ImGui::Text("UI %d vertices, %d indices (%d triangles)", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
    }

    static bool uiPaused = false;

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Scene select
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
        if (ImGui::ArrowButton("##left", ImGuiDir_Left))
        {
            m_advanceScene = true;
            m_advanceSceneAmount = -1;
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::ArrowButton("##right", ImGuiDir_Right))
        {
            m_advanceScene = true;
            m_advanceSceneAmount = 1;
        }
        ImGui::PopItemFlag();
        ImGui::SetNextItemWidth(24);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::LabelText("Scene", "%d", m_currentScene);

        //Pause
        ImGui::Checkbox("Pause", &m_settings.m_paused);

        // Time
        double rangeMin = 0.0, rangeMax = __max(m_timeCurrent, 50);
        ImGui::SliderScalar("Time", ImGuiDataType_Double, &m_timeCurrent, &rangeMin, &rangeMax, "%.3f", 0 /*flags*/);

        // Camera
        ImGui::PushItemWidth(200);
        ImGui::DragFloat3("Camera Position", (float*)m_player->GetEyePositionPtr(), ImGuiColorEditFlags_Float);
        ImGui::DragFloat3("Camera Orientation", (float*)m_player->GetEyeRotationPtr(), ImGuiColorEditFlags_Float);
        ImGui::PopItemWidth();

        // Options
        ImGui::Checkbox("Show Shadow Map buffer", &m_settings.m_showShadowBuffer);
        ImGui::Checkbox("Use Wireframe", &m_settings.m_wireframe);
        ImGui::Checkbox("Enable Post Processing", &m_settings.m_enablePostProcessing);
        ImGui::Checkbox("Enable DXR", &m_settings.m_dxrEnabled);
        ImGui::Checkbox("Show Performance Graphs", &m_settings.m_showPerfGraph);
    }

    int nextInputBuffer = 0;

    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("%s", m_loader.GetSceneName(m_currentScene));
        if (!m_loader.GetLastError().empty())
            ImGui::TextWrapped("Scene load failed: %s", m_loader.GetLastError().c_str());
        for (WorldObject* obj : m_pScene->Children())
        {
            WorldObjectParams& params = obj->GetParams();
            ImGui::PushID(obj);

            ImGui::SeparatorText(params.name.empty() ? s_szObjectTypeNames[obj->GetObjectType()] : params.name.c_str());
            ImGui::Text("Primitive type: %s", s_szPrimitiveTypeNames[params.primitiveType]);

            if (ImGui::TreeNode("Materials"))
            {
                for (int m = 0; m < params.materials.size(); m++)
                {
                    auto& mat = params.materials[m];
                    ImGui::Text("Material %d", m);
                    ImGui::PushItemWidth(200);
                    ImGui::ColorEdit4("Diffuse##2f", (float*)&mat.Diffuse, ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit4("Specular##2f", (float*)&mat.Specular, ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit4("Ambient##2f", (float*)&mat.Ambient, ImGuiColorEditFlags_Float);
                    ImGui::PopItemWidth();
                }

                for (int t = 0; t < params.textureFilename.size(); t++)
                {
                    char path[MAX_PATH];
                    if (WideCharToMultiByte(CP_UTF8, 0, params.textureFilename[t].c_str(), -1, path, MAX_PATH, nullptr, nullptr))
                    {
                        ImGui::Text("Texture %d: %s", t, path);
                    }
                }
                ImGui::TreePop();
            }

            if (params.generatorType == LSystemGeneratorType)
            {
                if (ImGui::TreeNodeEx("L System", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    WorldObjectParameters<LSystemParams>& lsystemParams = obj->GetParams<LSystemParams>();
                    const auto& generator = lsystemParams.GetGeneratorParameters();
                    const int remainingBuffers = static_cast<int>(_countof(m_settings.m_ruleTextEditBuffers)) - nextInputBuffer;
                    bool canEdit = remainingBuffers > 0
                        && generator._rules.size() <= static_cast<size_t>((remainingBuffers - 1) / 2)
                        && generator._axiom.size() < m_settings.ruleTextBufferSize;
                    for (const auto& rule : generator._rules)
                        canEdit = canEdit && rule.input.size() < m_settings.ruleTextBufferSize
                            && rule.output.size() < m_settings.ruleTextBufferSize;
                    if (!canEdit)
                    {
                        ImGui::TextWrapped("These rules exceed the live editor's capacity. Edit the scene JSON to change them.");
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    // Iterations - TODO: make this is common code
                    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                    if (ImGui::ArrowButton("left", ImGuiDir_Left) &&
                        lsystemParams.GetGeneratorParameters()._numIterations > 0)
                    {
                        lsystemParams.GetGeneratorParameters()._numIterations--;
                        m_resetTree = true;
                    }
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    if (ImGui::ArrowButton("right", ImGuiDir_Right))
                    {
                        lsystemParams.GetGeneratorParameters()._numIterations++;
                        m_resetTree = true;
                    }
                    ImGui::PopItemFlag();
                    ImGui::SetNextItemWidth(24);
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::LabelText("Iterations", "%d", lsystemParams.GetGeneratorParameters()._numIterations);

                    // Axiom and rules
                    strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], lsystemParams.GetGeneratorParameters()._axiom.c_str());
                    ImGui::InputText("Axiom", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                        ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &lsystemParams.GetGeneratorParameters()._axiom);

                    for (int r = 0; r < lsystemParams.GetGeneratorParameters()._rules.size(); r++)
                    {
                        Rule& rule = lsystemParams.GetGeneratorParameters()._rules[r];
                        ImGui::PushID(r);

                        if (ImGui::TreeNodeEx("Rule", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            // Rule input
                            if (!m_settings.isInUse(nextInputBuffer))
                            {
                                strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], rule.input.c_str());
                            }

                            ImGui::InputText("Input", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.input);

                            // Rule output
                            if (!m_settings.isInUse(nextInputBuffer))
                            {
                                strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], rule.output.c_str());
                            }

                            ImGui::InputText("Output", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.output);

                            //ImGui::InputTextMultiline("Output", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                            //    ImVec2(0, 60), ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.output);

                            //ImGui::InputInt("Iterations", &rule.numIterations); TODO: Fix this!

                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::PopID();
        }

        m_settings.m_buffersInUse = nextInputBuffer;
    }

    ImGui::End();



    return S_OK;
}


//--------------------------------------------------------------------------------------
// Once per frame processing
//--------------------------------------------------------------------------------------
void Game::ComputeCPU()
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Frame begin");
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"ComputeCPU");

    FrameInputData& inputData = m_inputMgr->GetFrameInput(0);

    m_player->HandleInput(inputData.key);

    HandleInput(inputData.key);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    PIXEndEvent();  // ComputeCPU
}

void Game::ComputeGPU()
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeGPU");

    //Render shadow map
    if (m_gameData.useShadowMaps)
    {
        m_renderManager.RenderShadowMap();
    }
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void Game::Render(bool oculus)
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Render");

    m_renderManager.GetRenderData().frameStats[FPS_STAT].stat = m_timer.GetFramesPerSecond();

    XMStoreFloat4x4(&m_renderManager.GetRenderData().view, m_player->GetViewMatrix());
    m_renderManager.GetRenderData().eyePos = m_player->GetEyePosition();

    m_renderManager.Render(oculus, m_gameData.useAlphaBlendedRenderTarget, m_gameData.useShadowMaps, m_showHelp,
                           m_renderToSharedTexture, &m_gameData.clearColor.x);

    PIXEndEvent();  // Render
    PIXEndEvent();  // Frame begin
}

void Game::HandleInput(bool key[512])  // WM_KEYDOWN
{
    const char availableKeys[] = { '0', 'Z', 'P', 'M' , 'H', 'N', 'B', 'R', '1', '2', '3', 'Y', '<', '>', VK_TAB };
    for (char k : availableKeys)
    {
        if (key[k])
        {
            switch (k)
            {
            case ']':
                m_resetTree = true;
                m_loader._currentSeed++;
                key[k] = false;
                break;
            case '[':
                if (m_loader._currentSeed > 0)
                {
                    m_resetTree = true;
                    m_loader._currentSeed--;
                    key[k] = false;
                }
                break;
            case '0':
                m_timeStart = 0;
                key[k] = false; 
                break;
            case '>':
                m_timeCurrent += 1.0 / 60.0;
                break;
            case '<':
                m_timeCurrent -= 1.0 / 60.0;
                break;
            case 'Z':
                m_settings.m_showShadowBuffer = !m_settings.m_showShadowBuffer;
                m_settings.m_showShadowBuffer = m_settings.m_showShadowBuffer;
                key[k] = false;
                break;
            case 'P':
                m_settings.m_paused = !m_settings.m_paused;
                key[k] = false;
                break;
            case 'M':
                m_settings.m_wireframe = !m_settings.m_wireframe;
                m_settings.m_wireframe = m_settings.m_wireframe;
                key[k] = false;
                m_reloadDevice = true;
                break;
            case VK_TAB:
                m_settings.m_showDebugUI = !m_settings.m_showDebugUI;
                key[k] = false;
                break;
            case 'R':
                m_resetTree = true;
                //m_timeStart = 0;
                key[k] = false;
                break;
            case 'N':
                m_advanceScene = true;
                m_advanceSceneAmount = 1;
                key[k] = false;
                break;
            case 'B':
                m_advanceScene = true;
                m_advanceSceneAmount = -1;
                key[k] = false;
                break;
            case '2':
                m_reloadDevice = true;
                break;
            case '3':
                m_rotateLights = !m_rotateLights;
                key[k] = false;
                break;
                //case '1':
            //case '2':
            //case '3':
            //case '4':
            //case '5':
            //case '6':
            //	Select(k - '0');
            //	break;
            }
        }
    }
}




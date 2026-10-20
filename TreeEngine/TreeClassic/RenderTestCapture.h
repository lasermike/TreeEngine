#pragma once
#include <string>
#include <vector>
#include <array>

class Game;

struct RenderTestPoint
{
    double time;
    std::wstring filename;
    std::string mode;
    unsigned width = 0;
    unsigned height = 0;
    bool poseCache = true;
    bool showShadowBuffer = false;
    bool overrideCamera = false;
    std::array<float, 3> cameraPosition = {};
};

struct RenderTestRequest
{
    std::string scene;
    std::string mode;
    unsigned seed = 12345;
    unsigned width = 640;
    unsigned height = 480;
    int warmupFrames = 3;
    int benchmarkFrames = 0;
    double benchmarkTimeStep = 0;
    bool postProcessing = true;
    bool validateScenePersistence = false;
    std::wstring report;
    std::vector<RenderTestPoint> captures;
};

void ParseRenderTestCommandLine();
const RenderTestRequest* GetRenderTestRequest();
int RunRenderTestCapture(Game& game);

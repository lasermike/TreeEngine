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
    bool poseCache = true;
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
    std::wstring report;
    std::vector<RenderTestPoint> captures;
};

void ParseRenderTestCommandLine();
const RenderTestRequest* GetRenderTestRequest();
int RunRenderTestCapture(Game& game);

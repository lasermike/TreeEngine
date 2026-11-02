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
    float waterMotionSpeed = 1.0f;
    bool opacityMicromaps = true; // false makes DXR run the leaf alpha test without opacity micromaps.
    bool hasReflectionRays = false; // Omitted: preserve the application default.
    int reflectionRays = 0; // Explicit 0 disables traced transmission reflections; otherwise 1-32.
    double benchmarkTimeStep = 0;
    bool postProcessing = true;
    bool validateScenePersistence = false;
    // Performance run: profile every frame instead of capturing images.
    bool performance = false;
    int performanceFrames = 20;
    double performanceFrameTimeStep = 1.0 / 60.0;
    double performanceStartTime = 5; // Simulation time of frame 0, after the trees have grown.
    std::wstring report;
    std::vector<RenderTestPoint> captures;
};

void ParseRenderTestCommandLine();
const RenderTestRequest* GetRenderTestRequest();
int RunRenderTestCapture(Game& game);

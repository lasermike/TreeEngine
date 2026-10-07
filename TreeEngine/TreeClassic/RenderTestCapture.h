#pragma once
#include <string>
#include <vector>

class Game;

struct RenderTestPoint
{
    double time;
    std::wstring filename;
};

struct RenderTestRequest
{
    std::string scene;
    std::string mode;
    unsigned seed = 12345;
    unsigned width = 640;
    unsigned height = 480;
    int warmupFrames = 3;
    bool postProcessing = true;
    std::wstring report;
    std::vector<RenderTestPoint> captures;
};

void ParseRenderTestCommandLine();
const RenderTestRequest* GetRenderTestRequest();
int RunRenderTestCapture(Game& game);

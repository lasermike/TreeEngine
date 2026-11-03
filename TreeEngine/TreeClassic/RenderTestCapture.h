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
    // Stress sequences: load this scene first when it differs from the current one, as the app's scene cycling does.
    std::string scene;
    std::string antialiasing;   // "none" or "smaa"; empty uses the request's.
    bool postProcessing = true;
    // Motion-vector tests: the warm-up frames render from the previous camera and the captured frame from this one.
    bool hasCameraRotation = false;
    std::array<float, 3> cameraRotationDegrees = {};   // Pitch, yaw, roll; omitted keeps the player's rotation.
    bool hasPreviousCamera = false;
    std::array<float, 3> previousCameraPosition = {};
    std::array<float, 3> previousCameraRotationDegrees = {};
    std::string debugView = "none";   // "none", "motion", "reprojection" or "history" (Settings::MotionView).
    bool hasJitter = false;           // Fixed sub-pixel camera jitter, in pixels.
    std::array<float, 2> jitter = {};
};

// A camera pose on a performance run's path; time is seconds after the run's start time.
struct RenderTestCameraKey
{
    double time = 0;
    std::array<float, 3> position = {};
    std::array<float, 3> rotationDegrees = {}; // Pitch, yaw, roll, as in a scene's camera.
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
    std::string antialiasing = "none"; // "none" or "smaa"; none keeps gold images independent of the app default.
    bool hasJitter = false;            // A fixed camera jitter in pixels for every capture; see RenderTestPoint.
    std::array<float, 2> jitter = {};
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
    // Frames rendered at the start time before the measured frames: they absorb the scene load and the
    // driver's slow updates after the first acceleration-structure build. Reported with "settle": true.
    int performanceSettleFrames = 0;
    // Optional camera path, interpolated per measured frame. Settle frames hold the first key.
    std::vector<RenderTestCameraKey> performanceCamera;
    std::wstring report;
    std::vector<RenderTestPoint> captures;
    // Stress diagnostics.
    bool allCores = false;          // Update objects on every core, like the app, instead of one ordered thread.
    bool dred = false;              // Record DRED breadcrumbs and page faults; written to dredReport on device removal.
    std::wstring dredReport;
    std::wstring dumpFile;          // Minidump written if the process crashes or aborts.
};

void ParseRenderTestCommandLine();
const RenderTestRequest* GetRenderTestRequest();
int RunRenderTestCapture(Game& game);

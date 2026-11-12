#include "pch.h"
#include "RenderTestCapture.h"
#include "Game.h"
#include "Player.h"
#include "SceneDefinition.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace
{
    std::unique_ptr<RenderTestRequest> request;

    std::wstring Wide(const std::string& value)
    {
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
        if (!count) throw std::runtime_error("Invalid UTF-8 path in render test request");
        std::vector<wchar_t> buffer(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, buffer.data(), count);
        return buffer.data();
    }

    void CheckPersistence(bool passed, const std::string& message)
    {
        if (!passed) throw std::runtime_error("Scene persistence: " + message);
    }

    void ValidateScenePersistence(Game& game)
    {
        const std::string stem = "persistence-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        const auto directory = SceneData::Utf8Path(GameCommon::GetContentFileRoot()) + "\\UserScenes";
        const auto filename = directory + "\\" + stem + ".scene.json";
        const bool directoryExisted = GetFileAttributesW(Wide(directory).c_str()) != INVALID_FILE_ATTRIBUTES;
        struct Cleanup
        {
            std::string filename, directory;
            bool directoryExisted;
            ~Cleanup()
            {
                if (GetFileAttributesW(Wide(filename).c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(Wide(filename).c_str()))
                    OutputDebugStringA(("Cannot clean up persistence test scene: " + filename + "\n").c_str());
                if (!directoryExisted && !RemoveDirectoryW(Wide(directory).c_str()))
                    OutputDebugStringA(("Cannot clean up persistence test directory: " + directory + "\n").c_str());
            }
        } cleanup{ filename, directory, directoryExisted };
        CheckPersistence(SUCCEEDED(game.LoadScene("builtin:LoadTestTree")), game.GetSceneError());
        CheckPersistence(SUCCEEDED(game.RenderTestFrame(1)), "initial render failed");
        auto& data = game.GetRenderManager().GetRenderData();
        data.clearColor = { 0.1f, 0.2f, 0.3f, 1 };
        data.shadowLightPositionOverride = true;
        data.shadowLightPosition = { 7, 8, 9 };
        data.numPointLights = 1;
        data.pointLights[0].Position = { 2, 3, 4 };
        data.pointLights[0].Range = 8;
        data.pointLights[0].Att = { 1, 0.1f, 0 };
        game.GetPlayer()->SetPosition(DirectX::XMVectorSet(2, 3, -8, 1));
        std::string editedTree;
        for (auto* object : game.GetScene().Children())
        {
            auto& params = object->GetParams();
            if (params.generatorType != LSystemGeneratorType) continue;
            editedTree = params.name;
            object->SetPosition(DirectX::XMVectorSet(4, 5, 6, 1));
            params.textureCoordScale = { 2, 3 };
            params.materials.at(0).Diffuse = { 0.25f, 0.5f, 0.75f, 1 };
            params.materials.at(0).ApplyTextureCoordScale(4, 5);
            auto& generator = object->GetParams<LSystemParams>().GetGeneratorParameters();
            generator._axiom = "F[+F]";
            generator._rules = { Rule("F", 2, "FF") };
            generator._numIterations = 2;
            break;
        }
        CheckPersistence(!editedTree.empty(), "fixture has no editable tree");
        auto* originalScene = &game.GetScene();
        const float savedTime = data.time;
        CheckPersistence(SUCCEEDED(game.SaveScene("Runtime round trip", stem)), game.GetSceneError());
        CheckPersistence(&game.GetScene() == originalScene && data.time == savedTime, "saving reset the live scene");
        const auto savedText = SceneData::SerializeScene(SceneData::ReadScene(filename));
        auto saved = SceneData::ReadScene(filename);
        CheckPersistence(saved.camera.position.x == 2 && saved.clearColor.y == 0.2f &&
            saved.hasShadowLightPosition && saved.shadowLightPosition.z == 9 && saved.pointLight.range == 8,
            "camera or lighting edits were lost");
        bool foundTree = false;
        for (const auto& object : saved.objects)
            if (object->id == editedTree)
            {
                const auto& tree = dynamic_cast<const SceneData::TreeDefinition&>(*object);
                CheckPersistence(tree.transform.position.x == 4 && tree.textureCoordScale.x == 2 &&
                    tree.trunk.textureCoordScale.x == 4 && tree.trunk.baseColor.x == 0.25f &&
                    tree.generator.axiom == "F[+F]" && tree.generator.rules.at(0).iterations == 2,
                    "object, material, UV or rule edits were lost");
                foundTree = true;
            }
        CheckPersistence(foundTree, "saved tree was omitted");
        CheckPersistence(SUCCEEDED(game.LoadScene("user:" + stem)), game.GetSceneError());
        CheckPersistence(DirectX::XMVectorGetX(game.GetPlayer()->GetPosition()) == 2 &&
            data.shadowLightPositionOverride && data.shadowLightPosition.z == 9, "reloading did not restore authored settings");
        originalScene = &game.GetScene();
        CheckPersistence(FAILED(game.LoadScene(filename + ".missing")) && &game.GetScene() == originalScene &&
            DirectX::XMVectorGetX(game.GetPlayer()->GetPosition()) == 2, "failed load replaced the active scene");
        CheckPersistence(FAILED(game.SaveScene()) && SceneData::SerializeScene(SceneData::ReadScene(filename)) == savedText,
            "unconfirmed overwrite changed the saved scene");
        CheckPersistence(SUCCEEDED(game.SaveScene({}, {}, true)), game.GetSceneError());
        for (auto* object : game.GetScene().Children())
            if (object->GetParams().name == editedTree)
            {
                auto& iterations = object->GetParams<LSystemParams>().GetGeneratorParameters()._numIterations;
                iterations = 21;
                CheckPersistence(FAILED(game.SaveScene({}, {}, true)), "invalid live state was saved");
                iterations = 2;
            }
        CheckPersistence(SceneData::ReadScene(filename).name == "Runtime round trip", "failed save damaged the target");
        CheckPersistence(SUCCEEDED(game.LoadScene(request->scene)), game.GetSceneError());
        game.ConfigureRenderTest(request->scene, request->mode == "raytracing", request->postProcessing);
        srand(request->seed);
        const auto before = request->report + L".before.png";
        const auto after = request->report + L".after.png";
        for (int frame = 0; frame < 3; ++frame)
            CheckPersistence(SUCCEEDED(game.RenderTestFrame(1, frame == 2 ? before : std::wstring())), "original scene render failed");
        CheckPersistence(SUCCEEDED(game.SaveScene("No-edit round trip", stem, true)), game.GetSceneError());
        SceneData::WriteScene(SceneData::ReadScene(filename), SceneData::Utf8Path(request->report) + ".scene.json");
        CheckPersistence(SUCCEEDED(game.LoadScene("user:" + stem)), game.GetSceneError());
        srand(request->seed);
        for (int frame = 0; frame < 3; ++frame)
            CheckPersistence(SUCCEEDED(game.RenderTestFrame(1, frame == 2 ? after : std::wstring())), "saved scene render failed");
        CheckPersistence(SUCCEEDED(game.LoadScene(request->scene)), game.GetSceneError());
        std::cerr << "Runtime scene persistence checks passed\n";
    }
}

void ParseRenderTestCommandLine()
{
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) throw std::runtime_error("Cannot read command line");
    std::wstring filename;
    if (count > 1 && std::wstring(args[1]) == L"--render-test")
    {
        if (count == 3) filename = args[2];
        else { LocalFree(args); throw std::runtime_error("Usage: TreeClassic --render-test <request.json>"); }
    }
    LocalFree(args);
    if (filename.empty()) return;
    std::ifstream input(filename.c_str());
    if (!input) throw std::runtime_error("Cannot open render test request");
    auto json = nlohmann::json::parse(input);
    auto parsed = std::make_unique<RenderTestRequest>();
    parsed->scene = json.at("scene").get<std::string>();
    parsed->mode = json.at("mode").get<std::string>();
    parsed->seed = json.value("seed", 12345u);
    parsed->width = json.at("width").get<unsigned>();
    parsed->height = json.at("height").get<unsigned>();
    parsed->warmupFrames = json.value("warmupFrames", 3);
    parsed->benchmarkFrames = json.value("benchmarkFrames", 0);
    parsed->hasReflectionRays = json.contains("reflectionRays");
    parsed->reflectionRays = json.value("reflectionRays", 0);
    parsed->waterMotionSpeed = json.value("waterMotionSpeed", 1.0f);
    parsed->opacityMicromaps = json.value("opacityMicromaps", true);
    parsed->antialiasing = json.value("antialiasing", std::string("none"));
    if (parsed->antialiasing != "none" && parsed->antialiasing != "smaa" && parsed->antialiasing != "taa"
        && parsed->antialiasing != "fsr" && parsed->antialiasing != "dlss")
        throw std::runtime_error("antialiasing must be \"none\", \"smaa\", \"taa\", \"fsr\" or \"dlss\"");
    const std::string alphaTest = json.value("alphaTest", std::string("hashed"));
    if (alphaTest != "hashed" && alphaTest != "cutoff")
        throw std::runtime_error("alphaTest must be \"hashed\" or \"cutoff\"");
    parsed->hashedAlphaTest = alphaTest == "hashed";
    parsed->viewCulling = json.value("viewCulling", true);
    parsed->batchInstances = json.value("batchInstances", true);
    parsed->lodFadeFrames = json.value("lodFadeFrames", 0);
    parsed->shadowCascades = json.value("shadowCascades", 3);
    parsed->shadowDistance = json.value("shadowDistance", 150.0f);
    if (parsed->shadowCascades < 1 || parsed->shadowCascades > 4 || !std::isfinite(parsed->shadowDistance) || parsed->shadowDistance <= 0)
        throw std::runtime_error("shadowCascades must be 1 to 4 and shadowDistance positive");
    if (parsed->lodFadeFrames < 0 || parsed->lodFadeFrames > 1000)
        throw std::runtime_error("lodFadeFrames must be 0 to 1000");
    parsed->hdr = json.value("hdr", false);
    parsed->tonemap = json.value("tonemap", std::string("clamp"));
    parsed->exposure = json.value("exposure", 0.0f);
    if ((parsed->tonemap != "clamp" && parsed->tonemap != "aces" && parsed->tonemap != "agx")
        || !std::isfinite(parsed->exposure) || std::abs(parsed->exposure) > 16)
        throw std::runtime_error("tonemap must be \"clamp\", \"aces\" or \"agx\", and exposure a finite EV");
    parsed->renderScale = json.value("renderScale", 1.0f);
    if (!(parsed->renderScale >= 0.25f && parsed->renderScale <= 1.0f))
        throw std::runtime_error("renderScale must be in [0.25, 1]");
    if (json.contains("jitter"))
    {
        parsed->hasJitter = true;
        parsed->jitter = json.at("jitter").get<std::array<float, 2>>();
        if (!std::isfinite(parsed->jitter[0]) || !std::isfinite(parsed->jitter[1]))
            throw std::runtime_error("Invalid jitter");
    }
    parsed->benchmarkTimeStep = json.value("benchmarkTimeStep", 0.0);
    parsed->postProcessing = json.value("postProcessing", true);
    parsed->validateScenePersistence = json.value("validateScenePersistence", false);
    parsed->report = Wide(json.at("report").get<std::string>());
    parsed->allCores = json.value("allCores", false);
    parsed->dred = json.value("dred", false);
    parsed->dredReport = Wide(json.value("dredReport", std::string()));
    parsed->dumpFile = Wide(json.value("dumpFile", std::string()));
    if (parsed->scene.empty() || (parsed->mode != "raster" && parsed->mode != "raytracing")
        || parsed->width < 32 || parsed->width > 2048 || parsed->height < 32 || parsed->height > 2048
        || parsed->warmupFrames < 0 || parsed->warmupFrames > 60
        || parsed->benchmarkFrames < 0 || parsed->benchmarkFrames > 10000
        || parsed->reflectionRays < 0 || parsed->reflectionRays > 32
        || !std::isfinite(parsed->waterMotionSpeed) || parsed->waterMotionSpeed < 0 || parsed->waterMotionSpeed > 4
        || !std::isfinite(parsed->benchmarkTimeStep) || parsed->benchmarkTimeStep < 0)
        throw std::runtime_error("Invalid render test settings");
    if (json.contains("performance"))
    {
        const auto& performance = json.at("performance");
        parsed->performance = true;
        parsed->performanceFrames = performance.value("frames", 20);
        parsed->performanceFrameTimeStep = performance.value("frameTimeStep", 1.0 / 60.0);
        parsed->performanceStartTime = performance.value("startTime", 5.0);
        parsed->performanceSettleFrames = performance.value("settleFrames", 0);
        if (parsed->performanceFrames < 1 || parsed->performanceFrames > 10000
            || parsed->performanceSettleFrames < 0 || parsed->performanceSettleFrames > 1000
            || !std::isfinite(parsed->performanceFrameTimeStep) || parsed->performanceFrameTimeStep < 0
            || !std::isfinite(parsed->performanceStartTime) || parsed->performanceStartTime < 0)
            throw std::runtime_error("Invalid performance settings");
        if (performance.contains("camera"))
        {
            for (const auto& value : performance.at("camera"))
            {
                RenderTestCameraKey key;
                key.time = value.at("time").get<double>();
                key.position = value.at("position").get<std::array<float, 3>>();
                key.rotationDegrees = value.at("rotationDegrees").get<std::array<float, 3>>();
                bool finite = std::isfinite(key.time);
                for (int i = 0; i < 3; ++i)
                    finite = finite && std::isfinite(key.position[i]) && std::isfinite(key.rotationDegrees[i]);
                if (!finite || key.time < 0 || (!parsed->performanceCamera.empty() && key.time <= parsed->performanceCamera.back().time))
                    throw std::runtime_error("Camera keys need finite values and increasing nonnegative times");
                parsed->performanceCamera.push_back(key);
            }
            // The first frame loads the scene, which places the camera from the scene definition.
            if (parsed->performanceCamera.size() < 2 || parsed->performanceSettleFrames < 1)
                throw std::runtime_error("A camera path needs at least two keys and one settle frame");
        }
        if (json.contains("captures") || parsed->validateScenePersistence)
            throw std::runtime_error("Performance runs do not capture images or validate persistence");
        request = std::move(parsed);
        return;
    }
    for (const auto& value : json.at("captures"))
    {
        RenderTestPoint point;
        point.time = value.at("time").get<double>();
        point.filename = Wide(value.at("filename").get<std::string>());
        point.mode = value.value("mode", parsed->mode);
        if (value.contains("width") != value.contains("height"))
            throw std::runtime_error("Capture width and height must be specified together");
        point.width = value.value("width", parsed->width);
        point.height = value.value("height", parsed->height);
        if (point.width < 32 || point.width > 2048 || point.height < 32 || point.height > 2048)
            throw std::runtime_error("Invalid capture dimensions");
        point.poseCache = value.value("poseCache", true);
        point.scene = value.value("scene", std::string());
        point.antialiasing = value.value("antialiasing", parsed->antialiasing);
        if (point.antialiasing != "none" && point.antialiasing != "smaa" && point.antialiasing != "taa"
            && point.antialiasing != "fsr" && point.antialiasing != "dlss")
            throw std::runtime_error("Capture antialiasing must be \"none\", \"smaa\", \"taa\", \"fsr\" or \"dlss\"");
        point.postProcessing = value.value("postProcessing", parsed->postProcessing);
        point.showShadowBuffer = value.value("showShadowBuffer", false);
        if (value.contains("cameraPosition"))
        {
            point.cameraPosition = value.at("cameraPosition").get<std::array<float, 3>>();
            for (float coordinate : point.cameraPosition)
                if (!std::isfinite(coordinate)) throw std::runtime_error("Invalid capture camera position");
            point.overrideCamera = true;
        }
        auto readVector = [&](const char* name, std::array<float, 3>& vector)
        {
            if (!value.contains(name)) return false;
            vector = value.at(name).get<std::array<float, 3>>();
            for (float coordinate : vector)
                if (!std::isfinite(coordinate)) throw std::runtime_error(std::string("Invalid ") + name);
            if (!point.overrideCamera) throw std::runtime_error(std::string(name) + " needs cameraPosition");
            return true;
        };
        point.hasCameraRotation = readVector("cameraRotationDegrees", point.cameraRotationDegrees);
        point.hasPreviousCamera = readVector("previousCameraPosition", point.previousCameraPosition);
        point.previousCameraRotationDegrees = point.cameraRotationDegrees;
        if (readVector("previousCameraRotationDegrees", point.previousCameraRotationDegrees))
        {
            if (!point.hasCameraRotation) throw std::runtime_error("previousCameraRotationDegrees needs cameraRotationDegrees");
            if (!point.hasPreviousCamera)
            {
                point.hasPreviousCamera = true;
                point.previousCameraPosition = point.cameraPosition;
            }
        }
        point.debugView = value.value("debugView", std::string("none"));
        if (point.debugView != "none" && point.debugView != "motion" && point.debugView != "reprojection" && point.debugView != "history")
            throw std::runtime_error("debugView must be \"none\", \"motion\", \"reprojection\" or \"history\"");
        point.hasJitter = parsed->hasJitter;
        point.jitter = parsed->jitter;
        if (value.contains("jitter"))
        {
            point.hasJitter = true;
            point.jitter = value.at("jitter").get<std::array<float, 2>>();
            if (!std::isfinite(point.jitter[0]) || !std::isfinite(point.jitter[1]))
                throw std::runtime_error("Invalid capture jitter");
        }
        point.hdr = value.value("hdr", parsed->hdr);
        point.tonemap = value.value("tonemap", parsed->tonemap);
        point.exposure = value.value("exposure", parsed->exposure);
        if ((point.tonemap != "clamp" && point.tonemap != "aces" && point.tonemap != "agx")
            || !std::isfinite(point.exposure) || std::abs(point.exposure) > 16)
            throw std::runtime_error("Capture tonemap must be \"clamp\", \"aces\" or \"agx\", and exposure a finite EV");
        point.renderScale = value.value("renderScale", parsed->renderScale);
        point.highlightsThroughGeometry = value.value("highlightsThroughGeometry", true);
        if (value.contains("expectedSelection"))
        {
            point.checkSelection = true;
            point.expectedSelection = value.at("expectedSelection").get<std::vector<std::string>>();
        }
        if (value.contains("selectionClicks"))
        {
            for (const auto& click : value.at("selectionClicks"))
            {
                RenderTestPoint::SelectionClick entry;
                entry.frame = click.value("frame", 1);
                entry.u = click.at("u").get<float>();
                entry.v = click.at("v").get<float>();
                entry.toggle = click.value("ctrl", false);
                if (entry.frame < 1 || entry.frame + 2 > parsed->warmupFrames + parsed->benchmarkFrames
                    || !(entry.u >= 0 && entry.u < 1 && entry.v >= 0 && entry.v < 1))
                    throw std::runtime_error("Selection clicks require valid viewport coordinates and two frames before capture");
                point.selectionClicks.push_back(entry);
            }
        }
        if (!(point.renderScale >= 0.25f && point.renderScale <= 1.0f))
            throw std::runtime_error("Capture renderScale must be in [0.25, 1]");
        if (value.contains("previousTime"))
        {
            point.hasPreviousTime = true;
            point.previousTime = value.at("previousTime").get<double>();
            if (!std::isfinite(point.previousTime) || point.previousTime < 0)
                throw std::runtime_error("Invalid capture previousTime");
        }
        if (!std::isfinite(point.time) || point.time < 0 || point.filename.empty())
            throw std::runtime_error("Invalid capture time or filename");
        if (point.mode != "raster" && point.mode != "raytracing")
            throw std::runtime_error("Invalid capture mode");
        parsed->captures.push_back(point);
    }
    if (parsed->captures.empty()) throw std::runtime_error("No captures requested");
    request = std::move(parsed);
}

const RenderTestRequest* GetRenderTestRequest() { return request.get(); }

namespace
{
    std::string Utf8(const std::wstring& value)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (!bytes) throw std::runtime_error("Cannot encode text as UTF-8");
        std::string result(bytes, '\0');
        if (!WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], bytes, nullptr, nullptr))
            throw std::runtime_error("Cannot encode text as UTF-8");
        result.pop_back(); // Remove the terminator written by WideCharToMultiByte.
        return result;
    }

    // A request's "antialiasing" value, already validated.
    Settings::Antialiasing AntialiasingSetting(const std::string& name)
    {
        return name == "smaa" ? Settings::Antialiasing::Smaa : name == "taa" ? Settings::Antialiasing::Taa
            : name == "fsr" ? Settings::Antialiasing::Fsr : name == "dlss" ? Settings::Antialiasing::Dlss
            : Settings::Antialiasing::None;
    }

    Settings::Tonemap TonemapSetting(const std::string& name)
    {
        return name == "aces" ? Settings::Tonemap::Aces : name == "agx" ? Settings::Tonemap::Agx : Settings::Tonemap::Clamp;
    }

    // The camera pose at time seconds into the path: linear between keys, held outside them.
    void ApplyCameraPath(Game& game, const std::vector<RenderTestCameraKey>& keys, double time)
    {
        using namespace DirectX;
        auto rotation = [](const RenderTestCameraKey& key)
        {
            return XMQuaternionRotationRollPitchYaw(XMConvertToRadians(key.rotationDegrees[0]),
                XMConvertToRadians(key.rotationDegrees[1]), XMConvertToRadians(key.rotationDegrees[2]));
        };
        size_t next = 1;
        while (next + 1 < keys.size() && keys[next].time < time) ++next;
        const auto& a = keys[next - 1];
        const auto& b = keys[next];
        const float t = static_cast<float>((std::min)(1.0, (std::max)(0.0, (time - a.time) / (b.time - a.time))));
        game.GetPlayer()->SetPosition(XMVectorLerp(XMVectorSet(a.position[0], a.position[1], a.position[2], 1),
            XMVectorSet(b.position[0], b.position[1], b.position[2], 1), t));
        game.GetPlayer()->SetRotation(XMQuaternionSlerp(rotation(a), rotation(b), t));
    }

    // Renders the requested frames with profiling on and reports every frame's timings and counters.
    int RunPerformanceCapture(Game& game)
    {
        nlohmann::json report = { { "scene", request->scene }, { "mode", request->mode },
            { "width", request->width }, { "height", request->height }, { "seed", request->seed },
            { "frames", request->performanceFrames }, { "frameTimeStep", request->performanceFrameTimeStep },
            { "renderScale", request->renderScale },
            { "startTime", request->performanceStartTime }, { "settleFrames", request->performanceSettleFrames },
            { "cameraKeys", request->performanceCamera.size() },
            { "workerThreads", game.GetWorkerThreadCount() },
            { "gpuPasses", { "frame", "shadow", "main", "deformation", "blas", "tlas", "rays", "antialiasing" } },
            { "samples", nlohmann::json::array() }, { "success", false } };
        int exitCode = 0;
        try
        {
            game.ConfigureRenderTest(request->scene, request->mode == "raytracing", request->postProcessing, true);
            if (request->hasReflectionRays) game.SetRenderTestReflectionRays(request->reflectionRays);
            game.SetRenderTestWaterMotionSpeed(request->waterMotionSpeed);
            game.SetRenderTestOpacityMicromaps(request->opacityMicromaps);
            game.SetRenderTestAntialiasing(AntialiasingSetting(request->antialiasing));
            game.SetRenderTestHashedAlphaTest(request->hashedAlphaTest);
            game.SetRenderTestViewCulling(request->viewCulling);
            game.SetRenderTestBatchInstances(request->batchInstances);
            game.SetRenderTestLodFadeFrames(request->lodFadeFrames);
            game.SetRenderTestShadows(request->shadowCascades, request->shadowDistance);
            game.SetRenderTestHdr(request->hdr, TonemapSetting(request->tonemap), request->exposure);
            game.SetRenderTestRenderScale(request->renderScale);
            const int settleFrames = request->performanceSettleFrames;
            for (int frame = 0; frame < settleFrames + request->performanceFrames; ++frame)
            {
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) throw std::runtime_error("Performance test window was closed");
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                // Settle frames hold the start time and the first camera key; measured frames then advance both.
                const double pathTime = frame < settleFrames ? 0.0 : (frame - settleFrames) * request->performanceFrameTimeStep;
                const double simulationTime = request->performanceStartTime + pathTime;
                if (!request->performanceCamera.empty())
                    ApplyCameraPath(game, request->performanceCamera, pathTime);
                const auto start = std::chrono::steady_clock::now();
                const HRESULT result = game.RenderTestFrame(simulationTime, {}, !request->performanceCamera.empty());
                const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (FAILED(result)) throw std::runtime_error("Rendering failed, HRESULT=" + std::to_string(static_cast<unsigned>(result)));
                const auto& profile = game.GetRenderManager().GetRenderData().profile;
                if (!profile.valid) throw std::runtime_error("GPU profiling did not complete");
                if (frame == 0)
                {
                    // Frame 0 finishes the load: generation and GPU setup run before it renders.
                    const auto& load = game.GetSceneLoadTiming();
                    if (!load.complete) throw std::runtime_error("Scene load timing is incomplete");
                    report["sceneLoad"] = { { "totalMs", load.TotalMs() }, { "definitionMs", load.definitionMs },
                        { "generationMs", load.generationMs }, { "graphicsMs", load.graphicsMs } };
                    report["adapter"] = Utf8(profile.adapter);
                    report["renderWidth"] = game.GetRenderManager().GetRenderData().renderWidth;
                    report["renderHeight"] = game.GetRenderManager().GetRenderData().renderHeight;
                    // Baked during the load; "requested" is the request's opacityMicromaps, applied when supported.
                    const auto& micromaps = game.GetRenderManager().GetRenderData().opacityMicromaps;
                    report["opacityMicromaps"] = { { "requested", request->opacityMicromaps }, { "supported", micromaps.supported },
                        { "micromaps", micromaps.micromaps }, { "specialTriangles", micromaps.specialTriangles },
                        { "subdivisionLevel", micromaps.subdivisionLevel }, { "bytes", micromaps.bytes },
                        { "states", { micromaps.states[0], micromaps.states[1], micromaps.states[2], micromaps.states[3] } } };
                }
                DirectX::XMFLOAT3 camera;
                DirectX::XMStoreFloat3(&camera, game.GetPlayer()->GetPosition());
                report["samples"].push_back({
                    { "frame", frame - settleFrames }, { "settle", frame < settleFrames },
                    { "cameraPosition", { camera.x, camera.y, camera.z } },
                    { "simulationTime", simulationTime }, { "frameMs", frameMs },
                    { "sceneUpdateMs", profile.sceneUpdateMs }, { "drawUploadMs", profile.drawUploadMs },
                    { "blasHashMs", profile.blasHashMs }, { "blasDescriptorsMs", profile.blasDescriptorsMs },
                    { "blasSnapshotMs", profile.blasSnapshotMs }, { "reusedBLAS", profile.reusedBLAS },
                    { "blasCpuMs", profile.blasCpuMs },
                    { "blasSubmitMs", profile.blasSubmitMs }, { "tlasSubmitMs", profile.tlasSubmitMs },
                    { "gpuMs", profile.gpuMs }, { "drawRecords", profile.drawRecords },
                    { "worldVertices", profile.worldVertices }, { "skinnedVertices", profile.skinnedVertices },
                    { "indexEntries", profile.indexEntries }, { "dispatchGroups", profile.dispatchGroups },
                    { "rebuiltPoseBranches", profile.rebuiltPoseBranches }, { "reusedPoseBranches", profile.reusedPoseBranches },
                    { "poseCacheBytes", profile.poseCacheBytes }, { "drawUploadBytes", profile.drawUploadBytes },
                    { "accelerationStructureBytes", profile.accelerationStructureBytes },
                    { "rebuiltBLAS", profile.rebuiltBLAS }, { "builtStaticBLAS", profile.builtStaticBLAS },
                    { "compactedStaticBLAS", profile.compactedStaticBLAS }, { "staticBlasBytes", profile.staticBlasBytes }, { "dynamicBlasBuilds", profile.dynamicBlasBuilds },
                    { "dynamicBlasRefits", profile.dynamicBlasRefits }, { "dynamicBlasReuses", profile.dynamicBlasReuses },
                    { "tlasInstances", profile.tlasInstances }, { "blasHeapBytes", profile.blasHeapBytes },
                    { "blasHeapLiveBytes", profile.blasHeapLiveBytes }, { "blasScratchBytes", profile.blasScratchBytes },
                    { "drawnObjects", profile.drawnObjects }, { "culledObjects", profile.culledObjects }, { "lodLevelObjects", profile.lodLevelObjects }, { "drawnLeaves", profile.drawnLeaves }, { "drawnSticks", profile.drawnSticks }, { "instanceUploadMs", profile.instanceUploadMs }, { "instanceUploadBytes", profile.instanceUploadBytes }, { "batchedDraws", profile.batchedDraws }, { "batchedObjects", profile.batchedObjects }, { "fadingObjects", profile.fadingObjects } });
            }
            std::cerr << "Profiled " << request->scene << " / " << request->mode << ", "
                << request->performanceFrames << " frames" << std::endl;
            report["success"] = true;
        }
        catch (const std::exception& error)
        {
            report["error"] = error.what();
            std::cerr << error.what() << '\n';
            exitCode = 3;
        }
        std::ofstream output(request->report.c_str());
        if (!output) return 4;
        output << report.dump(2);
        return exitCode;
    }
}

int RunRenderTestCapture(Game& game)
{
    if (request->performance) return RunPerformanceCapture(game);
    nlohmann::json report = { { "scene", request->scene }, { "mode", request->mode },
        { "width", request->width }, { "height", request->height }, { "seed", request->seed },
        { "captures", nlohmann::json::array() }, { "success", false } };
    int exitCode = 0;
    unsigned currentWidth = request->width;
    unsigned currentHeight = request->height;
    DirectX::XMFLOAT3 originalCamera;
    DirectX::XMStoreFloat3(&originalCamera, game.GetPlayer()->GetPosition());
    try
    {
        if (request->validateScenePersistence)
        {
            ValidateScenePersistence(game);
            DirectX::XMStoreFloat3(&originalCamera, game.GetPlayer()->GetPosition());
            report["scenePersistenceValidated"] = true;
        }
        std::string currentScene = request->scene;
        for (size_t step = 0; step < request->captures.size(); ++step)
        {
            const auto& point = request->captures[step];
            // Logged before any work so the last line names the step if the process dies.
            std::cerr << "step " << step << ": " << (point.scene.empty() ? currentScene : point.scene) << " / " << point.mode
                << " " << point.width << "x" << point.height << " aa=" << point.antialiasing
                << " post=" << (point.postProcessing ? 1 : 0) << " t=" << point.time << std::endl;
            if (!point.scene.empty() && point.scene != currentScene)
            {
                const HRESULT loaded = game.LoadScene(point.scene);
                if (FAILED(loaded))
                    throw std::runtime_error("Loading scene " + point.scene + " failed, HRESULT=" + std::to_string(static_cast<unsigned>(loaded)));
                currentScene = point.scene;
                DirectX::XMStoreFloat3(&originalCamera, game.GetPlayer()->GetPosition());
                // Trees draw from the process-wide rand(); reseed so the scene generates as in a fresh process
                // (as the persistence check does). Interactive loads keep varying.
                srand(request->seed);
            }
            if (point.width != currentWidth || point.height != currentHeight)
            {
                HWND window = game.GetWindow();
                RECT dimensions = { 0, 0, static_cast<LONG>(point.width), static_cast<LONG>(point.height) };
                if (!AdjustWindowRect(&dimensions, static_cast<DWORD>(GetWindowLongPtr(window, GWL_STYLE)), FALSE) ||
                    !SetWindowPos(window, nullptr, 0, 0, dimensions.right - dimensions.left,
                        dimensions.bottom - dimensions.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                    throw std::runtime_error("Cannot resize render test window");
                currentWidth = point.width;
                currentHeight = point.height;
            }
            game.ConfigureRenderTest(currentScene, point.mode == "raytracing", point.postProcessing,
                request->benchmarkFrames > 0, point.showShadowBuffer);
            if (request->hasReflectionRays) game.SetRenderTestReflectionRays(request->reflectionRays);
            game.SetRenderTestWaterMotionSpeed(request->waterMotionSpeed);
            game.SetRenderTestOpacityMicromaps(request->opacityMicromaps);
            game.SetRenderTestAntialiasing(AntialiasingSetting(point.antialiasing));
            game.SetRenderTestHashedAlphaTest(request->hashedAlphaTest);
            game.SetRenderTestViewCulling(request->viewCulling);
            game.SetRenderTestBatchInstances(request->batchInstances);
            game.SetRenderTestLodFadeFrames(request->lodFadeFrames);
            game.SetRenderTestShadows(request->shadowCascades, request->shadowDistance);
            game.SetRenderTestHdr(point.hdr, TonemapSetting(point.tonemap), point.exposure);
            game.SetRenderTestRenderScale(point.renderScale);
            game.SetRenderTestHighlightsThroughGeometry(point.highlightsThroughGeometry);
            game.GetRenderManager().GetRenderData().cacheTreePoses = point.poseCache;
            const Settings::MotionView motionView = point.debugView == "motion" ? Settings::MotionView::Motion
                : point.debugView == "reprojection" ? Settings::MotionView::ReprojectedHistory
                : point.debugView == "history" ? Settings::MotionView::History : Settings::MotionView::Off;
            game.SetRenderTestMotion(motionView, point.hasJitter, point.jitter[0], point.jitter[1]);
            game.GetPlayer()->SetPosition(point.overrideCamera
                ? DirectX::XMVectorSet(point.cameraPosition[0], point.cameraPosition[1], point.cameraPosition[2], 1)
                : DirectX::XMLoadFloat3(&originalCamera));
            // The camera for a frame: the previous one while warming up, when the capture names one.
            auto placeCamera = [&](bool previous)
            {
                using namespace DirectX;
                const auto& position = previous ? point.previousCameraPosition : point.cameraPosition;
                const auto& rotation = previous ? point.previousCameraRotationDegrees : point.cameraRotationDegrees;
                game.GetPlayer()->SetPosition(XMVectorSet(position[0], position[1], position[2], 1));
                if (point.hasCameraRotation)
                    game.GetPlayer()->SetRotation(XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation[0]),
                        XMConvertToRadians(rotation[1]), XMConvertToRadians(rotation[2])));
            };
            if (point.overrideCamera)
                placeCamera(false);
            nlohmann::json samples = nlohmann::json::array();
            const int captureFrame = request->warmupFrames + request->benchmarkFrames;
            for (int frame = 0; frame <= captureFrame; ++frame)
            {
                if (!request->benchmarkFrames || frame == 0 || frame == captureFrame)
                    std::cerr << "Rendering " << currentScene << " / " << point.mode
                        << " at " << point.time << "s, frame " << frame << std::endl;
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) throw std::runtime_error("Render test window was closed");
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                if (point.hasPreviousCamera)
                    placeCamera(frame < captureFrame);
                auto start = std::chrono::steady_clock::now();
                for (const auto& click : point.selectionClicks)
                {
                    if (click.frame == frame)
                        game.GetRenderManager().GetRenderData().selection.Click(click.u, click.v, click.toggle);
                }
                const double simulationTime = frame == captureFrame ? point.time
                    : point.hasPreviousTime ? point.previousTime
                    : point.time + std::max(0, frame - request->warmupFrames) * request->benchmarkTimeStep;
                HRESULT result = game.RenderTestFrame(simulationTime,
                    frame == captureFrame ? point.filename : std::wstring(), point.overrideCamera);
                if (FAILED(result)) throw std::runtime_error("Rendering/capture failed, HRESULT=" + std::to_string(static_cast<unsigned>(result)));
                if (request->benchmarkFrames && frame >= request->warmupFrames && frame < captureFrame)
                {
                    const auto& profile = game.GetRenderManager().GetRenderData().profile;
                    if (!profile.valid) throw std::runtime_error("GPU profiling did not complete");
                    samples.push_back({
                        { "simulationTime", simulationTime },
                        { "frameMs", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() },
                        { "sceneUpdateMs", profile.sceneUpdateMs }, { "drawUploadMs", profile.drawUploadMs },
                        { "blasHashMs", profile.blasHashMs }, { "blasDescriptorsMs", profile.blasDescriptorsMs },
                        { "blasSnapshotMs", profile.blasSnapshotMs }, { "reusedBLAS", profile.reusedBLAS },
                        { "blasCpuMs", profile.blasCpuMs },
                        { "blasSubmitMs", profile.blasSubmitMs }, { "tlasSubmitMs", profile.tlasSubmitMs },
                        { "gpuMs", profile.gpuMs }, { "drawRecords", profile.drawRecords },
                        { "worldVertices", profile.worldVertices }, { "skinnedVertices", profile.skinnedVertices },
                        { "indexEntries", profile.indexEntries }, { "dispatchGroups", profile.dispatchGroups },
                        { "rebuiltPoseBranches", profile.rebuiltPoseBranches }, { "reusedPoseBranches", profile.reusedPoseBranches },
                        { "poseCacheBytes", profile.poseCacheBytes }, { "drawUploadBytes", profile.drawUploadBytes },
                        { "drawUploadCapacityBytes", profile.drawUploadCapacityBytes },
                        { "worldVertexCapacityBytes", profile.worldVertexCapacityBytes },
                        { "accelerationStructureBytes", profile.accelerationStructureBytes },
                        { "rebuiltBLAS", profile.rebuiltBLAS }, { "builtStaticBLAS", profile.builtStaticBLAS },
                        { "compactedStaticBLAS", profile.compactedStaticBLAS }, { "staticBlasBytes", profile.staticBlasBytes }, { "dynamicBlasBuilds", profile.dynamicBlasBuilds },
                    { "dynamicBlasRefits", profile.dynamicBlasRefits }, { "dynamicBlasReuses", profile.dynamicBlasReuses },
                    { "tlasInstances", profile.tlasInstances }, { "blasHeapBytes", profile.blasHeapBytes },
                    { "blasHeapLiveBytes", profile.blasHeapLiveBytes }, { "blasScratchBytes", profile.blasScratchBytes },
                    { "drawnObjects", profile.drawnObjects }, { "culledObjects", profile.culledObjects }, { "lodLevelObjects", profile.lodLevelObjects }, { "drawnLeaves", profile.drawnLeaves }, { "drawnSticks", profile.drawnSticks }, { "instanceUploadMs", profile.instanceUploadMs }, { "instanceUploadBytes", profile.instanceUploadBytes }, { "batchedDraws", profile.batchedDraws }, { "batchedObjects", profile.batchedObjects }, { "fadingObjects", profile.fadingObjects }
                    });
                    const int adapterBytes = WideCharToMultiByte(CP_UTF8, 0, profile.adapter.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (!adapterBytes) throw std::runtime_error("Cannot encode GPU adapter name as UTF-8");
                    std::string adapter(adapterBytes, '\0');
                    if (!WideCharToMultiByte(CP_UTF8, 0, profile.adapter.c_str(), -1, &adapter[0], adapterBytes, nullptr, nullptr))
                        throw std::runtime_error("Cannot encode GPU adapter name as UTF-8");
                    adapter.pop_back(); // Remove the terminator written by WideCharToMultiByte.
                    report["adapter"] = adapter;
                }
            }
            const auto& projection = game.GetRenderManager().GetRenderData().projectionData;
            if (projection.screenWidth != point.width || projection.screenHeight != point.height)
                throw std::runtime_error("Render target did not follow the window size");
            const auto& renderData = game.GetRenderManager().GetRenderData();
            std::vector<std::string> selectedNames;
            for (const auto* object : renderData.selection.objects)
                selectedNames.push_back(static_cast<WorldObject*>(const_cast<void*>(object))->GetParams().name);
            if (point.checkSelection && selectedNames != point.expectedSelection)
                throw std::runtime_error("Selection mismatch: " + nlohmann::json(selectedNames).dump()
                    + "; expected " + nlohmann::json(point.expectedSelection).dump());
            report["captures"].push_back({ { "scene", currentScene }, { "antialiasing", point.antialiasing },
                { "postProcessing", point.postProcessing }, { "time", point.time }, { "mode", point.mode },
                { "width", point.width }, { "height", point.height }, { "renderScale", point.renderScale },
                { "renderWidth", renderData.renderWidth }, { "renderHeight", renderData.renderHeight },
                { "upscaler", std::string(renderData.upscalerStatus) }, { "success", true },
                { "poseCache", point.poseCache }, { "samples", samples }, { "selectedObjects", selectedNames } });
        }
        report["gpuPasses"] = { "frame", "shadow", "main", "deformation", "blas", "tlas", "rays", "antialiasing" };
        report["success"] = true;
    }
    catch (const std::exception& error)
    {
        report["error"] = error.what();
        std::cerr << error.what() << '\n';
        exitCode = 3;
    }
    std::ofstream output(request->report.c_str());
    if (!output) return 4;
    output << report.dump(2);
    return exitCode;
}

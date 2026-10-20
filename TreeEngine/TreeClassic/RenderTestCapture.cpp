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
            params.materials.at(0).textureCoordScale = { 4, 5, 1, 1 };
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
                    tree.trunk.textureCoordScale.x == 4 && tree.trunk.diffuse.x == 0.25f &&
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
    parsed->benchmarkTimeStep = json.value("benchmarkTimeStep", 0.0);
    parsed->postProcessing = json.value("postProcessing", true);
    parsed->validateScenePersistence = json.value("validateScenePersistence", false);
    parsed->report = Wide(json.at("report").get<std::string>());
    if (parsed->scene.empty() || (parsed->mode != "raster" && parsed->mode != "raytracing")
        || parsed->width < 32 || parsed->width > 2048 || parsed->height < 32 || parsed->height > 2048
        || parsed->warmupFrames < 0 || parsed->warmupFrames > 60
        || parsed->benchmarkFrames < 0 || parsed->benchmarkFrames > 10000
        || !std::isfinite(parsed->benchmarkTimeStep) || parsed->benchmarkTimeStep < 0)
        throw std::runtime_error("Invalid render test settings");
    for (const auto& value : json.at("captures"))
    {
        RenderTestPoint point = { value.at("time").get<double>(), Wide(value.at("filename").get<std::string>()) };
        point.mode = value.value("mode", parsed->mode);
        point.poseCache = value.value("poseCache", true);
        if (value.contains("cameraPosition"))
        {
            point.cameraPosition = value.at("cameraPosition").get<std::array<float, 3>>();
            for (float coordinate : point.cameraPosition)
                if (!std::isfinite(coordinate)) throw std::runtime_error("Invalid capture camera position");
            point.overrideCamera = true;
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

int RunRenderTestCapture(Game& game)
{
    nlohmann::json report = { { "scene", request->scene }, { "mode", request->mode },
        { "width", request->width }, { "height", request->height }, { "seed", request->seed },
        { "captures", nlohmann::json::array() }, { "success", false } };
    int exitCode = 0;
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
        for (const auto& point : request->captures)
        {
            game.ConfigureRenderTest(request->scene, point.mode == "raytracing", request->postProcessing,
                request->benchmarkFrames > 0);
            game.GetRenderManager().GetRenderData().cacheTreePoses = point.poseCache;
            game.GetPlayer()->SetPosition(point.overrideCamera
                ? DirectX::XMVectorSet(point.cameraPosition[0], point.cameraPosition[1], point.cameraPosition[2], 1)
                : DirectX::XMLoadFloat3(&originalCamera));
            nlohmann::json samples = nlohmann::json::array();
            const int captureFrame = request->warmupFrames + request->benchmarkFrames;
            for (int frame = 0; frame <= captureFrame; ++frame)
            {
                if (!request->benchmarkFrames || frame == 0 || frame == captureFrame)
                    std::cerr << "Rendering " << request->scene << " / " << point.mode
                        << " at " << point.time << "s, frame " << frame << std::endl;
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) throw std::runtime_error("Render test window was closed");
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                auto start = std::chrono::steady_clock::now();
                const double simulationTime = frame == captureFrame ? point.time
                    : point.time + std::max(0, frame - request->warmupFrames) * request->benchmarkTimeStep;
                HRESULT result = game.RenderTestFrame(simulationTime,
                    frame == captureFrame ? point.filename : std::wstring());
                if (FAILED(result)) throw std::runtime_error("Rendering/capture failed, HRESULT=" + std::to_string(static_cast<unsigned>(result)));
                if (request->benchmarkFrames && frame >= request->warmupFrames && frame < captureFrame)
                {
                    const auto& profile = game.GetRenderManager().GetRenderData().profile;
                    if (!profile.valid) throw std::runtime_error("GPU profiling did not complete");
                    samples.push_back({
                        { "simulationTime", simulationTime },
                        { "frameMs", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() },
                        { "sceneUpdateMs", profile.sceneUpdateMs }, { "drawUploadMs", profile.drawUploadMs },
                        { "gpuMs", profile.gpuMs }, { "drawRecords", profile.drawRecords },
                        { "worldVertices", profile.worldVertices }, { "skinnedVertices", profile.skinnedVertices },
                        { "indexEntries", profile.indexEntries }, { "dispatchGroups", profile.dispatchGroups },
                        { "rebuiltPoseBranches", profile.rebuiltPoseBranches }, { "reusedPoseBranches", profile.reusedPoseBranches },
                        { "poseCacheBytes", profile.poseCacheBytes }, { "drawUploadBytes", profile.drawUploadBytes },
                        { "drawUploadCapacityBytes", profile.drawUploadCapacityBytes },
                        { "worldVertexCapacityBytes", profile.worldVertexCapacityBytes },
                        { "accelerationStructureBytes", profile.accelerationStructureBytes },
                        { "rebuiltBLAS", profile.rebuiltBLAS }
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
            report["captures"].push_back({ { "time", point.time }, { "mode", point.mode }, { "success", true },
                { "poseCache", point.poseCache }, { "samples", samples } });
        }
        report["gpuPasses"] = { "frame", "shadow", "main", "deformation", "blas", "tlas", "rays" };
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

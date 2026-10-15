#include "pch.h"
#include "RenderTestCapture.h"
#include "Game.h"
#include "Player.h"
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
                    report["adapter"] = std::string(profile.adapter.begin(), profile.adapter.end());
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

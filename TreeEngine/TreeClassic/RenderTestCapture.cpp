#include "pch.h"
#include "RenderTestCapture.h"
#include "Game.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cmath>

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
    parsed->postProcessing = json.value("postProcessing", true);
    parsed->report = Wide(json.at("report").get<std::string>());
    if (parsed->scene.empty() || (parsed->mode != "raster" && parsed->mode != "raytracing")
        || parsed->width < 32 || parsed->width > 2048 || parsed->height < 32 || parsed->height > 2048
        || parsed->warmupFrames < 0 || parsed->warmupFrames > 60)
        throw std::runtime_error("Invalid render test settings");
    for (const auto& value : json.at("captures"))
    {
        RenderTestPoint point = { value.at("time").get<double>(), Wide(value.at("filename").get<std::string>()) };
        if (!std::isfinite(point.time) || point.time < 0 || point.filename.empty())
            throw std::runtime_error("Invalid capture time or filename");
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
    try
    {
        for (const auto& point : request->captures)
        {
            for (int frame = 0; frame <= request->warmupFrames; ++frame)
            {
                std::cerr << "Rendering " << request->scene << " / " << request->mode
                    << " at " << point.time << "s, frame " << frame << std::endl;
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) throw std::runtime_error("Render test window was closed");
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                HRESULT result = game.RenderTestFrame(point.time,
                    frame == request->warmupFrames ? point.filename : std::wstring());
                if (FAILED(result)) throw std::runtime_error("Rendering/capture failed, HRESULT=" + std::to_string(static_cast<unsigned>(result)));
            }
            report["captures"].push_back({ { "time", point.time }, { "success", true } });
        }
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

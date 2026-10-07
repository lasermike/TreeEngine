#include "pch.h"
#include "SceneDefinition.h"
#include "RenderData.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    using Json = nlohmann::json;
    int checks = 0;

    std::string DefaultSceneFilename()
    {
        char executable[32768] = {};
        DWORD length = GetModuleFileNameA(nullptr, executable, static_cast<DWORD>(sizeof(executable)));
        if (!length || length >= sizeof(executable))
            throw std::runtime_error("Cannot locate the test executable's data directory");
        std::string filename(executable, length);
        auto slash = filename.find_last_of("/\\");
        if (slash == std::string::npos)
            throw std::runtime_error("Test executable path has no directory");
        return filename.substr(0, slash + 1) + "TestData/ai-tree.scene.json";
    }

    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition) throw std::runtime_error(description);
    }

    void Reject(const std::string& input, const std::string& expectedPath)
    {
        try
        {
            SceneData::ParseScene(input, "test.scene.json");
        }
        catch (const std::runtime_error& error)
        {
            Check(std::string(error.what()).find(expectedPath) != std::string::npos, "error lacks expected property path");
            return;
        }
        throw std::runtime_error("Invalid scene was accepted: " + expectedPath);
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto minimal = Json::parse(R"({"version":1,"name":"Example","objects":[{"id":"box","type":"primitive"}]})");
        auto defaults = SceneData::ParseScene(minimal.dump(), "test.scene.json");
        Check(defaults.objects.size() == 1, "default object missing");
        Check(defaults.objects[0]->transform.scale.x == 1 && defaults.objects[0]->transform.rotation.w == 1, "transform default is not identity");
        Check(defaults.objects[0]->textureCoordScale.x == 1 && defaults.objects[0]->textureCoordScale.y == 1, "object UV default is not identity");
        auto& primitive = static_cast<SceneData::PrimitiveDefinition&>(*defaults.objects[0]);
        Check(primitive.material.textureCoordScale.x == 1 && primitive.material.textureCoordScale.y == 1, "material UV default is not identity");
        Check(defaults.useShadowMaps && defaults.useAlphaBlendedRenderTarget, "render defaults changed");

        auto authored = minimal;
        authored["materials"]["grid"] = { { "texture", "grid.png" }, { "textureCoordScale", { 2, 3 } } };
        authored["objects"][0]["material"] = "grid";
        authored["objects"][0]["textureCoordScale"] = { 4, 5 };
        authored["objects"][0]["transform"] = { { "rotationDegrees", { 0, 0, 90 } }, { "scale", { -30, -30, -30 } } };
        auto named = SceneData::ParseScene(authored.dump(), "test.scene.json");
        auto& ground = static_cast<SceneData::PrimitiveDefinition&>(*named.objects[0]);
        Check(ground.material.texture == "grid.png", "material reference did not resolve");
        Check(ground.material.textureCoordScale.x == 2 && ground.textureCoordScale.y == 5, "authored texture scales lost");
        Check(std::abs(ground.transform.rotation.z - 0.70710678f) < 1e-6f, "degree rotation was not converted");
        Check(ground.transform.scale.x == -30, "inverted skybox transform was rejected");

        auto inlineMaterial = minimal;
        inlineMaterial["objects"][0]["material"] = { { "diffuse", { 1, 0, 0, 1 } } };
        auto inlined = SceneData::ParseScene(inlineMaterial.dump(), "test.scene.json");
        Check(static_cast<SceneData::PrimitiveDefinition&>(*inlined.objects[0]).material.diffuse.x == 1, "inline material lost");

        auto bad = minimal;
        bad["objects"][0]["material"] = "missing";
        Reject(bad.dump(), "objects[0].material");
        bad = minimal; bad["objects"].push_back(bad["objects"][0]);
        Reject(bad.dump(), "objects[1].id");
        bad = minimal; bad["objects"][0]["textureCoordScael"] = { 2, 2 };
        Reject(bad.dump(), "textureCoordScael");
        bad = minimal; bad["objects"][0]["transform"]["position"] = { 0, 1 };
        Reject(bad.dump(), "transform.position");
        bad = minimal; bad["objects"][0]["transform"]["scale"] = { 1, 0, 1 };
        Reject(bad.dump(), "transform.scale");
        bad = minimal; bad["objects"][0]["transform"]["rotation"] = { 0, 0, 0, 0 };
        Reject(bad.dump(), "transform.rotation");
        bad = minimal; bad["objects"][0]["type"] = "typo";
        Reject(bad.dump(), "objects[0].type");
        bad = minimal; bad["objects"][0]["shape"] = "unknown";
        Reject(bad.dump(), "objects[0].shape");
        bad = minimal; bad["objects"][0]["material"] = { { "alphaClipThreshold", 2 } };
        Reject(bad.dump(), "alphaClipThreshold");
        bad = minimal; bad["objects"][0]["animationSpeed"] = -1;
        Reject(bad.dump(), "animationSpeed");
        bad = minimal; bad["version"] = 2;
        Reject(bad.dump(), "version");
        bad = minimal; bad["render"]["useShadowMaps"] = "true";
        Reject(bad.dump(), "useShadowMaps");
        bad = minimal; bad["objects"][0]["cubeMap"] = true;
        bad["objects"][0]["material"] = { { "texture", "sky.png" } };
        Reject(bad.dump(), "material.texture");
        Reject("{broken", "test.scene.json");

        auto treeJson = minimal;
        treeJson["objects"][0] = Json::parse(R"({"id":"tree","type":"tree","generator":{"axiom":"F","rules":[{"input":"F","output":"F[+F]","iterations":2}],"segmentLengthBehavior":"randomAddition"}})");
        auto treeScene = SceneData::ParseScene(treeJson.dump(), "test.scene.json");
        auto& tree = static_cast<SceneData::TreeDefinition&>(*treeScene.objects[0]);
        Check(tree.generator.rules[0].iterations == 2, "per-rule iterations lost");
        Check(tree.generator.segmentLengthBehavior == SceneData::SegmentLengthBehavior::RandomAddition, "named segment behavior lost");
        bad = treeJson; bad["objects"][0]["generator"]["iterations"] = 1.5;
        Reject(bad.dump(), "generator.iterations");
        bad = treeJson; bad["objects"][0]["generator"]["segmentLengthBehavior"] = "callback";
        Reject(bad.dump(), "segmentLengthBehavior");
        bad = treeJson; bad["objects"][0]["generator"]["segmentLength"] = 0;
        Reject(bad.dump(), "segmentLength");

        const std::string filename = argc > 1 ? argv[1] : DefaultSceneFilename();
        auto ai = SceneData::ReadScene(filename);
        Check(ai.name == "AI Tree" && ai.objects.size() == 6, "AI tree scene topology changed");
        auto& aiTree = static_cast<SceneData::TreeDefinition&>(*ai.objects[0]);
        Check(aiTree.generator.iterations == 14 && aiTree.generator.rules.size() == 2, "AI tree generator changed");
        Check(aiTree.generator.axiom == "F(50) T(100,.1)", "AI tree axiom changed");
        Check(aiTree.trunk.texture == "Bark_0005_diffuse.dds" && aiTree.leaf.alphaClipThreshold == 1, "tree material settings changed");
        auto& aiGround = static_cast<SceneData::PrimitiveDefinition&>(*ai.objects[1]);
        Check(aiGround.material.texture == "grid3.png" && aiGround.textureCoordScale.x == 4 && aiGround.textureCoordScale.y == 4, "ground texture or tiling changed");
        auto& sky = static_cast<SceneData::PrimitiveDefinition&>(*ai.objects[2]);
        Check(sky.cubeMap && sky.material.texture == "coords.dds", "skybox changed");
        Check(ai.camera.rotation.w == 0 && ai.camera.position.z == 2.05959034f, "legacy camera changed");
        bool missingFileRejected = false;
        try { SceneData::ReadScene("does-not-exist.scene.json"); }
        catch (const std::runtime_error& e) { missingFileRejected = std::string(e.what()).find("cannot open") != std::string::npos; }
        Check(missingFileRejected, "missing file did not report an error");

        // Applying a staged scene must not replace live renderer resources or time.
        RenderData live;
        int resourceSentinel = 0;
        live.instanceData = reinterpret_cast<InstancedData*>(&resourceSentinel);
        live.instanceBuffer = reinterpret_cast<D3DBuffer*>(&resourceSentinel);
        live.pShadowMap = reinterpret_cast<ShadowMap*>(&resourceSentinel);
        live.pDxrOutBuffer = reinterpret_cast<UavBuffer*>(&resourceSentinel);
        live.frameStats = reinterpret_cast<FrameStatistic*>(&resourceSentinel);
        live.inputManager = reinterpret_cast<IInputManager*>(&resourceSentinel);
        live.time = 12;
        live.frame = 99;
        live.projectionData.screenWidth = 1920;
        live.view._41 = 7;
        live.mSceneBounds.Radius = 5;
        live.shadowTransform._42 = 3;

        SceneRenderSettings nextSettings;
        nextSettings.clearColor = DirectX::XMFLOAT4(0.2f, 0.3f, 0.4f, 1);
        nextSettings.numDirectionalLights = 0;
        nextSettings.numPointLights = 1;
        nextSettings.dirLights[0].Diffuse = DirectX::XMFLOAT4(0.7f, 0.8f, 0.9f, 1);
        nextSettings.pointLights[0].Position = DirectX::XMFLOAT3(1, 2, 3);
        live.ApplySceneSettings(nextSettings);
        Check(live.clearColor.x == 0.2f && live.clearColor.y == 0.3f, "scene background was not applied");
        Check(live.numDirectionalLights == 0 && live.numPointLights == 1, "scene light counts were not applied");
        Check(live.dirLights[0].Diffuse.z == 0.9f && live.pointLights[0].Position.z == 3, "scene lights were not applied");
        Check(live.time == 12 && live.frame == 99, "applying scene settings reset frame state");
        Check(live.projectionData.screenWidth == 1920 && live.view._41 == 7, "applying scene settings replaced camera state");
        Check(live.mSceneBounds.Radius == 5 && live.shadowTransform._42 == 3, "applying scene settings replaced computed shadow state");
        Check(live.instanceData == reinterpret_cast<InstancedData*>(&resourceSentinel)
            && live.instanceBuffer == reinterpret_cast<D3DBuffer*>(&resourceSentinel), "instance resources were replaced");
        Check(live.pShadowMap == reinterpret_cast<ShadowMap*>(&resourceSentinel)
            && live.pDxrOutBuffer == reinterpret_cast<UavBuffer*>(&resourceSentinel), "shadow/raytracing resources were replaced");
        Check(live.frameStats == reinterpret_cast<FrameStatistic*>(&resourceSentinel)
            && live.inputManager == reinterpret_cast<IInputManager*>(&resourceSentinel), "renderer services were replaced");

        std::cout << checks << " scene definition checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

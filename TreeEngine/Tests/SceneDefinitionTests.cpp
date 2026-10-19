#include "pch.h"
#include "GltfLoader.h"
#include "SceneDefinition.h"
#include "RenderData.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using Json = nlohmann::json;
    int checks = 0;

    std::string TestDataFilename(const std::string& basename)
    {
        char executable[32768] = {};
        DWORD length = GetModuleFileNameA(nullptr, executable, static_cast<DWORD>(sizeof(executable)));
        if (!length || length >= sizeof(executable))
            throw std::runtime_error("Cannot locate the test executable's data directory");
        std::string filename(executable, length);
        auto slash = filename.find_last_of("/\\");
        if (slash == std::string::npos)
            throw std::runtime_error("Test executable path has no directory");
        return filename.substr(0, slash + 1) + "TestData/" + basename;
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

void TestSpecularExtension()
{
    std::ifstream input(TestDataFilename("static-scene.gltf"));
    Json fixture;
    input >> fixture;
    const std::string path = TestDataFilename("specular-generated-" + std::to_string(GetCurrentProcessId()) + ".gltf");
    struct Cleanup { std::string path; ~Cleanup() { DeleteFileA(path.c_str()); } } cleanup{ path };
    auto load = [&](const Json& source) {
        std::ofstream out(path, std::ios::binary);
        out << source.dump();
        out.close();
        Check(!out.fail(), "could not write specular test asset");
        return LoadGltfAsset(path);
    };
    auto reject = [&](const Json& source, const char* expected) {
        try { load(source); }
        catch (const std::runtime_error& e) {
            Check(std::string(e.what()).find(expected) != std::string::npos, "specular rejection lacks context");
            return;
        }
        throw std::runtime_error("Invalid specular material was accepted");
    };
    auto legacy = load(fixture);
    Check(legacy->materials[0].shaderMaterial.specularParams.x == 0, "legacy material enabled specular extension");
    fixture["extensionsUsed"] = { "KHR_materials_specular" };
    fixture["extensionsRequired"] = { "KHR_materials_specular" };
    auto& material = fixture["materials"][0];
    material["extensions"] = { { "KHR_materials_specular", Json::object() } };
    auto defaults = load(fixture);
    const auto& defaultMat = defaults->materials[0];
    Check(defaultMat.shaderMaterial.specularParams.x == 1 && defaultMat.shaderMaterial.specularParams.w == 1,
        "specular default strength or required extension import failed");
    Check(defaultMat.shaderMaterial.specularColor.x == 1 && defaultMat.specularTextureIndex == -1,
        "specular default color or missing texture changed");
    auto& spec = material["extensions"]["KHR_materials_specular"];
    spec["specularFactor"] = 0;
    spec["specularColorFactor"] = { 2.0, 0.5, 0.25 };
    auto authored = load(fixture);
    Check(authored->materials[0].shaderMaterial.specularParams.w == 0, "zero specular strength was lost");
    Check(authored->materials[0].shaderMaterial.specularColor.x == 2, "specular color above one was clamped at import");
    material["pbrMetallicRoughness"]["metallicFactor"] = 1.0;
    material["pbrMetallicRoughness"]["roughnessFactor"] = 0.3;
    auto metal = load(fixture);
    Check(metal->materials[0].shaderMaterial.specularParams.y == 1 &&
        std::abs(metal->materials[0].shaderMaterial.specularParams.z - 0.3f) < 1e-6f, "PBR factors were lost");
    material["normalTexture"] = { { "index", 0 }, { "scale", 0.35 } };
    auto normalMapped = load(fixture);
    Check(normalMapped->materials[0].normalTextureIndex >= 0, "normal texture was not imported");
    Check(std::abs(normalMapped->materials[0].shaderMaterial.normalTextures.y - 0.35f) < 1e-6f, "normal texture scale was lost");
    Check(normalMapped->textures[normalMapped->materials[0].normalTextureIndex].colorSpace == 1, "normal texture is not linear data");
    spec["specularTexture"] = { { "index", 0 } };
    spec["specularColorTexture"] = { { "index", 0 } };
    auto textured = load(fixture);
    const auto& tm = textured->materials[0];
    Check(tm.specularTextureIndex >= 0 && tm.specularColorTextureIndex >= 0, "specular maps were not imported");
    Check(tm.specularTextureIndex != tm.specularColorTextureIndex, "linear and sRGB texture roles were conflated");
    Check(tm.textureIndex == tm.specularColorTextureIndex, "same sRGB texture was not deduplicated");
    Check(textured->textures[tm.specularTextureIndex].colorSpace == 1 &&
        textured->textures[tm.specularColorTextureIndex].colorSpace == 2, "specular texture color spaces are incorrect");
    Check(textured->textures[tm.specularTextureIndex].IsMemoryBacked(), "external specular image bytes missing");
    fixture["samplers"] = Json::array({ { { "wrapS", 33071 }, { "wrapT", 33071 } } });
    fixture["textures"][0]["sampler"] = 0;
    auto clamped = load(fixture);
    Check(clamped->materials[0].shaderMaterial.specularTextures.z == 1 &&
        clamped->materials[0].shaderMaterial.specularTextures.w == 1, "specular clamp modes lost");
    auto bad = fixture;
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularFactor"] = -0.1;
    reject(bad, "specularFactor");
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularFactor"] = 1.1;
    reject(bad, "specularFactor");
    bad = fixture;
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularColorFactor"] = { 1, -1, 1 };
    reject(bad, "specularColorFactor");
    bad = fixture;
    bad["materials"][0]["extensions"]["KHR_materials_unlit"] = Json::object();
    reject(bad, "KHR_materials_unlit");
    bad = fixture;
    bad["materials"][0]["normalTexture"] = { { "index", 0 }, { "scale", -0.1 } };
    reject(bad, "normalTexture.scale");
    bad = fixture;
    bad["materials"][0]["normalTexture"] = { { "index", 0 }, { "texCoord", 1 } };
    reject(bad, "TEXCOORD_1");
    bad = fixture;
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularTexture"]["texCoord"] = 1;
    reject(bad, "TEXCOORD_1");
    bad = fixture;
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularColorTexture"]["extensions"]["KHR_texture_transform"] = Json::object();
    reject(bad, "KHR_texture_transform");
    bad = fixture;
    bad["extensionsRequired"].push_back("UNKNOWN_test_extension");
    reject(bad, "UNKNOWN_test_extension");
}

int main(int argc, char** argv)
{
    try
    {
        TestSpecularExtension();
        if (argc > 1 && std::string(argv[1]) == "--specular-only")
        {
            std::cout << checks << " specular extension checks passed\n";
            return 0;
        }
        const auto minimal = Json::parse(R"({"version":1,"name":"Example","objects":[{"id":"box","type":"primitive"}]})");
        auto defaults = SceneData::ParseScene(minimal.dump(), "test.scene.json");
        Check(defaults.objects.size() == 1, "default object missing");
        Check(defaults.objects[0]->transform.scale.x == 1 && defaults.objects[0]->transform.rotation.w == 1, "transform default is not identity");
        Check(defaults.objects[0]->textureCoordScale.x == 1 && defaults.objects[0]->textureCoordScale.y == 1, "object UV default is not identity");
        auto& primitive = static_cast<SceneData::PrimitiveDefinition&>(*defaults.objects[0]);
        Check(!primitive.hasMaterial, "omitted primitive material was marked as authored");
        Check(primitive.material.textureCoordScale.x == 1 && primitive.material.textureCoordScale.y == 1, "material UV default is not identity");
        Check(defaults.useShadowMaps && defaults.useAlphaBlendedRenderTarget, "render defaults changed");

        auto authored = minimal;
        authored["materials"]["grid"] = { { "texture", "grid.png" }, { "textureCoordScale", { 2, 3 } } };
        authored["objects"][0]["material"] = "grid";
        authored["objects"][0]["textureCoordScale"] = { 4, 5 };
        authored["objects"][0]["transform"] = { { "rotationDegrees", { 0, 0, 90 } }, { "scale", { -30, -30, -30 } } };
        auto named = SceneData::ParseScene(authored.dump(), "test.scene.json");
        auto& ground = static_cast<SceneData::PrimitiveDefinition&>(*named.objects[0]);
        Check(ground.hasMaterial, "authored primitive material was marked as omitted");
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
        Check(!tree.hasMaterials, "omitted tree materials were marked as authored");
        Check(tree.generator.rules[0].iterations == 2, "per-rule iterations lost");
        Check(tree.generator.segmentLengthBehavior == SceneData::SegmentLengthBehavior::RandomAddition, "named segment behavior lost");
        bad = treeJson; bad["objects"][0]["generator"]["iterations"] = 1.5;
        Reject(bad.dump(), "generator.iterations");
        bad = treeJson; bad["objects"][0]["generator"]["segmentLengthBehavior"] = "callback";
        Reject(bad.dump(), "segmentLengthBehavior");
        bad = treeJson; bad["objects"][0]["generator"]["segmentLength"] = 0;
        Reject(bad.dump(), "segmentLength");

        auto fixedTreeJson = minimal;
        fixedTreeJson["objects"][0] = Json::parse(R"({"id":"fixed","type":"fixedTree","trunkShape":"cylinder"})");
        auto fixedTreeScene = SceneData::ParseScene(fixedTreeJson.dump(), "test.scene.json");
        Check(fixedTreeScene.objects[0]->kind == SceneData::ObjectKind::FixedTree, "fixed tree object kind changed");
        Check(static_cast<SceneData::FixedTreeDefinition&>(*fixedTreeScene.objects[0]).trunkShape == SceneData::Shape::Cylinder,
            "fixed tree trunk shape changed");
        bad = fixedTreeJson; bad["objects"][0]["trunkShape"] = "box";
        Reject(bad.dump(), "trunkShape");

        auto graphJson = minimal;
        graphJson["objects"][0] = Json::parse(R"({"id":"graph","type":"graph","dataFile":"graphdata.txt"})");
        auto graphScene = SceneData::ParseScene(graphJson.dump(), "test.scene.json");
        Check(graphScene.objects[0]->kind == SceneData::ObjectKind::Graph, "graph object kind changed");
        Check(static_cast<SceneData::GraphDefinition&>(*graphScene.objects[0]).dataFile == "graphdata.txt", "graph data file changed");
        bad = graphJson; bad["objects"][0].erase("dataFile");
        Reject(bad.dump(), "dataFile");
        bad = graphJson; bad["objects"][0]["dataFile"] = "";
        Reject(bad.dump(), "dataFile");

        auto modelJson = minimal;
        modelJson["objects"][0] = Json::parse(R"({"id":"environment","type":"model","source":"Models/environment.glb","forceLit":true,"transform":{"position":[1,2,3]}})");
        auto modelScene = SceneData::ParseScene(modelJson.dump(), "test.scene.json");
        Check(modelScene.objects[0]->kind == SceneData::ObjectKind::Model, "model object kind changed");
        auto& model = static_cast<SceneData::ModelDefinition&>(*modelScene.objects[0]);
        Check(model.source == "Models/environment.glb", "model source changed");
        Check(model.forceLit, "model forceLit override changed");
        Check(model.transform.position.x == 1 && model.transform.position.z == 3, "model transform changed");
        bad = modelJson; bad["objects"][0].erase("source");
        Reject(bad.dump(), "source");
        bad = modelJson; bad["objects"][0]["source"] = "environment.obj";
        Reject(bad.dump(), "source");
        bad = modelJson; bad["objects"][0]["forceLit"] = "yes";
        Reject(bad.dump(), "forceLit");
        bad = modelJson; bad["objects"][0]["textureCoordScale"] = { 2, 2 };
        Reject(bad.dump(), "textureCoordScale");

        auto externalGltf = LoadGltfAsset(TestDataFilename("static-scene.gltf"));
        Check(externalGltf->renderParts.size() == 2, "glTF hierarchy instances were not imported");
        Check(externalGltf->primitives.size() == 2, "mirrored glTF node did not receive a winding variant");
        Check(externalGltf->materials.size() == 2 && externalGltf->textures.size() == 1, "glTF materials or textures were not imported");
        Check(!externalGltf->textures[0].IsMemoryBacked()
            && externalGltf->textures[0].filename.find(L"checker.png") != std::wstring::npos,
            "external glTF image did not resolve relative to the asset");
        Check(externalGltf->primitives[0].vertices.size() == 6
            && externalGltf->primitives[0].indices.size() == 6,
            "double-sided glTF primitive was not expanded");
        const auto& generatedVertex = externalGltf->primitives[0].vertices[0];
        Check(std::abs(generatedVertex.Normal.z) > 0.9f, "missing glTF normals were not generated");
        Check(std::abs(generatedVertex.TangentU.x) > 0.1f || std::abs(generatedVertex.TangentU.y) > 0.1f,
            "missing glTF tangents were not generated");
        const auto& forwardPart = externalGltf->renderParts[0];
        Check(std::abs(forwardPart.transform._41 - 1.0f) < 1e-6f
            && std::abs(forwardPart.transform._43 + 1.0f) < 1e-6f,
            "glTF node transform was not converted to left-handed coordinates");
        Check(std::abs(externalGltf->boundsMin.x) < 1e-6f
            && std::abs(externalGltf->boundsMin.y) < 1e-6f
            && std::abs(externalGltf->boundsMin.z + 1.0f) < 1e-6f
            && std::abs(externalGltf->boundsMax.x - 2.0f) < 1e-6f
            && std::abs(externalGltf->boundsMax.y - 3.0f) < 1e-6f
            && std::abs(externalGltf->boundsMax.z) < 1e-6f,
            "glTF hierarchy bounds changed");

        GeometryBufferData importedGeometry;
        externalGltf->AppendGeometry(importedGeometry);
        size_t appendedVertices = importedGeometry.vertices.size();
        size_t appendedIndices = importedGeometry.indices.size();
        Check(appendedVertices == 12 && appendedIndices == 12, "glTF geometry was not appended to the shared buffers");
        externalGltf->AppendGeometry(importedGeometry);
        Check(importedGeometry.vertices.size() == appendedVertices && importedGeometry.indices.size() == appendedIndices,
            "shared glTF geometry was appended more than once");
        importedGeometry.Release();
        externalGltf->AppendGeometry(importedGeometry);
        Check(importedGeometry.vertices.size() == appendedVertices, "glTF geometry was not rebuilt after buffer release");

        auto embeddedGlb = LoadGltfAsset(TestDataFilename("static-scene.glb"));
        Check(embeddedGlb->textures.size() == 1 && embeddedGlb->textures[0].IsMemoryBacked(),
            "GLB embedded image was not retained in memory");
        Check(embeddedGlb->textures[0].mimeType == "image/png" && embeddedGlb->textures[0].encodedData.size() > 16,
            "GLB embedded image metadata changed");

        auto blendedGltf = LoadGltfAsset(TestDataFilename("unsupported-blend.gltf"));
        Check(blendedGltf->materials[0].alphaBlend, "glTF alpha blending was not retained");
        Check(blendedGltf->materials[0].shaderMaterial.flags.x == 1.0f,
            "glTF alpha blending did not preserve source alpha");
        Check(std::abs(blendedGltf->materials[0].shaderMaterial.flags.z - (1.0f / 255.0f)) < 1e-6f,
            "glTF alpha blending did not retain a DXR visibility cutoff");
        auto unlitBlendGltf = LoadGltfAsset(TestDataFilename("unlit-blend.gltf"));
        Check(unlitBlendGltf->materials[0].alphaBlend
            && unlitBlendGltf->materials[0].shaderMaterial.flags.w == 1.0f,
            "KHR_materials_unlit alpha blending was not imported");
        bool linesRejected = false;
        try { LoadGltfAsset(TestDataFilename("unsupported-lines.gltf")); }
        catch (const std::runtime_error& error) { linesRejected = std::string(error.what()).find("not TRIANGLES") != std::string::npos; }
        Check(linesRejected, "unsupported glTF primitive mode was accepted");

        const std::string filename = argc > 1 ? argv[1] : TestDataFilename("ai-tree.scene.json");
        auto ai = SceneData::ReadScene(filename);
        Check(ai.name == "AI Tree" && ai.objects.size() == 6, "AI tree scene topology changed");
        auto& aiTree = static_cast<SceneData::TreeDefinition&>(*ai.objects[0]);
        Check(aiTree.generator.iterations == 11 && aiTree.generator.rules.size() == 4, "AI tree generator changed");
        Check(aiTree.generator.axiom == "F(35) A(100,0)", "AI tree axiom changed");
        Check(aiTree.trunk.texture == "Bark_0005_diffuse.dds" && aiTree.leaf.texture == "FirBranchTip.dds" &&
            aiTree.leaf.alphaClipThreshold == 0.6f, "tree material settings changed");
        auto& sky = static_cast<SceneData::PrimitiveDefinition&>(*ai.objects[1]);
        Check(sky.cubeMap && sky.material.texture == "cube.dds", "skybox changed");
        auto& aiModel = static_cast<SceneData::ModelDefinition&>(*ai.objects[5]);
        Check(aiModel.source == "Models/Gltf/pond_with_waterfalls.glb" && aiModel.forceLit,
            "AI tree imported model changed");
        Check(ai.camera.rotation.w == 0 && ai.camera.position.z == 2.05959034f, "legacy camera changed");

        const std::pair<const char*, const char*> catalogScenes[] = {
            { "tree.scene.json", "Tree" },
            { "test-tree.scene.json", "Test Tree" },
            { "sea.scene.json", "Sea" },
            { "curves.scene.json", "Curves" },
            { "trees.scene.json", "Trees" },
            { "test-block.scene.json", "Test Block" },
            { "graph.scene.json", "Graph" },
            { "simple-box.scene.json", "Simple Box" },
            { "ai-tree.scene.json", "AI Tree" },
            { "gltf-models.scene.json", "glTF Models" }
        };
        for (const auto& catalogScene : catalogScenes)
        {
            auto scene = SceneData::ReadScene(TestDataFilename(catalogScene.first));
            Check(scene.name == catalogScene.second, "catalog scene name changed");
        }

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

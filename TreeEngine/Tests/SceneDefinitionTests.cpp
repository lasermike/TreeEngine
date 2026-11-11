#include "pch.h"
#include "GltfLoader.h"
#include "SceneDefinition.h"
#include "RenderData.h"
#include "OpacityMicromapBaker.h"
#include "UpscalerMath.h"
#include "DebugVectorMath.h"
#include "Lod.h"
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
    Check(legacy->materials[0].shaderMaterial.specularParams.x == 1, "standard metallic-roughness material did not enable PBR shading");
    Check(legacy->materials[0].shaderMaterial.specularParams.y == 0 &&
        std::abs(legacy->materials[0].shaderMaterial.specularParams.z - 0.8f) < 1e-6f, "standard metallic-roughness factors were not imported");
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
    Check(defaultMat.shaderMaterial.uvTransformRow0[BaseColorTextureRole].x == 1.0f
        && defaultMat.shaderMaterial.uvTransformRow1[NormalTextureRole].y == 1.0f,
        "default texture transforms are not identity");
    auto iorFixture = fixture;
    iorFixture["extensionsUsed"].push_back("KHR_materials_ior");
    iorFixture["extensionsRequired"].push_back("KHR_materials_ior");
    iorFixture["materials"][0]["extensions"]["KHR_materials_ior"] = { { "ior", 1.3333 } };
    auto water = load(iorFixture);
    Check(std::abs(water->materials[0].shaderMaterial.opticalParams.x - 1.3333f) < 1e-4f, "IOR was not imported");
    iorFixture["materials"][0]["extensions"]["KHR_materials_ior"] = Json::object();
    auto defaultIor = load(iorFixture);
    Check(defaultIor->materials[0].shaderMaterial.opticalParams.x == 1.5f, "default IOR changed");
    iorFixture["materials"][0]["extensions"]["KHR_materials_ior"]["ior"] = 0.0;
    Check(load(iorFixture)->materials[0].shaderMaterial.opticalParams.x == 0.0f, "zero IOR compatibility mode was lost");
    iorFixture["materials"][0]["extensions"]["KHR_materials_ior"]["ior"] = 0.5;
    reject(iorFixture, "KHR_materials_ior.ior");
    iorFixture["materials"][0]["extensions"]["KHR_materials_ior"]["ior"] = 1.3333;
    iorFixture["extensionsUsed"].push_back("KHR_materials_transmission");
    iorFixture["extensionsRequired"].push_back("KHR_materials_transmission");
    iorFixture["materials"][0]["extensions"]["KHR_materials_transmission"] = { { "transmissionFactor", 1.0 } };
    auto transmissive = load(iorFixture);
    Check(transmissive->materials[0].alphaBlend && transmissive->materials[0].shaderMaterial.opticalParams.y == 1.0f,
        "transmission factor did not enable the raster fallback");
    iorFixture["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] = 1.1;
    reject(iorFixture, "transmissionFactor");
    auto& spec = material["extensions"]["KHR_materials_specular"];
    spec["specularFactor"] = 0;
    spec["specularColorFactor"] = { 2.0, 0.5, 0.25 };
    auto authored = load(fixture);
    Check(authored->materials[0].shaderMaterial.specularParams.w == 0, "zero specular strength was lost");
    Check(authored->materials[0].shaderMaterial.specularColor.x == 2, "specular color above one was clamped at import");
    material["pbrMetallicRoughness"]["metallicFactor"] = 1.0;
    material["pbrMetallicRoughness"]["roughnessFactor"] = 0.3;
    Json bad;
    auto metal = load(fixture);
    Check(metal->materials[0].shaderMaterial.specularParams.y == 1 &&
        std::abs(metal->materials[0].shaderMaterial.specularParams.z - 0.3f) < 1e-6f, "PBR factors were lost");
    bad = fixture;
    bad["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = 1.1;
    reject(bad, "metallicFactor");
    bad = fixture;
    bad["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = -0.1;
    reject(bad, "roughnessFactor");
    material["normalTexture"] = { { "index", 0 }, { "scale", 0.35 } };
    auto normalMapped = load(fixture);
    Check(normalMapped->materials[0].normalTextureIndex >= 0, "normal texture was not imported");
    Check(std::abs(normalMapped->materials[0].shaderMaterial.normalTextures.y - 0.35f) < 1e-6f, "normal texture scale was lost");
    Check(normalMapped->textures[normalMapped->materials[0].normalTextureIndex].colorSpace == 1, "normal texture is not linear data");
    material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = { { "index", 0 } };
    auto mrTextured = load(fixture);
    const auto& mrMaterial = mrTextured->materials[0];
    Check(mrMaterial.metallicRoughnessTextureIndex >= 0, "metallic-roughness texture was not imported");
    Check(mrTextured->textures[mrMaterial.metallicRoughnessTextureIndex].colorSpace == 1, "metallic-roughness texture is not linear data");
    Check(mrMaterial.shaderMaterial.metallicRoughnessTextures.y == 0, "default metallic-roughness wrap mode changed");
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
    Check(clamped->materials[0].shaderMaterial.metallicRoughnessTextures.y == 1, "metallic-roughness clamp mode lost");
    auto transformedFixture = fixture;
    transformedFixture["extensionsUsed"].push_back("KHR_texture_transform");
    transformedFixture["extensionsRequired"].push_back("KHR_texture_transform");
    auto& transformedMaterial = transformedFixture["materials"][0];
    transformedMaterial["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] =
        { { "offset", { 0.25, 0.5 } }, { "rotation", 1.5707963267948966 }, { "scale", { 2.0, 3.0 } } };
    transformedMaterial["pbrMetallicRoughness"]["metallicRoughnessTexture"]["extensions"]["KHR_texture_transform"] =
        { { "scale", { 4.0, 5.0 } } };
    transformedMaterial["normalTexture"]["extensions"]["KHR_texture_transform"] =
        { { "offset", { 0.0, -9.0 } }, { "scale", { 10.0, 10.0 } } };
    transformedMaterial["extensions"]["KHR_materials_specular"]["specularTexture"]["extensions"]["KHR_texture_transform"] =
        { { "offset", { 0.1, 0.2 } } };
    transformedMaterial["extensions"]["KHR_materials_specular"]["specularColorTexture"]["texCoord"] = 1;
    transformedMaterial["extensions"]["KHR_materials_specular"]["specularColorTexture"]["extensions"]["KHR_texture_transform"] =
        { { "scale", { -1.0, 1.0 } }, { "offset", { 1.0, 0.0 } }, { "texCoord", 0 } };
    auto transformed = load(transformedFixture);
    const auto& uv = transformed->materials[0].shaderMaterial;
    Check(std::abs(uv.uvTransformRow0[BaseColorTextureRole].x) < 1e-5f
        && std::abs(uv.uvTransformRow0[BaseColorTextureRole].y + 3.0f) < 1e-5f
        && std::abs(uv.uvTransformRow0[BaseColorTextureRole].z - 0.25f) < 1e-5f
        && std::abs(uv.uvTransformRow1[BaseColorTextureRole].x - 2.0f) < 1e-5f
        && std::abs(uv.uvTransformRow1[BaseColorTextureRole].z - 0.5f) < 1e-5f,
        "base-color UV rotation, scale, or offset was imported incorrectly");
    Check(uv.uvTransformRow0[MetallicRoughnessTextureRole].x == 4.0f
        && uv.uvTransformRow1[MetallicRoughnessTextureRole].y == 5.0f
        && uv.uvTransformRow0[NormalTextureRole].x == 10.0f
        && uv.uvTransformRow1[NormalTextureRole].z == -9.0f,
        "independent metallic-roughness or normal UV transform was lost");
    Check(std::abs(uv.uvTransformRow0[SpecularStrengthTextureRole].z - 0.1f) < 1e-6f
        && uv.uvTransformRow0[SpecularColorTextureRole].x == -1.0f
        && uv.uvTransformRow0[SpecularColorTextureRole].z == 1.0f,
        "specular texture transform or texCoord override was lost");
    ShaderMaterial legacyScale;
    legacyScale.ApplyTextureCoordScale(2.0f, 3.0f);
    Check(legacyScale.uvTransformRow0[BaseColorTextureRole].x == 2.0f
        && legacyScale.uvTransformRow1[BaseColorTextureRole].y == 3.0f
        && legacyScale.uvTransformRow0[NormalTextureRole].x == 2.0f,
        "legacy UV scale was not folded into every texture row");
    ShaderMaterial composed = uv;
    composed.ApplyTextureCoordScale(2.0f, 3.0f);
    Check(std::abs(composed.uvTransformRow0[BaseColorTextureRole].y + 9.0f) < 1e-5f
        && std::abs(composed.uvTransformRow1[BaseColorTextureRole].x - 4.0f) < 1e-5f
        && std::abs(composed.uvTransformRow0[BaseColorTextureRole].z - 0.25f) < 1e-5f
        && composed.uvTransformRow0[NormalTextureRole].x == 20.0f,
        "shared scale was not composed before the authored texture transform");
    bad = fixture;
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
    bad = transformedFixture;
    bad["materials"][0]["extensions"]["KHR_materials_specular"]["specularColorTexture"]["extensions"]["KHR_texture_transform"]["texCoord"] = 1;
    reject(bad, "TEXCOORD_1");
    bad = fixture;
    bad["extensionsRequired"].push_back("UNKNOWN_test_extension");
    reject(bad, "UNKNOWN_test_extension");
}

void TestPbrSceneMaterials()
{
    // The shared factor mapping must reproduce what glTF import produces.
    std::ifstream input(TestDataFilename("static-scene.gltf"));
    Json fixture;
    input >> fixture;
    const auto& source = fixture["materials"][0];
    PbrMaterialFactors factors;
    const auto& pbrJson = source["pbrMetallicRoughness"];
    if (pbrJson.contains("baseColorFactor"))
    {
        const auto& color = pbrJson["baseColorFactor"];
        factors.baseColor = { color[0].get<float>(), color[1].get<float>(), color[2].get<float>(), color[3].get<float>() };
    }
    if (pbrJson.contains("metallicFactor")) factors.metallic = pbrJson["metallicFactor"].get<float>();
    if (pbrJson.contains("roughnessFactor")) factors.roughness = pbrJson["roughnessFactor"].get<float>();
    const std::string mode = source.value("alphaMode", "OPAQUE");
    factors.alphaMode = mode == "MASK" ? PbrAlphaMode::Mask : mode == "BLEND" ? PbrAlphaMode::Blend : PbrAlphaMode::Opaque;
    factors.alphaCutoff = source.value("alphaCutoff", 0.5f);
    bool alphaBlend = false;
    const auto built = BuildPbrShaderMaterial(factors, alphaBlend);
    const auto imported = LoadGltfAsset(TestDataFilename("static-scene.gltf"))->materials[0];
    auto same = [](const XMFLOAT4& a, const XMFLOAT4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; };
    Check(same(built.Ambient, imported.shaderMaterial.Ambient) && same(built.Diffuse, imported.shaderMaterial.Diffuse)
        && same(built.specularParams, imported.shaderMaterial.specularParams)
        && same(built.opticalParams, imported.shaderMaterial.opticalParams)
        && built.flags.x == imported.shaderMaterial.flags.x && built.flags.z == imported.shaderMaterial.flags.z
        && built.flags.w == imported.shaderMaterial.flags.w && alphaBlend == imported.alphaBlend,
        "shared PBR mapping differs from glTF import");
    PbrMaterialFactors glass;
    glass.transmission = 0.5f;
    glass.ior = 1.33f;
    const auto glassMaterial = BuildPbrShaderMaterial(glass, alphaBlend);
    Check(alphaBlend && glassMaterial.flags.x == 1 && glassMaterial.flags.z == 0 && glassMaterial.opticalParams.y == 0.5f
        && glassMaterial.opticalParams.x == 1.33f, "transmission did not force the raster blend fallback");

    // Material maps load as memory-backed PNG/JPEG with role-specific color spaces.
    const auto checkerAsset = LoadGltfAsset(TestDataFilename("static-scene.gltf"));
    const std::wstring checker = checkerAsset->textures.at(0).filename;
    const auto linear = LoadMaterialTextureFile(checker, 1);
    const auto srgb = LoadMaterialTextureFile(checker, 2);
    Check(linear.IsMemoryBacked() && linear.colorSpace == 1 && srgb.colorSpace == 2 && linear.key != srgb.key,
        "scene material maps were not loaded by color space");
    bool bitmapRejected = false;
    try { LoadMaterialTextureFile(L"bark.bmp", 2); }
    catch (const std::runtime_error& e) { bitmapRejected = std::string(e.what()).find("PNG, JPEG, or DDS") != std::string::npos; }
    Check(bitmapRejected, "unsupported material map format was accepted");
    bool missingDdsReported = false;
    try { LoadMaterialTextureFile(L"missing-bark.dds", 2); }
    catch (const std::runtime_error& e) { missingDdsReported = std::string(e.what()).find("was not found") != std::string::npos; }
    Check(missingDdsReported, "DDS material map was not accepted as a format");

    // Scene schema: every glTF BRDF property on primitives and on both tree slots.
    auto scene = Json::parse(R"({"version":1,"name":"PBR","objects":[{"id":"ball","type":"primitive","shape":"sphere"}]})");
    scene["materials"]["chrome"] = { { "textureCoordScale", { 2, 2 } },
        { "baseColor", { 0.9, 0.8, 0.7, 1 } }, { "metallic", 1 }, { "roughness", 0.05 },
        { "specular", 0.5 }, { "specularColor", { 1, 0.5, 2 } }, { "ior", 1.4 }, { "transmission", 0.25 },
        { "alphaMode", "mask" }, { "alphaCutoff", 0.3 },
        { "baseColorTexture", { { "source", "base.png" }, { "wrap", false },
            { "transform", { { "offset", { 0.25, 0.5 } }, { "rotation", 1.5 }, { "scale", { 2, 3 } } } } } },
        { "metallicRoughnessTexture", { { "source", "mr.png" } } },
        { "normalTexture", { { "source", "normal.png" }, { "scale", 0.35 } } },
        { "specularTexture", { { "source", "spec.png" } } },
        { "specularColorTexture", { { "source", "specColor.jpg" } } } };
    scene["objects"][0]["material"] = "chrome";
    scene["objects"].push_back(Json::parse(R"({"id":"tree","type":"tree","generator":{"axiom":"F"},
        "materials":{"trunk":"chrome","leaf":{"baseColor":[0,1,0,1],"alphaMode":"blend","unlit":true}}})"));
    auto parsed = SceneData::ParseScene(scene.dump(), "pbr.scene.json");
    auto checkChrome = [](const SceneData::MaterialDefinition& p, const char* description) {
        Check(p.textureCoordScale.x == 2 && p.baseColor.y == 0.8f && p.metallic == 1 && p.roughness == 0.05f
            && p.specular == 0.5f && p.specularColor.z == 2 && p.ior == 1.4f && p.transmission == 0.25f
            && p.alphaMode == SceneData::AlphaMode::Mask && p.alphaCutoff == 0.3f && !p.unlit
            && p.baseColorTexture.source == "base.png" && !p.baseColorTexture.wrap && p.baseColorTexture.hasTransform
            && p.baseColorTexture.offset.y == 0.5f && p.baseColorTexture.rotation == 1.5f && p.baseColorTexture.scale.y == 3
            && p.metallicRoughnessTexture.source == "mr.png" && p.metallicRoughnessTexture.wrap
            && !p.metallicRoughnessTexture.hasTransform && p.normalTexture.source == "normal.png" && p.normalScale == 0.35f
            && p.specularTexture.source == "spec.png" && p.specularColorTexture.source == "specColor.jpg", description);
    };
    auto checkScene = [&](const SceneData::SceneDefinition& s, const char* description) {
        const auto& ball = static_cast<const SceneData::PrimitiveDefinition&>(*s.objects[0]);
        Check(ball.shape == SceneData::Shape::Sphere && ball.hasMaterial, "sphere primitive was not parsed");
        checkChrome(ball.material, description);
        const auto& pbrTree = static_cast<const SceneData::TreeDefinition&>(*s.objects[1]);
        checkChrome(pbrTree.trunk, description);
        Check(pbrTree.leaf.unlit && pbrTree.leaf.alphaMode == SceneData::AlphaMode::Blend
            && pbrTree.leaf.baseColor.y == 1 && pbrTree.leaf.metallic == 1, "PBR leaf material was not parsed");
    };
    checkScene(parsed, "PBR material was not parsed");
    checkScene(SceneData::ParseScene(SceneData::SerializeScene(parsed), "roundtrip"), "PBR material did not round-trip");

    auto rejectPbr = [&](const char* field, const Json& value, const std::string& expectedPath) {
        auto bad = scene;
        bad["materials"]["chrome"][field] = value;
        Reject(bad.dump(), expectedPath);
    };
    rejectPbr("metallic", 1.5, "chrome.metallic");
    rejectPbr("roughness", -0.1, "chrome.roughness");
    rejectPbr("specular", 2, "chrome.specular");
    rejectPbr("specularColor", Json::array({ 1, -1, 1 }), "chrome.specularColor");
    rejectPbr("ior", 0.5, "chrome.ior");
    rejectPbr("transmission", 1.1, "chrome.transmission");
    rejectPbr("alphaMode", "MASK", "chrome.alphaMode");
    rejectPbr("alphaCutoff", 2, "chrome.alphaCutoff");
    rejectPbr("emissive", Json::array({ 1, 1, 1 }), "chrome.emissive");
    rejectPbr("normalTexture", { { "source", "n.png" }, { "scale", -1 } }, "normalTexture.scale");
    rejectPbr("specularTexture", { { "source", "s.png" }, { "scale", 1 } }, "specularTexture.scale");
    rejectPbr("baseColorTexture", { { "wrap", true } }, "baseColorTexture.source");
    rejectPbr("baseColorTexture", { { "source", "b.png" }, { "transform", { { "skew", 1 } } } }, "transform.skew");
    rejectPbr("unlit", true, "chrome.");
    auto bad = scene;
    bad["materials"]["chrome"]["diffuse"] = { 1, 1, 1, 1 };
    Reject(bad.dump(), "chrome.diffuse");
    bad = scene;
    bad["materials"]["chrome"]["flags"] = { 0, 0, 0, 1 };
    Reject(bad.dump(), "Phong materials are no longer supported");
    bad = scene;
    bad["materials"]["chrome"]["specular"] = { 1, 1, 1, 1 };
    Reject(bad.dump(), "chrome.specular");
    bad = scene;
    bad["materials"]["chrome"]["pbr"] = Json::object();
    Reject(bad.dump(), "the pbr block was removed");
}

void TestInstancing()
{
    // Instances copy everything from an earlier object except their ID, transform, and overridden materials.
    const auto scene = Json::parse(R"({"version":1,"name":"Instances","materials":{"red":{"baseColor":[1,0,0,1]}},"objects":[
        {"id":"ball","type":"primitive","shape":"sphere","textureCoordScale":[2,2],"material":{"baseColor":[0,0,1,1]}},
        {"id":"tree","type":"tree","depthLOD":2,"materials":{"trunk":"red","leaf":"red"},"generator":{"axiom":"F"}},
        {"id":"bare","type":"tree","generator":{"axiom":"FF"}},
        {"id":"model","type":"model","source":"Models/a.glb"},
        {"id":"ball-2","instanceOf":"ball","transform":{"position":[1,2,3]},"material":"red"},
        {"id":"tree-2","instanceOf":"tree","transform":{"scale":[2,2,2]},"materials":{"leaf":{"baseColor":[0,1,0,1]}}},
        {"id":"tree-3","instanceOf":"tree-2"},
        {"id":"model-2","instanceOf":"model"}]})");
    auto checkScene = [](const SceneData::SceneDefinition& s, const char* description) {
        Check(s.objects.size() == 8, description);
        const auto& ball = static_cast<const SceneData::PrimitiveDefinition&>(*s.objects[4]);
        Check(ball.kind == SceneData::ObjectKind::Primitive && ball.instanceOf == "ball" && ball.shape == SceneData::Shape::Sphere
            && ball.textureCoordScale.x == 2 && ball.transform.position.y == 2 && ball.overridesMaterial
            && ball.material.baseColor.x == 1 && ball.material.baseColor.z == 0, description);
        const auto& tree = static_cast<const SceneData::TreeDefinition&>(*s.objects[5]);
        Check(tree.kind == SceneData::ObjectKind::Tree && tree.instanceOf == "tree" && tree.depthLOD == 2
            && tree.generator.axiom == "F" && tree.transform.scale.y == 2 && tree.trunk.baseColor.x == 1
            && !tree.overridesMaterial && tree.overridesLeafMaterial && tree.leaf.baseColor.y == 1, description);
        const auto& nested = static_cast<const SceneData::TreeDefinition&>(*s.objects[6]);
        Check(nested.instanceOf == "tree-2" && nested.leaf.baseColor.y == 1 && !nested.overridesMaterial
            && !nested.overridesLeafMaterial && nested.transform.scale.y == 1, description);
        const auto& model = static_cast<const SceneData::ModelDefinition&>(*s.objects[7]);
        Check(model.instanceOf == "model" && model.source == "Models/a.glb", description);
    };
    const auto parsed = SceneData::ParseScene(scene.dump(), "instances.scene.json");
    checkScene(parsed, "instance was not resolved from its source");
    const auto saved = Json::parse(SceneData::SerializeScene(parsed));
    Check(!saved["objects"][4].contains("type") && !saved["objects"][4].contains("shape") && saved["objects"][4].contains("material")
        && saved["objects"][5]["materials"].contains("leaf") && !saved["objects"][5]["materials"].contains("trunk")
        && !saved["objects"][6].contains("materials") && saved["objects"][7].size() == 3,
        "instances were not saved in compact form");
    checkScene(SceneData::ParseScene(saved.dump(), "roundtrip"), "instance did not round-trip");

    auto rejectInstance = [&](const Json& instance, const std::string& expected) {
        auto bad = scene;
        bad["objects"].push_back(instance);
        Reject(bad.dump(), expected);
    };
    rejectInstance({ { "id", "x" }, { "instanceOf", "missing" } }, "unknown object 'missing'");
    rejectInstance({ { "id", "x" }, { "instanceOf", "x" } }, "cannot be an instance of itself");
    rejectInstance({ { "id", "x" }, { "instanceOf", "tree" }, { "type", "tree" } }, "objects[8].type");
    rejectInstance({ { "id", "x" }, { "instanceOf", "tree" }, { "depthLOD", 3 } }, "objects[8].depthLOD");
    rejectInstance({ { "id", "x" }, { "instanceOf", "tree" }, { "generator", Json::object() } }, "objects[8].generator");
    rejectInstance({ { "id", "x" }, { "instanceOf", "tree" }, { "material", "red" } }, "only primitive instances take a material");
    rejectInstance({ { "id", "x" }, { "instanceOf", "ball" }, { "materials", { { "trunk", "red" } } } }, "only tree, fixedTree, and graph");
    rejectInstance({ { "id", "x" }, { "instanceOf", "model" }, { "material", "red" } }, "model's own materials");
    rejectInstance({ { "id", "x" }, { "instanceOf", "bare" }, { "materials", { { "leaf", "red" } } } }, "specify both trunk and leaf");
    rejectInstance({ { "id", "ball" }, { "instanceOf", "tree" } }, "duplicate object ID 'ball'");
    auto forward = scene;
    forward["objects"].insert(forward["objects"].begin(), Json{ { "id", "early" }, { "instanceOf", "ball" } });
    Reject(forward.dump(), "instances must follow the object they copy");
}

void TestTestPassRemoved()
{
    // Performance tests are listed in Tests/Performance/scenes.json, not in scene files.
    auto scene = Json::parse(R"({"version":1,"name":"Passes","testPass":"performance","objects":[{"id":"box","type":"primitive"}]})");
    Reject(scene.dump(), "testPass: unknown property");
}

void TestLeafAtlases()
{
    // Atlases name sprite regions once; trees pick weighted regions for their leaves.
    auto scene = Json::parse(R"({"version":1,"name":"Atlases",
        "atlases":{"plants":{"aspect":2,"regions":{
            "oak":{"rect":[0.1,0.2,0.3,0.6],"stem":[0.2,0.6]},
            "fern":{"rect":[0.5,0,0.7,0.5]}}}},
        "objects":[
            {"id":"tree","type":"tree","leaves":{"atlas":"plants","regions":{"oak":3,"fern":1}},"generator":{"axiom":"F"}},
            {"id":"all","type":"fixedTree","leaves":{"atlas":"plants"}},
            {"id":"copy","instanceOf":"tree"}]})");
    auto parsed = SceneData::ParseScene(scene.dump(), "atlases.scene.json");
    const auto& atlas = parsed.atlases.at("plants");
    Check(atlas.aspect == 2 && atlas.regions.size() == 2, "atlas was not read");
    Check(atlas.regions.at("oak").stem.x == 0.2f && atlas.regions.at("oak").rect.w == 0.6f, "region rect or stem was not read");
    Check(atlas.regions.at("fern").stem.x == 0.6f && atlas.regions.at("fern").stem.y == 0.5f,
        "a region without a stem did not default to its bottom center");
    const auto& tree = static_cast<const SceneData::TreeDefinition&>(*parsed.objects[0]);
    Check(tree.leaves.atlas == "plants" && tree.leaves.regions.size() == 2, "tree leaves were not read");
    bool weighted = false;
    for (const auto& choice : tree.leaves.regions) weighted = weighted || (choice.region == "oak" && choice.weight == 3);
    Check(weighted, "region weight was not read");
    const auto& all = static_cast<const SceneData::FixedTreeDefinition&>(*parsed.objects[1]);
    Check(all.leaves.atlas == "plants" && all.leaves.regions.empty(), "leaves without regions should use the whole atlas");
    const auto& copy = static_cast<const SceneData::TreeDefinition&>(*parsed.objects[2]);
    Check(copy.leaves.atlas == "plants" && copy.leaves.regions.size() == 2, "an instance did not inherit its source's leaves");

    auto saved = Json::parse(SceneData::SerializeScene(parsed));
    Check(saved["atlases"]["plants"]["regions"]["fern"]["stem"][0] == 0.6f, "atlas was not saved");
    Check(saved["objects"][0]["leaves"]["regions"]["oak"] == 3.0f && !saved["objects"][1]["leaves"].contains("regions"),
        "tree leaves were not saved in their authored form");
    Check(!saved["objects"][2].contains("leaves"), "an instance saved inherited leaves");
    auto reparsed = SceneData::ParseScene(saved.dump(), "roundtrip");
    Check(reparsed.atlases.at("plants").regions.at("oak").rect.z == 0.3f, "atlas did not round-trip");

    auto rejectAtlas = [&](const char* region, const std::string& expected) {
        auto bad = scene;
        bad["atlases"]["plants"]["regions"]["bad"] = Json::parse(region);
        Reject(bad.dump(), expected);
    };
    rejectAtlas(R"({"rect":[0.5,0,0.4,1]})", "regions.bad.rect: expected [u0, v0, u1, v1]");
    rejectAtlas(R"({"rect":[0,0,1,1.5]})", "regions.bad.rect: expected [u0, v0, u1, v1]");
    rejectAtlas(R"({"rect":[0,0,0.5,0.5],"stem":[0.6,0.5]})", "regions.bad.stem: must lie inside rect");
    rejectAtlas(R"({"stem":[0.5,0.5]})", "regions.bad.rect: required property");
    rejectAtlas(R"({"rect":[0,0,1,1],"pivot":[0,0]})", "regions.bad.pivot: unknown property");
    auto bad = scene;
    bad["atlases"]["plants"]["aspect"] = 0;
    Reject(bad.dump(), "plants.aspect: must be positive");
    bad = scene;
    bad["atlases"]["plants"]["regions"] = Json::object();
    Reject(bad.dump(), "plants.regions: expected named regions");

    auto rejectLeaves = [&](const Json& leaves, const std::string& expected) {
        auto badLeaves = scene;
        badLeaves["objects"][0]["leaves"] = leaves;
        Reject(badLeaves.dump(), expected);
    };
    rejectLeaves(Json::parse(R"({"atlas":"missing"})"), "leaves.atlas: unknown atlas 'missing'");
    rejectLeaves(Json::parse(R"({"regions":{"oak":1}})"), "leaves.atlas: required property");
    rejectLeaves(Json::parse(R"({"atlas":"plants","regions":{"maple":1}})"), "leaves.regions.maple: atlas 'plants' has no region 'maple'");
    rejectLeaves(Json::parse(R"({"atlas":"plants","regions":{"oak":0}})"), "leaves.regions.oak: weight must be positive");
    rejectLeaves(Json::parse(R"({"atlas":"plants","regions":{}})"), "leaves.regions: expected region names with weights");
    auto instanceLeaves = scene;
    instanceLeaves["objects"][2]["leaves"] = { { "atlas", "plants" } };
    Reject(instanceLeaves.dump(), "objects[2].leaves: is inherited from instanceOf");

    // Twist and size variation are optional, saved only when set, and bounded.
    Check(tree.leaves.twistDegrees == 0 && tree.leaves.sizeVariation == 0, "leaf twist and size variation are not off by default");
    Check(!saved["objects"][0]["leaves"].contains("twistDegrees") && !saved["objects"][0]["leaves"].contains("sizeVariation"),
        "default leaf twist or size variation was saved");
    auto varied = scene;
    varied["objects"][0]["leaves"]["twistDegrees"] = 75;
    varied["objects"][0]["leaves"]["sizeVariation"] = 0.25;
    auto variedScene = SceneData::ParseScene(SceneData::SerializeScene(SceneData::ParseScene(varied.dump(), "varied")), "roundtrip");
    const auto& variedTree = static_cast<const SceneData::TreeDefinition&>(*variedScene.objects[0]);
    Check(variedTree.leaves.twistDegrees == 75 && variedTree.leaves.sizeVariation == 0.25f, "leaf twist or size variation did not round-trip");
    rejectLeaves(Json::parse(R"({"atlas":"plants","twistDegrees":181})"), "leaves.twistDegrees: must be between 0 and 180");
    rejectLeaves(Json::parse(R"({"atlas":"plants","twistDegrees":-1})"), "leaves.twistDegrees: must be between 0 and 180");
    rejectLeaves(Json::parse(R"({"atlas":"plants","sizeVariation":0.95})"), "leaves.sizeVariation: must be between 0 and 0.9");
}

void TestOpacityMicromaps()
{
    using namespace OpacityMicromap;
    Check(IndexRangeFits(88, 2, 360), "last two micromap indices should fit exactly");
    Check(!IndexRangeFits(88, 3, 360), "micromap range extending past the buffer must be rejected");
    Check(!IndexRangeFits(154, 2, 360), "micromap start beyond the buffer must be rejected");
    Check(!IndexRangeFits(-1, 2, 360), "missing micromap index must be rejected");
    Check(!IndexRangeFits(0, UINT64_MAX, 360), "oversized micromap range must be rejected without overflow");
    // Every micro-triangle maps to a distinct index along the space-filling curve.
    for (uint32_t level = 1; level <= 8; ++level)
    {
        const uint32_t size = 1u << level;
        std::vector<bool> seen(size * size, false);
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; i + j < size; ++j)
                for (int inverted = 0; inverted < 2; ++inverted)
                {
                    if (inverted && i + j + 1 >= size) continue;
                    const float u = (i + (inverted ? 2.0f : 1.0f) / 3) / size, v = (j + (inverted ? 2.0f : 1.0f) / 3) / size;
                    const uint32_t index = BarycentricsToIndex(u, v, level);
                    Check(index < size * size && !seen[index], "micro-triangle indices are not a bijection");
                    seen[index] = true;
                }
    }

    // An opaque disk on a transparent background, with a soft edge.
    AlphaImage image;
    image.width = image.height = 64;
    image.wrap = false;
    image.alpha.resize(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const float d = std::sqrt((x - 31.5f) * (x - 31.5f) + (y - 31.5f) * (y - 31.5f));
            image.alpha[y * 64 + x] = static_cast<uint8_t>(std::min(std::max((22.0f - d) * 64.0f, 0.0f), 255.0f));
        }
    const Uv uv[3] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    const uint32_t level = 5;
    const auto baked = BakeTriangle(image, 0.5f, uv, level);
    Check(baked.specialIndex == 0 && baked.data.size() == (1u << (2 * level)) / 4, "disk triangle should store OC1_4_STATE data");
    Check(baked.stateCounts[Opaque] > 0 && baked.stateCounts[Transparent] > 0
        && baked.stateCounts[UnknownOpaque] + baked.stateCounts[UnknownTransparent] > 0, "disk triangle should have every kind of state");
    // Certain states agree with dense bilinear samples inside each micro-triangle.
    auto alphaAt = [&](float u, float v)
    {
        const float x = u * 64 - 0.5f, y = v * 64 - 0.5f;
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const float fx = x - x0, fy = y - y0;
        auto at = [&](int px, int py) { return image.alpha[std::min(std::max(py, 0), 63) * 64 + std::min(std::max(px, 0), 63)] / 255.0f; };
        return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
    };
    const uint32_t size = 1u << level;
    for (uint32_t i = 0; i < size; ++i)
        for (uint32_t j = 0; i + j < size; ++j)
        {
            const float u = static_cast<float>(i) / size, v = static_cast<float>(j) / size, step = 1.0f / size;
            const uint32_t index = BarycentricsToIndex(u + step / 3, v + step / 3, level);
            const uint32_t state = (baked.data[index / 4] >> (2 * (index % 4))) & 3;
            for (int sample = 0; sample < 25 && (state == Opaque || state == Transparent); ++sample)
            {
                float a = (sample % 5 + 0.5f) / 5, b = (sample / 5 + 0.5f) / 5;
                if (a + b > 1) { a = 1 - a; b = 1 - b; }
                const bool passes = alphaAt(u + a * step, v + b * step) >= 0.5f;
                Check(passes == (state == Opaque), "a certain micro-triangle state disagrees with the texture");
            }
        }

    // Uniform triangles use special indices and store nothing.
    image.alpha.assign(64 * 64, 255);
    const auto opaque = BakeTriangle(image, 0.5f, uv, 4);
    Check(opaque.specialIndex == FullyOpaqueIndex && opaque.data.empty(), "fully opaque triangle should use the special index");
    image.alpha.assign(64 * 64, 0);
    Check(BakeTriangle(image, 0.5f, uv, 4).specialIndex == FullyTransparentIndex, "fully transparent triangle should use the special index");
}

int RunScenePersistenceTests();


void TestLodSelection()
{
    Lod::Thresholds two;
    two.distances = { 10, 40 };
    Check(Lod::Select(0, two) == 0 && Lod::Select(9.99f, two) == 0 && Lod::Select(10, two) == 1
        && Lod::Select(39, two) == 1 && Lod::Select(40, two) == 2 && Lod::Select(1000, two) == 2, "LOD levels are wrong");
    Check(Lod::Select(15, two, 2.0f) == 0 && Lod::Select(25, two, 2.0f) == 1, "LOD distance scale is not applied");
    Check(Lod::Select(10.5f, two, 1.0f, 0) == 1, "no hysteresis should switch at the threshold");
    two.hysteresis = 0.1f;
    Check(Lod::Select(10.5f, two, 1.0f, 0) == 0 && Lod::Select(11.5f, two, 1.0f, 0) == 1,
        "hysteresis should hold level 0 until 10% past its distance");
    Check(Lod::Select(9.5f, two, 1.0f, 1) == 1 && Lod::Select(8.5f, two, 1.0f, 1) == 0,
        "hysteresis should hold level 1 until 10% inside its near distance");
    Check(Lod::Select(43, two, 1.0f, 2) == 2 && Lod::Select(35, two, 1.0f, 2) == 1, "the last level's hysteresis is wrong");
    Check(Lod::Select(100, two, 1.0f, 0) == 2, "a large jump should go straight to the right level");
    Check(Lod::Valid(Lod::Thresholds()) && Lod::Valid(two), "valid thresholds were rejected");
    Lod::Thresholds bad = two;
    bad.distances = { 10, 10 };
    Check(!Lod::Valid(bad), "non-ascending distances were accepted");
    bad.distances = {};
    Check(!Lod::Valid(bad), "empty distances were accepted");
    bad.distances = { 10 };
    bad.hysteresis = 0.6f;
    Check(!Lod::Valid(bad), "hysteresis above 0.5 was accepted");

    // Screen heights: a 10 m object on a 1000-pixel display with a 90-degree field of view projects to
    // 10 * 1000 / (2 * d) pixels, so 100 pixels at 50 m and 50 pixels at 100 m.
    Lod::Thresholds screen;
    Lod::SetScreenHeights(screen, { 100, 50 });
    Check(screen.ScreenSpace() && Lod::Valid(screen), "valid screen heights were rejected");
    const float scale = Lod::Scale(screen, 10, 1000, 3.14159265f / 2, 1);
    Check(Lod::Select(49, screen, scale) == 0 && Lod::Select(51, screen, scale) == 1 && Lod::Select(99, screen, scale) == 1
        && Lod::Select(101, screen, scale) == 2, "screen-height levels are wrong");
    Check(Lod::Select(99, screen, Lod::Scale(screen, 20, 1000, 3.14159265f / 2, 1)) == 0,
        "a larger object should keep detail farther away");
    Check(Lod::Select(99, screen, Lod::Scale(screen, 10, 2000, 3.14159265f / 2, 1)) == 0,
        "a taller display should keep detail farther away");
    Check(Lod::Scale(two, 10, 1000, 1, 2) == 2, "distance thresholds should only take the global scale");
    Lod::Thresholds badScreen;
    Lod::SetScreenHeights(badScreen, { 50, 100 });
    Check(!Lod::Valid(badScreen), "ascending screen heights were accepted");
    Lod::SetScreenHeights(badScreen, { 100, -50 });
    Check(!Lod::Valid(badScreen), "negative screen heights were accepted");
}

void TestTreeLodSettings()
{
    const std::string json = R"({"version":1,"name":"Lod","objects":[
        {"id":"tree","type":"tree","lod":{"distances":[12,30],"hysteresis":0.2},"generator":{"axiom":"F"}},
        {"id":"tree-2","instanceOf":"tree"},
        {"id":"plain","type":"tree","generator":{"axiom":"F"}}]})";
    auto scene = SceneData::ParseScene(json, "lod");
    Check(scene.objects[0]->lod.distances == std::vector<float>{ 12, 30 } && scene.objects[0]->lod.hysteresis == 0.2f,
        "lod settings were not parsed");
    Check(scene.objects[1]->lod == scene.objects[0]->lod, "an instance did not inherit lod");
    Check(scene.objects[2]->lod == Lod::Thresholds(), "lod did not default to 10 units without hysteresis");
    const auto saved = Json::parse(SceneData::SerializeScene(scene));
    Check(saved["objects"][0].contains("lod") && !saved["objects"][1].contains("lod") && !saved["objects"][2].contains("lod"),
        "lod should be saved only where it differs from the default");
    Check(SceneData::ParseScene(saved.dump(), "saved-lod").objects[0]->lod == scene.objects[0]->lod, "lod did not round-trip");
    auto bad = Json::parse(json);
    bad["objects"][0]["lod"]["distances"] = Json::array({ 30, 12 });
    Reject(bad.dump(), ".lod");
    bad = Json::parse(json);
    bad["objects"][0]["lod"]["hysteresis"] = 0.9;
    Reject(bad.dump(), ".lod");
    bad = Json::parse(json);
    bad["objects"][0]["lod"]["bias"] = 1;
    Reject(bad.dump(), ".lod.bias");
    bad = Json::parse(json);
    bad["objects"][0]["lod"]["reduction"] = "collapse";
    Reject(bad.dump(), ".lod.reduction");
    auto merged = Json::parse(json);
    merged["objects"][0]["lod"]["reduction"] = "merge";
    auto mergedScene = SceneData::ParseScene(merged.dump(), "lod-merge");
    Check(mergedScene.objects[0]->lod.reduction == Lod::Reduction::Merge && scene.objects[0]->lod.reduction == Lod::Reduction::Prune,
        "lod reduction was not parsed");
    Check(SceneData::ParseScene(SceneData::SerializeScene(mergedScene), "saved-merge").objects[0]->lod.reduction == Lod::Reduction::Merge,
        "lod reduction did not round-trip");
    bad = Json::parse(json);
    bad["objects"][1]["lod"] = Json::object();
    Reject(bad.dump(), "objects[1].lod");

    auto screen = Json::parse(json);
    screen["objects"][0]["lod"].erase("distances");
    screen["objects"][0]["lod"]["screenHeights"] = Json::array({ 400, 200, 100 });
    auto screenScene = SceneData::ParseScene(screen.dump(), "lod-screen");
    Check(screenScene.objects[0]->lod.screenHeights == std::vector<float>{ 400, 200, 100 }
        && screenScene.objects[0]->lod.distances.size() == 3 && screenScene.objects[1]->lod == screenScene.objects[0]->lod,
        "lod screen heights were not parsed");
    const auto savedScreen = Json::parse(SceneData::SerializeScene(screenScene));
    Check(savedScreen["objects"][0]["lod"].contains("screenHeights") && !savedScreen["objects"][0]["lod"].contains("distances")
        && SceneData::ParseScene(savedScreen.dump(), "saved-screen").objects[0]->lod == screenScene.objects[0]->lod,
        "lod screen heights did not round-trip");
    bad = screen;
    bad["objects"][0]["lod"]["screenHeights"] = Json::array({ 100, 200 });
    Reject(bad.dump(), ".lod");
    bad = screen;
    bad["objects"][0]["lod"]["distances"] = Json::array({ 10 });
    Reject(bad.dump(), ".lod");
    bad = Json::parse(R"({"version":1,"name":"Lod","objects":[{"id":"box","type":"primitive","shape":"box","lod":{}}]})");
    Reject(bad.dump(), "objects[0].lod");
}

void TestUpscalerMath()
{
    using namespace UpscalerMath;
    const Size display{ 1920, 1080 };
    auto same = [](Size a, Size b) { return a.width == b.width && a.height == b.height; };
    Check(same(RenderSize(display, 1.0f), display), "native render size differs from the display");
    Check(same(RenderSize(display, 0.5f), Size{ 960, 540 }), "performance render size is wrong");
    Check(same(RenderSize(display, PresetScale(Preset::Quality, 0)), Size{ 1280, 720 }), "quality render size is wrong");
    Check(same(RenderSize(display, PresetScale(Preset::UltraPerformance, 0)), Size{ 640, 360 }), "ultra performance size is wrong");
    Check(same(RenderSize(Size{ 641, 3 }, 0.5f), Size{ 321, 2 }), "odd sizes do not round to nearest");
    Check(same(RenderSize(Size{ 1, 1 }, 0.25f), Size{ 1, 1 }), "render size fell below one pixel");
    Check(same(RenderSize(display, 4.0f), display), "render scale above one was not clamped");
    Check(same(RenderSize(display, 0.01f), RenderSize(display, MinRenderScale)), "render scale below the minimum was not clamped");
    Check(same(RenderSize(display, std::nanf("")), display), "a NaN render scale was not treated as native");
    Check(MipBias(display, display) == 0.0f, "native mip bias is not zero");
    Check(std::abs(MipBias(Size{ 960, 540 }, display) + 1.0f) < 1e-6f, "half-resolution mip bias is not -1");
    Check(JitterPhaseCount(display, display) == 8, "native jitter phase count changed");
    Check(JitterPhaseCount(Size{ 960, 540 }, display) == 32, "half-resolution jitter phase count is wrong");
    Check(JitterPhaseCount(Size{ 1280, 720 }, display) == 18, "quality jitter phase count is wrong");
    Check(PresetForScale(0.5f) == Preset::Performance && PresetForScale(1.0f) == Preset::Native
        && PresetForScale(0.8f) == Preset::Custom, "preset lookup is wrong");
}

void TestAmbientOcclusionSettings()
{
    auto disabled = SceneData::ParseScene(R"({"version":1,"name":"AO defaults","objects":[{"id":"box","type":"primitive","shape":"box"}]})", "ao");
    Check(disabled.ambientOcclusionRadius == 0 && disabled.ambientOcclusionStrength == 1,
        "AO must be disabled by default");
    auto input = Json::parse(SceneData::SerializeScene(disabled));
    input["render"]["ambientOcclusion"] = { {"radius", 1.25}, {"strength", 0.65} };
    auto configured = SceneData::ParseScene(input.dump(), "ao");
    auto roundtrip = SceneData::ParseScene(SceneData::SerializeScene(configured), "ao saved");
    Check(roundtrip.ambientOcclusionRadius == 1.25f && roundtrip.ambientOcclusionStrength == 0.65f,
        "AO settings were lost during scene serialization");
    input["render"]["ambientOcclusion"]["radius"] = -1;
    Reject(input.dump(), ".ambientOcclusion.radius");
    input["render"]["ambientOcclusion"]["radius"] = 0;
    input["render"]["ambientOcclusion"]["strength"] = 1.1;
    Reject(input.dump(), ".ambientOcclusion.strength");
    input["render"]["ambientOcclusion"]["strength"] = -0.1;
    Reject(input.dump(), ".ambientOcclusion.strength");
    input["render"]["ambientOcclusion"] = { {"raduis", 1} };
    Reject(input.dump(), ".ambientOcclusion.raduis");
}


void TestModelNormalAnimation()
{
    const std::string json = R"({"version":1,"name":"Water","objects":[{"id":"water","type":"model","source":"water.glb","normalAnimation":{"material":"River water","velocity1":[0.012,0.006],"velocity2":[-0.005,0.009],"strength":4}}]})";
    auto scene = SceneData::ParseScene(json, "normal-animation");
    auto& model = static_cast<SceneData::ModelDefinition&>(*scene.objects[0]);
    Check(model.animatedNormalMaterial == "River water" && model.normalVelocity1.x == 0.012f
        && model.normalVelocity2.y == 0.009f && model.normalStrength == 4.0f, "normal animation settings were not parsed");
    auto saved = SceneData::ParseScene(SceneData::SerializeScene(scene), "saved-normal-animation");
    auto& savedModel = static_cast<SceneData::ModelDefinition&>(*saved.objects[0]);
    Check(savedModel.animatedNormalMaterial == model.animatedNormalMaterial &&
        savedModel.normalVelocity1.x == model.normalVelocity1.x &&
        savedModel.normalVelocity2.y == model.normalVelocity2.y && savedModel.normalStrength == 4.0f, "normal animation did not round-trip");
    auto bad = Json::parse(json);
    bad["objects"][0]["normalAnimation"]["velocity1"] = Json::array({1});
    Reject(bad.dump(), ".normalAnimation.velocity1");
    bad = Json::parse(json);
    bad["objects"][0]["normalAnimation"].erase("material");
    Reject(bad.dump(), ".normalAnimation.material");
    bad = Json::parse(json);
    bad["objects"][0]["normalAnimation"]["strength"] = -1;
    Reject(bad.dump(), ".normalAnimation.strength");
    bad = Json::parse(json);
    bad["objects"][0]["normalAnimation"]["material"] = "";
    Reject(bad.dump(), ".normalAnimation.material");
}


void TestModelMaterialTint()
{
    const std::string json = R"({"version":1,"name":"Tint","objects":[{"id":"rock","type":"model","source":"rock.glb","materialTint":{"material":"Limestone","texture":"Terrain/tint.png","uvScale":[6.5,5],"uvOffset":[-2.9,-2]}}]})";
    auto scene = SceneData::ParseScene(json, "material-tint");
    auto& model = static_cast<SceneData::ModelDefinition&>(*scene.objects[0]);
    Check(model.tintMaterial == "Limestone" && model.tintTexture == "Terrain/tint.png" && model.tintUvScale.x == 6.5f
        && model.tintUvScale.y == 5.0f && model.tintUvOffset.x == -2.9f && model.tintUvOffset.y == -2.0f, "material tint was not parsed");
    auto saved = SceneData::ParseScene(SceneData::SerializeScene(scene), "saved-material-tint");
    auto& savedModel = static_cast<SceneData::ModelDefinition&>(*saved.objects[0]);
    Check(savedModel.tintMaterial == model.tintMaterial && savedModel.tintTexture == model.tintTexture
        && savedModel.tintUvScale.x == model.tintUvScale.x && savedModel.tintUvOffset.y == model.tintUvOffset.y, "material tint did not round-trip");
    // The transform is optional: identity.
    auto plain = Json::parse(json);
    plain["objects"][0]["materialTint"].erase("uvScale");
    plain["objects"][0]["materialTint"].erase("uvOffset");
    auto defaults = SceneData::ParseScene(plain.dump(), "material-tint-defaults");
    auto& defaultModel = static_cast<SceneData::ModelDefinition&>(*defaults.objects[0]);
    Check(defaultModel.tintUvScale.x == 1 && defaultModel.tintUvScale.y == 1 && defaultModel.tintUvOffset.x == 0, "material tint transform should default to identity");
    auto bad = Json::parse(json);
    bad["objects"][0]["materialTint"].erase("texture");
    Reject(bad.dump(), ".materialTint.texture");
    bad = Json::parse(json);
    bad["objects"][0]["materialTint"]["material"] = "";
    Reject(bad.dump(), ".materialTint.material");
    bad = Json::parse(json);
    bad["objects"][0]["materialTint"]["uvScale"] = Json::array({1});
    Reject(bad.dump(), ".materialTint.uvScale");
    bad = Json::parse(json);
    bad["objects"][0]["materialTint"]["strength"] = 2;
    Reject(bad.dump(), ".materialTint.strength");
}


void TestModelBiplanar()
{
    const std::string json = R"({"version":1,"name":"Biplanar","objects":[{"id":"rock","type":"model","source":"rock.glb","biplanar":{"material":"Limestone","uvPerUnit":0.0483,"sharpness":2}}]})";
    auto scene = SceneData::ParseScene(json, "biplanar");
    auto& model = static_cast<SceneData::ModelDefinition&>(*scene.objects[0]);
    Check(model.biplanarMaterial == "Limestone" && model.biplanarUvPerUnit == 0.0483f && model.biplanarSharpness == 2.0f,
        "biplanar settings were not parsed");
    auto saved = SceneData::ParseScene(SceneData::SerializeScene(scene), "saved-biplanar");
    auto& savedModel = static_cast<SceneData::ModelDefinition&>(*saved.objects[0]);
    Check(savedModel.biplanarMaterial == model.biplanarMaterial && savedModel.biplanarUvPerUnit == model.biplanarUvPerUnit
        && savedModel.biplanarSharpness == model.biplanarSharpness, "biplanar settings did not round-trip");
    // Sharpness is optional; without the property biplanar mapping is off.
    auto plain = Json::parse(json);
    plain["objects"][0]["biplanar"].erase("sharpness");
    auto defaults = SceneData::ParseScene(plain.dump(), "biplanar-defaults");
    Check(static_cast<SceneData::ModelDefinition&>(*defaults.objects[0]).biplanarSharpness == 1, "biplanar sharpness should default to 1");
    plain["objects"][0].erase("biplanar");
    auto off = SceneData::ParseScene(plain.dump(), "biplanar-off");
    auto& offModel = static_cast<SceneData::ModelDefinition&>(*off.objects[0]);
    Check(offModel.biplanarMaterial.empty() && offModel.biplanarUvPerUnit == 0, "biplanar mapping should default to off");
    Check(SceneData::SerializeScene(off).find("biplanar") == std::string::npos,"a model without biplanar mapping should not save it");
    auto bad = Json::parse(json);
    bad["objects"][0]["biplanar"].erase("uvPerUnit");
    Reject(bad.dump(), ".biplanar.uvPerUnit");
    bad = Json::parse(json);
    bad["objects"][0]["biplanar"]["uvPerUnit"] = 0;
    Reject(bad.dump(), ".biplanar.uvPerUnit");
    bad = Json::parse(json);
    bad["objects"][0]["biplanar"]["material"] = "";
    Reject(bad.dump(), ".biplanar.material");
    bad = Json::parse(json);
    bad["objects"][0]["biplanar"]["sharpness"] = 0;
    Reject(bad.dump(), ".biplanar.sharpness");
    bad = Json::parse(json);
    bad["objects"][0]["biplanar"]["sharpness"] = 17;
    Reject(bad.dump(), ".biplanar.sharpness");
    bad = Json::parse(json);
    bad["objects"][0]["biplanar"]["scale"] = 1;
    Reject(bad.dump(), ".biplanar.scale");
}


void TestSunShadowSettings()
{
    const std::string json = R"({"version":1,"name":"Sun","light":{"angularDiameterDegrees":2,"shadowSamples":8},"objects":[{"id":"box","type":"primitive","shape":"box"}]})";
    auto scene = SceneData::ParseScene(json, "sun");
    auto saved = SceneData::ParseScene(SceneData::SerializeScene(scene), "saved-sun");
    Check(saved.light.angularDiameterDegrees == 2 && saved.light.shadowSamples == 8,
        "sun shadow settings did not round-trip");
    auto bad = Json::parse(json);
    bad["light"]["angularDiameterDegrees"] = -1;
    Reject(bad.dump(), ".light.angularDiameterDegrees");
    bad["light"]["angularDiameterDegrees"] = 11;
    Reject(bad.dump(), ".light.angularDiameterDegrees");
    bad["light"]["angularDiameterDegrees"] = 2;
    bad["light"]["shadowSamples"] = 0;
    Reject(bad.dump(), ".light.shadowSamples");
    bad["light"]["shadowSamples"] = 33;
    Reject(bad.dump(), ".light.shadowSamples");
}

int main(int argc, char** argv)
{
    try
    {
        using namespace DebugVectorMath;
        for (const auto& angles : { XMFLOAT3(0, 0, 0), XMFLOAT3(25, -110, 40), XMFLOAT3(90, 30, 0), XMFLOAT3(-90, 30, 0) })
        {
            auto quaternion = FromDegrees(angles);
            auto roundTrip = FromDegrees(ToDegrees(quaternion));
            Check(std::abs(XMVectorGetX(XMVector4Dot(XMLoadFloat4(&quaternion), XMLoadFloat4(&roundTrip)))) > 0.9999f,
                "debug Euler editor preserves quaternion orientation, including poles");
            auto reflected = GizmoRotation(GizmoRotation(quaternion));
            Check(XMVector4Equal(XMLoadFloat4(&reflected), XMLoadFloat4(&quaternion)), "gizmo quaternion reflection round trip");
        }
        float scale[] = { 2, -4, 6 };
        Check(ScaleLinked(scale, 3, 0, 3) && scale[1] == -6 && scale[2] == 9, "linked scale preserves proportions and reflection");
        float zero[] = { 0, 1, 2 };
        Check(!ScaleLinked(zero, 3, 0, 2) && zero[1] == 1, "linked zero axis does not divide by zero");
        XMFLOAT4 invalid(0, 0, 0, 0);
        Check(!Normalize(invalid), "zero quaternion rejected");
        const float copied[] = { 1.25f, -2.5f, 3 };
        Check(Format("Tree / Position", copied, 3) == "Tree / Position: [1.25, -2.5, 3]", "clipboard includes field and components");
        TestSunShadowSettings();
        TestModelNormalAnimation();
        TestModelMaterialTint();
        TestModelBiplanar();
        TestAmbientOcclusionSettings();
        TestUpscalerMath();
        TestLodSelection();
        TestTreeLodSettings();
        if (argc == 3 && std::string(argv[1]) == "--gltf-only")
        {
            auto asset = LoadGltfAsset(argv[2]);
            Check(!asset->materials.empty() && !asset->renderParts.empty(), "glTF model has no renderable material");
            std::cout << "Imported " << asset->materials.size() << " materials and "
                << asset->renderParts.size() << " render parts\n";
            return 0;
        }
        RunScenePersistenceTests();
        if (argc > 1 && std::string(argv[1]) == "--persistence-only") return 0;
        TestSpecularExtension();
        if (argc > 1 && std::string(argv[1]) == "--specular-only")
        {
            std::cout << checks << " specular extension checks passed\n";
            return 0;
        }
        TestPbrSceneMaterials();
        TestInstancing();
        TestTestPassRemoved();
        TestLeafAtlases();
        TestOpacityMicromaps();
        const auto minimal = Json::parse(R"({"version":1,"name":"Example","objects":[{"id":"box","type":"primitive"}]})");
        auto defaults = SceneData::ParseScene(minimal.dump(), "test.scene.json");
        Check(defaults.objects.size() == 1, "default object missing");
        Check(defaults.objects[0]->transform.scale.x == 1 && defaults.objects[0]->transform.rotation.w == 1, "transform default is not identity");
        Check(defaults.objects[0]->textureCoordScale.x == 1 && defaults.objects[0]->textureCoordScale.y == 1, "object UV default is not identity");
        auto& primitive = static_cast<SceneData::PrimitiveDefinition&>(*defaults.objects[0]);
        Check(!primitive.hasMaterial, "omitted primitive material was marked as authored");
        Check(primitive.material.textureCoordScale.x == 1 && primitive.material.textureCoordScale.y == 1, "material UV default is not identity");
        auto empty = minimal;
        empty["objects"][0]["material"] = Json::object();
        auto emptyScene = SceneData::ParseScene(empty.dump(), "test.scene.json");
        const auto& emptyMaterial = static_cast<const SceneData::PrimitiveDefinition&>(*emptyScene.objects[0]).material;
        Check(emptyMaterial.baseColor.x == 1 && emptyMaterial.metallic == 1 && emptyMaterial.roughness == 1
            && emptyMaterial.alphaMode == SceneData::AlphaMode::Opaque && emptyMaterial.alphaCutoff == 0.5f
            && !emptyMaterial.unlit && emptyMaterial.baseColorTexture.source.empty(), "material defaults are not glTF defaults");
        Check(defaults.useShadowMaps && defaults.useAlphaBlendedRenderTarget, "render defaults changed");
        Check(defaults.farClippingPlane == 30.0f, "far clipping default changed");
        Check(defaults.enablePostProcessing && defaults.blurAmount == 1.3f,
            "post-processing defaults changed");
        Check(defaults.environmentRotationDegrees == 0, "environment rotation default changed");

        auto authored = minimal;
        authored["render"]["farClippingPlane"] = 300.0;
        authored["render"]["enablePostProcessing"] = false;
        authored["render"]["blurAmount"] = 2.5;
        authored["render"]["environment"] = { { "diffuse", "Environment/diffuse.dds" },
            { "specular", "Environment/specular.dds" }, { "rotationDegrees", 90.0 } };
        authored["materials"]["grid"] = { { "baseColorTexture", { { "source", "grid.png" } } }, { "textureCoordScale", { 2, 3 } } };
        authored["objects"][0]["material"] = "grid";
        authored["objects"][0]["textureCoordScale"] = { 4, 5 };
        authored["objects"][0]["transform"] = { { "rotationDegrees", { 0, 0, 90 } }, { "scale", { -30, -30, -30 } } };
        auto named = SceneData::ParseScene(authored.dump(), "test.scene.json");
        Check(named.farClippingPlane == 300.0f, "authored far clipping plane was lost");
        Check(!named.enablePostProcessing && named.blurAmount == 2.5f,
            "authored post-processing settings were lost");
        auto roundTrip = SceneData::ParseScene(SceneData::SerializeScene(named), "roundtrip");
        Check(!roundTrip.enablePostProcessing && roundTrip.blurAmount == 2.5f,
            "post-processing settings did not round-trip");
        Check(named.environmentDiffuse == "Environment/diffuse.dds" &&
            named.environmentSpecular == "Environment/specular.dds", "authored environment was lost");
        Check(SceneData::ParseScene(SceneData::SerializeScene(named), "roundtrip").environmentSpecular ==
            named.environmentSpecular, "environment did not round-trip");
        Check(named.environmentRotationDegrees == 90.0f &&
            SceneData::ParseScene(SceneData::SerializeScene(named), "roundtrip").environmentRotationDegrees == 90.0f,
            "environment rotation did not round-trip");
        auto& ground = static_cast<SceneData::PrimitiveDefinition&>(*named.objects[0]);
        Check(ground.hasMaterial, "authored primitive material was marked as omitted");
        Check(ground.material.baseColorTexture.source == "grid.png", "material reference did not resolve");
        Check(ground.material.textureCoordScale.x == 2 && ground.textureCoordScale.y == 5, "authored texture scales lost");
        Check(std::abs(ground.transform.rotation.z - 0.70710678f) < 1e-6f, "degree rotation was not converted");
        Check(ground.transform.scale.x == -30, "inverted skybox transform was rejected");

        auto inlineMaterial = minimal;
        inlineMaterial["objects"][0]["material"] = { { "baseColor", { 1, 0, 0, 1 } } };
        auto inlined = SceneData::ParseScene(inlineMaterial.dump(), "test.scene.json");
        Check(static_cast<SceneData::PrimitiveDefinition&>(*inlined.objects[0]).material.baseColor.y == 0, "inline material lost");

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
        bad = minimal; bad["objects"][0]["material"] = { { "alphaCutoff", 2 } };
        Reject(bad.dump(), "alphaCutoff");
        bad = minimal; bad["objects"][0]["animationSpeed"] = -1;
        Reject(bad.dump(), "animationSpeed");
        bad = minimal; bad["version"] = 2;
        Reject(bad.dump(), "version");
        bad = minimal; bad["render"]["farClippingPlane"] = 0.2;
        Reject(bad.dump(), "farClippingPlane");
        bad = minimal; bad["render"]["useShadowMaps"] = "true";
        Reject(bad.dump(), "useShadowMaps");
        bad = minimal; bad["objects"][0]["cubeMap"] = true;
        Reject(bad.dump(), "render.sky");
        bad = minimal; bad["render"]["sky"] = { { "cubeMap", "sky.png" } };
        Reject(bad.dump(), "render.sky.cubeMap");
        bad = minimal; bad["render"]["sky"] = { { "rotationDegrees", 90 } };
        Reject(bad.dump(), "render.sky.cubeMap");
        bad = minimal; bad["render"]["sky"] = { { "cubeMap", "sky.dds" }, { "scale", 2 } };
        Reject(bad.dump(), "render.sky");

        auto skyJson = minimal;
        skyJson["render"]["sky"] = { { "cubeMap", "Sky/sky.DDS" }, { "rotationDegrees", 135 } };
        auto skyScene = SceneData::ParseScene(skyJson.dump(), "test.scene.json");
        Check(skyScene.skyCubeMap == "Sky/sky.DDS" && skyScene.skyRotationDegrees == 135.0f, "render.sky was not parsed");
        auto skyRoundTrip = SceneData::ParseScene(SceneData::SerializeScene(skyScene), "roundtrip");
        Check(skyRoundTrip.skyCubeMap == skyScene.skyCubeMap && skyRoundTrip.skyRotationDegrees == 135.0f,
            "render.sky did not round-trip");
        Check(defaults.skyCubeMap.empty() && defaults.skyRotationDegrees == 0, "sky defaults changed");
        Check(SceneData::SerializeScene(defaults).find("\"sky\"") == std::string::npos, "absent sky was serialized");
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
        auto uvModel = SceneData::ParseScene(bad.dump(), "test.scene.json");
        auto uvRoundTrip = SceneData::ParseScene(SceneData::SerializeScene(uvModel), "test.scene.json");
        Check(uvRoundTrip.objects[0]->textureCoordScale.x == 2 && uvRoundTrip.objects[0]->textureCoordScale.y == 2,
            "model UV scale survives scene serialization");
        bad["objects"][0]["textureCoordScale"] = { 2, "invalid" };
        Reject(bad.dump(), "textureCoordScale");
        bad = authored; bad["render"]["blurAmount"] = -0.1;
        Reject(bad.dump(), "blurAmount");
        bad = authored; bad["render"]["blurAmount"] = 2.6;
        Reject(bad.dump(), "blurAmount");

        auto externalGltf = LoadGltfAsset(TestDataFilename("static-scene.gltf"));
        Check(externalGltf->renderParts.size() == 2, "glTF hierarchy instances were not imported");
        Check(externalGltf->primitives.size() == 2, "mirrored glTF node did not receive a winding variant");
        Check(externalGltf->materials.size() == 2 && externalGltf->textures.size() == 1, "glTF materials or textures were not imported");
        Check(externalGltf->textures[0].origin == GltfTextureSource::Origin::ExternalFile
            && externalGltf->textures[0].filename.find(L"checker.png") != std::wstring::npos,
            "external glTF image did not resolve relative to the asset");
        Check(externalGltf->meshes.size() == 1 && externalGltf->meshes[0].name == "SharedTriangle"
            && externalGltf->materials[0].name == "MaskedLeaves", "glTF source names were not retained");
        Check(externalGltf->renderParts[0].nodeName == "ForwardTriangle"
            && externalGltf->renderParts[1].nodeName == "MirroredTriangle"
            && externalGltf->renderParts[0].meshIndex == 0
            && externalGltf->renderParts[0].sourcePrimitiveIndex == 0,
            "glTF mesh and node identities were not retained");
        Check(externalGltf->primitives[0].sourceVertexCount == 3
            && externalGltf->primitives[0].sourceIndexCount == 3
            && !externalGltf->primitives[0].hasNormals
            && externalGltf->primitives[0].hasTexcoords
            && !externalGltf->primitives[0].hasTangents,
            "glTF source geometry metadata was not retained");
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
        Check(embeddedGlb->textures[0].origin == GltfTextureSource::Origin::BufferView,
            "GLB embedded image origin was not retained");

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
        Check(ai.name == "AI Tree" && ai.objects.size() == 202, "AI tree scene topology changed");
        Check(ai.skyCubeMap == "Environment/terra-daylight-sky.dds", "AI tree sky changed");
        Check(ai.objects[0]->kind == SceneData::ObjectKind::Model, "AI tree terrain missing");
        auto& aiModel = static_cast<SceneData::ModelDefinition&>(*ai.objects[0]);
        Check(aiModel.source == "Models/Gltf/terra.glb" && !aiModel.forceLit, "AI tree imported model changed");
        Check(ai.cameraAnimationLoop && ai.cameraAnimation.size() == 7 && ai.cameraAnimation.back().time == 42, "AI tree flight changed");

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
            { "gltf-models.scene.json", "glTF Models" },
            { "instancing.scene.json", "Instancing" }
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

        // Camera paths survive scene saves and reject invalid timing.
        auto animated = SceneData::ParseScene(R"({"version":1,"name":"Camera","objects":[{"id":"box","type":"primitive","shape":"box"}],"camera":{"animation":{"loop":true,"keys":[{"time":0,"position":[0,2,0],"lookAt":[0,0,1]},{"time":2,"position":[1,2,0],"lookAt":[0,0,1]}]}}})", "camera-test");
        auto restored = SceneData::ParseScene(SceneData::SerializeScene(animated), "camera-roundtrip");
        Check(restored.cameraAnimationLoop && restored.cameraAnimation.size() == 2 && restored.cameraAnimation[1].time == 2 &&
            restored.cameraAnimation[1].position.x == 1 && restored.cameraAnimation[1].lookAt.z == 1, "camera animation did not survive serialization");
        bool invalidCameraRejected = false;
        try
        {
            SceneData::ParseScene(R"({"version":1,"name":"Camera","objects":[{"id":"box","type":"primitive","shape":"box"}],"camera":{"animation":{"keys":[{"time":0,"position":[0,0,0],"lookAt":[0,0,1]},{"time":0,"position":[1,0,0],"lookAt":[0,0,1]}]}}})", "camera-test");
        }
        catch (const std::runtime_error&) { invalidCameraRejected = true; }
        Check(invalidCameraRejected, "camera animation accepted duplicate times");

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
        live.projectionData.farClippingPlane = 30.0f;
        live.view._41 = 7;
        live.mSceneBounds.Radius = 5;
        live.shadowTransform._42 = 3;

        SceneRenderSettings nextSettings;
        nextSettings.clearColor = DirectX::XMFLOAT4(0.2f, 0.3f, 0.4f, 1);
        nextSettings.numDirectionalLights = 0;
        nextSettings.numPointLights = 1;
        nextSettings.farClippingPlane = 300.0f;
        nextSettings.dirLights[0].Diffuse = DirectX::XMFLOAT4(0.7f, 0.8f, 0.9f, 1);
        nextSettings.pointLights[0].Position = DirectX::XMFLOAT3(1, 2, 3);
        live.ApplySceneSettings(nextSettings);
        Check(live.clearColor.x == 0.2f && live.clearColor.y == 0.3f, "scene background was not applied");
        Check(live.numDirectionalLights == 0 && live.numPointLights == 1, "scene light counts were not applied");
        Check(live.dirLights[0].Diffuse.z == 0.9f && live.pointLights[0].Position.z == 3, "scene lights were not applied");
        Check(live.time == 12 && live.frame == 99, "applying scene settings reset frame state");
        Check(live.projectionData.screenWidth == 1920 && live.view._41 == 7, "applying scene settings replaced camera state");
        Check(live.projectionData.farClippingPlane == 300.0f, "scene far clipping plane was not applied");
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

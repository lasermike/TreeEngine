#include "pch.h"
#include "GltfLoader.h"
#include "SceneDefinition.h"
#include "RenderData.h"
#include "OpacityMicromapBaker.h"
#include "UpscalerMath.h"
#include "DebugVectorMath.h"
#include "Lod.h"
#include "TerrainPackage.h"
#include "RoadNetwork.h"
#include "RoadMesh.h"
#include "RoadGrading.h"
#include "SignText.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <chrono>
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
    // An OPAQUE (or MASK) transmissive material is blended only for the raster fallback, at full coverage (flags.x 2,
    // so the shader ignores its base-color alpha); a BLEND one keeps its alpha as coverage (flags.x 1).
    Check(alphaBlend && glassMaterial.flags.x == 2 && glassMaterial.flags.z == 0 && glassMaterial.opticalParams.y == 0.5f
        && glassMaterial.opticalParams.x == 1.33f, "transmission did not force the raster blend fallback");
    glass.baseColor.w = 0;
    Check(BuildPbrShaderMaterial(glass, alphaBlend).flags.x == 2 && alphaBlend,
        "an OPAQUE transmissive material with zero base alpha lost full coverage");
    glass.alphaMode = PbrAlphaMode::Mask;
    Check(BuildPbrShaderMaterial(glass, alphaBlend).flags.x == 2, "a MASK transmissive material lost full coverage");
    glass.alphaMode = PbrAlphaMode::Blend;
    const auto blendedGlass = BuildPbrShaderMaterial(glass, alphaBlend);
    Check(blendedGlass.flags.x == 1 && blendedGlass.flags.z == 1.0f / 255.0f && alphaBlend,
        "a BLEND transmissive material did not keep its alpha as coverage");
    PbrMaterialFactors opaque;
    Check(BuildPbrShaderMaterial(opaque, alphaBlend).flags.x == 0 && !alphaBlend, "an opaque material was blended");

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


// Writes a small terrain package, reads it back, and checks block layout, borders, quantization and the manifest.
void TestTerrainPackage()
{
    using namespace TerrainFormat;
    Check(BlockStride % 512 == 0 && BlockStride >= BlockBytes, "terrain blocks are 512-byte aligned");
    Check(TerrainPackage::LevelsFor(65) == 1 && TerrainPackage::LevelsFor(66) == 2 && TerrainPackage::LevelsFor(8193) == 8,
        "terrain level count covers the samples");
    Check(TerrainPackage::NodeIndex(0, 0, 0) == 0 && TerrainPackage::NodeIndex(1, 1, 0) == 2 && TerrainPackage::NodeIndex(2, 0, 1) == 9
        && TerrainPackage::NodeCount(8) == 21845, "terrain node order is level by level, row-major");

    const int samples = 257;    // Three levels.
    const double spacing = 2.0;
    std::vector<float> heights(size_t(samples) * samples);
    for (int j = 0; j < samples; ++j)
        for (int i = 0; i < samples; ++i)
            heights[size_t(j) * samples + i] = float(-20 + 0.5 * i + 0.25 * j + 3 * std::sin(i * 0.1));
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring directory = std::wstring(temp) + L"TreeTerrainTest" + std::to_wstring(GetCurrentProcessId());
    std::string error;
    Check(TerrainPackage::Write(directory, heights, samples, spacing, "{\"test\":1}", error), "terrain package write");
    Check(!TerrainPackage::Write(directory + L"x", heights, samples - 1, spacing, "{}", error), "terrain write rejects a partial quadtree");

    TerrainPackage package;
    Check(package.Open(directory), "terrain package opens");
    const TerrainManifest& m = package.Manifest();
    Check(m.levels == 3 && m.worldSize == 512 && m.sampleSpacing == 2 && m.NodeSpacing(2) == 2 && m.NodeSpacing(0) == 8,
        "terrain manifest extent and levels");
    Check(m.sourceJson == "{\"test\":1}", "terrain manifest keeps its source");
    std::vector<uint16_t> block(size_t(BlockSamples) * BlockSamples);
    const float tolerance = m.heightScale * 0.51f;
    for (int level = 0; level < 3; ++level)
    {
        const int step = 1 << (2 - level);
        for (int nz = 0; nz < (1 << level); ++nz)
            for (int nx = 0; nx < (1 << level); ++nx)
            {
                Check(package.ReadBlock(level, nx, nz, block.data()), "terrain block reads");
                for (int b = 0; b < BlockSamples; ++b)
                    for (int a = 0; a < BlockSamples; ++a)
                    {
                        const int i = std::min(std::max((nx * NodeQuads + a - 1) * step, 0), samples - 1);
                        const int j = std::min(std::max((nz * NodeQuads + b - 1) * step, 0), samples - 1);
                        const float expected = heights[size_t(j) * samples + i];
                        if (std::abs(m.Height(block[size_t(b) * BlockSamples + a]) - expected) > tolerance)
                            Check(false, "terrain block sample matches the source within quantization");
                    }
            }
    }
    Check(!package.ReadBlock(3, 0, 0, block.data()) && !package.ReadBlock(1, 2, 0, block.data()), "terrain rejects nodes outside the tree");
    // Node bounds cover the source heights in their area, at every level.
    for (int level = 0; level < 3; ++level)
    {
        const int cells = NodeQuads << (2 - level);
        for (int nz = 0; nz < (1 << level); ++nz)
            for (int nx = 0; nx < (1 << level); ++nx)
            {
                const uint64_t node = TerrainPackage::NodeIndex(level, nx, nz);
                float low = FLT_MAX, high = -FLT_MAX;
                for (int j = nz * cells; j <= (nz + 1) * cells; ++j)
                    for (int i = nx * cells; i <= (nx + 1) * cells; ++i)
                    {
                        low = std::min(low, heights[size_t(j) * samples + i]);
                        high = std::max(high, heights[size_t(j) * samples + i]);
                    }
                Check(m.Height(package.MinValue(node)) <= low + 1e-3f && m.Height(package.MaxValue(node)) >= high - 1e-3f
                    && m.Height(package.MaxValue(node)) - high < 2 * m.heightScale, "terrain node bounds cover their heights");
            }
    }
    package.Close();

    TerrainManifest parsed;
    Check(!TerrainPackage::ParseManifest("{\"format\":\"TreeTerrain\",\"version\":1,\"nodeQuads\":32}", parsed, error),
        "terrain manifest rejects another block layout");
    Check(TerrainPackage::ParseManifest(TerrainPackage::ManifestJson(m), parsed, error) && parsed.levels == 3
        && parsed.heightOffset == m.heightOffset, "terrain manifest round trip");
    DeleteFileW((directory + L"\\nodes.bin").c_str());
    DeleteFileW((directory + L"\\bounds.bin").c_str());
    DeleteFileW((directory + L"\\manifest.json").c_str());
    RemoveDirectoryW(directory.c_str());
}

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

void TestTerrainSettings()
{
    const std::string json = R"({"version":1,"name":"Terrain","materials":{"grass":{"baseColor":[0.2,0.3,0.1,1],"roughness":0.9}},
        "objects":[{"id":"ground","type":"terrain","source":"Terrain/Hills.terrain","material":"grass","lodRange":3,"slots":256,
        "transform":{"position":[0,-5,0]},"textureCoordScale":[0.25,0.25]},
        {"id":"plain","type":"terrain","source":"Terrain/Hills.terrain"}]})";
    auto scene = SceneData::ParseScene(json, "terrain");
    const auto& ground = static_cast<const SceneData::TerrainDefinition&>(*scene.objects[0]);
    Check(scene.objects[0]->kind == SceneData::ObjectKind::Terrain && ground.source == "Terrain/Hills.terrain" && ground.hasMaterial
        && ground.lodRange == 3 && ground.slots == 256 && ground.transform.position.y == -5 && ground.textureCoordScale.x == 0.25f,
        "terrain settings were not parsed");
    const auto& plain = static_cast<const SceneData::TerrainDefinition&>(*scene.objects[1]);
    Check(!plain.hasMaterial && plain.lodRange == 2 && plain.slots == 512, "terrain defaults changed");
    const auto saved = Json::parse(SceneData::SerializeScene(scene));
    Check(saved["objects"][0]["type"] == "terrain" && !saved["objects"][0].contains("depthLOD")
        && !saved["objects"][1].contains("textureCoordScale"), "terrain saved fields it doesn't take");
    const auto reparsed = SceneData::ParseScene(saved.dump(), "saved-terrain");
    const auto& again = static_cast<const SceneData::TerrainDefinition&>(*reparsed.objects[0]);
    Check(again.source == ground.source && again.lodRange == 3 && again.slots == 256 && again.hasMaterial
        && again.textureCoordScale.x == 0.25f, "terrain did not round-trip");
    auto bad = Json::parse(json);
    bad["objects"][0].erase("source");
    Reject(bad.dump(), ".source");
    bad = Json::parse(json);
    bad["objects"][0]["lodRange"] = 0.5;
    Reject(bad.dump(), ".lodRange");
    bad = Json::parse(json);
    bad["objects"][0]["slots"] = 8;
    Reject(bad.dump(), ".slots");
    bad = Json::parse(json);
    bad["objects"][0]["generator"] = Json::object();
    Reject(bad.dump(), ".generator");
    bad = Json::parse(json);
    bad["objects"][1] = { { "id", "copy" }, { "instanceOf", "ground" } };
    Reject(bad.dump(), ".instanceOf");

    // Material layers.
    const std::string layered = R"({"version":1,"name":"Layers","materials":{"grass":{"roughness":0.9},"rock":{"roughness":0.8}},
        "objects":[{"id":"ground","type":"terrain","source":"Terrain/Hills.terrain","layers":[
            {"material":"grass","tile":3},
            {"material":"rock","tile":6,"biplanar":4,"slope":[30,90],"slopeBlend":5,"noise":{"slope":8,"scale":12}},
            {"material":{"baseColor":[0.9,0.9,0.95,1]},"height":[400,10000],"heightBlend":20}]}]})";
    auto layers = SceneData::ParseScene(layered, "layers");
    const auto& layeredTerrain = static_cast<const SceneData::TerrainDefinition&>(*layers.objects[0]);
    Check(layeredTerrain.layers.size() == 3 && !layeredTerrain.hasMaterial && layeredTerrain.layers[0].tile == 3
        && layeredTerrain.layers[1].biplanar == 4 && layeredTerrain.layers[1].slope.x == 30 && layeredTerrain.layers[1].noiseSlope == 8
        && layeredTerrain.layers[1].noiseScale == 12 && layeredTerrain.layers[2].height.x == 400 && layeredTerrain.layers[2].heightBlend == 20
        && layeredTerrain.layers[2].tile == 4 && layeredTerrain.layers[0].slope.y == 90, "terrain layers were not parsed");
    const auto savedLayers = Json::parse(SceneData::SerializeScene(layers));
    const auto& savedLayer = savedLayers["objects"][0]["layers"];
    Check(savedLayer.size() == 3 && !savedLayer[0].contains("height") && !savedLayer[0].contains("slope")
        && savedLayer[1]["slope"][0] == 30 && savedLayer[2]["height"][0] == 400, "terrain layers saved defaults or lost rules");
    const auto reparsedLayers = SceneData::ParseScene(savedLayers.dump(), "saved-layers");
    const auto& again2 = static_cast<const SceneData::TerrainDefinition&>(*reparsedLayers.objects[0]);
    Check(again2.layers.size() == 3, "terrain layers did not round-trip (count)");
    Check(again2.layers[1].noiseScale == 12, "terrain layers did not round-trip (noise)");
    Check(std::abs(again2.layers[2].material.baseColor.z - 0.95f) < 1e-6f, "terrain layers did not round-trip (material)");
    auto badLayers = Json::parse(layered);
    badLayers["objects"][0]["material"] = "grass";
    Reject(badLayers.dump(), ".layers");
    badLayers = Json::parse(layered);
    badLayers["objects"][0]["layers"][0]["slope"] = Json::array({ 10, 90 });
    Reject(badLayers.dump(), ".layers[0].slope");
    badLayers = Json::parse(layered);
    badLayers["objects"][0]["layers"][1]["slope"] = Json::array({ 50, 30 });
    Reject(badLayers.dump(), ".layers[1].slope");
    badLayers = Json::parse(layered);
    badLayers["objects"][0]["layers"][1]["noise"].erase("scale");
    Reject(badLayers.dump(), ".layers[1].noise.scale");
    badLayers = Json::parse(layered);
    badLayers["objects"][0]["layers"][1]["tile"] = 0;
    Reject(badLayers.dump(), ".layers[1].tile");
    badLayers = Json::parse(layered);
    for (int i = 0; i < 2; ++i)
        badLayers["objects"][0]["layers"].push_back(badLayers["objects"][0]["layers"][1]);
    Reject(badLayers.dump(), ".layers");
}

void TestWaterSettings()
{
    const std::string json = R"({"version":1,"name":"Water","objects":[
        {"id":"inlet","type":"water","transform":{"position":[0,1.5,0]},"size":[4096,2048],"tile":8,
         "ripples":{"velocity1":[0,-0.1],"velocity2":[0.004,-0.095],"strength":2.5},"tide":{"range":4,"period":600,"phase":30}},
        {"id":"pond","type":"water"}]})";
    const auto scene = SceneData::ParseScene(json, "water");
    const auto& inlet = static_cast<const SceneData::WaterDefinition&>(*scene.objects[0]);
    Check(inlet.kind == SceneData::ObjectKind::Water && inlet.size.y == 2048 && inlet.tile == 8 && inlet.hasRipples
        && inlet.rippleStrength == 2.5f && inlet.tideRange == 4 && inlet.tidePeriod == 600 && inlet.tidePhase == 30
        && inlet.transform.position.y == 1.5f, "water settings were not parsed");
    const auto& pond = static_cast<const SceneData::WaterDefinition&>(*scene.objects[1]);
    Check(!pond.hasMaterial && !pond.hasRipples && pond.tideRange == 0 && pond.tidePeriod == 44712, "water defaults changed");
    const auto saved = Json::parse(SceneData::SerializeScene(scene));
    Check(saved["objects"][0]["tide"]["period"] == 600 && !saved["objects"][1].contains("tide")
        && !saved["objects"][1].contains("ripples") && !saved["objects"][0].contains("depthLOD"), "water saved the wrong fields");
    const auto reparsed = SceneData::ParseScene(saved.dump(), "saved-water");
    const auto& again = static_cast<const SceneData::WaterDefinition&>(*reparsed.objects[0]);
    Check(again.tideRange == 4 && again.rippleVelocity2.x == 0.004f && again.size.x == 4096, "water did not round-trip");
    auto bad = Json::parse(json);
    bad["objects"][0]["tide"]["period"] = 0;
    Reject(bad.dump(), ".tide.period");
    bad = Json::parse(json);
    bad["objects"][0]["size"] = Json::array({ 0, 10 });
    Reject(bad.dump(), ".size");
    bad = Json::parse(json);
    bad["objects"][1] = { { "id", "copy" }, { "instanceOf", "inlet" } };
    Reject(bad.dump(), ".instanceOf");
}

void TestTide()
{
    double seconds = 0;
    Check(Tide::ParseUtc("2026-06-21T00:00:00Z", seconds) && seconds == 1782000000.0, "UTC time was not parsed");
    Check(Tide::ParseUtc("2000-02-29T12:30:15Z", seconds) && seconds == 951827415.0, "leap day was not parsed");
    Check(Tide::ParseUtc("2024-02-29T23:59:59Z", seconds) && seconds == 1709251199.0, "a leap year's February 29 was rejected");
    for (const char* malformed : { "2026-06-21 00:00:00", "2026-13-01T00:00:00Z", "2026-02-29T00:00:00Z",
             "1900-02-29T00:00:00Z", "2026-04-31T00:00:00Z", "2026-06-21T00:00:00Zjunk", " 2026-06-21T00:00:00Z",
             "2026-06-21T-1:00:00Z", "2026-06-21T24:00:00Z", "2026-06-21T00:60:00Z", "2026-06-21T00:00:60Z",
             "2026-6-21T00:00:00Z", "+026-06-21T00:00:00Z", "" })
        Check(!Tide::ParseUtc(malformed, seconds), "a malformed or impossible UTC time was accepted");
    // Budd Inlet (NOAA 9446807): its published constituents and mean sea level (1.290 m above NAVD88) against NOAA's
    // own predictions above NAVD88, every 6 hours from 2026-06-21T00:00Z.
    const std::vector<Tide::Constituent> budd = {
        { "M2", 1.459, 30.3 }, { "S2", 0.343, 62.0 }, { "N2", 0.276, 4.5 }, { "K1", 0.877, 289.6 }, { "M4", 0.049, 294.4 },
        { "O1", 0.472, 266.4 }, { "M6", 0.029, 143.8 }, { "MK3", 0.029, 145.8 }, { "S4", 0.004, 334.8 },
        { "MN4", 0.020, 265.4 }, { "NU2", 0.064, 10.6 }, { "MU2", 0.025, 193.0 }, { "2N2", 0.035, 345.2 },
        { "OO1", 0.026, 5.6 }, { "LAM2", 0.027, 46.8 }, { "S1", 0.017, 63.5 }, { "J1", 0.051, 331.4 },
        { "SSA", 0.033, 231.1 }, { "SA", 0.077, 292.9 }, { "MF", 0.022, 140.5 }, { "RHO", 0.025, 269.8 },
        { "Q1", 0.070, 264.7 }, { "T2", 0.015, 69.9 }, { "R2", 0.012, 118.3 }, { "2Q1", 0.008, 238.2 },
        { "P1", 0.269, 285.3 }, { "2SM2", 0.014, 273.4 }, { "M3", 0.006, 254.8 }, { "L2", 0.068, 64.4 },
        { "2MK3", 0.006, 267.3 }, { "K2", 0.098, 58.8 }, { "M8", 0.003, 18.8 }, { "MS4", 0.029, 313.7 } };
    const double noaa[] = { -0.986, 3.291, 0.295, 1.912, -0.374, 2.977, 0.525, 1.391 };
    for (int i = 0; i < 8; ++i)
    {
        const double height = 1.290 + Tide::Height(budd, 1782000000.0 + i * 6 * 3600.0);
        Check(std::abs(height - noaa[i]) < 0.1, "the tide is more than 10 cm from NOAA's prediction");
    }
    Check(Tide::Supported("M2") && Tide::Supported("2MK3") && !Tide::Supported("M1"), "constituent support changed");
}

void TestTideSettings()
{
    const std::string json = R"({"version":1,"name":"Tide","objects":[
        {"id":"inlet","type":"water","tide":{"start":"2026-06-21T19:00:00Z","source":"test",
         "constituents":[{"name":"M2","amplitude":1.5,"phase":57},{"name":"K1","amplitude":0.9}]}}]})";
    const auto scene = SceneData::ParseScene(json, "tide");
    const auto& inlet = static_cast<const SceneData::WaterDefinition&>(*scene.objects[0]);
    Check(inlet.tideConstituents.size() == 2 && inlet.tideConstituents[0].phase == 57 && inlet.tideConstituents[1].phase == 0
        && inlet.tideStart == "2026-06-21T19:00:00Z" && inlet.tideSource == "test" && inlet.tideRange == 0
        && inlet.tideSimulationTicksPerDay == 300, "tide constituents were not parsed");
    auto faster = Json::parse(json);
    faster["objects"][0]["tide"]["simulationTicksPerDay"] = 60;
    const auto fasterScene = SceneData::ParseScene(faster.dump(), "tide-faster");
    Check(static_cast<const SceneData::WaterDefinition&>(*fasterScene.objects[0]).tideSimulationTicksPerDay == 60
        && Json::parse(SceneData::SerializeScene(fasterScene))["objects"][0]["tide"]["simulationTicksPerDay"] == 60,
        "the tide's simulation ticks per day did not round-trip");
    const auto saved = Json::parse(SceneData::SerializeScene(scene));
    Check(saved["objects"][0]["tide"]["constituents"].size() == 2 && !saved["objects"][0]["tide"].contains("range"),
        "tide constituents saved the wrong fields");
    const auto again = SceneData::ParseScene(saved.dump(), "saved-tide");
    const auto& reparsed = static_cast<const SceneData::WaterDefinition&>(*again.objects[0]);
    Check(reparsed.tideConstituents.size() == 2 && reparsed.tideConstituents[1].amplitude == 0.9
        && reparsed.tideStart == inlet.tideStart && reparsed.tideSource == "test", "tide constituents did not round-trip");
    auto bad = Json::parse(json);
    bad["objects"][0]["tide"]["range"] = 4;
    Reject(bad.dump(), ".tide");
    bad = Json::parse(json);
    bad["objects"][0]["tide"].erase("start");
    Reject(bad.dump(), ".tide.start");
    bad = Json::parse(json);
    bad["objects"][0]["tide"]["start"] = "June 21";
    Reject(bad.dump(), ".tide.start");
    bad = Json::parse(json);
    bad["objects"][0]["tide"]["start"] = "2026-02-29T00:00:00Z";
    Reject(bad.dump(), ".tide.start");
    bad = Json::parse(json);
    bad["objects"][0]["tide"]["constituents"][0]["name"] = "M1";
    Reject(bad.dump(), ".constituents[0].name");
    bad = Json::parse(json);
    bad["objects"][0]["tide"]["constituents"][1]["amplitude"] = -1;
    Reject(bad.dump(), ".constituents[1].amplitude");
    bad = Json::parse(json);
    bad["objects"][0]["tide"]["simulationTicksPerDay"] = 0;
    Reject(bad.dump(), ".tide.simulationTicksPerDay");
    bad = Json::parse(R"({"version":1,"name":"Tide","objects":[{"id":"pond","type":"water","tide":{"range":2,"simulationTicksPerDay":300}}]})");
    Reject(bad.dump(), ".tide.simulationTicksPerDay");
}

void TestRoadNetwork()
{
    std::string crs, error;
    double originX = 0, originY = 0;
    // A 100 x 300 m source at 1 m packs into a 512 m world (513 samples), padded north and east: the southwest pixel
    // center (1000.5, 2000.5) is the package's -x/-z corner, so the origin is 256 m further on each axis.
    Check(Roads::PackageFrame(R"({"crs":"EPSG:32610","bounds":[1000,2000,1100,2300],"size":[100,300]})", 512, crs,
        originX, originY, error) && crs == "EPSG:32610" && originX == 1256.5 && originY == 2256.5,
        "the package frame was not read from the packed grid");
    Check(Roads::PackageFrame(R"({"crs":"urn:ogc:def:crs:EPSG::32610","bounds":[0,0,2048,1024],"size":[2048,1024]})",
        2048, crs, originX, originY, error) && originX == 1024.5 && originY == 1024.5 && crs == "EPSG:32610",
        "a padded rectangular source's origin is wrong");
    Check(!Roads::PackageFrame(R"({"preset":"hills"})", 512, crs, originX, originY, error) && !error.empty(),
        "a procedural package was given a frame");
    Check(!Roads::PackageFrame(R"({"crs":"EPSG:32610","bounds":[1000,2000,1100,2300]})", 512, crs, originX, originY, error),
        "a source without a raster size was given a frame");

    const std::string file = R"({"type":"FeatureCollection",
        "crs":{"type":"name","properties":{"name":"urn:ogc:def:crs:EPSG::32610"}},
        "features":[
          {"type":"Feature","properties":{"kind":"road","id":"county-1","source":"county","edited":true,"name":"E TEST RD",
            "surface":"gravel","owner":"county","width":5,"shoulder":0.3,"ditchDepth":0.5,"ditchWidth":2,"speedLimit":25},
           "geometry":{"type":"LineString","coordinates":[[1050,2150],[1060,2160.5]]}},
          {"type":"Feature","properties":{"kind":"driveway","id":"osm-way-7"},
           "geometry":{"type":"MultiLineString","coordinates":[[[1040,2140],[1041,2141]],[[1042,2142],[1043,2143]]]}},
          {"type":"Feature","properties":{"kind":"prop","id":"mailbox-1","source":"generated","prop":"mailbox","heading":270,
            "text":"12 E TEST RD"},"geometry":{"type":"Point","coordinates":[1052,2147]}},
          {"type":"Feature","properties":{"kind":"prop","id":"sign-1","prop":"street-name-sign"},
           "geometry":{"type":"Point","coordinates":[1050,2150,3]}}]})";
    const auto network = Roads::Parse(file, "EPSG:32610", 1050, 2150);
    Check(network.crs == "EPSG:32610" && network.lines.size() == 3, "roads were not parsed (multilines split)");
    Check(network.props.size() == 2 && network.props[0].model == Roads::PropModel::Mailbox && network.props[0].heading == 270
        && network.props[0].text == "12 E TEST RD" && network.props[0].point.x == 2 && network.props[0].point.y == -3
        && network.props[1].model == Roads::PropModel::StreetNameSign && network.props[1].heading == 0
        && network.props[1].point.x == 0, "props were not parsed");
    const auto& road = network.lines[0];
    Check(road.kind == Roads::Kind::Road && road.edited && road.name == "E TEST RD" && road.surface == "gravel"
        && road.width == 5 && road.ditchDepth == 0.5f && road.speedLimit == 25 && road.points.size() == 2
        && road.points[0].x == 0 && road.points[0].y == 0 && road.points[1].x == 10 && road.points[1].y == 10.5f,
        "a road's properties or package coordinates are wrong");
    const auto& driveway = network.lines[1];
    Check(driveway.kind == Roads::Kind::Driveway && driveway.width == 3 && driveway.surface == "paved"
        && driveway.points[0].x == -10 && network.lines[2].points[0].x == -8, "a driveway's defaults or parts are wrong");

    const auto rejects = [&](const std::string& text, const std::string& expected)
    {
        try { Roads::Parse(text, "EPSG:32610", 1050, 2150); }
        catch (const std::runtime_error& e)
        {
            Check(std::string(e.what()).find(expected) != std::string::npos, "a roads error lacks its location");
            return;
        }
        Check(false, "an invalid roads file was accepted");
    };
    auto bad = Json::parse(file);
    bad["crs"]["properties"]["name"] = "EPSG:4326";
    rejects(bad.dump(), "crs");
    bad = Json::parse(file);
    bad["features"][0]["properties"]["surface"] = "dirt";
    rejects(bad.dump(), "features[0] (county-1).properties.surface");
    bad = Json::parse(file);
    bad["features"][0]["properties"]["width"] = 0;
    rejects(bad.dump(), "properties.width");
    bad = Json::parse(file);
    bad["features"][1]["geometry"] = { { "type", "Point" }, { "coordinates", Json::array({ 1, 2 }) } };
    rejects(bad.dump(), "features[1] (osm-way-7).geometry");
    bad = Json::parse(file);
    bad["features"][0]["geometry"]["coordinates"] = Json::array({ Json::array({ 1050, 2150 }) });
    rejects(bad.dump(), "at least two points");
    // Removed features are left out (they only keep the tools from making them again); removed must be a boolean.
    auto removed = Json::parse(file);
    removed["features"][0]["properties"]["removed"] = true;
    removed["features"][2]["properties"]["removed"] = true;
    removed["features"][3]["properties"]["removed"] = false;
    const auto kept = Roads::Parse(removed.dump(), "EPSG:32610", 1050, 2150);
    Check(kept.lines.size() == 2 && kept.lines[0].id == "osm-way-7" && kept.props.size() == 1 && kept.props[0].id == "sign-1",
        "removed features were drawn");
    bad = Json::parse(file);
    bad["features"][1]["properties"]["removed"] = "yes";
    rejects(bad.dump(), "features[1] (osm-way-7).properties.removed");
    bad = Json::parse(file);
    bad["features"][2]["properties"]["prop"] = "bench";
    rejects(bad.dump(), "features[2] (mailbox-1).properties.prop");
    bad = Json::parse(file);
    bad["features"][2]["properties"].erase("prop");
    rejects(bad.dump(), "features[2] (mailbox-1).properties.prop");
    bad = Json::parse(file);
    bad["features"][2]["geometry"] = { { "type", "LineString" }, { "coordinates", Json::array({ Json::array({ 1, 2 }), Json::array({ 3, 4 }) }) } };
    rejects(bad.dump(), "a prop must be a Point");
    bad = Json::parse(file);
    bad["features"][3]["geometry"]["coordinates"] = Json::array({ 1050 });
    rejects(bad.dump(), "features[3] (sign-1).geometry.coordinates");
    bad = Json::parse(file);
    bad["features"][0]["geometry"].erase("coordinates");
    rejects(bad.dump(), "features[0] (county-1).geometry.coordinates");
    bad = Json::parse(file);
    bad["features"][1]["geometry"].erase("coordinates");
    rejects(bad.dump(), "features[1] (osm-way-7).geometry.coordinates");
    bad = Json::parse(file);
    bad["features"][1]["geometry"]["coordinates"] = Json::array();
    rejects(bad.dump(), "at least one line");
    bad = Json::parse(file);
    bad["features"][0]["geometry"]["type"] = 7;
    rejects(bad.dump(), "features[0] (county-1).geometry");
    rejects("[]", "FeatureCollection");

    // The scene field, saved and parsed again.
    const std::string scene = R"({"version":1,"name":"Roads","objects":[
        {"id":"ground","type":"terrain","source":"Terrain/Hills.terrain","roads":"Terrain/Hills.roads.geojson"}]})";
    const auto parsed = SceneData::ParseScene(scene, "roads");
    const auto& terrain = static_cast<const SceneData::TerrainDefinition&>(*parsed.objects[0]);
    Check(terrain.roads == "Terrain/Hills.roads.geojson", "the terrain's roads file was not parsed");
    const auto saved = Json::parse(SceneData::SerializeScene(parsed));
    Check(saved["objects"][0]["roads"] == "Terrain/Hills.roads.geojson", "the terrain's roads file was not saved");
    auto noRoads = Json::parse(scene);
    noRoads["objects"][0].erase("roads");
    Check(!Json::parse(SceneData::SerializeScene(SceneData::ParseScene(noRoads.dump(), "no-roads")))["objects"][0].contains("roads"),
        "a terrain without roads saved a roads field");
    auto empty = Json::parse(scene);
    empty["objects"][0]["roads"] = "";
    Reject(empty.dump(), ".roads");
}

void TestRoadMesh()
{
    // On flat ground at y = 2: a 24 m county paved road (6 m wide, 0.6 m shoulders, 45 mph) along +x, a gravel
    // county road, and a gravel driveway, offset by the terrain's position (10, 2, 20).
    Roads::Network network;
    Roads::Line road;
    road.surface = "paved";
    road.owner = "county";
    road.width = 6;
    road.shoulder = 0.6f;
    road.speedLimit = 45;
    road.points = { XMFLOAT2(0, 0), XMFLOAT2(24, 0) };
    network.lines.push_back(road);
    Roads::Line gravelRoad = road;
    gravelRoad.surface = "gravel";
    gravelRoad.points = { XMFLOAT2(0, 50), XMFLOAT2(10, 50) };
    network.lines.push_back(gravelRoad);
    Roads::Line driveway;
    driveway.kind = Roads::Kind::Driveway;
    driveway.surface = "gravel";
    driveway.width = 3;
    driveway.points = { XMFLOAT2(5, 3), XMFLOAT2(5, 13) };
    network.lines.push_back(driveway);
    const auto flat = [](float, float, float& y) { y = 2; return true; };
    const auto mesh = Roads::BuildMesh(network, XMFLOAT3(10, 0, 20), flat);

    const auto& asphalt = mesh[Roads::Part::Asphalt];
    // 25 cross-sections (every meter), and 3.6 m half-width in columns at most 1.5 m apart: 7 columns.
    Check(asphalt.vertices.size() == 25 * 7 && asphalt.indices.size() == 24 * 6 * 6, "the paved road's strip has the wrong size");
    bool lifted = true, up = true, inside = true;
    for (const auto& v : asphalt.vertices)
    {
        lifted = lifted && std::abs(v.Pos.y - (2 + Roads::RoadLift)) < 1e-5f && v.Normal.y > 0.999f;
        inside = inside && v.Pos.x >= 10 - 1e-4f && v.Pos.x <= 34 + 1e-4f && std::abs(v.Pos.z - 20) <= 3.6f + 1e-4f
            && v.Tex.x == v.Pos.x && v.Tex.y == -v.Pos.z;
    }
    for (size_t i = 0; i + 2 < asphalt.indices.size(); i += 3)
    {
        const auto& a = asphalt.vertices[asphalt.indices[i]].Pos;
        const auto& b = asphalt.vertices[asphalt.indices[i + 1]].Pos;
        const auto& c = asphalt.vertices[asphalt.indices[i + 2]].Pos;
        up = up && ((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z)) > 0;
    }
    Check(lifted && inside, "the paved road isn't on the ground, lifted, world-planar and as wide as it should be");
    Check(up, "a road triangle faces down");

    // Dashed yellow center: dashes over [0, 3] and [12, 15] m, then the one starting at 24 m is too short to draw.
    const auto& yellow = mesh[Roads::Part::YellowLine];
    Check(yellow.vertices.size() == 2 * 2 * 4 && yellow.indices.size() == 2 * 3 * 6, "the center line's dashes are wrong");
    bool centered = true;
    for (const auto& v : yellow.vertices)
        centered = centered && std::abs(v.Pos.z - 20) <= Roads::MarkingWidth / 2 + 1e-5f
            && std::abs(v.Pos.y - (2 + Roads::MarkingLift)) < 1e-5f && ((v.Pos.x >= 10 && v.Pos.x <= 13) || (v.Pos.x >= 22 && v.Pos.x <= 25));
    Check(centered, "a center-line dash is out of place");
    // 45 mph: white edge lines 0.15 m inside each edge of the 6 m lane width.
    const auto& white = mesh[Roads::Part::WhiteLine];
    Check(white.vertices.size() == 2 * 25 * 2, "the edge lines are missing");
    bool edges = true;
    for (const auto& v : white.vertices)
        edges = edges && std::abs(std::abs(v.Pos.z - 20) - 2.85f) <= Roads::MarkingWidth / 2 + 1e-5f;
    Check(edges, "an edge line is out of place");

    // Gravel: the gravel road (11 cross-sections, 7 columns) and the driveway (11 cross-sections, 3 m: 3 columns),
    // no markings; the driveway sits below roads.
    const auto& gravel = mesh[Roads::Part::Gravel];
    Check(gravel.vertices.size() == 11 * 7 + 11 * 3, "the gravel strips have the wrong size");
    Check(std::abs(gravel.vertices.front().Pos.y - (2 + Roads::GravelRoadLift)) < 1e-5f
        && std::abs(gravel.vertices.back().Pos.y - (2 + Roads::DrivewayLift)) < 1e-5f
        && Roads::DrivewayLift < Roads::GravelRoadLift && Roads::GravelRoadLift < Roads::RoadLift,
        "gravel roads and driveways aren't layered below paved roads");

    // A bend is smoothed: a right angle at (10, 0) is cut, so no sample passes within 1 m of the corner.
    Roads::Network bend;
    Roads::Line corner = driveway;
    corner.points = { XMFLOAT2(0, 0), XMFLOAT2(10, 0), XMFLOAT2(10, 10) };
    bend.lines.push_back(corner);
    const Roads::Mesh bentMesh = Roads::BuildMesh(bend, XMFLOAT3(0, 0, 0), flat);
    const auto& bent = bentMesh[Roads::Part::Gravel];
    Check(!bent.vertices.empty() && bent.vertices.size() % 3 == 0, "the bend's strip is missing");
    float nearest = 1e9f;
    for (size_t v = 1; v < bent.vertices.size(); v += 3)     // The centre column of the 3-column strip.
        nearest = std::min(nearest, std::hypot(bent.vertices[v].Pos.x - 10, bent.vertices[v].Pos.z));
    Check(nearest > 0.2f && nearest < 0.6f, "a right-angle bend wasn't smoothed, or moved too far");

    // Smoothing stays near the surveyed line however long its segments: a 100 m, 5 m, 100 m zigzag's corners move
    // under 0.6 m (before the split into 3 m pieces, the long segments moved them several meters).
    const std::vector<XMFLOAT2> zigzag = { XMFLOAT2(0, 0), XMFLOAT2(100, 0), XMFLOAT2(100, 5), XMFLOAT2(0, 60) };
    const auto smoothed = Roads::SmoothCenterline(zigzag);
    float worst = 0;
    for (const XMFLOAT2& corner : zigzag)
    {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < smoothed.size(); ++i)
        {
            const XMFLOAT2 a = smoothed[i], b = smoothed[i + 1];
            const float dx = b.x - a.x, dz = b.y - a.y, length2 = dx * dx + dz * dz;
            const float t = length2 > 0 ? std::max(0.0f, std::min(1.0f, ((corner.x - a.x) * dx + (corner.y - a.y) * dz) / length2)) : 0;
            best = std::min(best, std::hypot(corner.x - a.x - t * dx, corner.y - a.y - t * dz));
        }
        worst = std::max(worst, best);
    }
    Check(worst < 0.6f && smoothed.front().x == 0 && smoothed.back().y == 60, "smoothing moved a surveyed corner too far");

    // No ground for x in [10, 14): the strip ends before the gap and starts again after it, with no triangle across.
    Roads::Network gap;
    Roads::Line across = driveway;
    across.points = { XMFLOAT2(0, 0), XMFLOAT2(24, 0) };
    gap.lines.push_back(across);
    const auto holed = [](float x, float, float& y) { y = 50; return x < 10 || x >= 14; };
    const Roads::Mesh gapMesh = Roads::BuildMesh(gap, XMFLOAT3(0, 0, 0), holed);
    const auto& gapped = gapMesh[Roads::Part::Gravel];
    bool bridged = false, grounded = true;
    for (size_t i = 0; i + 2 < gapped.indices.size(); i += 3)
    {
        float low = 1e9f, high = -1e9f;
        for (int k = 0; k < 3; ++k)
        {
            const auto& p = gapped.vertices[gapped.indices[i + k]].Pos;
            low = std::min(low, p.x);
            high = std::max(high, p.x);
            grounded = grounded && std::abs(p.y - (50 + Roads::DrivewayLift)) < 1e-4f;
        }
        bridged = bridged || (low < 10 && high >= 14) || (low >= 10 && low < 14) || (high >= 10 && high < 14);
    }
    Check(!gapped.indices.empty() && !bridged && grounded, "a strip bridged ground it didn't have");

    // A slower road has no edge lines; a private paved road has no markings at all.

    // A slower road has no edge lines; a private paved road has no markings at all.
    network.lines.resize(1);
    network.lines[0].speedLimit = 25;
    Check(Roads::BuildMesh(network, XMFLOAT3(0, 0, 0), flat)[Roads::Part::WhiteLine].vertices.empty(), "a 25 mph road got edge lines");
    network.lines[0].owner = "private";
    Check(Roads::BuildMesh(network, XMFLOAT3(0, 0, 0), flat)[Roads::Part::YellowLine].vertices.empty(), "a private road got a center line");
}

void TestRoadGrading()
{
    // A 129 x 129 grid at 1 m (package coordinates -64..64) tilted across the road: ground = 10 + 0.1 z. A county
    // paved road along x at z = 0: 6 m wide, 1 m shoulders, ditches 0.5 m deep and 2.4 m wide.
    const int n = 129;
    const auto at = [&](std::vector<float>& h, float x, float z) -> float& { return h[size_t(z + 64) * n + size_t(x + 64)]; };
    std::vector<float> heights(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            heights[size_t(j) * n + i] = 10 + 0.1f * (j - 64);
    const std::vector<float> original = heights;
    Roads::Network network;
    Roads::Line road;
    road.surface = "paved";
    road.owner = "county";
    road.width = 6;
    road.shoulder = 1;
    road.ditchDepth = 0.5f;
    road.ditchWidth = 2.4f;
    road.points = { XMFLOAT2(-40, 0), XMFLOAT2(40, 0) };
    network.lines.push_back(road);
    const auto report = Roads::Grade(network, heights, n, 1);
    const auto equal = [](float a, float b) { return std::abs(a - b) < 2e-3f; };
    // The bed follows the ground along the road (10 here), crowned the same both ways despite the tilt.
    Check(equal(at(heights, 0, 0), 10) && equal(at(heights, 0, 2), 9.96f) && equal(at(heights, 0, -2), 9.96f),
        "the road bed isn't level across and crowned");
    // The traveled way's edge (3 m out, 9.94), and the shoulder 4% below it at its outer edge (4 m out, 9.90).
    Check(equal(at(heights, 0, 3), 9.94f) && equal(at(heights, 0, -4), 9.90f) && equal(at(heights, 0, 4), 9.90f),
        "the edges or shoulders are wrong");
    // Ditches: 1 m down the 1.2 m foreslope toward 0.5 m deep; on the backslope, 0.8 m of 1.2 m up from the bottom
    // (9.40) toward the ground, which is higher on the uphill side.
    Check(equal(at(heights, 0, 5), 9.90f - 0.5f / 1.2f) && equal(at(heights, 0, -5), 9.90f - 0.5f / 1.2f), "the foreslopes are wrong");
    Check(equal(at(heights, 0, 6), 9.40f + (10.6f - 9.40f) * (0.8f / 1.2f)) && equal(at(heights, 0, -6), 9.40f),
        "the backslopes are wrong");
    // Past the ditches, and past the road's ends, the ground is as surveyed.
    Check(at(heights, 0, 8) == at(const_cast<std::vector<float>&>(original), 0, 8) && at(heights, 0, -8) == at(const_cast<std::vector<float>&>(original), 0, -8)
        && at(heights, 50, 0) == at(const_cast<std::vector<float>&>(original), 50, 0), "grading reached past the road");
    Check(report.graded > 0 && report.ditched > 0 && report.deepestCut > 0.4f, "the grading report is empty");

    // Along the road: a 3 m bump 1 m high (all across x in [0, 2]) is planed off the bed.
    std::vector<float> bumpy(size_t(n) * n, 5);
    for (int j = 0; j < n; ++j)
        for (int i = 64; i <= 66; ++i)
            bumpy[size_t(j) * n + i] = 6;
    Roads::Network flat;
    Roads::Line lane = road;
    lane.ditchDepth = lane.ditchWidth = 0;
    flat.lines.push_back(lane);
    Roads::Grade(flat, bumpy, n, 1);
    Check(std::abs(at(bumpy, 1, 0) - 5) < 0.05f, "a short bump wasn't planed off the road bed");

    // A driveway crossing the road: on the road's traveled way the road wins (its crown), beyond it the driveway.
    std::vector<float> level(size_t(n) * n, 5);
    Roads::Network crossing = flat;
    Roads::Line driveway;
    driveway.kind = Roads::Kind::Driveway;
    driveway.surface = "gravel";
    driveway.width = 3;
    driveway.points = { XMFLOAT2(0, -20), XMFLOAT2(0, 20) };
    crossing.lines.push_back(driveway);
    Roads::Grade(crossing, level, n, 1);
    Check(equal(at(level, 0, 1), 5 - 0.02f) && equal(at(level, 0, 12), 5), "a driveway won over the road it crosses");

    // A grid finer than the centerline's samples (0.25 m) on a steady 20% grade along the road: the bed keeps the
    // grade from grid point to grid point, with no steps where one sample's stretch ends.
    const int fine = 129;
    const float step = 0.25f;
    const auto fineIndex = [&](float x, float z) { return size_t(std::lround(z / step + 64)) * fine + size_t(std::lround(x / step + 64)); };
    std::vector<float> graded(size_t(fine) * fine);
    for (int j = 0; j < fine; ++j)
        for (int i = 0; i < fine; ++i)
            graded[size_t(j) * fine + i] = 10 + 0.2f * (i - 64) * step;
    Roads::Network grade;
    Roads::Line uphill = lane;
    uphill.points = { XMFLOAT2(-15, 0), XMFLOAT2(15, 0) };
    grade.lines.push_back(uphill);
    Roads::Grade(grade, graded, fine, step);
    bool steady = true;
    for (float x = -5; x < 5; x += step)
        steady = steady && std::abs(graded[fineIndex(x + step, 0)] - graded[fineIndex(x, 0)] - 0.2f * step) < 0.005f;
    Check(steady, "the bed steps on a steady grade between centerline samples");

    // A right-angle turn on flat ground at 0.5 m: every grid point on the traveled way (0.5 to 2.9 m from the smoothed
    // centerline, where the crown lowers it) is graded, the outside of the bend included, and nothing past the ends.
    const float coarse = 0.5f;
    std::vector<float> turn(size_t(fine) * fine, 10);
    Roads::Network bend;
    Roads::Line corner = lane;
    corner.shoulder = 0;
    corner.points = { XMFLOAT2(-12, 0), XMFLOAT2(0, 0), XMFLOAT2(0, 12) };
    bend.lines.push_back(corner);
    Roads::Grade(bend, turn, fine, coarse);
    const auto smoothed = Roads::SmoothCenterline(corner.points);
    const auto distance = [&](float x, float z)
    {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < smoothed.size(); ++i)
        {
            const XMFLOAT2 a = smoothed[i], b = smoothed[i + 1];
            const float dx = b.x - a.x, dz = b.y - a.y, length2 = dx * dx + dz * dz;
            const float t = length2 > 0 ? std::max(0.0f, std::min(1.0f, ((x - a.x) * dx + (z - a.y) * dz) / length2)) : 0;
            best = std::min(best, std::hypot(x - a.x - t * dx, z - a.y - t * dz));
        }
        return best;
    };
    bool covered = true;
    for (int j = 0; j < fine; ++j)
        for (int i = 0; i < fine; ++i)
        {
            const float x = (i - 64) * coarse, z = (j - 64) * coarse;
            const float d = distance(x, z);   // On the traveled way, off the centerline (the crown is 0 on it).
            if (x > -11.5f && z < 11.5f && d > 0.5f && d < 2.9f)
                covered = covered && turn[size_t(j) * fine + i] < 10 - 1e-4f;
        }
    const auto turnAt = [&](float x, float z) { return turn[size_t(std::lround(z / coarse + 64)) * fine + size_t(std::lround(x / coarse + 64))]; };
    Check(covered && turnAt(0, -1.5f) < 10 - 1e-4f, "the outside of a bend was left ungraded");
    Check(turnAt(-12.5f, 0) == 10 && turnAt(0, 12.5f) == 10, "grading ran past a road's ends");

    // A 50% side slope falling to -z, a 6 m road with 0.5 m deep, 2 m wide ditches and no shoulder: the downhill side
    // runs continuously down to the ground, its ditch bottom above the falling ground, with no jump.
    std::vector<float> side(size_t(fine) * fine);
    for (int j = 0; j < fine; ++j)
        for (int i = 0; i < fine; ++i)
            side[size_t(j) * fine + i] = 10 + 0.5f * (j - 64) * step;
    Roads::Network slope;
    Roads::Line hillside = road;
    hillside.shoulder = 0;
    hillside.ditchDepth = 0.5f;
    hillside.ditchWidth = 2;
    hillside.points = { XMFLOAT2(-15, 0), XMFLOAT2(15, 0) };
    slope.lines.push_back(hillside);
    Roads::Grade(slope, side, fine, step);
    float steepest = 0;
    for (float z = -8; z < 0; z += step)
        steepest = std::max(steepest, std::abs(side[fineIndex(0, z + step)] - side[fineIndex(0, z)]));
    Check(steepest <= 0.5f * step * 2.5f, "the downhill ditch jumps to the ground");

    // An 80% side slope, falling faster than a 1:1.5 embankment: the fill never meets the ground on its own, so it
    // tapers to it by the cap. Across the cap there is no jump (before the taper, 3.9 m in one 0.25 m step).
    std::vector<float> cliff(size_t(fine) * fine);
    for (int j = 0; j < fine; ++j)
        for (int i = 0; i < fine; ++i)
            cliff[size_t(j) * fine + i] = 10 + 0.8f * (j - 64) * step;
    const std::vector<float> cliffGround = cliff;
    Roads::Grade(slope, cliff, fine, step);
    float sheer = 0;
    for (float z = -15.5f; z < 0; z += step)
        sheer = std::max(sheer, std::abs(cliff[fineIndex(0, z + step)] - cliff[fineIndex(0, z)]));
    Check(sheer <= 2 * step && cliff[fineIndex(0, -15)] == cliffGround[fineIndex(0, -15)],
        "the embankment ends in a cliff at its cap");

    // A 20 m wide, 5 m deep ditch on flat ground at 0.5 m: the backslope reaches the ground 23 m out, with no jump
    // where the search used to stop (21 m).
    std::vector<float> wide(size_t(fine) * fine, 10);
    Roads::Network canal;
    Roads::Line widened = hillside;
    widened.ditchDepth = 5;
    widened.ditchWidth = 20;
    canal.lines.push_back(widened);
    Roads::Grade(canal, wide, fine, coarse);
    float widest = 0;
    for (int j = 64; j < fine - 1; ++j)
        widest = std::max(widest, std::abs(wide[size_t(j + 1) * fine + 64] - wide[size_t(j) * fine + 64]));
    Check(widest <= 0.3f && wide[size_t(64 + 46) * fine + 64] == 10 && wide[size_t(64 + 40) * fine + 64] < 10,
        "a wide ditch stops short of the ground");
}

void TestPropEditing()
{
    // A roads file as import_roads.py writes it: header lines, a feature per line (CRLF here), Python's spacing.
    const std::string file =
        "{\r\n\"type\": \"FeatureCollection\",\r\n\"features\": [\r\n"
        "{\"type\": \"Feature\", \"properties\": {\"kind\": \"road\", \"id\": \"county-1\", \"source\": \"county\", \"edited\": false}, \"geometry\": {\"type\": \"LineString\", \"coordinates\": [[1000.0, 2000.0], [1100.5, 2000.25]]}},\r\n"
        "{\"type\": \"Feature\", \"properties\": {\"kind\": \"prop\", \"id\": \"prop-mailbox-a\", \"source\": \"generated\", \"edited\": false, \"prop\": \"mailbox\", \"heading\": 90.0, \"text\": \"12 E TEST RD\"}, \"geometry\": {\"type\": \"Point\", \"coordinates\": [1010.25, 2003.5]}},\r\n"
        "{\"type\": \"Feature\", \"properties\": {\"kind\": \"prop\", \"id\": \"prop-stop-b\", \"source\": \"generated\", \"edited\": false, \"prop\": \"stop-sign\", \"heading\": 0.0}, \"geometry\": {\"type\": \"Point\", \"coordinates\": [1050.0, 1996.0]}}\r\n"
        "]\r\n}\r\n";
    Roads::PropEdit moved;
    moved.id = "prop-mailbox-a";
    moved.point = DirectX::XMFLOAT2(12.344f, 5.006f);       // Package coordinates about (1000, 2000).
    moved.heading = 271.26f;
    Roads::PropEdit removed;
    removed.id = "prop-stop-b";
    removed.point = DirectX::XMFLOAT2(50, -4);
    removed.removed = true;
    const std::string updated = Roads::UpdateProps(file, { moved, removed }, 1000, 2000);
    // Only the two prop lines change; the moved one keeps its properties' order and text, gains edited, and its
    // point and heading round as import_roads.py rounds them; the removed one is marked; line endings and commas stay.
    const std::string movedLine = "{\"type\": \"Feature\", \"properties\": {\"kind\": \"prop\", \"id\": \"prop-mailbox-a\", "
        "\"source\": \"generated\", \"edited\": true, \"prop\": \"mailbox\", \"heading\": 271.3, \"text\": \"12 E TEST RD\"}, "
        "\"geometry\": {\"type\": \"Point\", \"coordinates\": [1012.34, 2005.01]}},\r\n";
    const std::string removedLine = "{\"type\": \"Feature\", \"properties\": {\"kind\": \"prop\", \"id\": \"prop-stop-b\", "
        "\"source\": \"generated\", \"edited\": true, \"prop\": \"stop-sign\", \"heading\": 0.0, \"removed\": true}, "
        "\"geometry\": {\"type\": \"Point\", \"coordinates\": [1050.0, 1996.0]}}\r\n";
    const size_t roadEnd = file.find("},\r\n", file.find("county-1")) + 4;
    Check(updated.compare(0, roadEnd, file, 0, roadEnd) == 0 && updated.find(movedLine) == roadEnd
        && updated.find(removedLine) == roadEnd + movedLine.size() && updated.substr(updated.size() - 6) == "]\r\n}\r\n",
        "a prop edit rewrote the roads file wrongly");
    // The result still parses, with the edits in it.
    const auto network = Roads::Parse(updated.substr(0, 1) + "\"crs\": {\"type\": \"name\", \"properties\": {\"name\": \"EPSG:32610\"}}, "
        + updated.substr(1), "EPSG:32610", 1000, 2000);
    Check(network.props.size() == 1 && std::abs(network.props[0].point.x - 12.34f) < 1e-3f && network.props[0].heading == 271.3f
        && network.props[0].edited, "an edited roads file doesn't parse back to its edits");
    // A prop that isn't in the file is an error, not a silent no-op.
    Roads::PropEdit missing;
    missing.id = "prop-mailbox-z";
    bool threw = false;
    try { Roads::UpdateProps(file, { missing }, 1000, 2000); }
    catch (const std::runtime_error&) { threw = true; }
    Check(threw, "editing a prop the file doesn't have succeeded");
    // A collection written on one line is refused (its line is the collection, not the prop's feature), not edited.
    const std::string oneLine = "{\"type\": \"FeatureCollection\", \"features\": [{\"type\": \"Feature\", \"properties\": "
        "{\"kind\": \"prop\", \"id\": \"prop-mailbox-a\", \"prop\": \"mailbox\"}, \"geometry\": {\"type\": \"Point\", "
        "\"coordinates\": [1010.0, 2003.0]}}]}\n";
    threw = false;
    try { Roads::UpdateProps(oneLine, { moved }, 1000, 2000); }
    catch (const std::runtime_error&) { threw = true; }
    Check(threw, "a one-line roads file was edited");
    // A feature whose id comes last in its properties (no comma after it), as the last line of the list.
    const std::string idLast = "{\"features\": [\n{\"type\": \"Feature\", \"properties\": {\"kind\": \"prop\", \"prop\": "
        "\"mailbox\", \"id\": \"prop-mailbox-a\"}, \"geometry\": {\"type\": \"Point\", \"coordinates\": [1010.0, 2003.0]}}\n]}\n";
    const std::string idLastUpdated = Roads::UpdateProps(idLast, { moved }, 1000, 2000);
    Check(idLastUpdated.find("\"id\": \"prop-mailbox-a\", \"edited\": true, \"heading\": 271.3}, \"geometry\": {\"type\": "
        "\"Point\", \"coordinates\": [1012.34, 2005.01]}}\n]}") != std::string::npos, "a prop with its id last wasn't edited");

    // Saving safely: a staged file is complete beside its target until it's published over it; staging beside a
    // missing folder fails, leaving the target alone.
    // In a folder of this run's own, so concurrent test runs don't share files.
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    std::wstring own;
    for (int attempt = 0; attempt < 100 && own.empty(); ++attempt)
    {
        const std::wstring candidate = std::wstring(temp) + L"tree-roads-save-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        if (CreateDirectoryW(candidate.c_str(), nullptr))
            own = candidate + L"\\";
    }
    Check(!own.empty(), "the save test couldn't make its folder");
    const std::wstring folder = own;
    const std::string target = SceneData::Utf8Path(folder) + "roads.geojson";
    {
        std::ofstream original(SceneData::WidePath(target), std::ios::binary | std::ios::trunc);
        original << "old";
    }
    const auto contents = [&](const std::string& path)
    {
        std::ifstream input(SceneData::WidePath(path), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    };
    const std::wstring staged = SceneData::StageFile(target, "new text");
    Check(contents(target) == "old" && contents(SceneData::Utf8Path(staged)) == "new text", "a staged file replaced its target early");
    SceneData::PublishFile(staged, target);
    Check(contents(target) == "new text" && GetFileAttributesW(staged.c_str()) == INVALID_FILE_ATTRIBUTES,
        "publishing a staged file didn't replace its target");
    threw = false;
    try { SceneData::StageFile(SceneData::Utf8Path(folder) + "no-such-folder-for-tree-tests\\roads.geojson", "x"); }
    catch (const std::runtime_error&) { threw = true; }
    Check(threw && contents(target) == "new text", "staging into a missing folder succeeded");
    DeleteFileW(SceneData::WidePath(target).c_str());
    RemoveDirectoryW(folder.c_str());

    // Mirroring a save into the build's source: only for the build's copy of a relative path inside both folders.
    const std::wstring build = L"C:\\work\\TreeEngine\\TreeClassic\\bin\\x64\\Release";
    const auto onlySource = [](const std::wstring& path)
    {
        return _wcsicmp(path.c_str(), L"C:\\work\\TreeEngine\\Game\\Resources\\Terrain\\A.roads.geojson") == 0
            || _wcsicmp(path.c_str(), L"C:\\work\\Shared\\A.roads.geojson") == 0
            || _wcsicmp(path.c_str(), L"C:\\work\\TreeEngine\\Game\\Resources\\Terrain\\Caf\u00e9.roads.geojson") == 0;
    };
    Check(Roads::BuildSource(build + L"\\Terrain\\A.roads.geojson", L"Terrain/A.roads.geojson", build, onlySource)
        == L"C:\\work\\TreeEngine\\Game\\Resources\\Terrain\\A.roads.geojson", "the build's copy has no source");
    // A scene's own copy (loaded from beside the scene, not the build) is saved alone.
    Check(Roads::BuildSource(L"C:\\scenes\\Terrain\\A.roads.geojson", L"Terrain/A.roads.geojson", build, onlySource).empty(),
        "a scene's own roads file was mirrored into the build's source");
    // A path escaping the folders (../Shared, which resolves beside the build and beside Game\\Resources, where a file
    // with the same props exists) or absolute is never mirrored.
    Check(Roads::BuildSource(L"C:\\work\\TreeEngine\\TreeClassic\\bin\\x64\\Shared\\A.roads.geojson", L"../Shared/A.roads.geojson",
        build, onlySource).empty() && Roads::BuildSource(L"C:\\work\\Shared\\A.roads.geojson", L"C:/work/Shared/A.roads.geojson",
        build, onlySource).empty() && Roads::BuildSource(build + L"\\Terrain\\A.roads.geojson", L"Terrain/./A.roads.geojson",
        build, onlySource).empty(), "an escaping or absolute roads path was mirrored");
    // A name beyond ASCII: the scene's UTF-8 path, decoded as the asset loader decodes it (é is C3 A9).
    Check(Roads::BuildSource(build + L"\\Terrain\\Caf\u00e9.roads.geojson", SceneData::WidePath("Terrain/Caf\xc3\xa9.roads.geojson"),
        build, onlySource) == L"C:\\work\\TreeEngine\\Game\\Resources\\Terrain\\Caf\u00e9.roads.geojson",
        "an accented roads file's source wasn't found");

    // Picking: a ray hits a box from outside at its entry distance, misses beside it, and from inside starts at 0.
    const DirectX::XMFLOAT3 low(-1, 0, -1), high(1, 3, 1);
    Check(std::abs(Roads::RayBox({ -5, 1, 0 }, { 1, 0, 0 }, low, high) - 4) < 1e-5f
        && Roads::RayBox({ -5, 5, 0 }, { 1, 0, 0 }, low, high) < 0 && Roads::RayBox({ 5, 1, 0 }, { 1, 0, 0 }, low, high) < 0
        && Roads::RayBox({ 0, 1, 0 }, { 0, 0, 1 }, low, high) == 0, "a prop pick ray tested its box wrongly");
}

void TestSignText()
{
    // The font draws every letter and digit, upper or lower case, and the punctuation street names use.
    const std::string drawn = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-'/&";
    bool all = true;
    for (char c : drawn)
        all = all && SignText::Drawn(c);
    Check(all && !SignText::Drawn('#'), "the sign font misses a character a street name uses");
    Check(SignText::Width("") == 0 && SignText::Width("E") < SignText::Width("E SCOTT DR")
        && SignText::Width("i") == SignText::Width("I"), "sign text widths are wrong");
    // A short name fits the model's 0.9 m blade; a longer one gets a longer blade, up to 1.2 m; a longer one still is
    // squeezed to fit that.
    Check(SignText::BladeLength("E ROSE LN") == SignText::ModelBladeLength && SignText::Squeeze("E ROSE LN") == 1,
        "a short sign name doesn't fit the model's blade");
    const float scott = SignText::BladeLength("E SCOTT DR");
    Check(scott > SignText::ModelBladeLength && scott < SignText::LongestBlade && SignText::Squeeze("E SCOTT DR") == 1,
        "a medium sign name's blade is wrong");
    Check(SignText::BladeLength("E EVERGREEN WAY EAST") == SignText::LongestBlade && SignText::Squeeze("E EVERGREEN WAY EAST") < 1
        && SignText::Squeeze("E EVERGREEN WAY EAST") > 0.6f, "a long sign name isn't squeezed onto the longest blade");

    // Blade materials: exactly the two faces and two edges; a replacement model's other materials are left alone.
    bool face = true;
    Check(SignText::BladeMaterial("sign-blade-lower", face) == 0 && face
        && SignText::BladeMaterial("sign-blade-upper-edge", face) == 1 && !face
        && SignText::BladeMaterial("sign-blade-lower-bracket", face) == -1 && !face
        && SignText::BladeMaterial("steel", face) == -1 && !face, "a sign blade material was misclassified");

    // Three names: two cells across, two rows, each region a padded cell inside the atlas.
    const auto atlas = SignText::Build({ "E SCOTT DR", "E BENSON LOOP", "" });
    const int strideX = SignText::CellWidth + 2 * SignText::Padding, strideY = SignText::CellHeight + 2 * SignText::Padding;
    Check(atlas.width == 2 * strideX && atlas.height == 2 * strideY && atlas.regions.size() == 3
        && atlas.pixels.size() == size_t(atlas.width) * atlas.height * 3, "the sign atlas has the wrong size");
    const auto& second = atlas.regions[1];
    Check(std::abs(second.x - float(strideX + SignText::Padding) / atlas.width) < 1e-6f
        && std::abs(second.y - float(SignText::Padding) / atlas.height) < 1e-6f
        && std::abs(second.z - float(SignText::CellWidth) / atlas.width) < 1e-6f
        && std::abs(second.w - float(SignText::CellHeight) / atlas.height) < 1e-6f, "a sign cell's region is wrong");
    const auto pixel = [&](int x, int y) { return &atlas.pixels[(size_t(y) * atlas.width + x) * 3]; };
    // The first cell: green padding, a white border band inside the edge, white letters across its middle row, and
    // green face beside them. The empty third cell is a plain green face inside its border.
    const int x0 = SignText::Padding, y0 = SignText::Padding, middle = y0 + SignText::CellHeight / 2;
    Check(pixel(0, 0)[1] < 120 && pixel(x0 + 5, middle)[0] > 200, "a sign cell's padding or border is wrong");
    int white = 0;
    for (int x = x0 + 20; x < x0 + SignText::CellWidth - 20; ++x)
        white += pixel(x, middle)[0] > 200;
    Check(white > 20 && white < SignText::CellWidth / 2, "a sign cell's letters are missing or cover it");
    bool blank = true;
    for (int x = x0 + 20; x < x0 + SignText::CellWidth - 20; ++x)
        blank = blank && pixel(x, strideY + middle)[0] < 50;
    Check(blank, "an empty sign cell has letters");
    // The BMP: its header, and rows bottom-up as BGR.
    const auto bmp = atlas.Bmp();
    const auto u32 = [&](size_t at) { return uint32_t(bmp[at]) | uint32_t(bmp[at + 1]) << 8 | uint32_t(bmp[at + 2]) << 16 | uint32_t(bmp[at + 3]) << 24; };
    Check(bmp.size() == 54 + size_t(atlas.width) * 3 * atlas.height && bmp[0] == 'B' && bmp[1] == 'M'
        && u32(2) == bmp.size() && u32(18) == uint32_t(atlas.width) && u32(22) == uint32_t(atlas.height) && bmp[28] == 24,
        "the sign atlas BMP header is wrong");
    const uint8_t* topLeft = &bmp[54 + size_t(atlas.height - 1) * atlas.width * 3];
    Check(topLeft[0] == pixel(0, 0)[2] && topLeft[1] == pixel(0, 0)[1] && topLeft[2] == pixel(0, 0)[0],
        "the sign atlas BMP's rows or channels are out of order");
}

void TestTrails()
{
    // Parsing: a trail is 1 m wide unless it says otherwise (narrower than any road allows), with no surface rule.
    const std::string file = R"({"type":"FeatureCollection",
        "crs":{"type":"name","properties":{"name":"EPSG:32610"}},
        "features":[
          {"type":"Feature","properties":{"kind":"trail","id":"trail-1"},
           "geometry":{"type":"LineString","coordinates":[[1000,2000],[1030,2010]]}},
          {"type":"Feature","properties":{"kind":"trail","id":"trail-2","width":0.6,"surface":"bark"},
           "geometry":{"type":"LineString","coordinates":[[1000,2020],[1030,2020]]}}]})";
    const auto network = Roads::Parse(file, "EPSG:32610", 1000, 2000);
    Check(network.lines.size() == 2 && network.lines[0].kind == Roads::Kind::Trail && network.lines[0].width == Roads::TrailWidth
        && network.lines[1].width == 0.6f, "trails were parsed wrongly");
    // The documented minimum, 0.3 m, is accepted exactly; anything narrower isn't.
    auto narrow = Json::parse(file);
    narrow["features"][1]["properties"]["width"] = 0.3;
    Check(Roads::Parse(narrow.dump(), "EPSG:32610", 1000, 2000).lines[1].width == 0.3f, "a 0.3 m trail was refused");
    narrow["features"][1]["properties"]["width"] = 0.29;
    bool narrowRefused = false;
    try { Roads::Parse(narrow.dump(), "EPSG:32610", 1000, 2000); }
    catch (const std::runtime_error& error) { narrowRefused = std::string(error.what()).find("width") != std::string::npos; }
    Check(narrowRefused, "a trail narrower than 0.3 m was accepted");
    bool threw = false;
    try { Roads::Parse(std::string(file).replace(file.find("\"kind\":\"trail\""), 14, "\"kind\":\"path\""), "EPSG:32610", 1000, 2000); }
    catch (const std::runtime_error& error) { threw = std::string(error.what()).find("road, driveway, trail or prop") != std::string::npos; }
    Check(threw, "an unknown kind's error doesn't list trails");

    // The mesh: trails go in their own part, on the ground at the trail lift, as wide as they say; nothing else.
    Roads::Network trails;
    Roads::Line trail;
    trail.kind = Roads::Kind::Trail;
    trail.width = 1;
    trail.points = { XMFLOAT2(0, 0), XMFLOAT2(20, 0) };
    trails.lines.push_back(trail);
    const auto flat = [](float, float, float& y) { y = 2; return true; };
    const auto mesh = Roads::BuildMesh(trails, XMFLOAT3(0, 0, 0), flat);
    const auto& tread = mesh[Roads::Part::Trail];
    float minZ = 1e9f, maxZ = -1e9f;
    bool level = true;
    for (const auto& v : tread.vertices)
    {
        minZ = std::min(minZ, v.Pos.z);
        maxZ = std::max(maxZ, v.Pos.z);
        level = level && std::abs(v.Pos.y - (2 + Roads::TrailLift)) < 1e-5f;
    }
    Check(!tread.indices.empty() && level && std::abs(maxZ - minZ - 1) < 1e-4f && mesh[Roads::Part::Asphalt].vertices.empty()
        && mesh[Roads::Part::Gravel].vertices.empty() && mesh[Roads::Part::YellowLine].vertices.empty(),
        "a trail's mesh is wrong");
    Check(Roads::TrailLift < Roads::DrivewayLift, "trails would show over driveways");
    // On a terrain, trails are built by TrailBuilder: the ground's own triangles clipped to each trail's outline, each
    // the trail lift above its plane, for any level's grid. They follow that level's ground exactly.
    // The surface height of a mesh part at (x, z): the highest triangle there, or a very low value where none is.
    const auto surfaceAt = [](const Roads::MeshPart& part, float x, float z)
    {
        float best = -1e9f;
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3)
        {
            const XMFLOAT3& a = part.vertices[part.indices[i]].Pos;
            const XMFLOAT3& b = part.vertices[part.indices[i + 1]].Pos;
            const XMFLOAT3& c = part.vertices[part.indices[i + 2]].Pos;
            const float d = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
            if (std::abs(d) < 1e-9f)
                continue;
            const float u = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / d;
            const float v = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / d;
            if (u < -1e-5f || v < -1e-5f || u + v > 1 + 1e-5f)
                continue;
            best = std::max(best, a.y + (b.y - a.y) * u + (c.y - a.y) * v);
        }
        return best;
    };
    // A ridge along z (so the terrain's triangles are exactly its profile), `peak` high at x = 1, sampled on a grid of
    // `spacing`; the trail runs along x from `from`. Everywhere across it, it is the grid's ground plus the trail lift.
    const auto ridgeCase = [&](float peak, float from, float spacing, const char* what)
    {
        Roads::GroundGrid grid;
        grid.spacing = spacing;
        // The ground the grid draws: the ridge at its nodes, linear between them.
        const auto nodes = [peak](float x, float, float& y) { y = peak * std::max(0.0f, 1 - std::abs(x - 1)); return true; };
        const auto drawn = [&](float x)
        {
            const float i = std::floor(x / spacing);
            float a = 0, b = 0;
            nodes(i * spacing, 0, a);
            nodes((i + 1) * spacing, 0, b);
            return a + (b - a) * (x / spacing - i);
        };
        Roads::Network network;
        Roads::Line line = trail;
        line.points = { XMFLOAT2(from, 0), XMFLOAT2(from + 2, 0) };
        network.lines.push_back(line);
        const auto part = Roads::TrailBuilder(network, XMFLOAT3(0, 0, 0)).Build(grid, -20, -20, 20, 20, nodes);
        float worst = 0, covered = 1e9f;
        for (float x = from + 0.01f; x <= from + 1.99f; x += 0.01f)
            for (float z = -0.45f; z <= 0.45f; z += 0.15f)
            {
                const float y = surfaceAt(part, x, z);
                covered = std::min(covered, y);
                worst = std::max(worst, std::abs(y - (drawn(x) + Roads::TrailLift)));
            }
        Check(covered > -1 && worst < 1e-4f, (std::string(what) + ": the trail doesn't follow the ground at its lift").c_str());
    };
    ridgeCase(0.2f, 0.5f, 1, "a 0.2 m ridge between cross-sections");
    ridgeCase(1.0f, 0.4375f, 1, "a 1 m crest off the cross-sections");
    // A coarser level (2 m between samples, as the terrain draws further off): the trail follows that level's
    // triangles, not the finest ground's.
    ridgeCase(0.2f, 0.5f, 2, "a coarser level's ground");
    // A window: only the cells inside it (a terrain tile).
    {
        Roads::Network network;
        Roads::Line line = trail;
        line.points = { XMFLOAT2(-10, 0.5f), XMFLOAT2(10, 0.5f) };
        network.lines.push_back(line);
        Roads::GroundGrid grid;
        grid.spacing = 1;
        const auto part = Roads::TrailBuilder(network, XMFLOAT3(0, 0, 0)).Build(grid, 0, -4, 3, 3, flat);
        float lowX = 1e9f, highX = -1e9f;
        for (const auto& v : part.vertices)
        {
            lowX = std::min(lowX, v.Pos.x);
            highX = std::max(highX, v.Pos.x);
        }
        Check(!part.indices.empty() && lowX >= -1e-4f && highX <= 4 + 1e-4f, "a trail tile reaches past its window");
    }
    // Roads and driveways show over trails everywhere: the trail leaves out what a driveway covers, here a driveway
    // crossing the trail on the ridge, along z at x = 0.5 (3 m wide: x from -1 to 2).
    {
        const auto ridge = [](float x, float, float& y) { y = 0.2f * std::max(0.0f, 1 - std::abs(x - 1)); return true; };
        Roads::Network network;
        Roads::Line line = trail;
        line.points = { XMFLOAT2(-3, 0), XMFLOAT2(4, 0) };
        network.lines.push_back(line);
        Roads::Line crossing;
        crossing.kind = Roads::Kind::Driveway;
        crossing.surface = "gravel";
        crossing.width = 3;
        crossing.points = { XMFLOAT2(0.5f, -5), XMFLOAT2(0.5f, 5) };
        network.lines.push_back(crossing);
        Roads::GroundGrid grid;
        grid.spacing = 1;
        const auto part = Roads::TrailBuilder(network, XMFLOAT3(0, 0, 0)).Build(grid, -20, -20, 20, 20, ridge);
        bool underDriveway = false, beside = true;
        for (float x = -2.9f; x <= 3.9f; x += 0.05f)
            for (float z = -0.45f; z <= 0.45f; z += 0.15f)
            {
                const bool covered = surfaceAt(part, x, z) > -1;
                if (x > -0.99f && x < 1.99f)
                    underDriveway = underDriveway || covered;
                else if (x < -1.01f || x > 2.01f)
                    beside = beside && covered;
            }
        Check(!underDriveway && beside, "a trail shows under a driveway crossing it, or is missing beside it");

        // The same 8192 m out (world coordinates of a large terrain): the trail's and the driveway's outlines keep
        // their area, so the trail is there beside the driveway and not under it.
        Roads::GroundGrid distant;
        distant.spacing = 1;
        distant.originX = distant.originZ = 8192;
        const auto farRidge = [&](float x, float z, float& y) { return ridge(x - 8192, z - 8192, y); };
        const auto farPart = Roads::TrailBuilder(network, XMFLOAT3(8192, 0, 8192)).Build(distant, -20, -20, 20, 20, farRidge);
        bool farUnder = false, farBeside = true;
        for (float x = -2.9f; x <= 3.9f; x += 0.05f)
        {
            const bool covered = surfaceAt(farPart, 8192 + x, 8192) > -1;
            if (x > -0.99f && x < 1.99f)
                farUnder = farUnder || covered;
            else if (x < -1.01f || x > 2.01f)
                farBeside = farBeside && covered;
        }
        Check(!farUnder && farBeside, "8 km out, a trail's or a driveway's outline lost its area");
    }
    // Which levels draw trails: the finest always, however coarse its samples; others up to 8 m between samples.
    Check(Roads::DrawsTrails(10, true) && !Roads::DrawsTrails(10, false) && Roads::DrawsTrails(8, false)
        && Roads::DrawsTrails(1, true), "the levels that draw trails are wrong");
    // Only road and driveway outlines that reach a trail are kept to cut it.
    {
        Roads::Network network;
        Roads::Line line = trail;
        line.points = { XMFLOAT2(0, 0), XMFLOAT2(10, 0) };
        network.lines.push_back(line);
        Roads::Line farDriveway;
        farDriveway.kind = Roads::Kind::Driveway;
        farDriveway.width = 3;
        farDriveway.points = { XMFLOAT2(1000, 0), XMFLOAT2(1000, 20) };
        network.lines.push_back(farDriveway);
        Check(Roads::TrailBuilder(network, XMFLOAT3(0, 0, 0)).Covers() == 0, "a driveway 1 km from any trail was kept to cut trails");
        Roads::Line nearDriveway = farDriveway;
        nearDriveway.points = { XMFLOAT2(5, -10), XMFLOAT2(5, 10) };
        network.lines.push_back(nearDriveway);
        Check(Roads::TrailBuilder(network, XMFLOAT3(0, 0, 0)).Covers() > 0, "a driveway crossing a trail wasn't kept to cut it");
    }
    // Many roads far from many trails: choosing the covers is local (buckets), not every road piece against every
    // trail piece (20 km of each, 400 million pairs, took many seconds).
    {
        Roads::Network network;
        for (int k = 0; k < 20; ++k)
        {
            Roads::Line road;
            road.kind = Roads::Kind::Road;
            road.points = { XMFLOAT2(0, k * 20.0f), XMFLOAT2(1000, k * 20.0f) };
            network.lines.push_back(road);
            Roads::Line path = trail;
            path.points = { XMFLOAT2(5000, k * 20.0f), XMFLOAT2(6000, k * 20.0f) };
            network.lines.push_back(path);
        }
        const auto start = std::chrono::steady_clock::now();
        const Roads::TrailBuilder builder(network, XMFLOAT3(0, 0, 0));
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(builder.Covers() == 0 && seconds < 5, ("choosing trail covers took " + std::to_string(seconds) + " s").c_str());
    }
    // Grading leaves trails alone: they lie on the ground as it is.
    const int n = 65;
    std::vector<float> bumpy(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            bumpy[size_t(j) * n + i] = 10 + 0.5f * std::sin(i * 0.7f) * std::cos(j * 0.4f);
    const auto before = bumpy;
    Roads::Network graded;
    Roads::Line crossing = trail;
    crossing.points = { XMFLOAT2(-20, -5), XMFLOAT2(20, 5) };
    graded.lines.push_back(crossing);
    const auto report = Roads::Grade(graded, bumpy, n, 1);
    Check(report.graded == 0 && bumpy == before, "grading changed the ground under a trail");
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
        TestTerrainPackage();
        TestTreeLodSettings();
        TestTerrainSettings();
        TestWaterSettings();
        TestTide();
        TestTideSettings();
        TestRoadNetwork();
        TestRoadMesh();
        TestRoadGrading();
        TestTrails();
        TestSignText();
        TestPropEditing();
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

        // The south Puget Sound species (Tools/forest/make_pnw_plants.py writes the scene): each an L-system tree with
        // a bark and a leaf material, its leaves drawn from regions of its atlas.
        {
            const auto pnw = SceneData::ReadScene(TestDataFilename("pnw-plants.scene.json"));
            const char* species[] = { "douglas-fir", "western-hemlock", "western-red-cedar", "red-alder", "bigleaf-maple",
                "pacific-madrone", "vine-maple", "red-huckleberry", "himalayan-blackberry", "salal", "oregon-grape",
                "sword-fern", "pasture-grass", "douglas-fir-stand", "pacific-madrone-bluff", "lichen-snag",
                "western-red-cedar-sapling", "western-hemlock-sapling", "bracken-fern", "oceanspray", "oceanspray-plumes",
                "orchard-grass", "orchard-grass-seed" };
            for (const char* id : species)
            {
                const SceneData::TreeDefinition* found = nullptr;
                for (const auto& object : pnw.objects)
                    if (object->id == id && object->instanceOf.empty())
                        found = dynamic_cast<const SceneData::TreeDefinition*>(object.get());
                const std::string what = std::string("PNW plants: ") + id;
                Check(found != nullptr, (what + " is missing or not a tree").c_str());
                if (!found)
                    continue;
                Check(!found->generator.axiom.empty() && found->hasMaterials && !found->leaf.baseColorTexture.source.empty(),
                    (what + " lacks a generator or materials").c_str());
                const auto atlas = pnw.atlases.find(found->leaves.atlas);
                bool regionsFound = atlas != pnw.atlases.end() && !found->leaves.regions.empty();
                for (const auto& region : found->leaves.regions)
                    regionsFound = regionsFound && atlas->second.regions.count(region.region);
                Check(regionsFound, (what + "'s leaf regions aren't in its atlas").c_str());
                // Trees draw half a metre below their position: plants stand at 0.5 on the meadow.
                Check(std::abs(found->transform.position.y - 0.5f) < 1e-6f, (what + " isn't standing on the ground").c_str());
            }
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

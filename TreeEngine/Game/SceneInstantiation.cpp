#include "pch.h"
#include "SceneInstantiation.h"
#include "SceneRenderSettings.h"
#include "GameLoader.h"
#include "SceneRoot.h"
#include "Primitive.h"
#include "Tree.h"
#include "Graph.h"
#include "GltfModel.h"
#include "LSystemModelGenerator.h"
#include "Player.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>

namespace
{
    const char* const SkyObjectName = "sky";

    PrimitiveType RuntimeShape(SceneData::Shape shape)
    {
        switch (shape)
        {
        case SceneData::Shape::Box: return PrimitiveType_Box;
        case SceneData::Shape::Cylinder: return PrimitiveType_Cylinder;
        case SceneData::Shape::CylinderLD: return PrimitiveType_CylinderLD;
        case SceneData::Shape::CylinderHD: return PrimitiveType_CylinderHD;
        case SceneData::Shape::SkinnedCylinder: return PrimitiveType_SkinnedCylinder;
        case SceneData::Shape::Sprite: return PrimitiveType_Sprite;
        case SceneData::Shape::Sphere: return PrimitiveType_Sphere;
        }
        throw std::runtime_error("Unsupported scene shape");
    }

    // Texture maps of a PBR material with their roles and color spaces (2 = sRGB, 1 = linear).
    struct PbrTextureMap
    {
        const SceneData::TextureMapDefinition* map;
        MaterialTextureRole role;
        int colorSpace;
    };

    std::vector<PbrTextureMap> PbrTextureMaps(const SceneData::MaterialDefinition& material)
    {
        return {
            { &material.baseColorTexture, BaseColorTextureRole, 2 },
            { &material.metallicRoughnessTexture, MetallicRoughnessTextureRole, 1 },
            { &material.normalTexture, NormalTextureRole, 1 },
            { &material.specularTexture, SpecularStrengthTextureRole, 1 },
            { &material.specularColorTexture, SpecularColorTextureRole, 2 } };
    }

    // The component of a role's clamp vector that holds its clamp flag (see ShaderMaterial).
    float& ClampFlag(ShaderMaterial& material, MaterialTextureRole role)
    {
        switch (role)
        {
        case BaseColorTextureRole: return material.specularColor.w;
        case MetallicRoughnessTextureRole: return material.metallicRoughnessTextures.y;
        case NormalTextureRole: return material.normalTextures.z;
        case SpecularStrengthTextureRole: return material.specularTextures.z;
        case SpecularColorTextureRole: return material.specularTextures.w;
        default: throw std::runtime_error("Unsupported material texture role");
        }
    }

    ShaderMaterial RuntimeMaterial(const SceneData::MaterialDefinition& definition)
    {
        PbrMaterialFactors factors;
        factors.baseColor = definition.baseColor;
        factors.metallic = definition.metallic;
        factors.roughness = definition.roughness;
        factors.specular = definition.specular;
        factors.specularColor = definition.specularColor;
        factors.ior = definition.ior;
        factors.transmission = definition.transmission;
        factors.alphaMode = definition.alphaMode == SceneData::AlphaMode::Mask ? PbrAlphaMode::Mask
            : definition.alphaMode == SceneData::AlphaMode::Blend ? PbrAlphaMode::Blend
            : PbrAlphaMode::Opaque;
        factors.alphaCutoff = definition.alphaCutoff;
        factors.unlit = definition.unlit;
        bool alphaBlend = false;
        ShaderMaterial result = BuildPbrShaderMaterial(factors, alphaBlend);
        for (const auto& entry : PbrTextureMaps(definition))
        {
            const auto& map = *entry.map;
            if (map.source.empty()) continue;
            if (map.hasTransform) SetMaterialUvTransform(result, entry.role, map.offset, map.rotation, map.scale);
            ClampFlag(result, entry.role) = map.wrap ? 0.0f : 1.0f;
        }
        if (!definition.normalTexture.source.empty()) result.normalTextures.y = definition.normalScale;
        result.flags.y = definition.baseColorTexture.source.empty() ? 0.0f : 1.0f;
        result.ApplyTextureCoordScale(definition.textureCoordScale.x, definition.textureCoordScale.y);
        return result;
    }

    std::wstring RuntimeTexture(const std::string& texture, const std::string& objectId, const std::string& sceneFilename)
    {
        if (texture.empty()) return {};
        try { return SceneData::WidePath(SceneData::ResolveSceneAsset(texture, sceneFilename)); }
        catch (const std::runtime_error& error)
        {
            throw std::runtime_error("Object '" + objectId + "': " + error.what());
        }
    }

    MaterialTextureSources RuntimeMaterialTextures(const SceneData::MaterialDefinition& definition,
        const std::string& objectId, const std::string& sceneFilename)
    {
        MaterialTextureSources result;
        for (const auto& entry : PbrTextureMaps(definition))
        {
            if (entry.map->source.empty()) continue;
            const auto filename = RuntimeTexture(entry.map->source, objectId, sceneFilename);
            try
            {
                result.roles[entry.role] = LoadSharedMaterialTextureFile(filename, entry.colorSpace);
            }
            catch (const std::runtime_error& error)
            {
                throw std::runtime_error("Object '" + objectId + "': " + error.what());
            }
        }
        return result;
    }

    std::string RuntimeDataFile(const std::string& filename, const std::string& objectId, const std::string& sceneFilename)
    {
        try { return SceneData::ResolveSceneAsset(filename, sceneFilename); }
        catch (const std::runtime_error& error)
        {
            throw std::runtime_error("Object '" + objectId + "': " + error.what());
        }
    }

    std::vector<XMFLOAT2> LoadGraphPoints(const std::string& filename, const std::string& objectId, const std::string& sceneFilename)
    {
        std::ifstream input(SceneData::WidePath(RuntimeDataFile(filename, objectId, sceneFilename)).c_str());
        std::vector<XMFLOAT2> values;
        float maxX = 0;
        std::string line;
        while (std::getline(input, line))
        {
            std::stringstream fields(line);
            std::string time;
            std::string value;
            if (!std::getline(fields, time, ',') || !std::getline(fields, value, ','))
                throw std::runtime_error("Object '" + objectId + "': malformed graph row");
            float x = static_cast<float>(std::atof(time.c_str()));
            float y = static_cast<float>(std::atof(value.c_str()));
            values.emplace_back(x, y);
            maxX = std::max(maxX, x);
        }
        if (values.size() < 2 || maxX <= 0 || values[1].x == 0)
            throw std::runtime_error("Object '" + objectId + "': graph data requires at least two positive time samples");

        std::vector<XMFLOAT2> points;
        float xScale = 10.0f / maxX;
        float xDelta = .5f / values[1].x;
        for (size_t i = 1; i < values.size(); ++i)
            points.emplace_back(values[i].x * xScale, values[i].y * xDelta);
        return points;
    }

    std::shared_ptr<GltfAsset> RuntimeModel(const std::string& filename, const std::string& objectId, const std::string& sceneFilename)
    {
        static std::map<std::string, std::weak_ptr<GltfAsset>> cache;
        std::string resolved = RuntimeDataFile(filename, objectId, sceneFilename);
        auto existing = cache.find(resolved);
        if (existing != cache.end())
        {
            auto asset = existing->second.lock();
            if (asset) return asset;
        }
        auto asset = LoadGltfAsset(resolved);
        cache[resolved] = asset;
        return asset;
    }

    void CommonParameters(const SceneData::ObjectDefinition& definition, WorldObjectParams& parameters)
    {
        parameters.name = definition.id;
        parameters.position = definition.transform.position;
        parameters.rotation = definition.transform.rotation;
        parameters.scale = definition.transform.scale;
        parameters.textureCoordScale = definition.textureCoordScale;
        parameters._animationSpeed = definition.animationSpeed;
        parameters.depthLOD = definition.depthLOD;
    }

    double RandomSegmentLength(LSystemParams* parameters, double)
    {
        return parameters->_segmentLength + rand() / double(RAND_MAX) * 0.2;
    }

    template<class T>
    void ApplyTreeMaterials(const T& tree, WorldObjectParams& parameters, const std::string& sceneFilename)
    {
        if (!tree.hasMaterials) return;
        parameters.textureFilename.push_back(L"");
        parameters.textureFilename.push_back(L"");
        parameters.materials.push_back(RuntimeMaterial(tree.trunk));
        parameters.materials.push_back(RuntimeMaterial(tree.leaf));
        parameters.materialTextures.push_back(RuntimeMaterialTextures(tree.trunk, tree.id, sceneFilename));
        parameters.materialTextures.push_back(RuntimeMaterialTextures(tree.leaf, tree.id, sceneFilename));
    }
}

void InstantiateScene(const SceneData::SceneDefinition& definition, SceneRoot& scene,
    SceneRenderSettings& renderSettings, Player& player, GameData& gameData, const std::string& sceneFilename)
{
    std::vector<std::unique_ptr<WorldObject>> objects;
    if (!definition.skyCubeMap.empty())
    {
        // The sky is drawn as a camera-centered cube first so later geometry covers it.
        // Its inverted scale matches how the cube map faces are authored.
        auto parameters = std::make_unique<WorldObjectParams>(PrimitiveGeneratorType);
        parameters->name = SkyObjectName;
        parameters->scale = XMFLOAT3(30, -30, 30);
        XMStoreFloat4(&parameters->rotation, XMQuaternionRotationRollPitchYaw(0, XMConvertToRadians(definition.skyRotationDegrees), 0));
        parameters->primitiveType = PrimitiveType_Box;
        parameters->cubeMap = true;
        ShaderMaterial material;
        material.flags = XMFLOAT4(0, 0, 0, 1);
        parameters->materials.push_back(material);
        parameters->textureFilename.push_back(RuntimeTexture(definition.skyCubeMap, "render.sky", sceneFilename));
        auto runtime = std::make_unique<Primitive>(parameters.get());
        parameters.release();
        objects.push_back(std::move(runtime));
    }
    // Tree-kind instances share their root source's model and branch poses; primitive and model instances are
    // ordinary objects built from the definition the parser copied.
    std::map<std::string, WorldObject*> runtimeById;
    auto linkTreeInstance = [&](const SceneData::ObjectDefinition& object, WorldObjectParams& parameters)
    {
        if (object.instanceOf.empty()) return;
        WorldObject* source = runtimeById.at(object.instanceOf);
        if (source->IsInstance()) source = source->GetParams().instanceSource;
        parameters.instanceSource = source;
        static_cast<Tree*>(source)->RegisterInstance();
    };
    for (const auto& object : definition.objects)
    {
        if (object->kind == SceneData::ObjectKind::Primitive)
        {
            const auto& primitive = static_cast<const SceneData::PrimitiveDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParams>(PrimitiveGeneratorType);
            CommonParameters(primitive, *parameters);
            parameters->primitiveType = RuntimeShape(primitive.shape);
            if (primitive.hasMaterial)
            {
                parameters->textureFilename.push_back(L"");
                parameters->materials.push_back(RuntimeMaterial(primitive.material));
                parameters->materialTextures.push_back(RuntimeMaterialTextures(primitive.material, primitive.id, sceneFilename));
            }
            auto runtime = std::make_unique<Primitive>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else if (object->kind == SceneData::ObjectKind::Tree)
        {
            const auto& tree = static_cast<const SceneData::TreeDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParameters<LSystemParams>>(LSystemGeneratorType);
            CommonParameters(tree, *parameters);
            parameters->meshes.push_back(RuntimeShape(tree.trunkShape));
            ApplyTreeMaterials(tree, *parameters, sceneFilename);
            auto& generator = parameters->GetGeneratorParameters();
            generator._axiom = tree.generator.axiom;
            generator._constants = tree.generator.constants;
            generator._numIterations = tree.generator.iterations;
            generator._angle = tree.generator.angle;
            generator._segmentLength = tree.generator.segmentLength;
            generator.thickness = tree.generator.thickness;
            generator._initialDirection = tree.generator.initialDirection;
            if (tree.generator.segmentLengthBehavior == SceneData::SegmentLengthBehavior::RandomAddition)
                generator.SegmentLength = RandomSegmentLength;
            for (const auto& rule : tree.generator.rules)
                generator._rules.emplace_back(rule.input.c_str(), rule.iterations, rule.output.c_str());
            linkTreeInstance(tree, *parameters);
            auto runtime = std::make_unique<Tree>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else if (object->kind == SceneData::ObjectKind::FixedTree)
        {
            const auto& tree = static_cast<const SceneData::FixedTreeDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParams>(FixedTreeGeneratorType);
            CommonParameters(tree, *parameters);
            parameters->meshes.push_back(RuntimeShape(tree.trunkShape));
            ApplyTreeMaterials(tree, *parameters, sceneFilename);
            linkTreeInstance(tree, *parameters);
            auto runtime = std::make_unique<Tree>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else if (object->kind == SceneData::ObjectKind::Graph)
        {
            const auto& graph = static_cast<const SceneData::GraphDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParameters<GraphParams>>(GraphGeneratorType);
            CommonParameters(graph, *parameters);
            if (graph.instanceOf.empty())
                parameters->GetGeneratorParameters().points = LoadGraphPoints(graph.dataFile, graph.id, sceneFilename);
            ApplyTreeMaterials(graph, *parameters, sceneFilename);
            linkTreeInstance(graph, *parameters);
            auto runtime = std::make_unique<Tree>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else
        {
            const auto& model = static_cast<const SceneData::ModelDefinition&>(*object);
            auto parameters = std::make_unique<GltfModelParams>();
            CommonParameters(model, *parameters);
            parameters->asset = RuntimeModel(model.source, model.id, sceneFilename);
            parameters->forceLit = model.forceLit;
            parameters->animatedNormalMaterial = model.animatedNormalMaterial;
            parameters->normalVelocity1 = model.normalVelocity1;
            parameters->normalVelocity2 = model.normalVelocity2;
            parameters->normalStrength = model.normalStrength;
            if (!model.animatedNormalMaterial.empty())
            {
                int matches = 0;
                for (const auto& material : parameters->asset->materials)
                    if (material.name == model.animatedNormalMaterial)
                    {
                        ++matches;
                        if (material.normalTextureIndex < 0 || material.shaderMaterial.normalTextures.z > 0)
                            throw std::runtime_error("Animated normal material requires a repeating normal texture: " + model.animatedNormalMaterial);
                    }
                if (matches != 1) throw std::runtime_error("Animated normal material must match exactly one glTF material: " + model.animatedNormalMaterial);
            }
            auto runtime = std::make_unique<GltfModel>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        runtimeById[object->id] = objects.back().get();
    }

    for (auto& object : objects)
    {
        scene.AddChild(object.get());
        object.release();
    }
    gameData.useShadowMaps = definition.useShadowMaps;
    gameData.useAlphaBlendedRenderTarget = definition.useAlphaBlendedRenderTarget;
    gameData.clearColor = definition.clearColor;
    renderSettings.clearColor = definition.clearColor;
    renderSettings.farClippingPlane = definition.farClippingPlane;
    renderSettings.enablePostProcessing = definition.enablePostProcessing;
    renderSettings.blurAmount = definition.blurAmount;
    renderSettings.ambientOcclusionRadius = definition.ambientOcclusionRadius;
    renderSettings.ambientOcclusionStrength = definition.ambientOcclusionStrength;
    renderSettings.environmentDiffuse = definition.environmentDiffuse.empty() ? std::string() :
        SceneData::ResolveSceneAsset(definition.environmentDiffuse, sceneFilename);
    renderSettings.environmentSpecular = definition.environmentSpecular.empty() ? std::string() :
        SceneData::ResolveSceneAsset(definition.environmentSpecular, sceneFilename);
    renderSettings.environmentRotationDegrees = definition.environmentRotationDegrees;
    auto& light = renderSettings.dirLights[0];
    light.Ambient = definition.light.ambient;
    light.Diffuse = definition.light.diffuse;
    light.Specular = definition.light.specular;
    XMStoreFloat3(&light.Direction, XMVector3Normalize(XMLoadFloat3(&definition.light.direction)));
    renderSettings.numDirectionalLights = 1;
    renderSettings.numPointLights = definition.pointLightCount;
    auto& point = renderSettings.pointLights[0];
    point.Ambient = definition.pointLight.ambient;
    point.Diffuse = definition.pointLight.diffuse;
    point.Specular = definition.pointLight.specular;
    point.Position = definition.pointLight.position;
    point.Range = definition.pointLight.range;
    point.Att = definition.pointLight.attenuation;
    renderSettings.shadowLightPositionOverride = definition.hasShadowLightPosition;
    renderSettings.shadowLightPosition = definition.shadowLightPosition;
    player.SetPosition(XMLoadFloat3(&definition.camera.position));
    player.SetRotation(XMLoadFloat4(&definition.camera.rotation));
}

namespace
{
    SceneData::Shape AuthoredShape(PrimitiveType shape)
    {
        switch (shape)
        {
        case PrimitiveType_Box: return SceneData::Shape::Box;
        case PrimitiveType_Cylinder: return SceneData::Shape::Cylinder;
        case PrimitiveType_CylinderLD: return SceneData::Shape::CylinderLD;
        case PrimitiveType_CylinderHD: return SceneData::Shape::CylinderHD;
        case PrimitiveType_SkinnedCylinder: return SceneData::Shape::SkinnedCylinder;
        case PrimitiveType_Sprite: return SceneData::Shape::Sprite;
        case PrimitiveType_Sphere: return SceneData::Shape::Sphere;
        default: throw std::runtime_error("Cannot save unsupported primitive shape");
        }
    }

    std::string RebaseAsset(const std::string& asset, const std::string& source, const std::string& destination)
    {
        if (asset.empty()) return {};
        const auto resolved = SceneData::WidePath(SceneData::ResolveSceneAsset(asset, source));
        const auto directory = SceneData::WidePath(SceneData::ParentPath(SceneData::AbsolutePath(destination)));
        auto split = [](const std::wstring& path) {
            std::vector<std::wstring> parts;
            size_t start = 0;
            while (start < path.size())
            {
                const auto end = path.find_first_of(L"/\\", start);
                parts.push_back(path.substr(start, end == std::wstring::npos ? end : end - start));
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
            return parts;
        };
        const auto from = split(directory), to = split(resolved);
        size_t common = 0;
        while (common < from.size() && common < to.size() && _wcsicmp(from[common].c_str(), to[common].c_str()) == 0)
            ++common;
        if (common == 0 || (resolved.substr(0, 2) == L"\\\\" && common < 4))
            throw std::runtime_error("Cannot save a portable reference to asset on another drive/share: " + asset);
        std::wstring relative;
        for (size_t i = common; i < from.size(); ++i) relative += L"..\\";
        for (size_t i = common; i < to.size(); ++i)
        {
            if (i != common) relative += L"\\";
            relative += to[i];
        }
        return SceneData::Utf8Path(relative);
    }

    SceneData::TextureMapDefinition& PbrMapForRole(SceneData::MaterialDefinition& material, MaterialTextureRole role)
    {
        switch (role)
        {
        case BaseColorTextureRole: return material.baseColorTexture;
        case MetallicRoughnessTextureRole: return material.metallicRoughnessTexture;
        case NormalTextureRole: return material.normalTexture;
        case SpecularStrengthTextureRole: return material.specularTexture;
        case SpecularColorTextureRole: return material.specularColorTexture;
        default: throw std::runtime_error("Unsupported material texture role");
        }
    }

    // Reverses RuntimeMaterial. Material UV scale is folded into each map's transform.
    SceneData::MaterialDefinition CaptureMaterial(const WorldObjectParams& params, size_t slot,
        const std::string& source, const std::string& destination)
    {
        auto material = params.materials.at(slot);
        if (material.specularParams.x == 0)
            throw std::runtime_error("Object '" + params.name + "': Phong materials cannot be saved to a scene");
        SceneData::MaterialDefinition result;
        result.baseColor = material.Diffuse;
        result.metallic = material.specularParams.y;
        result.roughness = material.specularParams.z;
        result.specular = material.specularParams.w;
        result.specularColor = { material.specularColor.x, material.specularColor.y, material.specularColor.z };
        result.ior = material.opticalParams.x;
        result.transmission = material.opticalParams.y;
        result.unlit = material.flags.w != 0;
        if (material.flags.x != 0 && material.flags.z == 1.0f / 255.0f) result.alphaMode = SceneData::AlphaMode::Blend;
        else if (material.flags.z > 0)
        {
            result.alphaMode = SceneData::AlphaMode::Mask;
            result.alphaCutoff = material.flags.z;
        }

        const auto* maps = params.MaterialTextures(slot);
        const bool hasMaps = maps && !maps->Empty();
        if (!hasMaps)
        {
            result.textureCoordScale = { material.uvTransformRow0[BaseColorTextureRole].x,
                material.uvTransformRow1[BaseColorTextureRole].y };
            return result;
        }
        for (int role = 0; role < MaterialTextureRoleCount; ++role)
        {
            const auto& texture = maps->roles[role];
            if (!texture) continue;
            auto& map = PbrMapForRole(result, static_cast<MaterialTextureRole>(role));
            map.source = RebaseAsset(SceneData::Utf8Path(texture->filename), source, destination);
            map.wrap = ClampFlag(material, static_cast<MaterialTextureRole>(role)) == 0;
            const auto& row0 = material.uvTransformRow0[role];
            const auto& row1 = material.uvTransformRow1[role];
            map.hasTransform = row0.x != 1 || row0.y != 0 || row0.z != 0 || row1.x != 0 || row1.y != 1 || row1.z != 0;
            if (map.hasTransform)
            {
                map.rotation = std::atan2(row1.x, row0.x);
                const float cosine = std::cos(map.rotation), sine = std::sin(map.rotation);
                map.scale = { std::sqrt(row0.x * row0.x + row1.x * row1.x), cosine * row1.y - sine * row0.y };
                map.offset = { row0.z, row1.z };
            }
        }
        if (maps->roles[NormalTextureRole]) result.normalScale = material.normalTextures.y;
        return result;
    }

    template<class T>
    void CaptureTreeMaterials(T& tree, const WorldObjectParams& params, const std::string& source, const std::string& destination)
    {
        if (params.materials.empty()) return;
        if (params.materials.size() != 2) throw std::runtime_error("Object '" + params.name + "': expected trunk and leaf material slots");
        tree.hasMaterials = true;
        tree.trunk = CaptureMaterial(params, 0, source, destination);
        tree.leaf = CaptureMaterial(params, 1, source, destination);
    }
}

SceneData::SceneDefinition CaptureScene(const SceneData::SceneDefinition& authored, SceneRoot& scene,
    const SceneRenderSettings& settings, Player& player, const GameData& gameData,
    const std::string& sourceFilename, const std::string& destinationFilename)
{
    auto result = SceneData::ParseScene(SceneData::SerializeScene(authored), sourceFilename);
    std::map<std::string, WorldObject*> objects;
    for (auto* object : scene.Children())
    {
        if (object->GetParams().cubeMap) continue; // The sky is saved as render.sky, not as an object.
        if (object->GetParams().name.empty() || !objects.emplace(object->GetParams().name, object).second)
            throw std::runtime_error("Cannot save objects without unique authored IDs");
    }
    if (objects.size() != result.objects.size()) throw std::runtime_error("Cannot save a scene with untracked objects");
    for (auto& definition : result.objects)
    {
        const auto found = objects.find(definition->id);
        if (found == objects.end()) throw std::runtime_error("Missing authored object '" + definition->id + "'");
        auto& runtime = *found->second;
        const auto& params = runtime.GetParams();
        XMStoreFloat3(&definition->transform.position, runtime.GetPosition());
        XMStoreFloat4(&definition->transform.rotation, runtime.GetRotation());
        XMStoreFloat3(&definition->transform.scale, runtime.GetScale());
        if (!definition->instanceOf.empty())
        {
            // Instances save their transform and overridden materials; everything else comes from the source.
            auto captureSlot = [&](size_t slot) { return CaptureMaterial(params, slot, sourceFilename, destinationFilename); };
            switch (definition->kind)
            {
            case SceneData::ObjectKind::Primitive:
                if (definition->overridesMaterial)
                    static_cast<SceneData::PrimitiveDefinition&>(*definition).material = captureSlot(0);
                break;
            case SceneData::ObjectKind::Tree:
            {
                auto& tree = static_cast<SceneData::TreeDefinition&>(*definition);
                if (tree.overridesMaterial) tree.trunk = captureSlot(0);
                if (tree.overridesLeafMaterial) tree.leaf = captureSlot(1);
                break;
            }
            case SceneData::ObjectKind::FixedTree:
            {
                auto& tree = static_cast<SceneData::FixedTreeDefinition&>(*definition);
                if (tree.overridesMaterial) tree.trunk = captureSlot(0);
                if (tree.overridesLeafMaterial) tree.leaf = captureSlot(1);
                break;
            }
            case SceneData::ObjectKind::Graph:
            {
                auto& graph = static_cast<SceneData::GraphDefinition&>(*definition);
                if (graph.overridesMaterial) graph.trunk = captureSlot(0);
                if (graph.overridesLeafMaterial) graph.leaf = captureSlot(1);
                break;
            }
            default:
                break;
            }
            continue;
        }
        definition->textureCoordScale = params.textureCoordScale;
        definition->animationSpeed = params._animationSpeed;
        definition->depthLOD = params.depthLOD;
        switch (definition->kind)
        {
        case SceneData::ObjectKind::Primitive:
        {
            if (params.generatorType != PrimitiveGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& primitive = static_cast<SceneData::PrimitiveDefinition&>(*definition);
            primitive.shape = AuthoredShape(params.primitiveType);
            if (params.materials.size() > 1) throw std::runtime_error("Primitive has unsupported additional materials");
            if (!params.materials.empty())
            {
                primitive.hasMaterial = true;
                primitive.material = CaptureMaterial(params, 0, sourceFilename, destinationFilename);
            }
            break;
        }
        case SceneData::ObjectKind::Tree:
        {
            if (params.generatorType != LSystemGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& tree = static_cast<SceneData::TreeDefinition&>(*definition);
            auto& generator = runtime.GetParams<LSystemParams>().GetGeneratorParameters();
            tree.trunkShape = params.meshes.empty() ? tree.trunkShape : AuthoredShape(params.meshes[0]);
            CaptureTreeMaterials(tree, params, sourceFilename, destinationFilename);
            auto& g = tree.generator;
            const LSystemParams defaults;
            if (generator.SegmentLength == defaults.SegmentLength) g.segmentLengthBehavior = SceneData::SegmentLengthBehavior::Constant;
            else if (generator.SegmentLength == RandomSegmentLength) g.segmentLengthBehavior = SceneData::SegmentLengthBehavior::RandomAddition;
            else throw std::runtime_error("Object '" + params.name + "': unsupported segment-length callback");
            g.axiom = generator._axiom;
            g.constants = generator._constants;
            g.iterations = generator._numIterations;
            g.angle = generator._angle;
            g.segmentLength = generator._segmentLength;
            g.thickness = generator.thickness;
            g.initialDirection = generator._initialDirection;
            g.rules.clear();
            for (const auto& rule : generator._rules) g.rules.push_back({ rule.input, rule.output, rule.numIterations });
            break;
        }
        case SceneData::ObjectKind::FixedTree:
        {
            if (params.generatorType != FixedTreeGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& tree = static_cast<SceneData::FixedTreeDefinition&>(*definition);
            tree.trunkShape = params.meshes.empty() ? tree.trunkShape : AuthoredShape(params.meshes[0]);
            CaptureTreeMaterials(tree, params, sourceFilename, destinationFilename);
            break;
        }
        case SceneData::ObjectKind::Graph:
        {
            if (params.generatorType != GraphGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& graph = static_cast<SceneData::GraphDefinition&>(*definition);
            graph.dataFile = RebaseAsset(graph.dataFile, sourceFilename, destinationFilename);
            CaptureTreeMaterials(graph, params, sourceFilename, destinationFilename);
            break;
        }
        case SceneData::ObjectKind::Model:
        {
            if (params.generatorType != ModelGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& model = static_cast<SceneData::ModelDefinition&>(*definition);
            model.source = RebaseAsset(model.source, sourceFilename, destinationFilename);
            const auto& modelParams = dynamic_cast<const GltfModelParams&>(params);
            model.forceLit = modelParams.forceLit;
            model.animatedNormalMaterial = modelParams.animatedNormalMaterial;
            model.normalVelocity1 = modelParams.normalVelocity1;
            model.normalVelocity2 = modelParams.normalVelocity2;
            model.normalStrength = modelParams.normalStrength;
            break;
        }
        default: throw std::runtime_error("Cannot save unsupported scene object");
        }
    }
    // Runtime objects own resolved material values; named authoring templates are no longer authoritative.
    result.materials.clear();
    XMStoreFloat3(&result.camera.position, player.GetPosition());
    XMStoreFloat4(&result.camera.rotation, player.GetRotation());
    result.clearColor = settings.clearColor;
    result.farClippingPlane = settings.farClippingPlane;
    result.enablePostProcessing = settings.enablePostProcessing;
    result.blurAmount = settings.blurAmount;
    result.ambientOcclusionRadius = settings.ambientOcclusionRadius;
    result.ambientOcclusionStrength = settings.ambientOcclusionStrength;
    result.environmentDiffuse = RebaseAsset(settings.environmentDiffuse, sourceFilename, destinationFilename);
    result.environmentSpecular = RebaseAsset(settings.environmentSpecular, sourceFilename, destinationFilename);
    result.environmentRotationDegrees = settings.environmentRotationDegrees;
    result.skyCubeMap = RebaseAsset(authored.skyCubeMap, sourceFilename, destinationFilename);
    result.useShadowMaps = gameData.useShadowMaps;
    result.useAlphaBlendedRenderTarget = gameData.useAlphaBlendedRenderTarget;
    if (settings.numDirectionalLights != 1 || settings.numPointLights < 0 || settings.numPointLights > 1)
        throw std::runtime_error("Unsupported scene light counts");
    result.light.ambient = settings.dirLights[0].Ambient;
    result.light.diffuse = settings.dirLights[0].Diffuse;
    result.light.specular = settings.dirLights[0].Specular;
    result.light.direction = settings.dirLights[0].Direction;
    result.hasShadowLightPosition = settings.shadowLightPositionOverride;
    result.shadowLightPosition = settings.shadowLightPosition;
    result.pointLightCount = settings.numPointLights;
    const auto& point = settings.pointLights[0];
    result.pointLight.ambient = point.Ambient;
    result.pointLight.diffuse = point.Diffuse;
    result.pointLight.specular = point.Specular;
    result.pointLight.position = point.Position;
    result.pointLight.range = point.Range;
    result.pointLight.attenuation = point.Att;
    SceneData::SerializeScene(result);
    return result;
}

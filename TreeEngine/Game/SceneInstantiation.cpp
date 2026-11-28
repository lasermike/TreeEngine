#include "pch.h"
#include "SceneInstantiation.h"
#include "SceneRenderSettings.h"
#include "GameLoader.h"
#include "SceneRoot.h"
#include "Primitive.h"
#include "Tree.h"
#include "Graph.h"
#include "GltfModel.h"
#include "Terrain.h"
#include "Water.h"
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
        parameters.lod = definition.lod;
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

    void ApplyLeafAtlas(const SceneData::LeafAtlasDefinition& leaves, const SceneData::SceneDefinition& scene,
        WorldObjectParams& parameters)
    {
        if (leaves.atlas.empty()) return;
        parameters.leafTwistRadians = XMConvertToRadians(leaves.twistDegrees);
        parameters.leafSizeVariation = leaves.sizeVariation;
        const auto& atlas = scene.atlases.at(leaves.atlas);
        std::vector<SceneData::LeafRegionWeight> choices = leaves.regions;
        if (choices.empty())
            for (const auto& region : atlas.regions) choices.push_back({ region.first, 1 });
        for (const auto& choice : choices)
        {
            const auto& region = atlas.regions.at(choice.region);
            const auto& r = region.rect;
            const float width = r.z - r.x, height = r.w - r.y;
            // The unit leaf quad spans [-0.5, 0.5] with v = 0 at the top; its base center attaches to the branch.
            const float stemU = (region.stem.x - r.x) / width, stemV = (region.stem.y - r.y) / height;
            WorldObjectParams::LeafAtlasRegion runtime;
            runtime.atlasRect = XMFLOAT4(r.x, r.y, width, height);
            runtime.stemOffset = XMFLOAT2(0.5f - stemU, stemV - 1);
            runtime.widthPerLength = width * atlas.aspect / height;
            runtime.weight = choice.weight;
            parameters.leafRegions.push_back(runtime);
        }
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
            ApplyLeafAtlas(tree.leaves, definition, *parameters);
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
            ApplyLeafAtlas(tree.leaves, definition, *parameters);
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
            ApplyLeafAtlas(graph.leaves, definition, *parameters);
            linkTreeInstance(graph, *parameters);
            auto runtime = std::make_unique<Tree>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else if (object->kind == SceneData::ObjectKind::Water)
        {
            const auto& water = static_cast<const SceneData::WaterDefinition&>(*object);
            const auto& t = water.transform;
            if (t.rotation.x != 0 || t.rotation.y != 0 || t.rotation.z != 0 || t.scale.x != 1 || t.scale.y != 1 || t.scale.z != 1)
                throw std::runtime_error("Object '" + water.id + "': water can be moved, but not rotated or scaled");
            auto parameters = std::make_unique<WaterParams>();
            CommonParameters(water, *parameters);
            parameters->primitiveType = PrimitiveType_Box;
            parameters->size = water.size;
            parameters->tile = water.tile;
            parameters->level = t.position.y;
            parameters->rippleVelocity1 = water.rippleVelocity1;
            parameters->rippleVelocity2 = water.rippleVelocity2;
            parameters->rippleStrength = water.rippleStrength;
            parameters->tideRange = water.tideRange;
            parameters->tidePeriod = water.tidePeriod;
            parameters->tidePhase = water.tidePhase;
            parameters->tideConstituents = water.tideConstituents;
            if (!water.tideConstituents.empty())
                Tide::ParseUtc(water.tideStart, parameters->tideStart);
            parameters->tideStartText = water.tideStart;
            parameters->tideSource = water.tideSource;
            parameters->tideSimulationTicksPerDay = water.tideSimulationTicksPerDay;
            parameters->textureFilename.push_back(L"");
            if (water.hasMaterial)
            {
                parameters->materials.push_back(RuntimeMaterial(water.material));
                parameters->materialTextures.push_back(RuntimeMaterialTextures(water.material, water.id, sceneFilename));
            }
            else
            {
                // Clear seawater, as AI Tree's river: transmissive, smooth, faintly green.
                SceneData::MaterialDefinition seawater;
                seawater.baseColor = XMFLOAT4(0.6f, 0.78f, 0.73f, 1);
                seawater.metallic = 0;
                seawater.roughness = 0.12f;
                seawater.ior = 1.333f;
                seawater.transmission = 0.85f;
                parameters->materials.push_back(RuntimeMaterial(seawater));
                parameters->materialTextures.push_back(RuntimeMaterialTextures(seawater, water.id, sceneFilename));
            }
            auto runtime = std::make_unique<Water>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else if (object->kind == SceneData::ObjectKind::Terrain)
        {
            const auto& terrain = static_cast<const SceneData::TerrainDefinition&>(*object);
            const auto& t = terrain.transform;
            if (t.rotation.x != 0 || t.rotation.y != 0 || t.rotation.z != 0 || t.scale.x != 1 || t.scale.y != 1 || t.scale.z != 1)
                throw std::runtime_error("Object '" + terrain.id + "': terrain can be moved, but not rotated or scaled");
            auto parameters = std::make_unique<TerrainParams>();
            CommonParameters(terrain, *parameters);
            parameters->source = terrain.source;
            const std::string manifest = RuntimeDataFile(terrain.source + "/manifest.json", terrain.id, sceneFilename);
            parameters->packageDirectory = SceneData::WidePath(SceneData::ParentPath(manifest));
            parameters->roads = terrain.roads;
            if (!terrain.roads.empty())
            {
                parameters->roadsFile = SceneData::WidePath(RuntimeDataFile(terrain.roads, terrain.id, sceneFilename));
                // Roadside props' models, loaded as the roads file's props need them (Tools/terrain/make_road_props.py
                // writes them; any glTF may replace one). One that isn't installed draws nothing; one that is but
                // doesn't load is an error.
                const std::string terrainId = terrain.id;
                parameters->loadPropModel = [terrainId, sceneFilename](Roads::PropModel model) -> std::shared_ptr<GltfAsset>
                {
                    const std::string file = std::string("Models/Roadside/") + Roads::PropModelName(model) + ".glb";
                    try { SceneData::ResolveSceneAsset(file, sceneFilename); }
                    catch (const std::runtime_error&) { return nullptr; }
                    return RuntimeModel(file, terrainId, sceneFilename);
                };
                // Road surfaces, in Roads::Part order: asphalt and gravel tiled every 4 m (TerrainTool texture
                // maps), yellow and white paint, then trails (the dirt maps).
                // The scene's roadSurfaces replace them by name (asphalt, gravel, trail).
                const auto surface = [&terrain](const char* kind)
                {
                    const auto replaced = terrain.roadSurfaces.find(std::string(kind) == "dirt" ? "trail" : kind);
                    if (replaced != terrain.roadSurfaces.end())
                        return replaced->second;
                    SceneData::MaterialDefinition material;
                    material.metallic = 0;
                    material.roughness = 1;
                    const std::string prefix = std::string("Terrain/Textures/") + kind;
                    material.baseColorTexture.source = prefix + "_color.png";
                    material.normalTexture.source = prefix + "_normal.png";
                    material.metallicRoughnessTexture.source = prefix + "_mr.png";
                    material.textureCoordScale = XMFLOAT2(0.25f, 0.25f);
                    return material;
                };
                const auto paint = [](XMFLOAT4 color)
                {
                    SceneData::MaterialDefinition material;
                    material.baseColor = color;
                    material.metallic = 0;
                    material.roughness = 0.6f;
                    return material;
                };
                for (const auto& material : { surface("asphalt"), surface("gravel"),
                         paint(XMFLOAT4(0.78f, 0.55f, 0.06f, 1)), paint(XMFLOAT4(0.82f, 0.82f, 0.8f, 1)) })
                {
                    parameters->roadMaterials.push_back(RuntimeMaterial(material));
                    parameters->roadMaterialTextures.push_back(RuntimeMaterialTextures(material, terrain.id, sceneFilename));
                }
                // Trails: their maps load only if the roads have a trail (Terrain::LoadRoads).
                const auto dirt = surface("dirt");
                parameters->roadMaterials.push_back(RuntimeMaterial(dirt));
                parameters->roadMaterialTextures.push_back(MaterialTextureSources());
                parameters->loadTrailMaterialTextures = [dirt, terrainId, sceneFilename]()
                    { return RuntimeMaterialTextures(dirt, terrainId, sceneFilename); };
            }
            parameters->lodRange = terrain.lodRange;
            parameters->antiTiling = terrain.antiTiling;
            parameters->variation = terrain.variation;
            parameters->variationSize = terrain.variationSize;
            parameters->slots = static_cast<UINT>(terrain.slots);
            if (terrain.hasMaterial)
            {
                parameters->textureFilename.push_back(L"");
                parameters->materials.push_back(RuntimeMaterial(terrain.material));
                parameters->materialTextures.push_back(RuntimeMaterialTextures(terrain.material, terrain.id, sceneFilename));
            }
            for (const auto& layer : terrain.layers)
            {
                parameters->textureFilename.push_back(L"");
                parameters->materials.push_back(RuntimeMaterial(layer.material));
                parameters->materialTextures.push_back(RuntimeMaterialTextures(layer.material, terrain.id, sceneFilename));
                TerrainLayerParams runtimeLayer;
                runtimeLayer.tile = layer.tile;
                runtimeLayer.biplanar = layer.biplanar;
                runtimeLayer.height = XMFLOAT3(layer.height.x, layer.height.y, layer.heightBlend);
                runtimeLayer.slope = XMFLOAT3(layer.slope.x, layer.slope.y, layer.slopeBlend);
                runtimeLayer.noise = XMFLOAT3(layer.noiseHeight, layer.noiseSlope, layer.noiseScale);
                parameters->layers.push_back(runtimeLayer);
            }
            auto runtime = std::make_unique<Terrain>(parameters.get());
            parameters.release();
            if (!runtime->IsOpen())
                throw std::runtime_error("Object '" + terrain.id + "': terrain package " + terrain.source + ": " + runtime->Error());
            if (!runtime->RoadsError().empty())
                throw std::runtime_error("Object '" + terrain.id + "': " + terrain.roads + ": " + runtime->RoadsError());
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
            if (!model.tintMaterial.empty())
            {
                const auto matches = std::count_if(parameters->asset->materials.begin(), parameters->asset->materials.end(),
                    [&](const auto& material) { return material.name == model.tintMaterial; });
                if (matches != 1) throw std::runtime_error("Tinted material must match exactly one glTF material: " + model.tintMaterial);
                parameters->tintMaterial = model.tintMaterial;
                parameters->tintTexturePath = model.tintTexture;
                parameters->tintTexture = LoadSharedMaterialTextureFile(RuntimeTexture(model.tintTexture, model.id, sceneFilename), 1);
                parameters->tintUvScale = model.tintUvScale;
                parameters->tintUvOffset = model.tintUvOffset;
            }
            if (!model.biplanarMaterial.empty())
            {
                const auto matches = std::count_if(parameters->asset->materials.begin(), parameters->asset->materials.end(),
                    [&](const auto& material) { return material.name == model.biplanarMaterial; });
                if (matches != 1) throw std::runtime_error("Biplanar material must match exactly one glTF material: " + model.biplanarMaterial);
                parameters->biplanarMaterial = model.biplanarMaterial;
                parameters->biplanarUvPerUnit = model.biplanarUvPerUnit;
                parameters->biplanarSharpness = model.biplanarSharpness;
            }
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
    renderSettings.environmentIntensity = definition.environmentIntensity;
    renderSettings.sunAngularDiameterDegrees = definition.light.angularDiameterDegrees;
    renderSettings.sunShadowSamples = definition.light.shadowSamples;
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
        definition->lod = params.lod;
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
            model.tintMaterial = modelParams.tintMaterial;
            model.tintTexture = modelParams.tintTexturePath.empty() ? std::string()
                : RebaseAsset(modelParams.tintTexturePath, sourceFilename, destinationFilename);
            model.tintUvScale = modelParams.tintUvScale;
            model.tintUvOffset = modelParams.tintUvOffset;
            model.biplanarMaterial = modelParams.biplanarMaterial;
            model.biplanarUvPerUnit = modelParams.biplanarUvPerUnit;
            model.biplanarSharpness = modelParams.biplanarSharpness;
            break;
        }
        case SceneData::ObjectKind::Water:
        {
            if (params.generatorType != PrimitiveGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& water = static_cast<SceneData::WaterDefinition&>(*definition);
            const auto& waterParams = dynamic_cast<const WaterParams&>(params);
            // The authored mean height and size, not the tide's current height or the plane's scale.
            definition->transform.position.y = waterParams.level;
            definition->transform.scale = XMFLOAT3(1, 1, 1);
            definition->textureCoordScale = XMFLOAT2(1, 1);
            water.size = waterParams.size;
            water.tile = waterParams.tile;
            water.rippleVelocity1 = waterParams.rippleVelocity1;
            water.rippleVelocity2 = waterParams.rippleVelocity2;
            water.rippleStrength = waterParams.rippleStrength;
            water.tideRange = waterParams.tideRange;
            water.tidePeriod = waterParams.tidePeriod;
            water.tidePhase = waterParams.tidePhase;
            water.tideConstituents = waterParams.tideConstituents;
            water.tideStart = waterParams.tideStartText;
            water.tideSource = waterParams.tideSource;
            water.tideSimulationTicksPerDay = waterParams.tideSimulationTicksPerDay;
            if (water.hasMaterial && !params.materials.empty())
                water.material = CaptureMaterial(params, 0, sourceFilename, destinationFilename);
            break;
        }
        case SceneData::ObjectKind::Terrain:
        {
            if (params.generatorType != TerrainGeneratorType) throw std::runtime_error("Object generator type changed");
            auto& terrain = static_cast<SceneData::TerrainDefinition&>(*definition);
            const auto& terrainParams = dynamic_cast<const TerrainParams&>(params);
            const std::string manifest = RebaseAsset(terrainParams.source + "/manifest.json", sourceFilename, destinationFilename);
            terrain.source = manifest.substr(0, manifest.find_last_of("/\\"));
            terrain.roads = RebaseAsset(terrainParams.roads, sourceFilename, destinationFilename);
            // Road surfaces keep their authored materials; their maps are rebased like everything else.
            for (auto& surface : terrain.roadSurfaces)
                for (SceneData::TextureMapDefinition* map : { &surface.second.baseColorTexture,
                         &surface.second.metallicRoughnessTexture, &surface.second.normalTexture,
                         &surface.second.specularTexture, &surface.second.specularColorTexture })
                    map->source = RebaseAsset(map->source, sourceFilename, destinationFilename);
            terrain.lodRange = terrainParams.lodRange;
            terrain.slots = static_cast<int>(terrainParams.slots);
            terrain.antiTiling = terrainParams.antiTiling;
            terrain.variation = terrainParams.variation;
            terrain.variationSize = terrainParams.variationSize;
            terrain.layers.clear();
            terrain.hasMaterial = false;
            if (!terrainParams.layers.empty())
            {
                for (size_t i = 0; i < terrainParams.layers.size(); ++i)
                {
                    const auto& runtimeLayer = terrainParams.layers[i];
                    SceneData::TerrainLayerDefinition layer;
                    layer.material = CaptureMaterial(params, i, sourceFilename, destinationFilename);
                    layer.tile = runtimeLayer.tile;
                    layer.biplanar = runtimeLayer.biplanar;
                    layer.height = XMFLOAT2(runtimeLayer.height.x, runtimeLayer.height.y);
                    layer.heightBlend = runtimeLayer.height.z;
                    layer.slope = XMFLOAT2(runtimeLayer.slope.x, runtimeLayer.slope.y);
                    layer.slopeBlend = runtimeLayer.slope.z;
                    layer.noiseHeight = runtimeLayer.noise.x;
                    layer.noiseSlope = runtimeLayer.noise.y;
                    layer.noiseScale = runtimeLayer.noise.z;
                    terrain.layers.push_back(layer);
                }
            }
            else if (!params.materials.empty())
            {
                terrain.hasMaterial = true;
                terrain.material = CaptureMaterial(params, 0, sourceFilename, destinationFilename);
            }
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
    result.environmentIntensity = settings.environmentIntensity;
    result.skyCubeMap = RebaseAsset(authored.skyCubeMap, sourceFilename, destinationFilename);
    result.useShadowMaps = gameData.useShadowMaps;
    result.useAlphaBlendedRenderTarget = gameData.useAlphaBlendedRenderTarget;
    if (settings.numDirectionalLights != 1 || settings.numPointLights < 0 || settings.numPointLights > 1)
        throw std::runtime_error("Unsupported scene light counts");
    result.light.ambient = settings.dirLights[0].Ambient;
    result.light.diffuse = settings.dirLights[0].Diffuse;
    result.light.specular = settings.dirLights[0].Specular;
    result.light.direction = settings.dirLights[0].Direction;
    result.light.angularDiameterDegrees = settings.sunAngularDiameterDegrees;
    result.light.shadowSamples = settings.sunShadowSamples;
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

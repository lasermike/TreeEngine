#include "pch.h"
#include "SceneDefinition.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace SceneData
{
    namespace
    {
        using Json = nlohmann::json;
        using namespace DirectX;

        [[noreturn]] void Invalid(const std::string& path, const std::string& reason)
        {
            throw std::runtime_error(path + ": " + reason);
        }

        void Fields(const Json& value, const std::string& path, std::initializer_list<const char*> allowed)
        {
            if (!value.is_object()) Invalid(path, "expected an object");
            for (auto it = value.begin(); it != value.end(); ++it)
            {
                bool known = false;
                for (auto field : allowed) if (it.key() == field) known = true;
                if (!known) Invalid(path + "." + it.key(), "unknown property");
            }
        }

        std::string String(const Json& value, const std::string& path)
        {
            if (!value.is_string()) Invalid(path, "expected a string");
            auto result = value.get<std::string>();
            if (result.find('\0') != std::string::npos) Invalid(path, "embedded NUL is not allowed");
            return result;
        }

        float Number(const Json& value, const std::string& path)
        {
            if (!value.is_number()) Invalid(path, "expected a finite number");
            double result = value.get<double>();
            if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
                Invalid(path, "number is outside the float range");
            return static_cast<float>(result);
        }

        int Integer(const Json& value, const std::string& path, int minimum, int maximum)
        {
            if (!value.is_number_integer()) Invalid(path, "expected an integer");
            double result = value.get<double>();
            if (result < minimum || result > maximum) Invalid(path, "integer is outside the supported range");
            return static_cast<int>(result);
        }

        bool Boolean(const Json& value, const std::string& path)
        {
            if (!value.is_boolean()) Invalid(path, "expected true or false");
            return value.get<bool>();
        }

        void Vector(const Json& value, const std::string& path, float* output, size_t count)
        {
            if (!value.is_array() || value.size() != count)
                Invalid(path, "expected an array of " + std::to_string(count) + " numbers");
            for (size_t i = 0; i < count; ++i) output[i] = Number(value[i], path + "[" + std::to_string(i) + "]");
        }

        // A pass name or an array of them; names are lowercase words joined by hyphens.
        XMFLOAT2 Vector2(const Json& value, const std::string& path)
        {
            float v[2]; Vector(value, path, v, 2); return XMFLOAT2(v[0], v[1]);
        }
        XMFLOAT3 Vector3(const Json& value, const std::string& path)
        {
            float v[3]; Vector(value, path, v, 3); return XMFLOAT3(v[0], v[1], v[2]);
        }
        XMFLOAT4 Vector4(const Json& value, const std::string& path)
        {
            float v[4]; Vector(value, path, v, 4); return XMFLOAT4(v[0], v[1], v[2], v[3]);
        }

        Transform ReadTransform(const Json& value, const std::string& path)
        {
            Fields(value, path, { "position", "scale", "rotation", "rotationDegrees" });
            Transform result;
            if (value.contains("position")) result.position = Vector3(value["position"], path + ".position");
            if (value.contains("scale")) result.scale = Vector3(value["scale"], path + ".scale");
            if (result.scale.x == 0 || result.scale.y == 0 || result.scale.z == 0)
                Invalid(path + ".scale", "scale components must be nonzero");
            if (value.contains("rotation") && value.contains("rotationDegrees"))
                Invalid(path, "specify either rotation or rotationDegrees");
            if (value.contains("rotation"))
            {
                result.rotation = Vector4(value["rotation"], path + ".rotation");
                const auto& q = result.rotation;
                double length = double(q.x)*q.x + double(q.y)*q.y + double(q.z)*q.z + double(q.w)*q.w;
                if (length < 1e-12) Invalid(path + ".rotation", "quaternion must be nonzero");
                // Preserve authored quaternions exactly, including legacy camera values.
            }
            if (value.contains("rotationDegrees"))
            {
                auto v = Vector3(value["rotationDegrees"], path + ".rotationDegrees");
                XMStoreFloat4(&result.rotation, XMQuaternionRotationRollPitchYaw(
                    XMConvertToRadians(v.x), XMConvertToRadians(v.y), XMConvertToRadians(v.z)));
            }
            return result;
        }

        float UnitNumber(const Json& value, const std::string& path)
        {
            float result = Number(value, path);
            if (result < 0 || result > 1) Invalid(path, "expected a value between 0 and 1");
            return result;
        }

        TextureMapDefinition ReadTextureMap(const Json& value, const std::string& path, float* normalScale)
        {
            if (normalScale) Fields(value, path, { "source", "wrap", "transform", "scale" });
            else Fields(value, path, { "source", "wrap", "transform" });
            TextureMapDefinition result;
            if (!value.contains("source")) Invalid(path + ".source", "required property");
            result.source = String(value["source"], path + ".source");
            if (result.source.empty()) Invalid(path + ".source", "must not be empty");
            if (value.contains("wrap")) result.wrap = Boolean(value["wrap"], path + ".wrap");
            if (value.contains("transform"))
            {
                const auto& transform = value["transform"];
                const auto transformPath = path + ".transform";
                Fields(transform, transformPath, { "offset", "rotation", "scale" });
                result.hasTransform = true;
                if (transform.contains("offset")) result.offset = Vector2(transform["offset"], transformPath + ".offset");
                if (transform.contains("rotation")) result.rotation = Number(transform["rotation"], transformPath + ".rotation");
                if (transform.contains("scale")) result.scale = Vector2(transform["scale"], transformPath + ".scale");
            }
            if (normalScale && value.contains("scale"))
            {
                *normalScale = Number(value["scale"], path + ".scale");
                if (*normalScale < 0) Invalid(path + ".scale", "must be nonnegative");
            }
            return result;
        }

        MaterialDefinition ReadMaterial(const Json& value, const std::string& path)
        {
            if (value.is_object())
            {
                if (value.contains("pbr"))
                    Invalid(path + ".pbr", "the pbr block was removed; move its properties into the material");
                for (auto field : { "ambient", "diffuse", "reflect", "texture", "alphaClipThreshold", "flags" })
                    if (value.contains(field))
                        Invalid(path + "." + field, "Phong materials are no longer supported; see \"Converting Phong materials\" in the scenes README");
                if (value.contains("specular") && value["specular"].is_array())
                    Invalid(path + ".specular", "Phong specular colors are no longer supported; use roughness and specularColor");
            }
            Fields(value, path, { "baseColor", "metallic", "roughness", "specular", "specularColor", "ior", "transmission",
                "alphaMode", "alphaCutoff", "unlit", "baseColorTexture", "metallicRoughnessTexture", "normalTexture",
                "specularTexture", "specularColorTexture", "textureCoordScale" });
            MaterialDefinition result;
            if (value.contains("textureCoordScale")) result.textureCoordScale = Vector2(value["textureCoordScale"], path + ".textureCoordScale");
            if (value.contains("baseColor")) result.baseColor = Vector4(value["baseColor"], path + ".baseColor");
            if (value.contains("metallic")) result.metallic = UnitNumber(value["metallic"], path + ".metallic");
            if (value.contains("roughness")) result.roughness = UnitNumber(value["roughness"], path + ".roughness");
            if (value.contains("specular")) result.specular = UnitNumber(value["specular"], path + ".specular");
            if (value.contains("specularColor"))
            {
                result.specularColor = Vector3(value["specularColor"], path + ".specularColor");
                if (result.specularColor.x < 0 || result.specularColor.y < 0 || result.specularColor.z < 0)
                    Invalid(path + ".specularColor", "components must be nonnegative");
            }
            if (value.contains("ior"))
            {
                result.ior = Number(value["ior"], path + ".ior");
                if (result.ior != 0 && result.ior < 1) Invalid(path + ".ior", "must be zero or at least one");
            }
            if (value.contains("transmission")) result.transmission = UnitNumber(value["transmission"], path + ".transmission");
            if (value.contains("alphaMode"))
            {
                auto mode = String(value["alphaMode"], path + ".alphaMode");
                if (mode == "mask") result.alphaMode = AlphaMode::Mask;
                else if (mode == "blend") result.alphaMode = AlphaMode::Blend;
                else if (mode != "opaque") Invalid(path + ".alphaMode", "expected opaque, mask, or blend");
            }
            if (value.contains("alphaCutoff")) result.alphaCutoff = UnitNumber(value["alphaCutoff"], path + ".alphaCutoff");
            if (value.contains("unlit")) result.unlit = Boolean(value["unlit"], path + ".unlit");
            if (result.unlit)
                for (auto field : { "specular", "specularColor", "ior", "transmission", "metallicRoughnessTexture",
                    "normalTexture", "specularTexture", "specularColorTexture" })
                    if (value.contains(field)) Invalid(path + "." + field, "cannot be combined with unlit");
            if (value.contains("baseColorTexture"))
                result.baseColorTexture = ReadTextureMap(value["baseColorTexture"], path + ".baseColorTexture", nullptr);
            if (value.contains("metallicRoughnessTexture"))
                result.metallicRoughnessTexture = ReadTextureMap(value["metallicRoughnessTexture"], path + ".metallicRoughnessTexture", nullptr);
            if (value.contains("normalTexture"))
                result.normalTexture = ReadTextureMap(value["normalTexture"], path + ".normalTexture", &result.normalScale);
            if (value.contains("specularTexture"))
                result.specularTexture = ReadTextureMap(value["specularTexture"], path + ".specularTexture", nullptr);
            if (value.contains("specularColorTexture"))
                result.specularColorTexture = ReadTextureMap(value["specularColorTexture"], path + ".specularColorTexture", nullptr);
            return result;
        }

        MaterialDefinition MaterialReference(const Json& value, const std::string& path, const SceneDefinition& scene)
        {
            if (!value.is_string()) return ReadMaterial(value, path);
            std::string name = String(value, path);
            auto found = scene.materials.find(name);
            if (found == scene.materials.end()) Invalid(path, "unknown material '" + name + "'");
            return found->second;
        }

        Shape ReadShape(const Json& value, const std::string& path)
        {
            auto name = String(value, path);
            if (name == "box") return Shape::Box;
            if (name == "cylinder") return Shape::Cylinder;
            if (name == "cylinderLD") return Shape::CylinderLD;
            if (name == "cylinderHD") return Shape::CylinderHD;
            if (name == "skinnedCylinder") return Shape::SkinnedCylinder;
            if (name == "sprite") return Shape::Sprite;
            if (name == "sphere") return Shape::Sphere;
            Invalid(path, "unknown shape '" + name + "'");
        }

        LSystemDefinition ReadGenerator(const Json& value, const std::string& path)
        {
            Fields(value, path, { "axiom", "constants", "iterations", "angle", "segmentLength", "thickness", "initialDirection", "segmentLengthBehavior", "rules" });
            LSystemDefinition result;
            if (!value.contains("axiom")) Invalid(path + ".axiom", "required property");
            result.axiom = String(value["axiom"], path + ".axiom");
            if (result.axiom.empty()) Invalid(path + ".axiom", "must not be empty");
            if (value.contains("constants")) result.constants = String(value["constants"], path + ".constants");
            if (value.contains("iterations")) result.iterations = Integer(value["iterations"], path + ".iterations", 0, 20);
            if (value.contains("angle")) result.angle = Number(value["angle"], path + ".angle");
            if (value.contains("segmentLength")) result.segmentLength = Number(value["segmentLength"], path + ".segmentLength");
            if (value.contains("thickness")) result.thickness = Number(value["thickness"], path + ".thickness");
            if (result.segmentLength <= 0) Invalid(path + ".segmentLength", "must be positive");
            if (result.thickness <= 0) Invalid(path + ".thickness", "must be positive");
            if (value.contains("initialDirection")) result.initialDirection = Vector3(value["initialDirection"], path + ".initialDirection");
            auto d = result.initialDirection;
            if (d.x == 0 && d.y == 0 && d.z == 0) Invalid(path + ".initialDirection", "must be nonzero");
            if (value.contains("segmentLengthBehavior"))
            {
                auto behavior = String(value["segmentLengthBehavior"], path + ".segmentLengthBehavior");
                if (behavior == "randomAddition") result.segmentLengthBehavior = SegmentLengthBehavior::RandomAddition;
                else if (behavior != "constant") Invalid(path + ".segmentLengthBehavior", "unknown behavior '" + behavior + "'");
            }
            if (value.contains("rules"))
            {
                const auto& rules = value["rules"];
                if (!rules.is_array()) Invalid(path + ".rules", "expected an array");
                for (size_t i = 0; i < rules.size(); ++i)
                {
                    auto rulePath = path + ".rules[" + std::to_string(i) + "]";
                    Fields(rules[i], rulePath, { "input", "output", "iterations" });
                    if (!rules[i].contains("input") || !rules[i].contains("output")) Invalid(rulePath, "input and output are required");
                    RuleDefinition rule;
                    rule.input = String(rules[i]["input"], rulePath + ".input");
                    rule.output = String(rules[i]["output"], rulePath + ".output");
                    if (rule.input.empty()) Invalid(rulePath + ".input", "must not be empty");
                    if (rules[i].contains("iterations")) rule.iterations = Integer(rules[i]["iterations"], rulePath + ".iterations", 0, 20);
                    result.rules.push_back(rule);
                }
            }
            return result;
        }

        void CommonObject(const Json& value, const std::string& path, ObjectDefinition& result)
        {
            if (!value.contains("id")) Invalid(path + ".id", "required property");
            result.id = String(value["id"], path + ".id");
            if (result.id.empty()) Invalid(path + ".id", "must not be empty");
            if (value.contains("transform")) result.transform = ReadTransform(value["transform"], path + ".transform");
            if (value.contains("textureCoordScale")) result.textureCoordScale = Vector2(value["textureCoordScale"], path + ".textureCoordScale");
            if (value.contains("animationSpeed")) result.animationSpeed = Number(value["animationSpeed"], path + ".animationSpeed");
            if (result.animationSpeed < 0) Invalid(path + ".animationSpeed", "must be nonnegative");
            if (value.contains("depthLOD")) result.depthLOD = Integer(value["depthLOD"], path + ".depthLOD", -1, 20);
        }

        template<class T>
        void ReadTreeMaterials(const Json& value, const std::string& path, const SceneDefinition& scene, T& object)
        {
            if (!value.contains("materials")) return;
            object.hasMaterials = true;
            Fields(value["materials"], path + ".materials", { "trunk", "leaf" });
            if (value["materials"].contains("trunk"))
                object.trunk = MaterialReference(value["materials"]["trunk"], path + ".materials.trunk", scene);
            if (value["materials"].contains("leaf"))
                object.leaf = MaterialReference(value["materials"]["leaf"], path + ".materials.leaf", scene);
        }

        AtlasDefinition ReadAtlas(const Json& value, const std::string& path)
        {
            Fields(value, path, { "aspect", "regions" });
            AtlasDefinition result;
            if (value.contains("aspect"))
            {
                result.aspect = Number(value["aspect"], path + ".aspect");
                if (result.aspect <= 0) Invalid(path + ".aspect", "must be positive");
            }
            if (!value.contains("regions") || !value["regions"].is_object() || value["regions"].empty())
                Invalid(path + ".regions", "expected named regions");
            for (auto it = value["regions"].begin(); it != value["regions"].end(); ++it)
            {
                const auto regionPath = path + ".regions." + it.key();
                if (it.key().empty()) Invalid(regionPath, "region names must not be empty");
                Fields(it.value(), regionPath, { "rect", "stem" });
                if (!it.value().contains("rect")) Invalid(regionPath + ".rect", "required property");
                AtlasRegionDefinition region;
                region.rect = Vector4(it.value()["rect"], regionPath + ".rect");
                const auto& r = region.rect;
                if (r.x < 0 || r.y < 0 || r.z > 1 || r.w > 1 || r.x >= r.z || r.y >= r.w)
                    Invalid(regionPath + ".rect", "expected [u0, v0, u1, v1] with 0 <= u0 < u1 <= 1 and 0 <= v0 < v1 <= 1");
                // The stem defaults to the bottom center of the rectangle.
                region.stem = XMFLOAT2((r.x + r.z) / 2, r.w);
                if (it.value().contains("stem"))
                {
                    region.stem = Vector2(it.value()["stem"], regionPath + ".stem");
                    if (region.stem.x < r.x || region.stem.x > r.z || region.stem.y < r.y || region.stem.y > r.w)
                        Invalid(regionPath + ".stem", "must lie inside rect");
                }
                result.regions.emplace(it.key(), region);
            }
            return result;
        }

        template<class T>
        void ReadLeaves(const Json& value, const std::string& path, const SceneDefinition& scene, T& object)
        {
            if (!value.contains("leaves")) return;
            const auto& leaves = value["leaves"];
            const auto leavesPath = path + ".leaves";
            Fields(leaves, leavesPath, { "atlas", "regions", "twistDegrees", "sizeVariation" });
            if (!leaves.contains("atlas")) Invalid(leavesPath + ".atlas", "required property");
            object.leaves.atlas = String(leaves["atlas"], leavesPath + ".atlas");
            const auto atlas = scene.atlases.find(object.leaves.atlas);
            if (atlas == scene.atlases.end()) Invalid(leavesPath + ".atlas", "unknown atlas '" + object.leaves.atlas + "'");
            if (leaves.contains("twistDegrees"))
            {
                object.leaves.twistDegrees = Number(leaves["twistDegrees"], leavesPath + ".twistDegrees");
                if (object.leaves.twistDegrees < 0 || object.leaves.twistDegrees > 180)
                    Invalid(leavesPath + ".twistDegrees", "must be between 0 and 180");
            }
            if (leaves.contains("sizeVariation"))
            {
                object.leaves.sizeVariation = Number(leaves["sizeVariation"], leavesPath + ".sizeVariation");
                if (object.leaves.sizeVariation < 0 || object.leaves.sizeVariation > 0.9f)
                    Invalid(leavesPath + ".sizeVariation", "must be between 0 and 0.9");
            }
            if (!leaves.contains("regions")) return;
            const auto& regions = leaves["regions"];
            if (!regions.is_object() || regions.empty()) Invalid(leavesPath + ".regions", "expected region names with weights");
            for (auto it = regions.begin(); it != regions.end(); ++it)
            {
                const auto regionPath = leavesPath + ".regions." + it.key();
                if (!atlas->second.regions.count(it.key()))
                    Invalid(regionPath, "atlas '" + object.leaves.atlas + "' has no region '" + it.key() + "'");
                LeafRegionWeight choice;
                choice.region = it.key();
                choice.weight = Number(it.value(), regionPath);
                if (choice.weight <= 0) Invalid(regionPath, "weight must be positive");
                object.leaves.regions.push_back(choice);
            }
        }

        // Applies an instance's trunk/leaf overrides on top of the materials copied from its source.
        template<class T>
        void ReadInstanceTreeMaterials(const Json& value, const std::string& path, const SceneDefinition& scene, T& object)
        {
            if (!value.contains("materials")) return;
            const auto& materials = value["materials"];
            Fields(materials, path + ".materials", { "trunk", "leaf" });
            if (!object.hasMaterials && !(materials.contains("trunk") && materials.contains("leaf")))
                Invalid(path + ".materials", "the instanced object uses default materials, so specify both trunk and leaf");
            object.hasMaterials = true;
            if (materials.contains("trunk"))
            {
                object.trunk = MaterialReference(materials["trunk"], path + ".materials.trunk", scene);
                object.overridesMaterial = true;
            }
            if (materials.contains("leaf"))
            {
                object.leaf = MaterialReference(materials["leaf"], path + ".materials.leaf", scene);
                object.overridesLeafMaterial = true;
            }
        }

        std::unique_ptr<ObjectDefinition> ReadInstance(const Json& value, const std::string& path, const SceneDefinition& scene)
        {
            if (value.contains("type")) Invalid(path + ".type", "is inherited from instanceOf; remove it");
            for (auto field : { "textureCoordScale", "animationSpeed", "depthLOD", "shape", "trunkShape", "leaves", "generator", "dataFile", "source", "forceLit" })
                if (value.contains(field)) Invalid(path + "." + field, "is inherited from instanceOf; an instance sets only transform and materials");
            Fields(value, path, { "id", "instanceOf", "transform", "material", "materials" });
            if (!value.contains("id")) Invalid(path + ".id", "required property");
            const std::string id = String(value["id"], path + ".id");
            if (id.empty()) Invalid(path + ".id", "must not be empty");
            const std::string sourceId = String(value["instanceOf"], path + ".instanceOf");
            if (sourceId == id) Invalid(path + ".instanceOf", "an object cannot be an instance of itself");
            const ObjectDefinition* source = nullptr;
            for (const auto& existing : scene.objects)
                if (existing->id == sourceId) source = existing.get();
            if (!source) Invalid(path + ".instanceOf", "unknown object '" + sourceId + "'; instances must follow the object they copy");

            auto result = source->Clone();
            result->id = id;
            result->instanceOf = sourceId;
            result->overridesMaterial = result->overridesLeafMaterial = false;
            result->transform = value.contains("transform") ? ReadTransform(value["transform"], path + ".transform") : Transform();
            if (result->kind == ObjectKind::Model && (value.contains("material") || value.contains("materials")))
                Invalid(path, "glTF model instances use the model's own materials");
            if (value.contains("material"))
            {
                if (result->kind != ObjectKind::Primitive)
                    Invalid(path + ".material", "only primitive instances take a material; trees use materials.trunk/leaf");
                auto& primitive = static_cast<PrimitiveDefinition&>(*result);
                primitive.material = MaterialReference(value["material"], path + ".material", scene);
                primitive.hasMaterial = true;
                primitive.overridesMaterial = true;
            }
            if (value.contains("materials"))
            {
                switch (result->kind)
                {
                case ObjectKind::Tree: ReadInstanceTreeMaterials(value, path, scene, static_cast<TreeDefinition&>(*result)); break;
                case ObjectKind::FixedTree: ReadInstanceTreeMaterials(value, path, scene, static_cast<FixedTreeDefinition&>(*result)); break;
                case ObjectKind::Graph: ReadInstanceTreeMaterials(value, path, scene, static_cast<GraphDefinition&>(*result)); break;
                default: Invalid(path + ".materials", "only tree, fixedTree, and graph instances take trunk/leaf materials");
                }
            }
            return result;
        }
    }

    SceneDefinition ParseScene(const std::string& jsonText, const std::string& sourceName)
    {
        try
        {
            const auto root = Json::parse(jsonText);
            Fields(root, sourceName, { "version", "id", "name", "priority", "camera", "render", "light", "pointLightCount", "pointLight", "materials", "atlases", "objects" });
            if (!root.contains("version") || Integer(root["version"], sourceName + ".version", 1, 1) != 1)
                Invalid(sourceName + ".version", "expected schema version 1");
            SceneDefinition scene;
            if (root.contains("id"))
            {
                scene.id = String(root["id"], sourceName + ".id");
                if (scene.id.empty()) Invalid(sourceName + ".id", "must not be empty");
                for (unsigned char character : scene.id)
                    if (character < 32 || character == 127 || character == ':' || character == '/' || character == '\\')
                        Invalid(sourceName + ".id", "must not contain control characters, colons, or path separators");
            }
            if (!root.contains("name")) Invalid(sourceName + ".name", "required property");
            scene.name = String(root["name"], sourceName + ".name");
            if (scene.name.empty()) Invalid(sourceName + ".name", "must not be empty");
            if (root.contains("priority"))
                scene.priority = Integer(root["priority"], sourceName + ".priority", 0, std::numeric_limits<int>::max());
            if (root.contains("camera"))
            {
                auto camera = root["camera"];
                Fields(camera, sourceName + ".camera", { "position", "scale", "rotation", "rotationDegrees", "animation" });
                if (camera.contains("animation"))
                {
                    const auto& animation = camera["animation"];
                    const auto animationPath = sourceName + ".camera.animation";
                    Fields(animation, animationPath, { "loop", "keys" });
                    if (animation.contains("loop"))
                        scene.cameraAnimationLoop = Boolean(animation["loop"], animationPath + ".loop");
                    if (!animation.contains("keys")) Invalid(animationPath + ".keys", "required property");
                    const auto& keys = animation["keys"];
                    const auto path = animationPath + ".keys";
                    if (!keys.is_array() || keys.size() < 2) Invalid(path, "expected at least two keys");
                    for (size_t i = 0; i < keys.size(); ++i)
                    {
                        const auto& value = keys[i];
                        const auto item = path + "[" + std::to_string(i) + "]";
                        Fields(value, item, { "time", "position", "lookAt" });
                        if (!value.contains("time") || !value.contains("position") || !value.contains("lookAt"))
                            Invalid(item, "time, position, and lookAt are required");
                        CameraAnimationKey key;
                        key.time = Number(value["time"], item + ".time");
                        key.position = Vector3(value["position"], item + ".position");
                        key.lookAt = Vector3(value["lookAt"], item + ".lookAt");
                        if (key.time < 0 || (i == 0 && key.time != 0) || (i > 0 && key.time <= scene.cameraAnimation.back().time))
                            Invalid(item + ".time", "first key must be zero and times must strictly increase");
                        if (XMVector3LessOrEqual(XMVector3LengthSq(XMLoadFloat3(&key.lookAt)), XMVectorReplicate(0.000001f)))
                            Invalid(item + ".lookAt", "must be nonzero");
                        scene.cameraAnimation.push_back(key);
                    }
                }
                camera.erase("animation");
                scene.camera = ReadTransform(camera, sourceName + ".camera");
            }
            if (root.contains("pointLightCount")) scene.pointLightCount = Integer(root["pointLightCount"], sourceName + ".pointLightCount", 0, 1);
            if (root.contains("pointLight"))
            {
                const auto& light = root["pointLight"];
                const auto path = sourceName + ".pointLight";
                Fields(light, path, { "ambient", "diffuse", "specular", "position", "range", "attenuation" });
                if (light.contains("ambient")) scene.pointLight.ambient = Vector4(light["ambient"], path + ".ambient");
                if (light.contains("diffuse")) scene.pointLight.diffuse = Vector4(light["diffuse"], path + ".diffuse");
                if (light.contains("specular")) scene.pointLight.specular = Vector4(light["specular"], path + ".specular");
                if (light.contains("position")) scene.pointLight.position = Vector3(light["position"], path + ".position");
                if (light.contains("range")) scene.pointLight.range = Number(light["range"], path + ".range");
                if (light.contains("attenuation")) scene.pointLight.attenuation = Vector3(light["attenuation"], path + ".attenuation");
                if (scene.pointLight.range < 0 || scene.pointLight.attenuation.x < 0 ||
                    scene.pointLight.attenuation.y < 0 || scene.pointLight.attenuation.z < 0)
                    Invalid(path, "range and attenuation must be nonnegative");
            }
            if (root.contains("render"))
            {
                const auto& render = root["render"];
                auto path = sourceName + ".render";
                Fields(render, path, { "clearColor", "farClippingPlane", "useShadowMaps", "useAlphaBlendedRenderTarget",
                    "enablePostProcessing", "blurAmount", "environment", "sky", "ambientOcclusion" });
                if (render.contains("ambientOcclusion"))
                {
                    const auto& ao = render["ambientOcclusion"];
                    const auto aoPath = path + ".ambientOcclusion";
                    Fields(ao, aoPath, { "radius", "strength" });
                    if (ao.contains("radius"))
                    {
                        scene.ambientOcclusionRadius = Number(ao["radius"], aoPath + ".radius");
                        if (scene.ambientOcclusionRadius < 0)
                            Invalid(aoPath + ".radius", "must be nonnegative");
                    }
                    if (ao.contains("strength"))
                        scene.ambientOcclusionStrength = UnitNumber(ao["strength"], aoPath + ".strength");
                }
                if (render.contains("sky"))
                {
                    const auto& sky = render["sky"];
                    Fields(sky, path + ".sky", { "cubeMap", "rotationDegrees" });
                    if (!sky.contains("cubeMap")) Invalid(path + ".sky.cubeMap", "required property");
                    scene.skyCubeMap = String(sky["cubeMap"], path + ".sky.cubeMap");
                    std::string ext = scene.skyCubeMap.size() >= 4 ? scene.skyCubeMap.substr(scene.skyCubeMap.size() - 4) : "";
                    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext != ".dds") Invalid(path + ".sky.cubeMap", "expected a DDS cube map");
                    if (sky.contains("rotationDegrees"))
                        scene.skyRotationDegrees = Number(sky["rotationDegrees"], path + ".sky.rotationDegrees");
                }
                if (render.contains("environment"))
                {
                    const auto& environment = render["environment"];
                    Fields(environment, path + ".environment", { "diffuse", "specular", "rotationDegrees" });
                    if (!environment.contains("diffuse") || !environment.contains("specular"))
                        Invalid(path + ".environment", "diffuse and specular cube maps are both required");
                    scene.environmentDiffuse = String(environment["diffuse"], path + ".environment.diffuse");
                    scene.environmentSpecular = String(environment["specular"], path + ".environment.specular");
                    if (environment.contains("rotationDegrees"))
                        scene.environmentRotationDegrees = Number(environment["rotationDegrees"], path + ".environment.rotationDegrees");
                    if (scene.environmentDiffuse.empty() || scene.environmentSpecular.empty())
                        Invalid(path + ".environment", "cube map paths must be nonempty");
                }
                if (render.contains("clearColor")) scene.clearColor = Vector4(render["clearColor"], path + ".clearColor");
                if (render.contains("farClippingPlane"))
                {
                    scene.farClippingPlane = Number(render["farClippingPlane"], path + ".farClippingPlane");
                    if (scene.farClippingPlane <= 0.2f) Invalid(path + ".farClippingPlane", "must be greater than the 0.2-unit near clipping plane");
                }
                if (render.contains("enablePostProcessing"))
                    scene.enablePostProcessing = Boolean(render["enablePostProcessing"], path + ".enablePostProcessing");
                if (render.contains("blurAmount"))
                {
                    scene.blurAmount = Number(render["blurAmount"], path + ".blurAmount");
                    if (scene.blurAmount < 0 || scene.blurAmount > 2.5f)
                        Invalid(path + ".blurAmount", "must be between 0 and 2.5");
                }
                if (render.contains("useShadowMaps")) scene.useShadowMaps = Boolean(render["useShadowMaps"], path + ".useShadowMaps");
                if (render.contains("useAlphaBlendedRenderTarget")) scene.useAlphaBlendedRenderTarget = Boolean(render["useAlphaBlendedRenderTarget"], path + ".useAlphaBlendedRenderTarget");
            }
            if (root.contains("light"))
            {
                const auto& light = root["light"];
                auto path = sourceName + ".light";
                Fields(light, path, { "ambient", "diffuse", "specular", "direction", "shadowPosition", "angularDiameterDegrees", "shadowSamples" });
                if (light.contains("shadowPosition"))
                {
                    scene.hasShadowLightPosition = true;
                    scene.shadowLightPosition = Vector3(light["shadowPosition"], path + ".shadowPosition");
                }
                if (light.contains("ambient")) scene.light.ambient = Vector4(light["ambient"], path + ".ambient");
                if (light.contains("diffuse")) scene.light.diffuse = Vector4(light["diffuse"], path + ".diffuse");
                if (light.contains("specular")) scene.light.specular = Vector4(light["specular"], path + ".specular");
                if (light.contains("direction")) scene.light.direction = Vector3(light["direction"], path + ".direction");
                if (light.contains("angularDiameterDegrees"))
                {
                    scene.light.angularDiameterDegrees = Number(light["angularDiameterDegrees"], path + ".angularDiameterDegrees");
                    if (scene.light.angularDiameterDegrees < 0 || scene.light.angularDiameterDegrees > 10)
                        Invalid(path + ".angularDiameterDegrees", "must be between 0 and 10");
                }
                if (light.contains("shadowSamples"))
                    scene.light.shadowSamples = Integer(light["shadowSamples"], path + ".shadowSamples", 1, 32);
                const auto& d = scene.light.direction;
                if (d.x == 0 && d.y == 0 && d.z == 0) Invalid(path + ".direction", "must be nonzero");
            }
            if (root.contains("materials"))
            {
                const auto& materials = root["materials"];
                if (!materials.is_object()) Invalid(sourceName + ".materials", "expected named materials");
                for (auto it = materials.begin(); it != materials.end(); ++it)
                    scene.materials.emplace(it.key(), ReadMaterial(it.value(), sourceName + ".materials." + it.key()));
            }
            if (root.contains("atlases"))
            {
                const auto& atlases = root["atlases"];
                if (!atlases.is_object()) Invalid(sourceName + ".atlases", "expected named atlases");
                for (auto it = atlases.begin(); it != atlases.end(); ++it)
                    scene.atlases.emplace(it.key(), ReadAtlas(it.value(), sourceName + ".atlases." + it.key()));
            }
            if (!root.contains("objects") || !root["objects"].is_array() || root["objects"].empty())
                Invalid(sourceName + ".objects", "expected a nonempty array");
            std::set<std::string> ids;
            for (size_t i = 0; i < root["objects"].size(); ++i)
            {
                const auto& value = root["objects"][i];
                auto path = sourceName + ".objects[" + std::to_string(i) + "]";
                if (value.is_object() && value.contains("instanceOf"))
                {
                    auto instance = ReadInstance(value, path, scene);
                    if (!ids.insert(instance->id).second) Invalid(path + ".id", "duplicate object ID '" + instance->id + "'");
                    scene.objects.push_back(std::move(instance));
                    continue;
                }
                if (!value.is_object() || !value.contains("type")) Invalid(path + ".type", "required property");
                std::string type = String(value["type"], path + ".type");
                std::unique_ptr<ObjectDefinition> object;
                if (type == "primitive")
                {
                    if (value.contains("cubeMap"))
                        Invalid(path + ".cubeMap", "skybox primitives are no longer supported; use render.sky");
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "shape", "material" });
                    auto primitive = std::make_unique<PrimitiveDefinition>();
                    if (value.contains("shape")) primitive->shape = ReadShape(value["shape"], path + ".shape");
                    if (value.contains("material"))
                    {
                        primitive->hasMaterial = true;
                        primitive->material = MaterialReference(value["material"], path + ".material", scene);
                    }
                    object = std::move(primitive);
                }
                else if (type == "tree")
                {
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "trunkShape", "materials", "leaves", "generator" });
                    auto tree = std::make_unique<TreeDefinition>();
                    if (value.contains("trunkShape")) tree->trunkShape = ReadShape(value["trunkShape"], path + ".trunkShape");
                    if (tree->trunkShape != Shape::Cylinder && tree->trunkShape != Shape::SkinnedCylinder)
                        Invalid(path + ".trunkShape", "expected cylinder or skinnedCylinder");
                    ReadTreeMaterials(value, path, scene, *tree);
                    ReadLeaves(value, path, scene, *tree);
                    if (!value.contains("generator")) Invalid(path + ".generator", "required property");
                    tree->generator = ReadGenerator(value["generator"], path + ".generator");
                    object = std::move(tree);
                }
                else if (type == "fixedTree")
                {
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "trunkShape", "materials", "leaves" });
                    auto tree = std::make_unique<FixedTreeDefinition>();
                    if (value.contains("trunkShape")) tree->trunkShape = ReadShape(value["trunkShape"], path + ".trunkShape");
                    if (tree->trunkShape != Shape::Cylinder && tree->trunkShape != Shape::SkinnedCylinder)
                        Invalid(path + ".trunkShape", "expected cylinder or skinnedCylinder");
                    ReadTreeMaterials(value, path, scene, *tree);
                    ReadLeaves(value, path, scene, *tree);
                    object = std::move(tree);
                }
                else if (type == "graph")
                {
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "dataFile", "materials", "leaves" });
                    auto graph = std::make_unique<GraphDefinition>();
                    if (!value.contains("dataFile")) Invalid(path + ".dataFile", "required property");
                    graph->dataFile = String(value["dataFile"], path + ".dataFile");
                    if (graph->dataFile.empty()) Invalid(path + ".dataFile", "must not be empty");
                    ReadTreeMaterials(value, path, scene, *graph);
                    ReadLeaves(value, path, scene, *graph);
                    object = std::move(graph);
                }
                else if (type == "model")
                {
                    Fields(value, path, { "id", "type", "transform", "source", "forceLit", "normalAnimation", "materialTint" });
                    auto model = std::make_unique<ModelDefinition>();
                    if (!value.contains("source")) Invalid(path + ".source", "required property");
                    model->source = String(value["source"], path + ".source");
                    if (model->source.empty()) Invalid(path + ".source", "must not be empty");
                    if (value.contains("forceLit"))
                        model->forceLit = Boolean(value["forceLit"], path + ".forceLit");
                    if (value.contains("normalAnimation"))
                    {
                        const auto& animation = value["normalAnimation"];
                        const auto animationPath = path + ".normalAnimation";
                        Fields(animation, animationPath, { "material", "velocity1", "velocity2", "strength" });
                        for (auto key : { "material", "velocity1", "velocity2" })
                            if (!animation.contains(key)) Invalid(animationPath + "." + key, "required property");
                        model->animatedNormalMaterial = String(animation["material"], animationPath + ".material");
                        if (model->animatedNormalMaterial.empty()) Invalid(animationPath + ".material", "must not be empty");
                        model->normalVelocity1 = Vector2(animation["velocity1"], animationPath + ".velocity1");
                        model->normalVelocity2 = Vector2(animation["velocity2"], animationPath + ".velocity2");
                        if (animation.contains("strength"))
                        {
                            model->normalStrength = Number(animation["strength"], animationPath + ".strength");
                            if (model->normalStrength < 0 || model->normalStrength > 16)
                                Invalid(animationPath + ".strength", "must be between 0 and 16");
                        }
                    }
                    if (value.contains("materialTint"))
                    {
                        const auto& tint = value["materialTint"];
                        const auto tintPath = path + ".materialTint";
                        Fields(tint, tintPath, { "material", "texture", "uvScale", "uvOffset" });
                        for (auto key : { "material", "texture" })
                            if (!tint.contains(key)) Invalid(tintPath + "." + key, "required property");
                        model->tintMaterial = String(tint["material"], tintPath + ".material");
                        if (model->tintMaterial.empty()) Invalid(tintPath + ".material", "must not be empty");
                        model->tintTexture = String(tint["texture"], tintPath + ".texture");
                        if (model->tintTexture.empty()) Invalid(tintPath + ".texture", "must not be empty");
                        if (tint.contains("uvScale")) model->tintUvScale = Vector2(tint["uvScale"], tintPath + ".uvScale");
                        if (tint.contains("uvOffset")) model->tintUvOffset = Vector2(tint["uvOffset"], tintPath + ".uvOffset");
                    }
                    auto dot = model->source.find_last_of('.');
                    std::string extension = dot == std::string::npos
                        ? std::string()
                        : model->source.substr(dot);
                    for (auto& character : extension)
                        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                    if (extension != ".gltf" && extension != ".glb")
                        Invalid(path + ".source", "expected a .gltf or .glb asset");
                    object = std::move(model);
                }
                else Invalid(path + ".type", "unknown object type '" + type + "'");
                CommonObject(value, path, *object);
                if (!ids.insert(object->id).second) Invalid(path + ".id", "duplicate object ID '" + object->id + "'");
                scene.objects.push_back(std::move(object));
            }
            return scene;
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(sourceName + ": invalid JSON: " + error.what());
        }
    }

    SceneDefinition ReadScene(const std::string& filename)
    {
        std::ifstream input(WidePath(filename).c_str(), std::ios::binary);
        if (!input) throw std::runtime_error(filename + ": cannot open scene file");
        std::ostringstream text;
        text << input.rdbuf();
        if (input.bad()) throw std::runtime_error(filename + ": cannot read scene file");
        return ParseScene(text.str(), filename);
    }

    namespace
    {
        Json VectorJson(const DirectX::XMFLOAT2& v) { return Json::array({ v.x, v.y }); }
        Json VectorJson(const DirectX::XMFLOAT3& v) { return Json::array({ v.x, v.y, v.z }); }
        Json VectorJson(const DirectX::XMFLOAT4& v) { return Json::array({ v.x, v.y, v.z, v.w }); }
        Json TransformJson(const Transform& t)
        {
            return { { "position", VectorJson(t.position) }, { "rotation", VectorJson(t.rotation) }, { "scale", VectorJson(t.scale) } };
        }
        Json TextureMapJson(const TextureMapDefinition& map)
        {
            Json result = { { "source", map.source }, { "wrap", map.wrap } };
            if (map.hasTransform)
                result["transform"] = { { "offset", VectorJson(map.offset) }, { "rotation", map.rotation }, { "scale", VectorJson(map.scale) } };
            return result;
        }
        Json MaterialJson(const MaterialDefinition& p)
        {
            Json result = { { "baseColor", VectorJson(p.baseColor) }, { "metallic", p.metallic }, { "roughness", p.roughness },
                { "alphaMode", p.alphaMode == AlphaMode::Mask ? "mask" : p.alphaMode == AlphaMode::Blend ? "blend" : "opaque" },
                { "alphaCutoff", p.alphaCutoff }, { "unlit", p.unlit }, { "textureCoordScale", VectorJson(p.textureCoordScale) } };
            if (!p.unlit)
            {
                result["specular"] = p.specular;
                result["specularColor"] = VectorJson(p.specularColor);
                result["ior"] = p.ior;
                result["transmission"] = p.transmission;
            }
            if (!p.baseColorTexture.source.empty()) result["baseColorTexture"] = TextureMapJson(p.baseColorTexture);
            if (!p.metallicRoughnessTexture.source.empty()) result["metallicRoughnessTexture"] = TextureMapJson(p.metallicRoughnessTexture);
            if (!p.normalTexture.source.empty())
            {
                result["normalTexture"] = TextureMapJson(p.normalTexture);
                result["normalTexture"]["scale"] = p.normalScale;
            }
            if (!p.specularTexture.source.empty()) result["specularTexture"] = TextureMapJson(p.specularTexture);
            if (!p.specularColorTexture.source.empty()) result["specularColorTexture"] = TextureMapJson(p.specularColorTexture);
            return result;
        }
        const char* ShapeName(Shape shape)
        {
            switch (shape)
            {
            case Shape::Box: return "box";
            case Shape::Cylinder: return "cylinder";
            case Shape::CylinderLD: return "cylinderLD";
            case Shape::CylinderHD: return "cylinderHD";
            case Shape::SkinnedCylinder: return "skinnedCylinder";
            case Shape::Sprite: return "sprite";
            case Shape::Sphere: return "sphere";
            }
            throw std::runtime_error("Cannot serialize unsupported shape");
        }
        template<class T>
        void TreeMaterialsJson(Json& value, const T& tree)
        {
            if (tree.hasMaterials)
                value["materials"] = { { "trunk", MaterialJson(tree.trunk) }, { "leaf", MaterialJson(tree.leaf) } };
            if (tree.leaves.atlas.empty()) return;
            value["leaves"] = { { "atlas", tree.leaves.atlas } };
            for (const auto& choice : tree.leaves.regions) value["leaves"]["regions"][choice.region] = choice.weight;
            if (tree.leaves.twistDegrees != 0) value["leaves"]["twistDegrees"] = tree.leaves.twistDegrees;
            if (tree.leaves.sizeVariation != 0) value["leaves"]["sizeVariation"] = tree.leaves.sizeVariation;
        }
        [[noreturn]] void FileError(const std::string& filename, const char* action, DWORD error = GetLastError())
        {
            throw std::runtime_error(filename + ": " + action + " (Windows error " + std::to_string(error) + ")");
        }
    }

    std::string SerializeScene(const SceneDefinition& scene)
    {
        Json root = { { "version", 1 }, { "name", scene.name }, { "priority", scene.priority },
            { "camera", TransformJson(scene.camera) },
            { "render", { { "clearColor", VectorJson(scene.clearColor) }, { "farClippingPlane", scene.farClippingPlane },
                { "useShadowMaps", scene.useShadowMaps }, { "useAlphaBlendedRenderTarget", scene.useAlphaBlendedRenderTarget },
                { "enablePostProcessing", scene.enablePostProcessing }, { "blurAmount", scene.blurAmount } } },
            { "light", { { "ambient", VectorJson(scene.light.ambient) }, { "diffuse", VectorJson(scene.light.diffuse) },
                { "specular", VectorJson(scene.light.specular) }, { "direction", VectorJson(scene.light.direction) } } },
            { "pointLightCount", scene.pointLightCount }, { "pointLight", {
                { "ambient", VectorJson(scene.pointLight.ambient) }, { "diffuse", VectorJson(scene.pointLight.diffuse) },
                { "specular", VectorJson(scene.pointLight.specular) }, { "position", VectorJson(scene.pointLight.position) },
                { "range", scene.pointLight.range }, { "attenuation", VectorJson(scene.pointLight.attenuation) } } },
            { "objects", Json::array() } };
        if (!scene.id.empty()) root["id"] = scene.id;
        if (!scene.cameraAnimation.empty())
        {
            root["camera"]["animation"] = { { "loop", scene.cameraAnimationLoop }, { "keys", Json::array() } };
            for (const auto& key : scene.cameraAnimation)
                root["camera"]["animation"]["keys"].push_back({ { "time", key.time }, { "position", VectorJson(key.position) },
                    { "lookAt", VectorJson(key.lookAt) } });
        }
        if (scene.environmentDiffuse.empty() != scene.environmentSpecular.empty())
            throw std::runtime_error("Scene environment needs both diffuse and specular cube maps");
        if (scene.ambientOcclusionRadius != 0 || scene.ambientOcclusionStrength != 1)
            root["render"]["ambientOcclusion"] = { { "radius", scene.ambientOcclusionRadius },
                { "strength", scene.ambientOcclusionStrength } };
        if (!scene.environmentDiffuse.empty())
        {
            root["render"]["environment"] = { { "diffuse", scene.environmentDiffuse },
                { "specular", scene.environmentSpecular } };
            if (scene.environmentRotationDegrees != 0)
                root["render"]["environment"]["rotationDegrees"] = scene.environmentRotationDegrees;
        }
        if (!scene.skyCubeMap.empty())
        {
            root["render"]["sky"] = { { "cubeMap", scene.skyCubeMap } };
            if (scene.skyRotationDegrees != 0)
                root["render"]["sky"]["rotationDegrees"] = scene.skyRotationDegrees;
        }
        if (scene.hasShadowLightPosition) root["light"]["shadowPosition"] = VectorJson(scene.shadowLightPosition);
        if (scene.light.angularDiameterDegrees != 0)
            root["light"]["angularDiameterDegrees"] = scene.light.angularDiameterDegrees;
        if (scene.light.shadowSamples != 8)
            root["light"]["shadowSamples"] = scene.light.shadowSamples;
        if (!scene.materials.empty())
            for (const auto& material : scene.materials) root["materials"][material.first] = MaterialJson(material.second);
        for (const auto& atlas : scene.atlases)
        {
            Json value = { { "aspect", atlas.second.aspect }, { "regions", Json::object() } };
            for (const auto& region : atlas.second.regions)
                value["regions"][region.first] = { { "rect", VectorJson(region.second.rect) }, { "stem", VectorJson(region.second.stem) } };
            root["atlases"][atlas.first] = std::move(value);
        }
        for (const auto& object : scene.objects)
        {
            if (!object) throw std::runtime_error("Cannot serialize a null scene object");
            if (!object->instanceOf.empty())
            {
                Json instance = { { "id", object->id }, { "instanceOf", object->instanceOf },
                    { "transform", TransformJson(object->transform) } };
                if (object->kind == ObjectKind::Primitive && object->overridesMaterial)
                    instance["material"] = MaterialJson(dynamic_cast<const PrimitiveDefinition&>(*object).material);
                auto treeMaterials = [&](const MaterialDefinition& trunk, const MaterialDefinition& leaf)
                {
                    if (object->overridesMaterial) instance["materials"]["trunk"] = MaterialJson(trunk);
                    if (object->overridesLeafMaterial) instance["materials"]["leaf"] = MaterialJson(leaf);
                };
                if (object->kind == ObjectKind::Tree)
                {
                    const auto& tree = dynamic_cast<const TreeDefinition&>(*object);
                    treeMaterials(tree.trunk, tree.leaf);
                }
                else if (object->kind == ObjectKind::FixedTree)
                {
                    const auto& tree = dynamic_cast<const FixedTreeDefinition&>(*object);
                    treeMaterials(tree.trunk, tree.leaf);
                }
                else if (object->kind == ObjectKind::Graph)
                {
                    const auto& graph = dynamic_cast<const GraphDefinition&>(*object);
                    treeMaterials(graph.trunk, graph.leaf);
                }
                root["objects"].push_back(std::move(instance));
                continue;
            }
            Json value = { { "id", object->id }, { "transform", TransformJson(object->transform) } };
            if (object->kind != ObjectKind::Model)
            {
                value["textureCoordScale"] = VectorJson(object->textureCoordScale);
                value["animationSpeed"] = object->animationSpeed;
                value["depthLOD"] = object->depthLOD;
            }
            switch (object->kind)
            {
            case ObjectKind::Primitive:
            {
                const auto& primitive = dynamic_cast<const PrimitiveDefinition&>(*object);
                value["type"] = "primitive";
                value["shape"] = ShapeName(primitive.shape);
                if (primitive.hasMaterial) value["material"] = MaterialJson(primitive.material);
                break;
            }
            case ObjectKind::Tree:
            {
                const auto& tree = dynamic_cast<const TreeDefinition&>(*object);
                value["type"] = "tree";
                value["trunkShape"] = ShapeName(tree.trunkShape);
                TreeMaterialsJson(value, tree);
                const auto& g = tree.generator;
                if (g.segmentLengthBehavior != SegmentLengthBehavior::Constant && g.segmentLengthBehavior != SegmentLengthBehavior::RandomAddition)
                    throw std::runtime_error("Cannot serialize unsupported segment-length behavior");
                value["generator"] = { { "axiom", g.axiom }, { "constants", g.constants }, { "iterations", g.iterations },
                    { "angle", g.angle }, { "segmentLength", g.segmentLength }, { "thickness", g.thickness },
                    { "initialDirection", VectorJson(g.initialDirection) },
                    { "segmentLengthBehavior", g.segmentLengthBehavior == SegmentLengthBehavior::Constant ? "constant" : "randomAddition" },
                    { "rules", Json::array() } };
                for (const auto& rule : g.rules)
                    value["generator"]["rules"].push_back({ { "input", rule.input }, { "output", rule.output }, { "iterations", rule.iterations } });
                break;
            }
            case ObjectKind::FixedTree:
            {
                const auto& tree = dynamic_cast<const FixedTreeDefinition&>(*object);
                value["type"] = "fixedTree";
                value["trunkShape"] = ShapeName(tree.trunkShape);
                TreeMaterialsJson(value, tree);
                break;
            }
            case ObjectKind::Graph:
            {
                const auto& graph = dynamic_cast<const GraphDefinition&>(*object);
                value["type"] = "graph";
                value["dataFile"] = graph.dataFile;
                TreeMaterialsJson(value, graph);
                break;
            }
            case ObjectKind::Model:
            {
                const auto& model = dynamic_cast<const ModelDefinition&>(*object);
                value["type"] = "model";
                value["source"] = model.source;
                value["forceLit"] = model.forceLit;
                if (!model.animatedNormalMaterial.empty())
                    value["normalAnimation"] = { { "material", model.animatedNormalMaterial },
                        { "velocity1", VectorJson(model.normalVelocity1) }, { "velocity2", VectorJson(model.normalVelocity2) },
                        { "strength", model.normalStrength } };
                if (!model.tintMaterial.empty())
                    value["materialTint"] = { { "material", model.tintMaterial }, { "texture", model.tintTexture },
                        { "uvScale", VectorJson(model.tintUvScale) }, { "uvOffset", VectorJson(model.tintUvOffset) } };
                break;
            }
            default: throw std::runtime_error("Cannot serialize unsupported scene object");
            }
            root["objects"].push_back(std::move(value));
        }
        const auto text = root.dump(2) + "\n";
        ParseScene(text, "Scene being saved");
        return text;
    }

    std::wstring WidePath(const std::string& path)
    {
        if (path.empty()) return {};
        if (path.find('\0') != std::string::npos) throw std::runtime_error("Path contains an embedded NUL");
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
        if (!count) throw std::runtime_error("Path is not valid UTF-8");
        std::vector<wchar_t> buffer(count);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, buffer.data(), count))
            FileError(path, "cannot convert path");
        return buffer.data();
    }

    std::string Utf8Path(const std::wstring& path)
    {
        if (path.empty()) return {};
        int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (!count) throw std::runtime_error("Path is not valid Unicode");
        std::vector<char> buffer(count);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(), -1, buffer.data(), count, nullptr, nullptr))
            throw std::runtime_error("Cannot convert Unicode path");
        return buffer.data();
    }

    std::string AbsolutePath(const std::string& path)
    {
        const auto wide = WidePath(path);
        DWORD count = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
        if (!count) FileError(path, "cannot resolve path");
        std::vector<wchar_t> buffer(count);
        DWORD length = GetFullPathNameW(wide.c_str(), count, buffer.data(), nullptr);
        if (!length || length >= count) FileError(path, "cannot resolve path");
        return Utf8Path(buffer.data());
    }

    std::string ParentPath(const std::string& path)
    {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }

    std::string ResolveSceneAsset(const std::string& asset, const std::string& sceneFilename)
    {
        if (asset.empty()) return {};
        std::vector<wchar_t> executable(32768);
        DWORD size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!size || size >= executable.size()) FileError(asset, "cannot locate runtime resources");
        std::vector<std::string> candidates;
        const bool absolute = asset.size() > 1 && (asset[1] == ':' || asset[0] == '\\' || asset[0] == '/');
        if (!absolute && !sceneFilename.empty()) candidates.push_back(ParentPath(sceneFilename) + asset);
        candidates.push_back(asset);
        candidates.push_back(ParentPath(Utf8Path(executable.data())) + asset);
        candidates.push_back("Game\\Resources\\" + asset);
        candidates.push_back("Game\\" + asset);
        for (const auto& candidate : candidates)
        {
            DWORD attributes = GetFileAttributesW(WidePath(candidate).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return AbsolutePath(candidate);
        }
        throw std::runtime_error(sceneFilename + ": asset '" + asset + "' was not found");
    }

    void WriteScene(const SceneDefinition& scene, const std::string& filename, bool overwrite)
    {
        const auto text = SerializeScene(scene);
        if (text.size() > MAXDWORD) throw std::runtime_error(filename + ": scene file is too large");
        static LONG sequence = 0;
        struct TemporaryFile
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            std::wstring path;
            ~TemporaryFile()
            {
                if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
                if (!path.empty() && !DeleteFileW(path.c_str()))
                    OutputDebugStringW((L"Cannot clean up scene save temporary file: " + path + L"\n").c_str());
            }
        } temporary;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            temporary.path = WidePath(filename + ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
                std::to_string(InterlockedIncrement(&sequence)));
            temporary.handle = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (temporary.handle != INVALID_HANDLE_VALUE) break;
            DWORD error = GetLastError();
            temporary.path.clear();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) FileError(filename, "cannot create temporary scene file", error);
        }
        if (temporary.handle == INVALID_HANDLE_VALUE) FileError(filename, "cannot create unique temporary scene file", ERROR_FILE_EXISTS);
        DWORD written = 0;
        if (!WriteFile(temporary.handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size())
            FileError(filename, "cannot write scene file");
        if (!FlushFileBuffers(temporary.handle)) FileError(filename, "cannot flush scene file");
        const HANDLE handle = temporary.handle;
        temporary.handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) FileError(filename, "cannot close scene file");
        if (!MoveFileExW(temporary.path.c_str(), WidePath(filename).c_str(),
            MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
            FileError(filename, "cannot publish scene file");
        temporary.path.clear();
    }
}

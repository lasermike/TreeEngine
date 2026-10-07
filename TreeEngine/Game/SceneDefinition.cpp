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

        MaterialDefinition ReadMaterial(const Json& value, const std::string& path)
        {
            Fields(value, path, { "ambient", "diffuse", "specular", "reflect", "texture", "textureCoordScale", "alphaClipThreshold" });
            MaterialDefinition result;
            if (value.contains("ambient")) result.ambient = Vector4(value["ambient"], path + ".ambient");
            if (value.contains("diffuse")) result.diffuse = Vector4(value["diffuse"], path + ".diffuse");
            if (value.contains("specular")) result.specular = Vector4(value["specular"], path + ".specular");
            if (value.contains("reflect")) result.reflect = Vector4(value["reflect"], path + ".reflect");
            if (value.contains("texture")) result.texture = String(value["texture"], path + ".texture");
            if (value.contains("textureCoordScale")) result.textureCoordScale = Vector2(value["textureCoordScale"], path + ".textureCoordScale");
            if (value.contains("alphaClipThreshold")) result.alphaClipThreshold = Number(value["alphaClipThreshold"], path + ".alphaClipThreshold");
            if (result.alphaClipThreshold < 0 || result.alphaClipThreshold > 1)
                Invalid(path + ".alphaClipThreshold", "expected a value between 0 and 1");
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
    }

    SceneDefinition ParseScene(const std::string& jsonText, const std::string& sourceName)
    {
        try
        {
            const auto root = Json::parse(jsonText);
            Fields(root, sourceName, { "version", "name", "camera", "render", "light", "pointLightCount", "materials", "objects" });
            if (!root.contains("version") || Integer(root["version"], sourceName + ".version", 1, 1) != 1)
                Invalid(sourceName + ".version", "expected schema version 1");
            SceneDefinition scene;
            if (!root.contains("name")) Invalid(sourceName + ".name", "required property");
            scene.name = String(root["name"], sourceName + ".name");
            if (scene.name.empty()) Invalid(sourceName + ".name", "must not be empty");
            if (root.contains("camera")) scene.camera = ReadTransform(root["camera"], sourceName + ".camera");
            if (root.contains("pointLightCount")) scene.pointLightCount = Integer(root["pointLightCount"], sourceName + ".pointLightCount", 0, 1);
            if (root.contains("render"))
            {
                const auto& render = root["render"];
                auto path = sourceName + ".render";
                Fields(render, path, { "clearColor", "useShadowMaps", "useAlphaBlendedRenderTarget" });
                if (render.contains("clearColor")) scene.clearColor = Vector4(render["clearColor"], path + ".clearColor");
                if (render.contains("useShadowMaps")) scene.useShadowMaps = Boolean(render["useShadowMaps"], path + ".useShadowMaps");
                if (render.contains("useAlphaBlendedRenderTarget")) scene.useAlphaBlendedRenderTarget = Boolean(render["useAlphaBlendedRenderTarget"], path + ".useAlphaBlendedRenderTarget");
            }
            if (root.contains("light"))
            {
                const auto& light = root["light"];
                auto path = sourceName + ".light";
                Fields(light, path, { "ambient", "diffuse", "specular", "direction" });
                if (light.contains("ambient")) scene.light.ambient = Vector4(light["ambient"], path + ".ambient");
                if (light.contains("diffuse")) scene.light.diffuse = Vector4(light["diffuse"], path + ".diffuse");
                if (light.contains("specular")) scene.light.specular = Vector4(light["specular"], path + ".specular");
                if (light.contains("direction")) scene.light.direction = Vector3(light["direction"], path + ".direction");
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
            if (!root.contains("objects") || !root["objects"].is_array() || root["objects"].empty())
                Invalid(sourceName + ".objects", "expected a nonempty array");
            std::set<std::string> ids;
            for (size_t i = 0; i < root["objects"].size(); ++i)
            {
                const auto& value = root["objects"][i];
                auto path = sourceName + ".objects[" + std::to_string(i) + "]";
                if (!value.is_object() || !value.contains("type")) Invalid(path + ".type", "required property");
                std::string type = String(value["type"], path + ".type");
                std::unique_ptr<ObjectDefinition> object;
                if (type == "primitive")
                {
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "shape", "cubeMap", "material" });
                    auto primitive = std::make_unique<PrimitiveDefinition>();
                    if (value.contains("shape")) primitive->shape = ReadShape(value["shape"], path + ".shape");
                    if (value.contains("cubeMap")) primitive->cubeMap = Boolean(value["cubeMap"], path + ".cubeMap");
                    if (value.contains("material")) primitive->material = MaterialReference(value["material"], path + ".material", scene);
                    if (primitive->cubeMap && primitive->material.texture.empty()) Invalid(path + ".material.texture", "cube maps require a DDS texture");
                    if (primitive->cubeMap)
                    {
                        std::string ext = primitive->material.texture.size() >= 4 ? primitive->material.texture.substr(primitive->material.texture.size() - 4) : "";
                        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (ext != ".dds") Invalid(path + ".material.texture", "cube maps require a DDS texture");
                    }
                    object = std::move(primitive);
                }
                else if (type == "tree")
                {
                    Fields(value, path, { "id", "type", "transform", "textureCoordScale", "animationSpeed", "depthLOD", "trunkShape", "materials", "generator" });
                    auto tree = std::make_unique<TreeDefinition>();
                    if (value.contains("trunkShape")) tree->trunkShape = ReadShape(value["trunkShape"], path + ".trunkShape");
                    if (tree->trunkShape != Shape::Cylinder && tree->trunkShape != Shape::SkinnedCylinder)
                        Invalid(path + ".trunkShape", "expected cylinder or skinnedCylinder");
                    if (value.contains("materials"))
                    {
                        Fields(value["materials"], path + ".materials", { "trunk", "leaf" });
                        if (value["materials"].contains("trunk")) tree->trunk = MaterialReference(value["materials"]["trunk"], path + ".materials.trunk", scene);
                        if (value["materials"].contains("leaf")) tree->leaf = MaterialReference(value["materials"]["leaf"], path + ".materials.leaf", scene);
                    }
                    if (!value.contains("generator")) Invalid(path + ".generator", "required property");
                    tree->generator = ReadGenerator(value["generator"], path + ".generator");
                    object = std::move(tree);
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
        std::ifstream input(filename, std::ios::binary);
        if (!input) throw std::runtime_error(filename + ": cannot open scene file");
        std::ostringstream text;
        text << input.rdbuf();
        return ParseScene(text.str(), filename);
    }
}

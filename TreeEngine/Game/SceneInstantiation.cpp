#include "pch.h"
#include "SceneInstantiation.h"
#include "SceneRenderSettings.h"
#include "GameLoader.h"
#include "SceneRoot.h"
#include "Primitive.h"
#include "Tree.h"
#include "LSystemModelGenerator.h"
#include "Player.h"
#include <stdexcept>

namespace
{
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
        }
        throw std::runtime_error("Unsupported scene shape");
    }

    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
        if (!size) throw std::runtime_error("Invalid UTF-8 texture filename");
        std::vector<wchar_t> result(size);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), size);
        return std::wstring(result.data());
    }

    ShaderMaterial RuntimeMaterial(const SceneData::MaterialDefinition& definition)
    {
        ShaderMaterial result;
        result.Ambient = definition.ambient;
        result.Diffuse = definition.diffuse;
        result.Specular = definition.specular;
        result.Reflect = definition.reflect;
        result.flags.y = definition.texture.empty() ? 0.0f : 1.0f;
        result.flags.z = definition.alphaClipThreshold;
        result.textureCoordScale = XMFLOAT4(definition.textureCoordScale.x, definition.textureCoordScale.y, 1, 1);
        return result;
    }

    std::wstring RuntimeTexture(const std::string& texture, const std::string& objectId)
    {
        if (texture.empty()) return {};
        auto filename = Wide(texture);
        std::vector<std::wstring> candidates = { filename };
        wchar_t executable[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (length && length < MAX_PATH)
        {
            std::wstring directory(executable);
            auto slash = directory.find_last_of(L"/\\");
            if (slash != std::wstring::npos)
                candidates.push_back(directory.substr(0, slash + 1) + filename);
        }
        candidates.push_back(L"Game/Resources/" + filename);
        candidates.push_back(L"Game/" + filename);
        for (const auto& candidate : candidates)
        {
            DWORD attributes = GetFileAttributesW(candidate.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return candidate;
        }
        throw std::runtime_error("Object '" + objectId + "': texture '" + texture + "' was not found");
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
}

void InstantiateScene(const SceneData::SceneDefinition& definition, SceneRoot& scene,
    SceneRenderSettings& renderSettings, Player& player, GameData& gameData)
{
    std::vector<std::unique_ptr<WorldObject>> objects;
    for (const auto& object : definition.objects)
    {
        if (object->kind == SceneData::ObjectKind::Primitive)
        {
            const auto& primitive = static_cast<const SceneData::PrimitiveDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParams>(PrimitiveGeneratorType);
            CommonParameters(primitive, *parameters);
            parameters->primitiveType = RuntimeShape(primitive.shape);
            parameters->cubeMap = primitive.cubeMap;
            parameters->textureFilename.push_back(RuntimeTexture(primitive.material.texture, primitive.id));
            parameters->materials.push_back(RuntimeMaterial(primitive.material));
            auto runtime = std::make_unique<Primitive>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
        else
        {
            const auto& tree = static_cast<const SceneData::TreeDefinition&>(*object);
            auto parameters = std::make_unique<WorldObjectParameters<LSystemParams>>(LSystemGeneratorType);
            CommonParameters(tree, *parameters);
            parameters->meshes.push_back(RuntimeShape(tree.trunkShape));
            parameters->textureFilename.push_back(RuntimeTexture(tree.trunk.texture, tree.id));
            parameters->textureFilename.push_back(RuntimeTexture(tree.leaf.texture, tree.id));
            parameters->materials.push_back(RuntimeMaterial(tree.trunk));
            parameters->materials.push_back(RuntimeMaterial(tree.leaf));
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
            auto runtime = std::make_unique<Tree>(parameters.get());
            parameters.release();
            objects.push_back(std::move(runtime));
        }
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
    auto& light = renderSettings.dirLights[0];
    light.Ambient = definition.light.ambient;
    light.Diffuse = definition.light.diffuse;
    light.Specular = definition.light.specular;
    XMStoreFloat3(&light.Direction, XMVector3Normalize(XMLoadFloat3(&definition.light.direction)));
    renderSettings.numDirectionalLights = 1;
    renderSettings.numPointLights = definition.pointLightCount;
    player.SetPosition(XMLoadFloat3(&definition.camera.position));
    player.SetRotation(XMLoadFloat4(&definition.camera.rotation));
}

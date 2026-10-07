#pragma once

#include <DirectXMath.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace SceneData
{
    struct Transform
    {
        DirectX::XMFLOAT3 position = { 0, 0, 0 };
        DirectX::XMFLOAT4 rotation = { 0, 0, 0, 1 };
        DirectX::XMFLOAT3 scale = { 1, 1, 1 };
    };

    struct MaterialDefinition
    {
        DirectX::XMFLOAT4 ambient = { 0, 0, 0, 1 };
        DirectX::XMFLOAT4 diffuse = { 0, 0, 0, 1 };
        DirectX::XMFLOAT4 specular = { 0, 0, 0, 1 };
        DirectX::XMFLOAT4 reflect = { 0, 0, 0, 1 };
        std::string texture;
        DirectX::XMFLOAT2 textureCoordScale = { 1, 1 };
        float alphaClipThreshold = 0;
    };

    enum class Shape { Box, Cylinder, CylinderLD, CylinderHD, SkinnedCylinder, Sprite };
    enum class ObjectKind { Primitive, Tree };
    enum class SegmentLengthBehavior { Constant, RandomAddition };

    struct ObjectDefinition
    {
        explicit ObjectDefinition(ObjectKind type) : kind(type) {}
        virtual ~ObjectDefinition() = default;
        ObjectKind kind;
        std::string id;
        Transform transform;
        DirectX::XMFLOAT2 textureCoordScale = { 1, 1 };
        float animationSpeed = 1;
        int depthLOD = 4;
    };

    struct PrimitiveDefinition : ObjectDefinition
    {
        PrimitiveDefinition() : ObjectDefinition(ObjectKind::Primitive) {}
        Shape shape = Shape::Box;
        bool cubeMap = false;
        MaterialDefinition material;
    };

    struct RuleDefinition
    {
        std::string input;
        std::string output;
        int iterations = 0;
    };

    struct LSystemDefinition
    {
        std::string axiom;
        std::string constants;
        std::vector<RuleDefinition> rules;
        int iterations = 0;
        float angle = 0;
        float segmentLength = 0.01f;
        float thickness = 0.02f;
        DirectX::XMFLOAT3 initialDirection = { 0, 1, 0 };
        SegmentLengthBehavior segmentLengthBehavior = SegmentLengthBehavior::Constant;
    };

    struct TreeDefinition : ObjectDefinition
    {
        TreeDefinition() : ObjectDefinition(ObjectKind::Tree) {}
        Shape trunkShape = Shape::SkinnedCylinder;
        MaterialDefinition trunk;
        MaterialDefinition leaf;
        LSystemDefinition generator;
    };

    struct DirectionalLightDefinition
    {
        DirectX::XMFLOAT4 ambient = { 0.5f, 0.5f, 0.5f, 1 };
        DirectX::XMFLOAT4 diffuse = { 1, 1, 1, 1 };
        DirectX::XMFLOAT4 specular = { 0.6f, 0.6f, 0.6f, 1 };
        DirectX::XMFLOAT3 direction = { -0.57735f, -0.57735f, 0.57735f };
    };

    struct SceneDefinition
    {
        std::string name;
        Transform camera;
        DirectX::XMFLOAT4 clearColor = { 0.5294118f, 0.8078431f, 0.9215686f, 1 };
        bool useShadowMaps = true;
        bool useAlphaBlendedRenderTarget = true;
        int pointLightCount = 0;
        DirectionalLightDefinition light;
        std::map<std::string, MaterialDefinition> materials;
        std::vector<std::unique_ptr<ObjectDefinition>> objects;
    };

    // Throw std::runtime_error with a filename/property path on invalid data.
    SceneDefinition ParseScene(const std::string& jsonText, const std::string& sourceName);
    SceneDefinition ReadScene(const std::string& filename);
}

#pragma once
#include "Materials.h"
#include <string>

// Scene-authored rendering values. No frame state or renderer-owned resources.
struct SceneRenderSettings
{
    XMFLOAT4 clearColor = { 0.5294118f, 0.8078431f, 0.9215686f, 1 };
    float farClippingPlane = 30.0f;
    std::string environmentDiffuse;
    std::string environmentSpecular;
    float environmentRotationDegrees = 0;
    int numDirectionalLights = 1;
    int numPointLights = 0;
    DirectionalLight dirLights[1];
    PointLight pointLights[1];
    XMFLOAT3 shadowLightPosition = { 0, 0, 0 };
    bool shadowLightPositionOverride = false;
};

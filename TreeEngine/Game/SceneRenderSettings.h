#pragma once
#include "Materials.h"

// Scene-authored rendering values. No frame state or renderer-owned resources.
struct SceneRenderSettings
{
    XMFLOAT4 clearColor = { 0.5294118f, 0.8078431f, 0.9215686f, 1 };
    int numDirectionalLights = 1;
    int numPointLights = 0;
    DirectionalLight dirLights[1];
    PointLight pointLights[1];
};

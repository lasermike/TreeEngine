#include "pch.h"
#include "Water.h"
#include "RenderManager.h"

Water::Water(WaterParams* params) : Primitive(params)
{
    // A unit box scaled to the surface's size, its top face at the surface.
    params->scale = XMFLOAT3(params->size.x, Thickness, params->size.y);
    params->textureCoordScale = XMFLOAT2(params->size.x / params->tile, params->size.y / params->tile);
    _scale = params->scale;
    _position.y = SurfaceHeight(0) - Thickness / 2;
}

float Water::SurfaceHeight(float time) const
{
    const WaterParams& p = Params();
    if (!p.tideConstituents.empty())
        return p.level + static_cast<float>(Tide::Height(p.tideConstituents, p.tideStart + time));
    if (p.tideRange <= 0 || p.tidePeriod <= 0)
        return p.level;
    return p.level + 0.5f * p.tideRange * sinf(XM_2PI * (time + p.tidePhase) / p.tidePeriod);
}

HRESULT Water::InitGraphics(RenderManager& renderManager)
{
    // The ripples drift the normal map: Primitive builds its material from slot 0, so it gets the motion and strength,
    // and the authored material (which saving captures) is restored afterwards.
    if (_params->materials.empty())
        return Primitive::InitGraphics(renderManager);
    const ShaderMaterial authored = _params->materials[0];
    ShaderMaterial& material = _params->materials[0];
    const WaterParams& p = Params();
    material.normalMotion = XMFLOAT4(p.rippleVelocity1.x, p.rippleVelocity1.y, p.rippleVelocity2.x, p.rippleVelocity2.y);
    material.normalTextures.y *= p.rippleStrength;
    const HRESULT hr = Primitive::InitGraphics(renderManager);
    material = authored;
    return hr;
}

HRESULT Water::ComputeConstants(IRenderFrame* frame)
{
    _position.y = SurfaceHeight(frame->GetRenderData().time) - Thickness / 2;
    return Primitive::ComputeConstants(frame);
}

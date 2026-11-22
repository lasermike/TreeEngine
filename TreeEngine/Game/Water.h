#pragma once
#include "Primitive.h"
#include "Tide.h"

// A water surface: a flat plane (a thin box) at the authored height, with a material whose normal map can drift
// (ripples) and an optional tide that raises and lowers it with simulation time. A real tide comes from a station's
// harmonic constituents, height = position.y + Tide::Height(constituents, start + time), position.y being mean sea
// level; a simple one is height = position.y + range / 2 * sin(2 pi (time + phase) / period).
// It draws like any primitive in raster and DXR (a transmissive material shows the ground beneath).
class WaterParams : public WorldObjectParams
{
public:
    WaterParams() : WorldObjectParams(PrimitiveGeneratorType) { primitiveType = PrimitiveType_Box; }
    XMFLOAT2 size = XMFLOAT2(100, 100);     // Meters along x and z.
    float tile = 4;                         // Meters per normal-map repeat.
    XMFLOAT2 rippleVelocity1 = XMFLOAT2(0, 0), rippleVelocity2 = XMFLOAT2(0, 0);   // Texture repeats per second.
    float rippleStrength = 1;               // Multiplies the normal map's scale.
    float tideRange = 0;                    // Meters, low to high water.
    float tidePeriod = 44712;               // Seconds (a semidiurnal tide, 12.42 hours).
    float tidePhase = 0;                    // Seconds.
    std::vector<Tide::Constituent> tideConstituents;    // A real tide when not empty (range, period, phase unused).
    double tideStart = 0;                   // The UTC time at simulation time 0, in seconds since 1970.
    std::string tideStartText, tideSource;  // As authored, for saving.
    float level = 0;                        // The mean height (the authored position's y).
};

class Water : public Primitive
{
public:
    static constexpr float Thickness = 0.05f;

    explicit Water(WaterParams* params);
    HRESULT InitGraphics(RenderManager& renderManager) override;
    HRESULT ComputeConstants(IRenderFrame* frame) override;
    // The surface's height at a simulation time.
    float SurfaceHeight(float time) const;
    const WaterParams& Params() const { return *static_cast<const WaterParams*>(_params); }
};

#pragma once
// Resolution arithmetic shared by the upscalers (Upscaler.h) and the unit tests. No graphics API here.
#include <algorithm>
#include <cmath>

namespace UpscalerMath
{
    struct Size { unsigned width = 0, height = 0; };

    // Render-scale presets, as fractions of the display size per axis (the ratios FSR and DLSS name the same way).
    enum class Preset { Native, Quality, Balanced, Performance, UltraPerformance, Custom };
    constexpr float MinRenderScale = 0.25f;
    constexpr float MaxRenderScale = 1.0f;

    inline float PresetScale(Preset preset, float custom)
    {
        switch (preset)
        {
        case Preset::Native: return 1.0f;
        case Preset::Quality: return 1.0f / 1.5f;
        case Preset::Balanced: return 1.0f / 1.7f;
        case Preset::Performance: return 0.5f;
        case Preset::UltraPerformance: return 1.0f / 3.0f;
        default: return custom;
        }
    }

    // The preset a scale matches, or Custom.
    inline Preset PresetForScale(float scale)
    {
        for (Preset preset : { Preset::Native, Preset::Quality, Preset::Balanced, Preset::Performance, Preset::UltraPerformance })
            if (std::abs(PresetScale(preset, 0) - scale) < 1e-4f)
                return preset;
        return Preset::Custom;
    }

    inline float ClampScale(float scale)
    {
        return std::isfinite(scale) ? std::min(MaxRenderScale, std::max(MinRenderScale, scale)) : MaxRenderScale;
    }

    // The render size for a display size: each axis scaled and rounded, at least one pixel, never above the display.
    inline Size RenderSize(Size display, float scale)
    {
        scale = ClampScale(scale);
        auto axis = [scale](unsigned d) {
            const unsigned r = static_cast<unsigned>(std::lround(d * static_cast<double>(scale)));
            return std::min(d, std::max(1u, r));
        };
        return { axis(display.width), axis(display.height) };
    }

    // Texture LOD bias so minified textures keep the display's detail: log2(render / display), 0 at native.
    inline float MipBias(Size render, Size display)
    {
        if (render.width == 0 || display.width == 0 || render.width >= display.width)
            return 0.0f;
        return std::log2(static_cast<float>(render.width) / static_cast<float>(display.width));
    }

    // Jitter sequence length: 8 at native, growing with the pixel ratio so every display pixel is covered by samples
    // (FSR's recommendation, ceil(8 * (display / render)^2)).
    inline unsigned JitterPhaseCount(Size render, Size display)
    {
        if (render.width == 0 || render.width >= display.width)
            return 8;
        const double ratio = static_cast<double>(display.width) / render.width;
        return static_cast<unsigned>(std::ceil(8.0 * ratio * ratio - 1e-9));
    }
}

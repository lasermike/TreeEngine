#include "pch.h"
#include "OpacityMicromapBaker.h"
#include <algorithm>
#include <cmath>

namespace OpacityMicromap
{
    // Reference implementation from the DXR / VK_EXT_opacity_micromap specifications.
    uint32_t BarycentricsToIndex(float u, float v, uint32_t level)
    {
        u = std::min(std::max(u, 0.0f), 1.0f);
        v = std::min(std::max(v, 0.0f), 1.0f);
        const uint32_t size = 1u << level;
        const float fu = u * size, fv = v * size;
        uint32_t iu = static_cast<uint32_t>(fu), iv = static_cast<uint32_t>(fv);
        const float uf = fu - static_cast<float>(iu), vf = fv - static_cast<float>(iv);
        if (iu >= size) iu = size - 1;
        if (iv >= size) iv = size - 1;
        const uint32_t iuv = iu + iv;
        if (iuv >= size) iu -= iuv - size + 1;
        uint32_t iw = ~(iu + iv);
        if (uf + vf >= 1.0f && iuv < size - 1) --iw;

        uint32_t b0 = ~(iu ^ iw);
        b0 &= size - 1;
        const uint32_t t = (iu ^ iv) & b0;
        uint32_t f = t;
        f ^= f >> 1;
        f ^= f >> 2;
        f ^= f >> 4;
        f ^= f >> 8;
        uint32_t b1 = ((f ^ iu) & ~b0) | t;

        b0 = (b0 | (b0 << 8)) & 0x00ff00ffu;
        b0 = (b0 | (b0 << 4)) & 0x0f0f0f0fu;
        b0 = (b0 | (b0 << 2)) & 0x33333333u;
        b0 = (b0 | (b0 << 1)) & 0x55555555u;
        b1 = (b1 | (b1 << 8)) & 0x00ff00ffu;
        b1 = (b1 | (b1 << 4)) & 0x0f0f0f0fu;
        b1 = (b1 | (b1 << 2)) & 0x33333333u;
        b1 = (b1 | (b1 << 1)) & 0x55555555u;
        return b0 | (b1 << 1);
    }

    namespace
    {
        int Address(int texel, uint32_t size, bool wrap)
        {
            const int n = static_cast<int>(size);
            return wrap ? ((texel % n) + n) % n : std::min(std::max(texel, 0), n - 1);
        }

        // Min and max alpha over every texel a bilinear sample anywhere in [u0,u1] x [v0,v1] can read.
        void AlphaRange(const AlphaImage& image, float u0, float v0, float u1, float v1, uint8_t& lo, uint8_t& hi)
        {
            int x0 = static_cast<int>(std::floor(u0 * image.width - 0.5f));
            int x1 = static_cast<int>(std::floor(u1 * image.width - 0.5f)) + 1;
            int y0 = static_cast<int>(std::floor(v0 * image.height - 0.5f));
            int y1 = static_cast<int>(std::floor(v1 * image.height - 0.5f)) + 1;
            lo = 255; hi = 0;
            if (image.wrap)
            {
                // A wrapped range wider than the texture covers all of it once.
                x1 = std::min(x1, x0 + static_cast<int>(image.width) - 1);
                y1 = std::min(y1, y0 + static_cast<int>(image.height) - 1);
            }
            else
            {
                x0 = Address(x0, image.width, false); x1 = Address(x1, image.width, false);
                y0 = Address(y0, image.height, false); y1 = Address(y1, image.height, false);
            }
            for (int y = y0; y <= y1; ++y)
            {
                const uint8_t* row = &image.alpha[static_cast<size_t>(Address(y, image.height, image.wrap)) * image.width];
                for (int x = x0; x <= x1; ++x)
                {
                    const uint8_t a = row[Address(x, image.width, image.wrap)];
                    lo = std::min(lo, a); hi = std::max(hi, a);
                    if (lo == 0 && hi == 255) return;
                }
            }
        }

        float Bilinear(const AlphaImage& image, float u, float v)
        {
            const float x = u * image.width - 0.5f, y = v * image.height - 0.5f;
            const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            const float fx = x - x0, fy = y - y0;
            auto at = [&](int px, int py)
            {
                return image.alpha[static_cast<size_t>(Address(py, image.height, image.wrap)) * image.width
                    + Address(px, image.width, image.wrap)] / 255.0f;
            };
            return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy)
                + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
        }
    }

    TriangleOpacity BakeTriangle(const AlphaImage& image, float cutoff, const Uv uv[3], uint32_t level)
    {
        TriangleOpacity result;
        const uint32_t size = 1u << level;
        const uint32_t count = size * size;
        result.data.assign((count + 3) / 4, 0);
        auto texture = [&](float bu, float bv)
        {
            const float bw = 1 - bu - bv;
            return Uv{ uv[0].u * bw + uv[1].u * bu + uv[2].u * bv, uv[0].v * bw + uv[1].v * bu + uv[2].v * bv };
        };
        auto classify = [&](float au, float av, float bu, float bv, float cu, float cv)
        {
            const Uv a = texture(au, av), b = texture(bu, bv), c = texture(cu, cv);
            uint8_t lo, hi;
            AlphaRange(image, std::min({ a.u, b.u, c.u }), std::min({ a.v, b.v, c.v }),
                std::max({ a.u, b.u, c.u }), std::max({ a.v, b.v, c.v }), lo, hi);
            // The shader compares sampled alpha in [0, 1] with the cutoff.
            State state;
            if (lo / 255.0f >= cutoff) state = Opaque;
            else if (hi / 255.0f < cutoff) state = Transparent;
            else
            {
                const Uv center = texture((au + bu + cu) / 3, (av + bv + cv) / 3);
                state = Bilinear(image, center.u, center.v) >= cutoff ? UnknownOpaque : UnknownTransparent;
            }
            const uint32_t index = BarycentricsToIndex((au + bu + cu) / 3, (av + bv + cv) / 3, level);
            result.data[index / 4] |= static_cast<uint8_t>(state << (2 * (index % 4)));
            ++result.stateCounts[state];
        };
        const float step = 1.0f / size;
        for (uint32_t i = 0; i < size; ++i)
            for (uint32_t j = 0; i + j < size; ++j)
            {
                const float u = i * step, v = j * step;
                classify(u, v, u + step, v, u, v + step);
                if (i + j + 1 < size)
                    classify(u + step, v, u, v + step, u + step, v + step);
            }
        if (result.stateCounts[Opaque] == count || result.stateCounts[Transparent] == count)
        {
            result.specialIndex = result.stateCounts[Opaque] == count ? FullyOpaqueIndex : FullyTransparentIndex;
            result.data.clear();
        }
        return result;
    }
}

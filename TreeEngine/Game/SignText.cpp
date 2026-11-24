#include "pch.h"
#include "SignText.h"

#include <algorithm>
#include <cmath>
#include <map>

using DirectX::XMFLOAT2;
using DirectX::XMFLOAT4;

namespace
{
    // The font: each glyph a width and strokes (polylines) in cap heights, y up from the baseline. Strokes are drawn
    // StrokeWidth wide with round ends, so a glyph's ink reaches half that beyond its points.
    constexpr float StrokeWidth = 0.16f;
    constexpr float Gap = 0.13f;            // Between glyphs' ink.
    constexpr float SpaceWidth = 0.38f;
    constexpr float CapMeters = 0.1f;       // 4 in letters,
    constexpr float CapHeight = CapMeters / SignText::BladeHeight * SignText::CellHeight;    // in pixels.
    constexpr float MarginMeters = 0.045f;  // From each end of the face to the text.
    constexpr int Border = 6, BorderInset = 3;  // The white border's width and its inset from the edge, in pixels.
    const uint8_t Green[3] = { 0, 105, 70 };
    const uint8_t White[3] = { 240, 240, 236 };

    struct Glyph
    {
        float width = 0;
        std::vector<std::vector<XMFLOAT2>> strokes;
    };

    typedef std::vector<XMFLOAT2> Stroke;

    // Points along an ellipse from angle a0 to a1 (degrees, counterclockwise from +x), appended to a stroke.
    Stroke& Arc(Stroke& stroke, float cx, float cy, float rx, float ry, float a0, float a1)
    {
        const int steps = std::max(2, static_cast<int>(std::abs(a1 - a0) / 10));
        for (int i = 0; i <= steps; ++i)
        {
            const float a = (a0 + (a1 - a0) * i / steps) * 3.14159265f / 180;
            stroke.push_back(XMFLOAT2(cx + rx * std::cos(a), cy + ry * std::sin(a)));
        }
        return stroke;
    }

    Stroke Line(std::initializer_list<XMFLOAT2> points) { return Stroke(points); }

    const std::map<char, Glyph>& Font()
    {
        static std::map<char, Glyph> font;
        if (!font.empty())
            return font;
        const auto add = [&](char c, float width, std::initializer_list<Stroke> strokes)
        {
            font[c].width = width;
            font[c].strokes.assign(strokes.begin(), strokes.end());
        };
        Stroke s;
        add('A', 0.72f, { Line({ { 0, 0 }, { 0.36f, 1 }, { 0.72f, 0 } }), Line({ { 0.14f, 0.34f }, { 0.58f, 0.34f } }) });
        s = Line({ { 0, 0.51f }, { 0, 1 }, { 0.33f, 1 } }); Arc(s, 0.33f, 0.755f, 0.24f, 0.245f, 90, -90); s.push_back({ 0, 0.51f });
        Stroke s2 = Line({ { 0, 0.51f }, { 0.35f, 0.51f } }); Arc(s2, 0.35f, 0.255f, 0.26f, 0.255f, 90, -90); s2.push_back({ 0, 0 }); s2.push_back({ 0, 0.51f });
        add('B', 0.61f, { s, s2 });
        s.clear(); Arc(s, 0.38f, 0.5f, 0.38f, 0.5f, 48, 312);
        add('C', 0.67f, { s });
        s = Line({ { 0, 0 }, { 0, 1 }, { 0.24f, 1 } }); Arc(s, 0.24f, 0.5f, 0.4f, 0.5f, 90, -90); s.push_back({ 0, 0 });
        add('D', 0.64f, { s });
        add('E', 0.56f, { Line({ { 0.56f, 1 }, { 0, 1 }, { 0, 0 }, { 0.56f, 0 } }), Line({ { 0, 0.52f }, { 0.46f, 0.52f } }) });
        add('F', 0.54f, { Line({ { 0.54f, 1 }, { 0, 1 }, { 0, 0 } }), Line({ { 0, 0.52f }, { 0.44f, 0.52f } }) });
        s.clear(); Arc(s, 0.38f, 0.5f, 0.38f, 0.5f, 48, 345);
        add('G', 0.76f, { s, Line({ { 0.44f, 0.42f }, { 0.76f, 0.42f }, { 0.76f, 0.17f } }) });
        add('H', 0.64f, { Line({ { 0, 0 }, { 0, 1 } }), Line({ { 0.64f, 0 }, { 0.64f, 1 } }), Line({ { 0, 0.52f }, { 0.64f, 0.52f } }) });
        add('I', 0, { Line({ { 0, 0 }, { 0, 1 } }) });
        s = Line({ { 0.5f, 1 }, { 0.5f, 0.27f } }); Arc(s, 0.25f, 0.27f, 0.25f, 0.27f, 0, -180);
        add('J', 0.5f, { s });
        add('K', 0.62f, { Line({ { 0, 0 }, { 0, 1 } }), Line({ { 0.62f, 1 }, { 0, 0.36f } }), Line({ { 0.2f, 0.56f }, { 0.64f, 0 } }) });
        add('L', 0.52f, { Line({ { 0, 1 }, { 0, 0 }, { 0.52f, 0 } }) });
        add('M', 0.8f, { Line({ { 0, 0 }, { 0, 1 }, { 0.4f, 0.28f }, { 0.8f, 1 }, { 0.8f, 0 } }) });
        add('N', 0.64f, { Line({ { 0, 0 }, { 0, 1 }, { 0.64f, 0 }, { 0.64f, 1 } }) });
        s.clear(); Arc(s, 0.38f, 0.5f, 0.38f, 0.5f, 0, 360);
        add('O', 0.76f, { s });
        s = Line({ { 0, 0 }, { 0, 1 }, { 0.34f, 1 } }); Arc(s, 0.34f, 0.73f, 0.26f, 0.27f, 90, -90); s.push_back({ 0, 0.46f });
        add('P', 0.6f, { s });
        s2.clear(); Arc(s2, 0.38f, 0.5f, 0.38f, 0.5f, 0, 360);
        add('Q', 0.78f, { s2, Line({ { 0.48f, 0.24f }, { 0.78f, -0.04f } }) });
        add('R', 0.62f, { s, Line({ { 0.3f, 0.46f }, { 0.62f, 0 } }) });
        s.clear(); Arc(s, 0.3f, 0.75f, 0.28f, 0.25f, 25, 270); Arc(s, 0.3f, 0.25f, 0.3f, 0.25f, 90, -155);
        add('S', 0.6f, { s });
        add('T', 0.62f, { Line({ { 0, 1 }, { 0.62f, 1 } }), Line({ { 0.31f, 1 }, { 0.31f, 0 } }) });
        s = Line({ { 0, 1 }, { 0, 0.32f } }); Arc(s, 0.32f, 0.32f, 0.32f, 0.32f, 180, 360); s.push_back({ 0.64f, 1 });
        add('U', 0.64f, { s });
        add('V', 0.7f, { Line({ { 0, 1 }, { 0.35f, 0 }, { 0.7f, 1 } }) });
        add('W', 0.96f, { Line({ { 0, 1 }, { 0.22f, 0 }, { 0.48f, 0.72f }, { 0.74f, 0 }, { 0.96f, 1 } }) });
        add('X', 0.66f, { Line({ { 0, 1 }, { 0.66f, 0 } }), Line({ { 0, 0 }, { 0.66f, 1 } }) });
        add('Y', 0.66f, { Line({ { 0, 1 }, { 0.33f, 0.48f }, { 0.66f, 1 } }), Line({ { 0.33f, 0.48f }, { 0.33f, 0 } }) });
        add('Z', 0.6f, { Line({ { 0, 1 }, { 0.6f, 1 }, { 0, 0 }, { 0.6f, 0 } }) });
        s.clear(); Arc(s, 0.3f, 0.5f, 0.3f, 0.5f, 0, 360);
        add('0', 0.6f, { s });
        add('1', 0.3f, { Line({ { 0, 0.8f }, { 0.22f, 1 }, { 0.22f, 0 } }) });
        s.clear(); Arc(s, 0.3f, 0.71f, 0.29f, 0.29f, 160, -35); s.push_back({ 0, 0 }); s.push_back({ 0.6f, 0 });
        add('2', 0.6f, { s });
        s.clear(); Arc(s, 0.28f, 0.76f, 0.26f, 0.24f, 150, -90); Arc(s, 0.28f, 0.27f, 0.3f, 0.27f, 90, -150);
        add('3', 0.58f, { s });
        add('4', 0.64f, { Line({ { 0.48f, 0 }, { 0.48f, 1 }, { 0, 0.3f }, { 0.64f, 0.3f } }) });
        s = Line({ { 0.56f, 1 }, { 0.06f, 1 }, { 0.03f, 0.56f } }); Arc(s, 0.3f, 0.32f, 0.3f, 0.32f, 135, -150);
        add('5', 0.6f, { s });
        s.clear(); Arc(s, 0.4f, 0.58f, 0.36f, 0.42f, 75, 180); Arc(s, 0.3f, 0.3f, 0.3f, 0.3f, 180, 540);
        add('6', 0.62f, { s });
        add('7', 0.58f, { Line({ { 0, 1 }, { 0.58f, 1 }, { 0.18f, 0 } }) });
        s.clear(); Arc(s, 0.3f, 0.76f, 0.25f, 0.24f, 0, 360);
        s2.clear(); Arc(s2, 0.3f, 0.27f, 0.3f, 0.27f, 0, 360);
        add('8', 0.6f, { s, s2 });
        s.clear(); Arc(s, 0.3f, 0.7f, 0.3f, 0.3f, 0, 360); s.push_back({ 0.6f, 0.7f }); s.push_back({ 0.52f, 0 });
        add('9', 0.6f, { s });
        add('.', 0, { Line({ { 0, 0 }, { 0, 0.02f } }) });
        add('-', 0.38f, { Line({ { 0, 0.45f }, { 0.38f, 0.45f } }) });
        add('\'', 0, { Line({ { 0, 1 }, { 0, 0.76f } }) });
        add('/', 0.5f, { Line({ { 0, -0.05f }, { 0.5f, 1.05f } }) });
        add('&', 0.7f, { Line({ { 0.7f, 0 }, { 0.12f, 0.62f }, { 0.12f, 0.85f }, { 0.28f, 1 }, { 0.44f, 0.85f }, { 0.44f, 0.7f },
            { 0, 0.32f }, { 0, 0.12f }, { 0.14f, 0 }, { 0.4f, 0 }, { 0.68f, 0.36f } }) });
        return font;
    }

    char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

    float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    // Draws the text into a cell (top-left pixel x0, y0 of an atlas `width` wide): its face, border and letters.
    void DrawCell(std::vector<float>& ink, std::vector<uint8_t>& border, int width, int x0, int y0, const std::string& text)
    {
        // The face's border: a white band inset from the edge (the padding around the cell stays green).
        for (int y = 0; y < SignText::CellHeight; ++y)
            for (int x = 0; x < SignText::CellWidth; ++x)
            {
                const int fromEdge = std::min(std::min(x, SignText::CellWidth - 1 - x), std::min(y, SignText::CellHeight - 1 - y));
                if (fromEdge >= BorderInset && fromEdge < BorderInset + Border)
                    border[size_t(y0 + y) * width + x0 + x] = 1;
            }
        // The letters, centered and squeezed sideways to fit: the cell spans the blade's whole length, so a
        // stretched blade's letters are drawn narrower here by as much as the stretch widens them.
        const float squeeze = SignText::Squeeze(text);
        const float pixelsPerMeter = SignText::CellWidth / SignText::BladeLength(text);
        const float scaleX = CapMeters * pixelsPerMeter * squeeze, scaleY = CapHeight;
        const float textWidth = SignText::Width(text) * scaleX;
        float cursor = x0 + (SignText::CellWidth - textWidth) / 2 + StrokeWidth / 2 * scaleX;
        const float baseline = y0 + (SignText::CellHeight + CapHeight) / 2;
        const float radius = StrokeWidth / 2 * CapHeight;     // Strokes keep their weight however squeezed.
        const auto& font = Font();
        for (char raw : text)
        {
            const auto glyph = font.find(Upper(raw));
            if (glyph == font.end())
            {
                cursor += (SpaceWidth + Gap) * scaleX;
                continue;
            }
            for (const Stroke& stroke : glyph->second.strokes)
                for (size_t i = 0; i < stroke.size(); ++i)
                {
                    // Each segment (or a lone point) as a capsule of the stroke's radius.
                    const XMFLOAT2 a(cursor + stroke[i].x * scaleX, baseline - stroke[i].y * scaleY);
                    const XMFLOAT2 b = i + 1 < stroke.size()
                        ? XMFLOAT2(cursor + stroke[i + 1].x * scaleX, baseline - stroke[i + 1].y * scaleY) : a;
                    if (i + 1 == stroke.size() && stroke.size() > 1)
                        break;
                    const int left = std::max(x0, static_cast<int>(std::floor(std::min(a.x, b.x) - radius - 1)));
                    const int right = std::min(x0 + SignText::CellWidth - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + radius + 1)));
                    const int top = std::max(y0, static_cast<int>(std::floor(std::min(a.y, b.y) - radius - 1)));
                    const int bottom = std::min(y0 + SignText::CellHeight - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + radius + 1)));
                    const float dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
                    for (int y = top; y <= bottom; ++y)
                        for (int x = left; x <= right; ++x)
                        {
                            const float px = x + 0.5f, py = y + 0.5f;
                            const float t = length2 > 0 ? Clamp01(((px - a.x) * dx + (py - a.y) * dy) / length2) : 0;
                            const float d = std::hypot(px - a.x - t * dx, py - a.y - t * dy);
                            float& value = ink[size_t(y) * width + x];
                            value = std::max(value, Clamp01(radius + 0.5f - d));
                        }
                }
            cursor += (glyph->second.width + StrokeWidth + Gap) * scaleX;
        }
    }
}

namespace SignText
{
    bool Drawn(char c)
    {
        return Font().count(Upper(c)) != 0;
    }

    float Width(const std::string& text)
    {
        float width = 0;
        for (char c : text)
        {
            const auto glyph = Font().find(Upper(c));
            width += (glyph == Font().end() ? SpaceWidth : glyph->second.width + StrokeWidth) + Gap;
        }
        return text.empty() ? 0 : width - Gap;
    }

    int BladeMaterial(const std::string& material, bool& face)
    {
        face = material == "sign-blade-lower" || material == "sign-blade-upper";
        if (material == "sign-blade-lower" || material == "sign-blade-lower-edge")
            return 0;
        if (material == "sign-blade-upper" || material == "sign-blade-upper-edge")
            return 1;
        return -1;
    }

    float BladeLength(const std::string& text)
    {
        const float needed = Width(text) * CapMeters + 2 * MarginMeters;
        return std::min(LongestBlade, std::max(ModelBladeLength, needed));
    }

    float Squeeze(const std::string& text)
    {
        const float natural = Width(text) * CapMeters;
        const float room = BladeLength(text) - 2 * MarginMeters;
        return natural > room ? room / natural : 1.0f;
    }

    Atlas Build(const std::vector<std::string>& names)
    {
        Atlas atlas;
        const int cellsWide = std::max(1, std::min(Columns, static_cast<int>(names.size())));
        const int rows = std::max(1, (static_cast<int>(names.size()) + Columns - 1) / Columns);
        const int strideX = CellWidth + 2 * Padding, strideY = CellHeight + 2 * Padding;
        atlas.width = cellsWide * strideX;
        atlas.height = rows * strideY;
        std::vector<float> ink(size_t(atlas.width) * atlas.height, 0.0f);
        std::vector<uint8_t> border(size_t(atlas.width) * atlas.height, 0);
        for (size_t i = 0; i < names.size(); ++i)
        {
            const int x0 = static_cast<int>(i % Columns) * strideX + Padding, y0 = static_cast<int>(i / Columns) * strideY + Padding;
            DrawCell(ink, border, atlas.width, x0, y0, names[i]);
            atlas.regions.push_back(XMFLOAT4(float(x0) / atlas.width, float(y0) / atlas.height,
                float(CellWidth) / atlas.width, float(CellHeight) / atlas.height));
        }
        atlas.pixels.resize(size_t(atlas.width) * atlas.height * 3);
        for (size_t p = 0; p < ink.size(); ++p)
        {
            const float w = border[p] ? 1.0f : ink[p];
            for (int c = 0; c < 3; ++c)
                atlas.pixels[p * 3 + c] = static_cast<uint8_t>(std::lround(Green[c] + (White[c] - Green[c]) * w));
        }
        return atlas;
    }

    std::vector<uint8_t> Atlas::Bmp() const
    {
        const uint32_t rowBytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;
        const uint32_t imageBytes = rowBytes * static_cast<uint32_t>(height);
        std::vector<uint8_t> file(54 + imageBytes, 0);
        const auto put16 = [&](size_t at, uint32_t v) { file[at] = uint8_t(v); file[at + 1] = uint8_t(v >> 8); };
        const auto put32 = [&](size_t at, uint32_t v) { put16(at, v & 0xFFFF); put16(at + 2, v >> 16); };
        file[0] = 'B';
        file[1] = 'M';
        put32(2, static_cast<uint32_t>(file.size()));
        put32(10, 54);                  // Pixel data offset.
        put32(14, 40);                  // BITMAPINFOHEADER.
        put32(18, static_cast<uint32_t>(width));
        put32(22, static_cast<uint32_t>(height));   // Positive: bottom-up rows.
        put16(26, 1);
        put16(28, 24);
        put32(34, imageBytes);
        for (int y = 0; y < height; ++y)
        {
            uint8_t* row = &file[54 + size_t(height - 1 - y) * rowBytes];
            for (int x = 0; x < width; ++x)
            {
                const uint8_t* rgb = &pixels[(size_t(y) * width + x) * 3];
                row[x * 3] = rgb[2];
                row[x * 3 + 1] = rgb[1];
                row[x * 3 + 2] = rgb[0];
            }
        }
        return file;
    }
}

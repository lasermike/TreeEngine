#pragma once

// Street names for the roadside street-name signs (Roads::PropModel::StreetNameSign): a stroke font of capitals,
// digits and a little punctuation, drawn white on highway green with a white border into a texture atlas of blade
// faces, one cell per name. The terrain builds the atlas when its roads load and maps a cell onto each blade face
// (the model's sign-blade-lower and sign-blade-upper materials) through the instance's atlas region.

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

namespace SignText
{
    // A cell: a blade face, 0.2 m tall at 570 pixels a meter and as long as the blade (BladeLength), with Padding
    // pixels of the face's edge color around it so filtering and mipmaps don't bleed between cells.
    constexpr float BladeHeight = 0.2f;     // Meters.
    constexpr float ModelBladeLength = 0.9f;    // Meters: the model's blades, which the engine stretches to BladeLength.
    constexpr float LongestBlade = 1.2f;    // Meters (48 in): longer names are squeezed instead.
    constexpr int CellWidth = 512;
    constexpr int CellHeight = 114;
    constexpr int Padding = 4;
    constexpr int Columns = 2;

    struct Atlas
    {
        int width = 0, height = 0;
        std::vector<uint8_t> pixels;                // RGB, top row first.
        std::vector<DirectX::XMFLOAT4> regions;     // Per name: (u offset, v offset, u scale, v scale).
        // The atlas as a 24-bit BMP file, for the engine's texture loader.
        std::vector<uint8_t> Bmp() const;
    };

    // Whether the font has a glyph for the character (lower case draws as upper case; others draw as spaces).
    bool Drawn(char c);
    // The text's width in cap heights at the font's natural spacing.
    float Width(const std::string& text);
    // The blade the text needs in meters: the model's 0.9 m, longer for a long name up to LongestBlade.
    float BladeLength(const std::string& text);
    // How much the text is squeezed sideways to fit its blade (1 when it fits as it is).
    float Squeeze(const std::string& text);
    // A street-name sign model's blade material: 0 for the lower blade, 1 for the upper, -1 for any other material.
    // Exactly sign-blade-lower and sign-blade-upper are faces (textured with the name); the same with -edge appended
    // are their edges (stretched with them, not textured).
    int BladeMaterial(const std::string& material, bool& face);
    // A cell per name, in order.
    Atlas Build(const std::vector<std::string>& names);
}

#pragma once
#include <string>
#include <vector>
#include "../../../3rdParty/GLM/glm.hpp"

// Baked ASCII font atlas (stb_truetype, reusing ImGui's vendored copy).
// Bakes glyphs 32..126 at a fixed pixel height into an RGBA bitmap (white RGB,
// glyph coverage in alpha) so the UI shader treats text and images uniformly.
struct FontAtlas
{
    static constexpr int FirstChar = 32;
    static constexpr int CharCount = 95;            // 32..126
    static constexpr float BakePixelHeight = 32.0f; // glyphs baked at this size
    static constexpr int AtlasSize = 512;

    bool Load(const std::string& ttfPath);
    bool IsLoaded() const { return loaded; }

    // One positioned glyph quad, in pixels relative to the text origin
    // (top-left of the text block), plus atlas UVs.
    struct GlyphQuad
    {
        glm::vec4 rect;    // x, y, w, h
        glm::vec4 uvRect;  // u0, v0, u1, v1
    };

    // Lay out a UTF-8/ASCII string at the given font size. Returns glyph quads
    // and the total text size (width, height) via outSize.
    std::vector<GlyphQuad> LayoutText(const std::string& text, float fontSize,
        glm::vec2* outSize = nullptr) const;

    // RGBA atlas pixels (AtlasSize x AtlasSize x 4); valid after Load.
    const std::vector<unsigned char>& Pixels() const { return rgbaPixels; }

private:
    bool loaded = false;
    std::vector<unsigned char> rgbaPixels;
    // stbtt_bakedchar is opaque here (stb header stays in the .cpp);
    // mirrored fields for layout.
    struct BakedChar { unsigned short x0, y0, x1, y1; float xoff, yoff, xadvance; };
    std::vector<BakedChar> bakedChars;
    float ascent = 0.0f;   // baked-scale ascent in pixels (baseline offset)
};

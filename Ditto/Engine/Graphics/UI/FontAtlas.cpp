// Reuse ImGui's vendored stb_truetype. STBTT_STATIC keeps every symbol
// file-local, so this does not clash with the copy compiled inside
// imgui_draw.cpp.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../3rdParty/ImGui/imstb_truetype.h"

#include "FontAtlas.h"
#include "../../Core/Logger.h"
#include <fstream>
#include <cstring>

static_assert(sizeof(stbtt_bakedchar) == sizeof(unsigned short) * 4 + sizeof(float) * 3,
    "FontAtlas::BakedChar must mirror stbtt_bakedchar");

bool FontAtlas::Load(const std::string& ttfPath)
{
    std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        DITTO_LOG_WARN_STREAM("[FontAtlas] Cannot open font file: " << ttfPath);
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> ttf(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(ttf.data()), size)) return false;

    std::vector<unsigned char> alpha(AtlasSize * AtlasSize);
    std::vector<stbtt_bakedchar> chars(CharCount);

    int result = stbtt_BakeFontBitmap(ttf.data(), 0, BakePixelHeight,
        alpha.data(), AtlasSize, AtlasSize, FirstChar, CharCount, chars.data());
    if (result <= 0)
    {
        DITTO_LOG_WARN_STREAM("[FontAtlas] BakeFontBitmap failed for: " << ttfPath);
        return false;
    }

    // Ascent (baseline offset from the top of the line) at bake scale, so
    // LayoutText can convert baseline-relative glyph quads to top-left origin.
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        int a, d, g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        ascent = a * stbtt_ScaleForPixelHeight(&info, BakePixelHeight);
    }
    else
    {
        ascent = BakePixelHeight * 0.8f;
    }

    // Expand single-channel coverage to RGBA (white, alpha = coverage) so the
    // UI shader's `texture * color` works identically for text and images.
    rgbaPixels.assign(AtlasSize * AtlasSize * 4, 255);
    for (int i = 0; i < AtlasSize * AtlasSize; ++i)
        rgbaPixels[i * 4 + 3] = alpha[i];

    bakedChars.resize(CharCount);
    std::memcpy(bakedChars.data(), chars.data(), CharCount * sizeof(stbtt_bakedchar));

    loaded = true;
    DITTO_LOG_INFO_STREAM("[FontAtlas] Baked font atlas from: " << ttfPath);
    return true;
}

std::vector<FontAtlas::GlyphQuad> FontAtlas::LayoutText(const std::string& text,
    float fontSize, glm::vec2* outSize) const
{
    std::vector<GlyphQuad> quads;
    if (!loaded || text.empty())
    {
        if (outSize) *outSize = glm::vec2(0.0f);
        return quads;
    }

    const float scale = fontSize / BakePixelHeight;
    const float invAtlas = 1.0f / static_cast<float>(AtlasSize);

    // Pen starts at the baseline of the first line; quads are converted to a
    // top-left text-block origin by adding the scaled ascent.
    float penX = 0.0f;
    float penY = ascent * scale;
    float maxX = 0.0f;
    int lines = 1;

    quads.reserve(text.size());
    for (char c : text)
    {
        if (c == '\n')
        {
            maxX = glm::max(maxX, penX);
            penX = 0.0f;
            penY += fontSize;
            ++lines;
            continue;
        }
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < FirstChar || uc >= FirstChar + CharCount) uc = '?';

        const BakedChar& bc = bakedChars[uc - FirstChar];

        float x0 = penX + bc.xoff * scale;
        float y0 = penY + bc.yoff * scale;
        float w = (bc.x1 - bc.x0) * scale;
        float h = (bc.y1 - bc.y0) * scale;

        if (w > 0.0f && h > 0.0f)
        {
            GlyphQuad q;
            q.rect = glm::vec4(x0, y0, w, h);
            q.uvRect = glm::vec4(bc.x0 * invAtlas, bc.y0 * invAtlas,
                bc.x1 * invAtlas, bc.y1 * invAtlas);
            quads.push_back(q);
        }

        penX += bc.xadvance * scale;
    }
    maxX = glm::max(maxX, penX);

    if (outSize) *outSize = glm::vec2(maxX, lines * fontSize);
    return quads;
}

#pragma once
#include <string>
#include <vector>
#include "../../../3rdParty/GLM/glm.hpp"




struct FontAtlas
{
    static constexpr int FirstChar = 32;
    static constexpr int CharCount = 95;            
    static constexpr float BakePixelHeight = 32.0f; 
    static constexpr int AtlasSize = 512;

    bool Load(const std::string& ttfPath);
    bool IsLoaded() const { return loaded; }

    
    
    struct GlyphQuad
    {
        glm::vec4 rect;    
        glm::vec4 uvRect;  
    };

    
    
    std::vector<GlyphQuad> LayoutText(const std::string& text, float fontSize,
        glm::vec2* outSize = nullptr) const;

    
    const std::vector<unsigned char>& Pixels() const { return rgbaPixels; }

private:
    bool loaded = false;
    std::vector<unsigned char> rgbaPixels;
    
    
    struct BakedChar { unsigned short x0, y0, x1, y1; float xoff, yoff, xadvance; };
    std::vector<BakedChar> bakedChars;
    float ascent = 0.0f;   
};

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AssetPreviewUtils.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../3rdParty/stb_image.h"
#include "../3rdParty/GLM/glm.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace fs = std::filesystem;

namespace Ditto::EditorUtils
{
    std::string LowerExt(std::string ext)
    {
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    bool IsImageExtension(const std::string& ext)
    {
        const std::string lower = LowerExt(ext);
        return lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
            lower == ".tga" || lower == ".bmp" || lower == ".hdr";
    }

    bool IsMaterialExtension(const std::string& ext)
    {
        return LowerExt(ext) == ".mat";
    }

    std::vector<unsigned char> GenerateMaterialPreviewPixels(const MaterialAsset& material, int size)
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 4), 0);
        const float radius = size * 0.38f;
        const float cx = (size - 1) * 0.5f;
        const float cy = (size - 1) * 0.5f;
        const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.45f, -0.75f, 0.9f));
        const glm::vec3 base = glm::clamp(glm::vec3(material.color), glm::vec3(0.0f), glm::vec3(1.0f));

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float nx = (x - cx) / radius;
                float ny = (y - cy) / radius;
                float d2 = nx * nx + ny * ny;
                size_t i = static_cast<size_t>((y * size + x) * 4);

                glm::vec3 color(0.12f, 0.15f, 0.18f);
                float alpha = 255.0f;
                if (d2 <= 1.0f)
                {
                    float nz = std::sqrt(std::max(0.0f, 1.0f - d2));
                    glm::vec3 normal = glm::normalize(glm::vec3(nx, -ny, nz));
                    float diffuse = std::max(0.0f, glm::dot(normal, lightDir));
                    float rim = std::pow(std::max(0.0f, 1.0f - nz), 2.0f) * 0.25f;
                    color = base * (0.22f + diffuse * 0.72f) + glm::vec3(rim);
                }
                else
                {
                    float checker = ((x / 8 + y / 8) % 2) ? 0.18f : 0.23f;
                    color = glm::vec3(checker);
                }

                color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                pixels[i + 0] = static_cast<unsigned char>(color.r * 255.0f);
                pixels[i + 1] = static_cast<unsigned char>(color.g * 255.0f);
                pixels[i + 2] = static_cast<unsigned char>(color.b * 255.0f);
                pixels[i + 3] = static_cast<unsigned char>(alpha);
            }
        }
        return pixels;
    }

    fs::path MakeUniquePath(const fs::path& desired)
    {
        if (!fs::exists(desired)) return desired;

        fs::path parent = desired.parent_path();
        std::string stem = desired.stem().string();
        std::string ext = desired.extension().string();
        for (int i = 1; i < 10000; ++i)
        {
            fs::path candidate = parent / (stem + "_" + std::to_string(i) + ext);
            if (!fs::exists(candidate))
                return candidate;
        }
        return desired;
    }

    unsigned char* LoadImageRGBA(const fs::path& path, int* width, int* height, int* channels)
    {
#ifdef _WIN32
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || !file)
            return nullptr;
        unsigned char* pixels = stbi_load_from_file(file, width, height, channels, 4);
        fclose(file);
        return pixels;
#else
        return stbi_load(path.string().c_str(), width, height, channels, 4);
#endif
    }
}

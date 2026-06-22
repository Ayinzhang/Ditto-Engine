#pragma once

#include <iosfwd>
#include <cstdint>
#include <string>

namespace Ditto::AssetReferenceIO
{
    void WriteString(std::ostream& file, const std::string& value);
    std::string ReadString(std::istream& file);

    void WriteAssetReference(std::ostream& file, const std::string& path);
    std::string ReadAssetReference(std::istream& file, std::uint32_t sceneVersion);
}

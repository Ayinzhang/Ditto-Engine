#include "AssetReferenceIO.h"
#include "AssetDatabase.h"
#include "AssetPath.h"

#include <cstdint>
#include <istream>
#include <ostream>

namespace Ditto::AssetReferenceIO
{
    void WriteString(std::ostream& file, const std::string& value)
    {
        uint32_t length = static_cast<uint32_t>(value.length());
        file.write(reinterpret_cast<const char*>(&length), sizeof(length));
        file.write(value.c_str(), length);
    }

    std::string ReadString(std::istream& file)
    {
        uint32_t length = 0;
        file.read(reinterpret_cast<char*>(&length), sizeof(length));
        std::string value(length, '\0');
        if (length > 0)
            file.read(value.data(), length);
        return value;
    }

    void WriteAssetReference(std::ostream& file, const std::string& path)
    {
        WriteString(file, AssetPath::NormalizeAssetKey(path));
        WriteString(file, AssetDatabase::Get().GuidForPath(path));
    }

    std::string ReadAssetReference(std::istream& file, std::uint32_t sceneVersion)
    {
        std::string path = ReadString(file);
        if (sceneVersion < 16)
            return path;

        std::string guid = ReadString(file);
        std::string relative = AssetDatabase::Get().RelativePathForGuid(guid);
        return relative.empty() ? path : relative;
    }
}

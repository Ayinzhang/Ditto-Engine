#include "AssetDatabase.h"
#include "AssetPath.h"
#include "../Core/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace Ditto
{
    namespace
    {
        fs::path Normalize(const fs::path& path)
        {
            std::error_code ec;
            fs::path absolute = path.is_absolute() ? path : fs::absolute(path, ec);
            if (ec) absolute = path;
            return absolute.lexically_normal();
        }

        std::string ToForwardSlashes(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::string Trim(const std::string& value)
        {
            size_t first = 0;
            while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
            size_t last = value.size();
            while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
            return value.substr(first, last - first);
        }

        std::string GenerateGuid()
        {
            std::array<unsigned char, 16> bytes{};
            std::random_device rd;
            for (unsigned char& byte : bytes)
                byte = static_cast<unsigned char>(rd());

            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (unsigned char byte : bytes)
                oss << std::setw(2) << static_cast<int>(byte);
            return oss.str();
        }

        bool IsHexGuid(const std::string& value)
        {
            if (value.size() != 32) return false;
            return std::all_of(value.begin(), value.end(),
                [](unsigned char c) { return std::isxdigit(c) != 0; });
        }

        std::string Hex64(uint64_t value)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0') << std::setw(16) << value;
            return oss.str();
        }

        std::string EscapeField(const std::string& value)
        {
            std::string out;
            for (char c : value)
            {
                if (c == '\\') out += "\\\\";
                else if (c == '\t') out += "\\t";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else out += c;
            }
            return out;
        }

        std::string UnescapeField(const std::string& value)
        {
            std::string out;
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '\\' || i + 1 >= value.size())
                {
                    out += value[i];
                    continue;
                }
                char next = value[++i];
                if (next == 't') out += '\t';
                else if (next == 'n') out += '\n';
                else if (next == 'r') out += '\r';
                else out += next;
            }
            return out;
        }

        std::vector<std::string> SplitTabs(const std::string& line)
        {
            std::vector<std::string> fields;
            size_t start = 0;
            while (start <= line.size())
            {
                size_t tab = line.find('\t', start);
                if (tab == std::string::npos)
                {
                    fields.push_back(line.substr(start));
                    break;
                }
                fields.push_back(line.substr(start, tab - start));
                start = tab + 1;
            }
            return fields;
        }
    }

    AssetDatabase& AssetDatabase::Get()
    {
        static AssetDatabase database;
        return database;
    }

    void AssetDatabase::Clear()
    {
        projectRoot.clear();
        assetsRoot.clear();
        byGuid.clear();
        guidByNormalizedPath.clear();
        diagnostics = {};
    }

    bool AssetDatabase::IsMetaFile(const fs::path& path)
    {
        return Lower(path.extension().string()) == ".meta";
    }

    bool AssetDatabase::IsGuidReference(const std::string& value)
    {
        return value.rfind("guid:", 0) == 0;
    }

    std::string AssetDatabase::StripGuidPrefix(const std::string& value)
    {
        return IsGuidReference(value) ? value.substr(5) : value;
    }

    void AssetDatabase::ScanProjectAssets(const fs::path& root, bool createMissingMeta)
    {
        Clear();
        if (root.empty()) return;

        projectRoot = Normalize(root);
        assetsRoot = projectRoot / "Assets";

        std::error_code ec;
        if (!fs::exists(assetsRoot, ec))
            return;

        for (const auto& entry : fs::recursive_directory_iterator(assetsRoot, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec) || IsMetaFile(entry.path())) continue;
            fs::path metaPath = entry.path();
            metaPath += ".meta";
            if (!fs::exists(metaPath, ec))
                AddUniqueDiagnostic(diagnostics.missingMeta, RelativePathForAsset(entry.path()));
            RegisterAsset(entry.path(), createMissingMeta, false);
        }

        SaveImportCache();
    }

    std::string AssetDatabase::EnsureMetaForAsset(const fs::path& assetPath)
    {
        std::string guid = RegisterAsset(assetPath, true, true);
        if (!guid.empty())
            SaveImportCache();
        return guid;
    }

    void AssetDatabase::ForgetAsset(const fs::path& assetPath)
    {
        if (assetPath.empty()) return;

        std::string pathKey = NormalizePathKey(assetPath);
        auto pathIt = guidByNormalizedPath.find(pathKey);
        if (pathIt == guidByNormalizedPath.end()) return;

        std::string guid = pathIt->second;
        guidByNormalizedPath.erase(pathIt);

        auto guidIt = byGuid.find(guid);
        if (guidIt != byGuid.end() && NormalizePathKey(guidIt->second.path) == pathKey)
            byGuid.erase(guidIt);
    }

    std::string AssetDatabase::GuidForPath(const fs::path& assetPath)
    {
        if (assetPath.empty()) return {};

        fs::path resolved = AssetPath::ResolveAssetPath(assetPath.string());
        std::string key = NormalizePathKey(resolved);
        auto it = guidByNormalizedPath.find(key);
        if (it != guidByNormalizedPath.end())
            return it->second;

        std::string guid = RegisterAsset(resolved, true, false);
        if (!guid.empty())
            SaveImportCache();
        return guid;
    }

    fs::path AssetDatabase::PathForGuid(const std::string& guid) const
    {
        std::string key = StripGuidPrefix(guid);
        auto it = byGuid.find(key);
        return it == byGuid.end() ? fs::path{} : it->second.path;
    }

    std::string AssetDatabase::RelativePathForGuid(const std::string& guid) const
    {
        std::string key = StripGuidPrefix(guid);
        auto it = byGuid.find(key);
        return it == byGuid.end() ? std::string{} : it->second.relativePath;
    }

    const AssetRecord* AssetDatabase::RecordForGuid(const std::string& guid) const
    {
        std::string key = StripGuidPrefix(guid);
        auto it = byGuid.find(key);
        return it == byGuid.end() ? nullptr : &it->second;
    }

    std::vector<AssetRecord> AssetDatabase::Records() const
    {
        std::vector<AssetRecord> records;
        records.reserve(byGuid.size());
        for (const auto& pair : byGuid)
            records.push_back(pair.second);
        std::sort(records.begin(), records.end(), [](const AssetRecord& a, const AssetRecord& b) {
            return a.relativePath < b.relativePath;
        });
        return records;
    }

    std::vector<std::string> AssetDatabase::ValidateReferences(const std::vector<std::string>& guids)
    {
        std::vector<std::string> missing;
        for (const std::string& guid : guids)
        {
            std::string key = StripGuidPrefix(guid);
            if (key.empty()) continue;
            if (PathForGuid(key).empty())
            {
                AddUniqueDiagnostic(diagnostics.missingGuidReference, key);
                missing.push_back(key);
            }
        }
        return missing;
    }

    bool AssetDatabase::SaveImportCache() const
    {
        fs::path cachePath = ImportCachePath();
        if (cachePath.empty()) return false;

        std::error_code ec;
        fs::create_directories(cachePath.parent_path(), ec);
        std::ofstream file(cachePath, std::ios::trunc);
        if (!file.is_open())
            return false;

        file << "DittoImportCache 1\n";
        for (const AssetRecord& record : Records())
        {
            file << EscapeField(record.guid) << "\t"
                 << EscapeField(record.relativePath) << "\t"
                 << EscapeField(record.extension) << "\t"
                 << record.sizeBytes << "\t"
                 << EscapeField(record.contentHash) << "\t"
                 << (record.imported ? "1" : "0") << "\n";
        }
        return true;
    }

    bool AssetDatabase::LoadImportCache(std::vector<AssetRecord>& recordsOut) const
    {
        recordsOut.clear();
        fs::path cachePath = ImportCachePath();
        if (cachePath.empty()) return false;

        std::ifstream file(cachePath);
        if (!file.is_open()) return false;

        std::string header;
        std::getline(file, header);
        if (Trim(header) != "DittoImportCache 1")
            return false;

        std::string line;
        while (std::getline(file, line))
        {
            if (Trim(line).empty()) continue;
            std::vector<std::string> fields = SplitTabs(line);
            if (fields.size() < 6) continue;

            AssetRecord record;
            record.guid = UnescapeField(fields[0]);
            record.relativePath = UnescapeField(fields[1]);
            record.extension = UnescapeField(fields[2]);
            try { record.sizeBytes = static_cast<std::uintmax_t>(std::stoull(fields[3])); }
            catch (...) { record.sizeBytes = 0; }
            record.contentHash = UnescapeField(fields[4]);
            record.imported = fields[5] == "1";
            if (!assetsRoot.empty())
                record.path = (assetsRoot / fs::path(record.relativePath)).lexically_normal();
            recordsOut.push_back(record);
        }
        return true;
    }

    fs::path AssetDatabase::ResolveReference(const std::string& path,
        const std::string& guid,
        const fs::path& preferredRoot) const
    {
        fs::path guidPath = PathForGuid(guid);
        std::error_code ec;
        if (!guidPath.empty() && fs::exists(guidPath, ec))
            return guidPath;
        return AssetPath::ResolveAssetPath(path, preferredRoot);
    }

    std::string AssetDatabase::RegisterAsset(const fs::path& assetPath, bool createMissingMeta, bool updateMetaAssetPath)
    {
        if (assetPath.empty() || IsMetaFile(assetPath)) return {};

        fs::path normalized = Normalize(assetPath);
        std::error_code ec;
        if (!fs::exists(normalized, ec) || fs::is_directory(normalized, ec))
            return {};

        if (assetsRoot.empty())
        {
            fs::path currentAssetsRoot = AssetPath::CurrentProjectAssetsRoot();
            if (!currentAssetsRoot.empty())
            {
                assetsRoot = Normalize(currentAssetsRoot);
                projectRoot = assetsRoot.parent_path();
            }
        }

        fs::path metaPath = normalized;
        metaPath += ".meta";
        std::string guid = ReadGuidFromMeta(metaPath);

        bool needsWrite = false;
        if (!guid.empty() && !IsHexGuid(guid))
        {
            AddUniqueDiagnostic(diagnostics.invalidMeta, RelativePathForAsset(normalized));
            guid.clear();
        }

        auto existingGuid = byGuid.find(guid);
        if (!guid.empty() && existingGuid != byGuid.end()
            && NormalizePathKey(existingGuid->second.path) != NormalizePathKey(normalized))
        {
            AddUniqueDiagnostic(diagnostics.duplicateGuid, guid);
            if (fs::exists(existingGuid->second.path, ec))
            {
                guid.clear();
            }
            else
            {
                guidByNormalizedPath.erase(NormalizePathKey(existingGuid->second.path));
                byGuid.erase(existingGuid);
            }
        }

        if (guid.empty() && createMissingMeta)
        {
            guid = MakeUniqueGuid();
            needsWrite = true;
        }
        if (guid.empty()) return {};

        AssetRecord record;
        record.guid = guid;
        record.path = normalized;
        record.relativePath = RelativePathForAsset(normalized);
        record.extension = Lower(normalized.extension().string());
        record.sizeBytes = fs::file_size(normalized, ec);
        if (ec) record.sizeBytes = 0;
        record.contentHash = ContentHashForAsset(normalized);
        record.imported = true;

        byGuid[guid] = record;
        guidByNormalizedPath[NormalizePathKey(normalized)] = guid;

        std::string metaAssetPath = ReadAssetPathFromMeta(metaPath);
        if (!metaAssetPath.empty() && metaAssetPath != record.relativePath)
            updateMetaAssetPath = true;

        if (needsWrite || updateMetaAssetPath)
            WriteMetaFile(metaPath, guid);

        return guid;
    }

    std::string AssetDatabase::ReadGuidFromMeta(const fs::path& metaPath) const
    {
        std::ifstream file(metaPath);
        if (!file.is_open()) return {};

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));
            if (key != "guid") continue;
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            return value;
        }
        return {};
    }

    std::string AssetDatabase::ReadAssetPathFromMeta(const fs::path& metaPath) const
    {
        std::ifstream file(metaPath);
        if (!file.is_open()) return {};

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));
            if (key != "asset") continue;
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            return ToForwardSlashes(value);
        }
        return {};
    }

    std::string AssetDatabase::MakeUniqueGuid() const
    {
        std::string guid;
        do
        {
            guid = GenerateGuid();
        }
        while (byGuid.find(guid) != byGuid.end());
        return guid;
    }

    bool AssetDatabase::WriteMetaFile(const fs::path& metaPath, const std::string& guid) const
    {
        std::error_code ec;
        fs::create_directories(metaPath.parent_path(), ec);
        std::ofstream file(metaPath, std::ios::trunc);
        if (!file.is_open())
        {
            Logger::Get().Warning("[AssetDatabase] Failed to write meta: " + metaPath.string());
            return false;
        }

        fs::path assetPath = metaPath;
        assetPath.replace_extension("");
        file << "DittoMeta 1\n";
        file << "guid = \"" << guid << "\"\n";
        file << "asset = \"" << RelativePathForAsset(assetPath) << "\"\n";
        return true;
    }

    std::string AssetDatabase::RelativePathForAsset(const fs::path& assetPath) const
    {
        if (assetPath.empty()) return {};

        fs::path normalizedAsset = Normalize(assetPath);
        fs::path root = assetsRoot.empty() ? AssetPath::CurrentProjectAssetsRoot() : assetsRoot;
        if (root.empty())
            return normalizedAsset.generic_string();

        fs::path relative = normalizedAsset.lexically_relative(Normalize(root));
        if (relative.empty() || relative.string().find("..") != std::string::npos)
            return normalizedAsset.generic_string();
        return relative.generic_string();
    }

    std::string AssetDatabase::NormalizePathKey(const fs::path& path) const
    {
        return Lower(ToForwardSlashes(Normalize(path).generic_string()));
    }

    std::string AssetDatabase::ContentHashForAsset(const fs::path& path) const
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};

        uint64_t hash = 14695981039346656037ull;
        char buffer[4096];
        while (file)
        {
            file.read(buffer, sizeof(buffer));
            std::streamsize count = file.gcount();
            for (std::streamsize i = 0; i < count; ++i)
            {
                hash ^= static_cast<unsigned char>(buffer[i]);
                hash *= 1099511628211ull;
            }
        }
        return Hex64(hash);
    }

    void AssetDatabase::AddUniqueDiagnostic(std::vector<std::string>& list, const std::string& value) const
    {
        if (value.empty()) return;
        if (std::find(list.begin(), list.end(), value) == list.end())
            list.push_back(value);
    }

    fs::path AssetDatabase::ImportCachePath() const
    {
        if (projectRoot.empty()) return {};
        return projectRoot / ".ditto" / "import-cache.txt";
    }
}

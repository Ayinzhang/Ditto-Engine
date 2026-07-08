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
#include <unordered_set>

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

        std::string JoinList(const std::vector<std::string>& values)
        {
            std::string out;
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i) out += ';';
                std::string escaped = EscapeField(values[i]);
                std::string listEscaped;
                for (char c : escaped)
                {
                    if (c == ';') listEscaped += "\\;";
                    else listEscaped += c;
                }
                out += listEscaped;
            }
            return out;
        }

        std::vector<std::string> SplitList(const std::string& value)
        {
            std::vector<std::string> out;
            std::string item;
            for (size_t i = 0; i < value.size(); ++i)
            {
                char c = value[i];
                if (c == '\\' && i + 1 < value.size() && value[i + 1] == ';')
                {
                    item += "\\;";
                    ++i;
                    continue;
                }
                if (c == ';')
                {
                    std::string decoded = UnescapeField(item);
                    if (!decoded.empty()) out.push_back(decoded);
                    item.clear();
                    continue;
                }
                item += c;
            }
            std::string decoded = UnescapeField(item);
            if (!decoded.empty()) out.push_back(decoded);
            return out;
        }

        void AddUnique(std::vector<std::string>& values, const std::string& value)
        {
            if (value.empty()) return;
            if (std::find(values.begin(), values.end(), value) == values.end())
                values.push_back(value);
        }

        std::string DefaultArtifactPath(const AssetRecord& record)
        {
            if (record.guid.empty()) return {};
            return ".ditto/artifacts/" + record.guid + ".artifact";
        }

        std::string ImporterTypeForExtension(const std::string& extension)
        {
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga")
                return "Texture";
            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb")
                return "Model";
            if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac")
                return "Audio";
            if (extension == ".cs")
                return "CSharpScript";
            if (extension == ".mat")
                return "Material";
            if (extension == ".shader" || extension == ".hlsl" || extension == ".glsl")
                return "Shader";
            if (extension == ".prefab")
                return "Prefab";
            if (extension == ".scene" || extension == ".bin")
                return "Scene";
            if (extension == ".physmat2d")
                return "PhysicsMaterial2D";
            return "Generic";
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
        cachedByGuid.clear();
        cachedByRelativePath.clear();
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

        std::vector<AssetRecord> cachedRecords;
        LoadImportCache(cachedRecords);
        for (const AssetRecord& record : cachedRecords)
        {
            cachedByGuid[record.guid] = record;
            cachedByRelativePath[Lower(ToForwardSlashes(record.relativePath))] = record;
        }

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

    AssetDatabaseDiagnostics AssetDatabase::Diagnose()
    {
        fs::path root = projectRoot;
        if (!root.empty())
            ScanProjectAssets(root, false);
        return diagnostics;
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

    bool AssetDatabase::NeedsReimport(const std::string& guid) const
    {
        const AssetRecord* record = RecordForGuid(guid);
        if (!record) return false;
        return !record->imported || !ArtifactExists(*record);
    }

    std::vector<AssetRecord> AssetDatabase::AssetsNeedingImport() const
    {
        std::vector<AssetRecord> records;
        for (const AssetRecord& record : Records())
        {
            if (NeedsReimport(record.guid))
                records.push_back(record);
        }
        return records;
    }

    bool AssetDatabase::ImportAsset(const std::string& guid)
    {
        std::string key = StripGuidPrefix(guid);
        auto it = byGuid.find(key);
        if (it == byGuid.end()) return false;

        AssetRecord& record = it->second;
        record.importerType = ImporterTypeForExtension(record.extension);
        record.importError.clear();
        if (record.artifactPaths.empty())
            record.artifactPaths.push_back(DefaultArtifactPath(record));
        for (const std::string& artifact : record.artifactPaths)
        {
            fs::path artifactPath = ArtifactAbsolutePath(artifact);
            if (artifactPath.empty()) return false;
            std::error_code ec;
            fs::create_directories(artifactPath.parent_path(), ec);
            std::ofstream file(artifactPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                record.importError = "Failed to write artifact: " + artifactPath.string();
                return false;
            }

            file << "DittoArtifact 1\n";
            file << "guid=" << record.guid << "\n";
            file << "asset=" << record.relativePath << "\n";
            file << "importer=" << record.importerType << "\n";
            file << "hash=" << record.contentHash << "\n";
            file << "size=" << record.sizeBytes << "\n";
            file << "extension=" << record.extension << "\n";
        }

        record.imported = true;
        cachedByGuid[record.guid] = record;
        cachedByRelativePath[Lower(ToForwardSlashes(record.relativePath))] = record;
        return SaveImportCache();
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

        file << "DittoImportCache 4\n";
        for (const AssetRecord& record : Records())
        {
            file << EscapeField(record.guid) << "\t"
                 << EscapeField(record.relativePath) << "\t"
                 << EscapeField(record.extension) << "\t"
                 << record.sizeBytes << "\t"
                 << EscapeField(record.contentHash) << "\t"
                 << (record.imported ? "1" : "0") << "\t"
                 << JoinList(record.artifactPaths) << "\t"
                 << JoinList(record.dependencies) << "\t"
                 << EscapeField(record.importerType) << "\t"
                 << EscapeField(record.importError) << "\n";
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
        const std::string trimmedHeader = Trim(header);
        const bool v1 = trimmedHeader == "DittoImportCache 1";
        const bool v2 = trimmedHeader == "DittoImportCache 2";
        const bool v3 = trimmedHeader == "DittoImportCache 3";
        const bool v4 = trimmedHeader == "DittoImportCache 4";
        if (!v1 && !v2 && !v3 && !v4)
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
            if ((v2 || v3 || v4) && fields.size() >= 8)
            {
                record.artifactPaths = SplitList(fields[6]);
                record.dependencies = SplitList(fields[7]);
            }
            else
            {
                std::string artifact = DefaultArtifactPath(record);
                if (!artifact.empty())
                    record.artifactPaths.push_back(artifact);
            }
            record.importerType = fields.size() >= 9
                ? UnescapeField(fields[8])
                : ImporterTypeForExtension(record.extension);
            record.importError = fields.size() >= 10
                ? UnescapeField(fields[9])
                : std::string();
            if (!assetsRoot.empty())
                record.path = (assetsRoot / fs::path(record.relativePath)).lexically_normal();
            for (std::string& dependency : record.dependencies)
                dependency = StripGuidPrefix(dependency);
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
        record.importerType = ImporterTypeForExtension(record.extension);
        record.artifactPaths.push_back(DefaultArtifactPath(record));

        const AssetRecord* cached = nullptr;
        auto cachedGuidIt = cachedByGuid.find(guid);
        if (cachedGuidIt != cachedByGuid.end())
            cached = &cachedGuidIt->second;
        else
        {
            auto cachedPathIt = cachedByRelativePath.find(Lower(ToForwardSlashes(record.relativePath)));
            if (cachedPathIt != cachedByRelativePath.end())
                cached = &cachedPathIt->second;
        }
        if (cached)
        {
            record.importError = cached->importError;
            record.dependencies = cached->dependencies;
            for (std::string& dependency : record.dependencies)
            {
                dependency = StripGuidPrefix(dependency);
                auto pathIt = cachedByRelativePath.find(Lower(ToForwardSlashes(dependency)));
                if (pathIt != cachedByRelativePath.end())
                    dependency = pathIt->second.guid;
            }
            std::sort(record.dependencies.begin(), record.dependencies.end());
            record.dependencies.erase(std::unique(record.dependencies.begin(), record.dependencies.end()), record.dependencies.end());
        }
        record.imported = cached
            && DependenciesMatchCache(record, *cached)
            && ArtifactExists(*cached);

        byGuid[guid] = record;
        guidByNormalizedPath[NormalizePathKey(normalized)] = guid;

        std::string metaAssetPath = ReadAssetPathFromMeta(metaPath);
        if (!metaAssetPath.empty() && metaAssetPath != record.relativePath)
            updateMetaAssetPath = true;

        if (needsWrite || updateMetaAssetPath)
            WriteMetaFile(metaPath, guid);

        return guid;
    }

    bool AssetDatabase::ArtifactExists(const AssetRecord& record) const
    {
        if (record.artifactPaths.empty()) return false;
        std::error_code ec;
        for (const std::string& artifact : record.artifactPaths)
        {
            fs::path artifactPath = ArtifactAbsolutePath(artifact);
            if (artifactPath.empty() || !fs::exists(artifactPath, ec) || fs::is_directory(artifactPath, ec))
                return false;
        }
        return true;
    }

    bool AssetDatabase::DependenciesMatchCache(const AssetRecord& current, const AssetRecord& cached) const
    {
        if (current.contentHash != cached.contentHash) return false;
        if (current.sizeBytes != cached.sizeBytes) return false;
        if (Lower(ToForwardSlashes(current.relativePath)) != Lower(ToForwardSlashes(cached.relativePath)))
            return false;
        return current.dependencies == cached.dependencies;
    }

    fs::path AssetDatabase::ArtifactAbsolutePath(const std::string& artifactPath) const
    {
        if (artifactPath.empty()) return {};
        fs::path path = fs::path(artifactPath);
        if (path.is_absolute()) return path.lexically_normal();
        if (projectRoot.empty()) return {};
        return (projectRoot / path).lexically_normal();
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

    std::vector<std::string> AssetDatabase::GetDependents(const std::string& guid) const
    {
        std::string key = StripGuidPrefix(guid);
        std::vector<std::string> dependents;
        for (const auto& [recordGuid, record] : cachedByGuid)
        {
            if (std::find(record.dependencies.begin(), record.dependencies.end(), key) != record.dependencies.end())
                AddUnique(dependents, record.guid);
        }
        return dependents;
    }

    std::vector<std::string> AssetDatabase::GetAllDependencies(const std::string& guid) const
    {
        std::vector<std::string> allDeps;
        std::vector<std::string> toProcess = { StripGuidPrefix(guid) };
        std::unordered_set<std::string> visited;

        while (!toProcess.empty())
        {
            std::string current = toProcess.back();
            toProcess.pop_back();

            if (visited.count(current)) continue;
            visited.insert(current);

            auto it = cachedByGuid.find(current);
            if (it != cachedByGuid.end())
            {
                for (const std::string& dep : it->second.dependencies)
                {
                    std::string depGuid = StripGuidPrefix(dep);
                    auto pathIt = cachedByRelativePath.find(Lower(ToForwardSlashes(depGuid)));
                    if (pathIt != cachedByRelativePath.end())
                        depGuid = pathIt->second.guid;
                    if (!visited.count(depGuid))
                    {
                        AddUnique(allDeps, depGuid);
                        toProcess.push_back(depGuid);
                    }
                }
            }
        }
        return allDeps;
    }

    void AssetDatabase::MarkDependentsForReimport(const std::string& guid)
    {
        std::vector<std::string> dependents = GetDependents(guid);
        for (const std::string& depGuid : dependents)
        {
            auto it = cachedByGuid.find(depGuid);
            if (it != cachedByGuid.end())
            {
                it->second.imported = false;
                cachedByRelativePath[Lower(ToForwardSlashes(it->second.relativePath))] = it->second;
            }
        }
    }

    bool AssetDatabase::ImportAssetWithDependents(const std::string& guid, std::vector<std::string>& importedGuids)
    {
        importedGuids.clear();

        
        std::vector<std::string> deps = GetAllDependencies(guid);
        for (const std::string& depGuid : deps)
        {
            if (NeedsReimport(depGuid))
            {
                if (ImportAsset(depGuid))
                {
                    importedGuids.push_back(depGuid);
                }
                else
                {
                    Logger::Get().Error("Failed to import dependency: " + depGuid);
                    return false;
                }
            }
        }

        if (ImportAsset(guid))
        {
            importedGuids.push_back(guid);
            return true;
        }

        return false;
    }

    bool AssetDatabase::CreateAssetMetaFile(const std::string& assetPath)
    {
        fs::path fullPath = projectRoot / assetPath;
        if (!fs::exists(fullPath))
            return false;

        fs::path metaPath = fs::path(fullPath.string() + ".meta");
        if (fs::exists(metaPath))
            return false;

        std::string guid = MakeUniqueGuid();
        return WriteMetaFile(metaPath, guid);
    }

    bool AssetDatabase::RegenerateGuid(const std::string& assetPath)
    {
        fs::path fullPath = projectRoot / assetPath;
        fs::path metaPath = fs::path(fullPath.string() + ".meta");

        if (!fs::exists(metaPath))
            return false;

        std::string newGuid = MakeUniqueGuid();

        if (!WriteMetaFile(metaPath, newGuid))
            return false;

        std::string relPath = RelativePathForAsset(fullPath);
        std::string key = Lower(ToForwardSlashes(relPath));

        auto it = cachedByRelativePath.find(key);
        if (it != cachedByRelativePath.end())
        {
            std::string oldGuid = it->second.guid;
            cachedByGuid.erase(oldGuid);

            it->second.guid = newGuid;
            cachedByGuid[newGuid] = it->second;
        }

        return SaveImportCache();
    }
}

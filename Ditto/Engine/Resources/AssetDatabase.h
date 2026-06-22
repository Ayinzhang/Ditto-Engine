#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ditto
{
    struct AssetRecord
    {
        std::string guid;
        std::filesystem::path path;
        std::string relativePath;
        std::string extension;
        std::uintmax_t sizeBytes = 0;
        std::string contentHash;
        bool imported = false;
        std::vector<std::string> artifactPaths;
        std::vector<std::string> dependencies;
    };

    struct AssetDatabaseDiagnostics
    {
        std::vector<std::string> missingMeta;
        std::vector<std::string> invalidMeta;
        std::vector<std::string> duplicateGuid;
        std::vector<std::string> missingGuidReference;
    };

    class AssetDatabase
    {
    public:
        static AssetDatabase& Get();

        void Clear();
        void ScanProjectAssets(const std::filesystem::path& projectRoot, bool createMissingMeta = true);
        std::string EnsureMetaForAsset(const std::filesystem::path& assetPath);
        void ForgetAsset(const std::filesystem::path& assetPath);

        std::string GuidForPath(const std::filesystem::path& assetPath);
        std::filesystem::path PathForGuid(const std::string& guid) const;
        std::string RelativePathForGuid(const std::string& guid) const;
        const AssetRecord* RecordForGuid(const std::string& guid) const;
        std::vector<AssetRecord> Records() const;
        const AssetDatabaseDiagnostics& Diagnostics() const { return diagnostics; }
        std::vector<std::string> ValidateReferences(const std::vector<std::string>& guids);
        bool SaveImportCache() const;
        bool LoadImportCache(std::vector<AssetRecord>& recordsOut) const;

        std::filesystem::path ResolveReference(const std::string& path,
            const std::string& guid = {},
            const std::filesystem::path& preferredRoot = {}) const;

        static bool IsMetaFile(const std::filesystem::path& path);
        static bool IsGuidReference(const std::string& value);
        static std::string StripGuidPrefix(const std::string& value);

    private:
        std::filesystem::path projectRoot;
        std::filesystem::path assetsRoot;
        std::unordered_map<std::string, AssetRecord> byGuid;
        std::unordered_map<std::string, std::string> guidByNormalizedPath;
        AssetDatabaseDiagnostics diagnostics;

        std::string RegisterAsset(const std::filesystem::path& assetPath, bool createMissingMeta, bool updateMetaAssetPath);
        std::string ReadGuidFromMeta(const std::filesystem::path& metaPath) const;
        std::string ReadAssetPathFromMeta(const std::filesystem::path& metaPath) const;
        bool WriteMetaFile(const std::filesystem::path& metaPath, const std::string& guid) const;
        std::string MakeUniqueGuid() const;
        std::string RelativePathForAsset(const std::filesystem::path& assetPath) const;
        std::string NormalizePathKey(const std::filesystem::path& path) const;
        std::string ContentHashForAsset(const std::filesystem::path& path) const;
        void AddUniqueDiagnostic(std::vector<std::string>& list, const std::string& value) const;
        std::filesystem::path ImportCachePath() const;
    };
}

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
        std::string importerType;
        std::string importError;
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
        AssetDatabaseDiagnostics Diagnose();
        std::vector<std::string> ValidateReferences(const std::vector<std::string>& guids);
        bool NeedsReimport(const std::string& guid) const;
        std::vector<AssetRecord> AssetsNeedingImport() const;
        bool ImportAsset(const std::string& guid);

        // Incremental import dependency helpers. Dependencies are stored as
        // GUID strings without the "guid:" prefix.
        std::vector<std::string> GetDependents(const std::string& guid) const;
        std::vector<std::string> GetAllDependencies(const std::string& guid) const;
        void MarkDependentsForReimport(const std::string& guid);
        bool ImportAssetWithDependents(const std::string& guid, std::vector<std::string>& importedGuids);

        // Asset repair helpers used by the editor health window.
        bool CreateAssetMetaFile(const std::string& assetPath);
        bool RegenerateGuid(const std::string& assetPath);

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
        std::unordered_map<std::string, AssetRecord> cachedByGuid;
        std::unordered_map<std::string, AssetRecord> cachedByRelativePath;
        AssetDatabaseDiagnostics diagnostics;

        std::string RegisterAsset(const std::filesystem::path& assetPath, bool createMissingMeta, bool updateMetaAssetPath);
        bool ArtifactExists(const AssetRecord& record) const;
        bool DependenciesMatchCache(const AssetRecord& current, const AssetRecord& cached) const;
        std::filesystem::path ArtifactAbsolutePath(const std::string& artifactPath) const;
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

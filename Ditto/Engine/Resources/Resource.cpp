#include "Resource.h"
#include "../Core/Logger.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <cstring>
#include "AssetPath.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"

#ifdef DITTO_ENABLE_ASSIMP
#include "../../3rdParty/Assimp/include/assimp/Importer.hpp"
#include "../../3rdParty/Assimp/include/assimp/postprocess.h"
#include "../../3rdParty/Assimp/include/assimp/scene.h"
#endif

namespace
{
    constexpr float kMeshCompareEpsilon = 1e-5f;

    bool NearlyEqual(float a, float b)
    {
        return std::fabs(a - b) <= kMeshCompareEpsilon;
    }

    bool IsObjPath(const std::string& path)
    {
        std::filesystem::path p(path);
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".obj";
    }

    ModelData::FaceIndices ParseObjFaceIndices(const std::string& token)
    {
        ModelData::FaceIndices indices{ -1, -1, -1 };
        std::vector<std::string> parts;
        std::stringstream ss(token);
        std::string part;

        while (std::getline(ss, part, '/')) parts.push_back(part);
        if (!parts.empty() && !parts[0].empty()) indices.posIdx = std::stoi(parts[0]) - 1;
        if (parts.size() > 1 && !parts[1].empty()) indices.texIdx = std::stoi(parts[1]) - 1;
        if (parts.size() > 2 && !parts[2].empty()) indices.normIdx = std::stoi(parts[2]) - 1;
        return indices;
    }

    void RecalculateAABB(MeshData& mesh)
    {
        mesh.aabbMin = glm::vec3(std::numeric_limits<float>::max());
        mesh.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

        for (const auto& vertex : mesh.vertices)
        {
            mesh.aabbMin.x = std::min(mesh.aabbMin.x, vertex.x); mesh.aabbMin.y = std::min(mesh.aabbMin.y, vertex.y); mesh.aabbMin.z = std::min(mesh.aabbMin.z, vertex.z);
            mesh.aabbMax.x = std::max(mesh.aabbMax.x, vertex.x); mesh.aabbMax.y = std::max(mesh.aabbMax.y, vertex.y); mesh.aabbMax.z = std::max(mesh.aabbMax.z, vertex.z);
        }

        if (mesh.vertices.empty())
        {
            mesh.aabbMin = glm::vec3(0.0f);
            mesh.aabbMax = glm::vec3(0.0f);
        }
    }

    std::string FloatKey(const float* values, size_t count)
    {
        std::string key;
        key.resize(count * sizeof(float));
        std::memcpy(key.data(), values, key.size());
        return key;
    }
}

Resource::Resource(const std::string& basePath)
{
    Initialize(basePath);
}

void Resource::Initialize(const std::string& basePath)
{
    
    if (basePath.empty())
    {
        
        
        std::filesystem::path cubePath = Ditto::AssetPath::ResolveTypedAssetPath("Cube", "Models", ".obj");
        resourcePath = cubePath.parent_path().string();
    }
    else
    {
        resourcePath = basePath;
    }

    
    cubeModel = std::make_unique<ModelData>(resourcePath + "/Cube.obj");
    sphereModel = std::make_unique<ModelData>(resourcePath + "/Sphere.obj");
    cubeMesh = std::make_unique<MeshData>(resourcePath + "/Cube.obj");
    sphereMesh = std::make_unique<MeshData>(resourcePath + "/Sphere.obj");
}

static void LoadLegacyRenderModel(ModelData& out, const std::string& path)
{
    std::ifstream file(path);

    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> texCoords;

    
    
    std::unordered_map<std::string, unsigned int> uniqueVerts;

    auto emitCorner = [&](const ModelData::FaceIndices& fi) -> unsigned int
    {
        const std::string key = std::to_string(fi.posIdx) + "/" + std::to_string(fi.normIdx) + "/" + std::to_string(fi.texIdx);
        auto it = uniqueVerts.find(key);
        if (it != uniqueVerts.end()) return it->second;

        glm::vec3 pos = glm::vec3(0.0f);
        if (fi.posIdx >= 0 && fi.posIdx < positions.size()) pos = positions[fi.posIdx];
        glm::vec3 norm = glm::vec3(0.0f, 1.0f, 0.0f);
        if (fi.normIdx >= 0 && fi.normIdx < normals.size()) norm = normals[fi.normIdx];
        glm::vec2 uv = glm::vec2(0.0f);
        if (fi.texIdx >= 0 && fi.texIdx < texCoords.size()) uv = texCoords[fi.texIdx];

        const unsigned int index = static_cast<unsigned int>(out.vertexData.size() / 8);
        out.vertexData.push_back(pos.x); out.vertexData.push_back(pos.y); out.vertexData.push_back(pos.z);
        out.vertexData.push_back(norm.x); out.vertexData.push_back(norm.y); out.vertexData.push_back(norm.z);
        out.vertexData.push_back(uv.x); out.vertexData.push_back(uv.y);
        uniqueVerts.emplace(key, index);
        return index;
    };

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line))
    {
        lineNum++;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream lineStream(line);
        std::string prefix;
        lineStream >> prefix;

        if (prefix == "v")
        {
            glm::vec3 pos;
            if (lineStream >> pos.x >> pos.y >> pos.z) positions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            glm::vec3 norm;
            if (lineStream >> norm.x >> norm.y >> norm.z) normals.push_back(glm::normalize(norm));
        }
        else if (prefix == "vt")
        {
            glm::vec2 tex;
            if (lineStream >> tex.x >> tex.y)
            {
                tex.y = 1.0f - tex.y;
                texCoords.push_back(tex);
            }
        }
        else if (prefix == "f")
        {
            std::vector<std::string> faceTokens;
            std::string token;

            while (lineStream >> token) faceTokens.push_back(token);

            
            std::vector<unsigned int> corner;
            corner.reserve(faceTokens.size());
            for (const auto& faceToken : faceTokens)
                corner.push_back(emitCorner(ParseObjFaceIndices(faceToken)));
            for (size_t i = 2; i < corner.size(); ++i)
            {
                out.indices.push_back(corner[0]);
                out.indices.push_back(corner[i - 1]);
                out.indices.push_back(corner[i]);
            }
        }
    }

    file.close();
    out.vertexCount = static_cast<int>(out.vertexData.size() / 8);
}

ModelData::FaceIndices ModelData::ParseFaceIndices(const std::string& token)
{
    return ParseObjFaceIndices(token);
}

static void LoadLegacyPhysicsMesh(MeshData& out, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("Failed to open OBJ file: " << filePath );
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream lineStream(line);
        std::string prefix; lineStream >> prefix;

        if (prefix == "v")
        {
            glm::vec3 pos;
            if (lineStream >> pos.x >> pos.y >> pos.z) out.vertices.push_back(pos);
        }
        else if (prefix == "f")
        {
            std::vector<std::string> faceTokens; std::string token;
            while (lineStream >> token) faceTokens.push_back(token);
            
            
            std::vector<unsigned int> faceIndices;
            for (const auto& faceToken : faceTokens)
            {
                
                std::string indexStr = faceToken;
                size_t slashPos = indexStr.find('/');
                if (slashPos != std::string::npos)
                    indexStr = indexStr.substr(0, slashPos);
                
                if (!indexStr.empty())
                {
                    int idx = std::stoi(indexStr) - 1; 
                    if (idx >= 0)
                        faceIndices.push_back(static_cast<unsigned int>(idx));
                }
            }
            
            
            for (size_t i = 2; i < faceIndices.size(); i++)
            {
                out.indices.push_back(faceIndices[0]);
                out.indices.push_back(faceIndices[i - 1]);
                out.indices.push_back(faceIndices[i]);
            }
        }
    }
    file.close();

    RecalculateAABB(out);
}

#ifdef DITTO_ENABLE_ASSIMP
static bool LoadAssimpRenderModel(ModelData& out, const std::string& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenNormals);
    if (!scene || !scene->HasMeshes())
    {
        DITTO_LOG_WARN_STREAM("[Assimp] Render import failed for " << path << ": " << importer.GetErrorString());
        return false;
    }

    out.vertexData.clear();
    out.indices.clear();

    std::unordered_map<std::string, unsigned int> uniqueVerts;

    auto emitRenderVertex = [&](const aiVector3D& pos, const aiVector3D& norm, const aiVector3D& uv) -> unsigned int
    {
        float packed[8] = { pos.x, pos.y, pos.z, norm.x, norm.y, norm.z, uv.x, uv.y };
        std::string key = FloatKey(packed, 8);
        auto it = uniqueVerts.find(key);
        if (it != uniqueVerts.end()) return it->second;

        const unsigned int index = static_cast<unsigned int>(out.vertexData.size() / 8);
        out.vertexData.push_back(pos.x); out.vertexData.push_back(pos.y); out.vertexData.push_back(pos.z);
        out.vertexData.push_back(norm.x); out.vertexData.push_back(norm.y); out.vertexData.push_back(norm.z);
        out.vertexData.push_back(uv.x); out.vertexData.push_back(uv.y);
        uniqueVerts.emplace(std::move(key), index);
        return index;
    };

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh) continue;

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (unsigned int k = 0; k < 3; ++k)
            {
                unsigned int idx = face.mIndices[k];
                aiVector3D norm(0.0f, 1.0f, 0.0f);
                if (mesh->HasNormals()) norm = mesh->mNormals[idx];
                aiVector3D uv(0.0f, 0.0f, 0.0f);
                if (mesh->HasTextureCoords(0)) uv = mesh->mTextureCoords[0][idx];
                out.indices.push_back(emitRenderVertex(mesh->mVertices[idx], norm, uv));
            }
        }
    }

    out.vertexCount = static_cast<int>(out.vertexData.size() / 8);
    return out.vertexCount > 0 && !out.indices.empty();
}

#endif

ModelData::ModelData(const std::string& path)
    : ModelData(path, true)
{
}

ModelData::ModelData(const std::string& path, bool useAssimp)
{
#ifdef DITTO_ENABLE_ASSIMP
    if (useAssimp)
    {
        ModelData imported(path, false);
        if (LoadAssimpRenderModel(imported, path))
        {
            DITTO_LOG_INFO_STREAM("[Assimp] Render mesh loaded: " << path
                << " (" << imported.vertexData.size() / 8 << " verts, "
                << imported.indices.size() / 3 << " tris)");
            *this = std::move(imported);
            return;
        }
    }
#endif

    LoadLegacyRenderModel(*this, path);
}

MeshData::MeshData(const std::string& filePath)
    : MeshData(filePath, true)
{
}

MeshData::MeshData(const std::string& filePath, bool useAssimp)
{
    (void)useAssimp;
    LoadLegacyPhysicsMesh(*this, filePath);
}



Resource::~Resource() = default;


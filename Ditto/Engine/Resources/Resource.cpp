#include "Resource.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "../Core/PathUtils.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLFW/glfw3.h"

Resource::Resource(const std::string& basePath)
{
    Initialize(basePath);
}

void Resource::Initialize(const std::string& basePath)
{
    // Determine the base path for model assets.
    if (basePath.empty())
    {
        cubeModel = nullptr; sphereModel = nullptr;
        cubeMesh = nullptr; sphereMesh = nullptr;

        // Anchor to the executable location rather than guessing relative
        // ladders against the (unpredictable) working directory.
        std::filesystem::path cubePath = PathUtils::ResolveAsset("Models/Cube.obj");
        resourcePath = cubePath.parent_path().string();
    }
    else
    {
        resourcePath = basePath;
    }
    
    // Clean up existing resources if reinitializing
    delete cubeModel; delete sphereModel;
    delete cubeMesh; delete sphereMesh;
    
    cubeModel = new ModelData(resourcePath + "/Cube.obj");
    sphereModel = new ModelData(resourcePath + "/Sphere.obj");
    cubeMesh = new MeshData(resourcePath + "/Cube.obj");
    sphereMesh = new MeshData(resourcePath + "/Sphere.obj");
}

ModelData::ModelData(const std::string& path)
{
    std::ifstream file(path);

    std::vector<glm::vec3> positions, normals;
    //std::vector<glm::vec2> texCoords;

    // Dedup map: (posIdx, normIdx) -> index into vertexData. Corners shared by
    // multiple faces collapse to one vertex; `indices` carries the topology.
    std::unordered_map<uint64_t, unsigned int> uniqueVerts;

    auto emitCorner = [&](const FaceIndices& fi) -> unsigned int
    {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(fi.posIdx)) << 32)
                           | static_cast<uint32_t>(fi.normIdx);
        auto it = uniqueVerts.find(key);
        if (it != uniqueVerts.end()) return it->second;

        glm::vec3 pos = glm::vec3(0.0f);
        if (fi.posIdx >= 0 && fi.posIdx < positions.size()) pos = positions[fi.posIdx];
        glm::vec3 norm = glm::vec3(0.0f, 1.0f, 0.0f);
        if (fi.normIdx >= 0 && fi.normIdx < normals.size()) norm = normals[fi.normIdx];

        const unsigned int index = static_cast<unsigned int>(vertexData.size() / 6);
        vertexData.push_back(pos.x); vertexData.push_back(pos.y); vertexData.push_back(pos.z);
        vertexData.push_back(norm.x); vertexData.push_back(norm.y); vertexData.push_back(norm.z);
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
        //else if (prefix == "vt")
        //{
        //    glm::vec2 tex;
        //    if (lineStream >> tex.x >> tex.y)
        //    {
        //        tex.y = 1.0f - tex.y;
        //        texCoords.push_back(tex);
        //    }
        //}
        else if (prefix == "f")
        {
            std::vector<std::string> faceTokens;
            std::string token;

            while (lineStream >> token) faceTokens.push_back(token);

            // Fan-triangulate (handles quads/ngons, not just pre-triangulated files).
            std::vector<unsigned int> corner;
            corner.reserve(faceTokens.size());
            for (const auto& faceToken : faceTokens)
                corner.push_back(emitCorner(ParseFaceIndices(faceToken)));
            for (size_t i = 2; i < corner.size(); ++i)
            {
                indices.push_back(corner[0]);
                indices.push_back(corner[i - 1]);
                indices.push_back(corner[i]);
            }
        }
    }

    file.close();
    vertexCount = vertexData.size() / 6;
}

Resource::~Resource()
{
    delete cubeModel; delete sphereModel;
}

ModelData::FaceIndices ModelData::ParseFaceIndices(const std::string& token)
{
    FaceIndices indices;
    std::vector<std::string> parts;
    std::stringstream ss(token);
    std::string part;

    while (std::getline(ss, part, '/')) parts.push_back(part);
    if (!parts[0].empty()) indices.posIdx = std::stoi(parts[0]) - 1;
    if (parts.size() > 1 && !parts[1].empty()) indices.texIdx = std::stoi(parts[1]) - 1;
    if (parts.size() > 2 && !parts[2].empty()) indices.normIdx = std::stoi(parts[2]) - 1;

    return indices;
}

MeshData::MeshData(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open OBJ file: " << filePath << std::endl;
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
            if (lineStream >> pos.x >> pos.y >> pos.z) vertices.push_back(pos);
        }
        else if (prefix == "f")
        {
            std::vector<std::string> faceTokens; std::string token;
            while (lineStream >> token) faceTokens.push_back(token);
            
            // Parse face indices (supporting triangles and quads, triangulate quads)
            std::vector<unsigned int> faceIndices;
            for (const auto& faceToken : faceTokens)
            {
                // Parse "vertexIndex/textureIndex/normalIndex" format, get vertex index
                std::string indexStr = faceToken;
                size_t slashPos = indexStr.find('/');
                if (slashPos != std::string::npos)
                    indexStr = indexStr.substr(0, slashPos);
                
                if (!indexStr.empty())
                {
                    int idx = std::stoi(indexStr) - 1; // OBJ indices are 1-based
                    if (idx >= 0)
                        faceIndices.push_back(static_cast<unsigned int>(idx));
                }
            }
            
            // Triangulate the face (assuming convex polygon)
            for (size_t i = 2; i < faceIndices.size(); i++)
            {
                indices.push_back(faceIndices[0]);
                indices.push_back(faceIndices[i - 1]);
                indices.push_back(faceIndices[i]);
            }
        }
    }
    file.close();

    aabbMin = glm::vec3(std::numeric_limits<float>::max());
    aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& vertex : vertices)
    {
        aabbMin.x = std::min(aabbMin.x, vertex.x); aabbMin.y = std::min(aabbMin.y, vertex.y); aabbMin.z = std::min(aabbMin.z, vertex.z);
        aabbMax.x = std::max(aabbMax.x, vertex.x); aabbMax.y = std::max(aabbMax.y, vertex.y); aabbMax.z = std::max(aabbMax.z, vertex.z);
    }
}

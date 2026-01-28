#include "Resource.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLFW/glfw3.h"

Resource::Resource()
{
    cubeModel = new ModelData("Assets/Models/Cube.obj");
    sphereModel = new ModelData("Assets/Models/Sphere.obj");
    planeModel = new ModelData("Assets/Models/Plane.obj");
}

ModelData::ModelData(const std::string& path)
{
    std::ifstream file(path);

    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> texCoords;

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

            for (const auto& faceToken : faceTokens)
            {
                FaceIndices indices = ParseFaceIndices(faceToken);

                glm::vec3 pos = glm::vec3(0.0f);
                if (indices.posIdx >= 0 && indices.posIdx < positions.size()) pos = positions[indices.posIdx];
                vertexData.push_back(pos.x); vertexData.push_back(pos.y); vertexData.push_back(pos.z);

                glm::vec3 norm = glm::vec3(0.0f, 1.0f, 0.0f);
                if (indices.normIdx >= 0 && indices.normIdx < normals.size()) norm = normals[indices.normIdx];
                vertexData.push_back(norm.x); vertexData.push_back(norm.y); vertexData.push_back(norm.z);

                // glm::vec2 tex = glm::vec2(0.0f);
                // if (indices.texIdx >= 0 && indices.texIdx < texCoords.size()) tex = texCoords[indices.texIdx];
                // vertexData.push_back(tex.x); vertexData.push_back(tex.y);
            }
        }
    }

    file.close();
    vertexCount = vertexData.size() / 8;
}

Resource::~Resource()
{
    delete cubeModel; delete sphereModel; delete planeModel;
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

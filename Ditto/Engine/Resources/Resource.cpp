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
    std::cout << "Loading resources from: " << resourcePath << std::endl;

    // 检查资源目录是否存在
    if (!std::filesystem::exists(resourcePath))
    {
        std::cerr << "Resource directory does not exist: " << resourcePath << std::endl;
        // 尝试使用当前工作目录
        resourcePath = "Assets/Models";
        if (!std::filesystem::exists(resourcePath))
        {
            std::cerr << "Failed to find models directory. Creating default models." << std::endl;
            // 这里可以创建默认的几何体，但先简单返回
            return;
        }
    }

    // 加载三个标准模型
    try
    {
        // 立方体
        std::string cubePath = resourcePath + "/Cube.obj";
        if (std::filesystem::exists(cubePath))
        {
            cubeModel = new ModelData(cubePath);
            std::cout << "Successfully loaded Cube model" << std::endl;
        }
        else
        {
            std::cerr << "Cube.obj not found at: " << cubePath << std::endl;
        }

        // 球体
        std::string spherePath = resourcePath + "/Sphere.obj";
        if (std::filesystem::exists(spherePath))
        {
            sphereModel = new ModelData(spherePath);
            std::cout << "Successfully loaded Sphere model" << std::endl;
        }
        else
        {
            std::cerr << "Sphere.obj not found at: " << spherePath << std::endl;
        }

        // 平面
        std::string planePath = resourcePath + "/Plane.obj";
        if (std::filesystem::exists(planePath))
        {
            planeModel = new ModelData(planePath);
            std::cout << "Successfully loaded Plane model" << std::endl;
        }
        else
        {
            std::cerr << "Plane.obj not found at: " << planePath << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading resources: " << e.what() << std::endl;
    }
}

ModelData::ModelData(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Error: Cannot open file: " << path << std::endl;
        return;
    }

    // 存储原始数据
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line))
    {
        lineNum++;

        // 去除行尾的\r（Windows 格式）
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // 跳过空行和注释
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream lineStream(line);
        std::string prefix;
        lineStream >> prefix;

        if (prefix == "v")  // 顶点位置
        {
            glm::vec3 pos;
            if (lineStream >> pos.x >> pos.y >> pos.z)
            {
                positions.push_back(pos);
            }
        }
        else if (prefix == "vn")  // 法线
        {
            glm::vec3 norm;
            if (lineStream >> norm.x >> norm.y >> norm.z)
            {
                normals.push_back(glm::normalize(norm));
            }
        }
        else if (prefix == "vt")  // 纹理坐标
        {
            glm::vec2 tex;
            if (lineStream >> tex.x >> tex.y)
            {
                tex.y = 1.0f - tex.y;  // 翻转 V 坐标
                texCoords.push_back(tex);
            }
        }
        else if (prefix == "f")  // 面（三角形）
        {
            std::vector<std::string> faceTokens;
            std::string token;

            // 收集所有顶点索引
            while (lineStream >> token)
            {
                faceTokens.push_back(token);
            }

            // 确保是三角形
            if (faceTokens.size() != 3)
            {
                std::cerr << "Warning: Face at line " << lineNum
                    << " has " << faceTokens.size()
                    << " vertices, expected 3" << std::endl;
                continue;
            }

            // 处理三个顶点
            for (const auto& faceToken : faceTokens)
            {
                FaceIndices indices = ParseFaceIndices(faceToken);

                // 获取位置（必需）
                glm::vec3 pos = glm::vec3(0.0f);
                if (indices.posIdx >= 0 && indices.posIdx < positions.size())
                {
                    pos = positions[indices.posIdx];
                }

                // 获取法线（可选）
                glm::vec3 norm = glm::vec3(0.0f, 1.0f, 0.0f);  // 默认向上
                if (indices.normIdx >= 0 && indices.normIdx < normals.size())
                {
                    norm = normals[indices.normIdx];
                }

                // 获取纹理坐标（可选）
                glm::vec2 tex = glm::vec2(0.0f);
                if (indices.texIdx >= 0 && indices.texIdx < texCoords.size())
                {
                    tex = texCoords[indices.texIdx];
                }

                vertexData.push_back(pos.x);
                vertexData.push_back(pos.y);
                vertexData.push_back(pos.z);

                // 法线 (3 floats)
                vertexData.push_back(norm.x);
                vertexData.push_back(norm.y);
                vertexData.push_back(norm.z);

                // 纹理坐标 (2 floats)
                vertexData.push_back(tex.x);
                vertexData.push_back(tex.y);
            }
        }
    }

    file.close();
    vertexCount = static_cast<unsigned int>(vertexData.size() / 8);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
        vertexData.size() * sizeof(float),
        vertexData.data(),
        GL_STATIC_DRAW);

    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 法线属性 (location = 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 纹理坐标属性 (location = 2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

ModelData::FaceIndices ModelData::ParseFaceIndices(const std::string& token)
{
    FaceIndices indices;
    std::vector<std::string> parts;
    std::stringstream ss(token);
    std::string part;

    // 按 '/' 分割
    while (std::getline(ss, part, '/'))
    {
        parts.push_back(part);
    }

    // 解析位置索引（总是第一个）
    if (!parts[0].empty())
    {
        indices.posIdx = std::stoi(parts[0]) - 1;  // OBJ 索引从 1 开始
    }

    // 解析纹理坐标索引（第二个，如果有）
    if (parts.size() > 1 && !parts[1].empty())
    {
        indices.texIdx = std::stoi(parts[1]) - 1;
    }

    // 解析法线索引（第三个，如果有）
    if (parts.size() > 2 && !parts[2].empty())
    {
        indices.normIdx = std::stoi(parts[2]) - 1;
    }

    return indices;
}

#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../../3rdParty/GLM/glm.hpp"

struct ModelData; struct MeshData;
struct Resource
{
	std::string resourcePath;
	
	
	
	std::unique_ptr<ModelData> cubeModel, sphereModel, planeModel;
	std::unique_ptr<MeshData> cubeMesh, sphereMesh, planeMesh;

	Resource(const std::string& basePath = "");
	~Resource();

	void Initialize(const std::string& basePath);
};

struct ModelData 
{
	std::string modelName; int vertexCount;
	
	
	
	
	std::vector<float> vertexData;
	std::vector<unsigned int> indices;
	ModelData(const std::string& path);
	ModelData(const std::string& path, bool useAssimp);
	struct FaceIndices { int posIdx, texIdx, normIdx; };
	FaceIndices ParseFaceIndices(const std::string& token);
};

struct MeshData 
{
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    glm::vec3 aabbMin, aabbMax;

    MeshData(const std::string& filePath);
    MeshData(const std::string& filePath, bool useAssimp);
};

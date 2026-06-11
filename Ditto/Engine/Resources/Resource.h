#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../../3rdParty/GLM/glm.hpp"

struct ModelData; struct MeshData;
struct Resource
{
	std::string resourcePath;
	// Owned built-in assets (RAII). unique_ptr also guarantees these start null,
	// so a non-empty basePath construction no longer touches uninitialized
	// pointers, and the meshes are no longer leaked on destruction.
	std::unique_ptr<ModelData> cubeModel, sphereModel, planeModel;
	std::unique_ptr<MeshData> cubeMesh, sphereMesh, planeMesh;

	Resource(const std::string& basePath = "");
	~Resource();

	void Initialize(const std::string& basePath);
};

struct ModelData // For Rendering
{
	std::string modelName; int vertexCount;
	// Indexed geometry: `vertexData` holds unique pos+normal+uv tuples
	// (interleaved, 8 floats each), `indices` references them per triangle.
	// Shared (posIdx, normIdx, texIdx) corners dedupe to one vertex, so a typical smooth mesh uploads
	// far fewer vertices than the old flattened-per-corner layout.
	std::vector<float> vertexData;
	std::vector<unsigned int> indices;
	ModelData(const std::string& path);
	ModelData(const std::string& path, bool useAssimp);
	struct FaceIndices { int posIdx, texIdx, normIdx; };
	FaceIndices ParseFaceIndices(const std::string& token);
};

struct MeshData // For Physics
{
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    glm::vec3 aabbMin, aabbMax;

    MeshData(const std::string& filePath);
    MeshData(const std::string& filePath, bool useAssimp);
};

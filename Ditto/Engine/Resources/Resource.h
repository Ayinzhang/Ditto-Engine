#include <string>
#include <vector>
#include "../../3rdParty/GLM/glm.hpp"

struct ModelData; struct MeshData;
struct Resource
{
	std::string resourcePath = "../../Assets/Models";
	ModelData* cubeModel, *sphereModel, *planeModel;
	MeshData* cubeMesh, *sphereMesh, *planeMesh;

	Resource();
	~Resource();
};

struct ModelData // For Rendering
{
	std::string modelName;
	int vertexCount;
	std::vector<float> vertexData;
	ModelData(const std::string& path);
	struct FaceIndices { int posIdx, texIdx, normIdx; };
	FaceIndices ParseFaceIndices(const std::string& token);
};

struct MeshData // For Physics
{
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    glm::vec3 aabbMin, aabbMax;

    MeshData(const std::string& filePath);

	void CalculateAABB();
    bool CheckAABBCollision(const MeshData& other) const;
    glm::vec3 GetAABBCenter() const;
    glm::vec3 GetAABBSize() const;
    glm::vec3 GetSupportPoint(const glm::vec3& direction) const;
};
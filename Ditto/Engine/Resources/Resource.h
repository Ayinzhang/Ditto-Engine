#include <string>
#include <vector>

struct ModelData;
struct Resource
{
	std::string resourcePath = "../../Assets/Models";
	ModelData* cubeModel, *sphereModel, *planeModel;

	Resource();
	~Resource();
};

struct ModelData
{
	std::string modelName;
	unsigned int vertexCount, VAO, VBO;
	std::vector<float> vertexData;
	ModelData(const std::string& path);
	~ModelData();
	struct FaceIndices { int posIdx, texIdx, normIdx; };
	FaceIndices ParseFaceIndices(const std::string& token);
};
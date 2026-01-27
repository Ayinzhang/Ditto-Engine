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
	int vertexCount;
	std::vector<float> vertexData;
	ModelData(const std::string& path);
	struct FaceIndices { int posIdx, texIdx, normIdx; };
	FaceIndices ParseFaceIndices(const std::string& token);
};
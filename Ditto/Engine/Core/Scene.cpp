#include "Scene.h"

Scene::Scene()
{
	name = "Default";
	GameObject* lightObj = new GameObject("DirLight");
	lightObj->AddComponent<LightComponent>();
	gameObjects.push_back(lightObj);
	
	GameObject* gameObject = new GameObject("Cube");
	gameObject->AddComponent<RendererComponent>();
	gameObject->AddComponent<RigidbodyComponent>();
	gameObjects.push_back(gameObject);
}

Scene::~Scene()
{
	for (GameObject* obj : gameObjects) delete obj;
}

void Scene::SaveScene(const std::string& filepath)
{

}

void Scene::LoadScene(const std::string& filepath)
{

}


#include "Scene.h"

Scene::Scene()
{
	name = "Default";
	GameObject* gameObject = new GameObject();
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
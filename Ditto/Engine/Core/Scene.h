#pragma once
#include <string>
#include <vector>
#include "GameObject.h"

struct Scene
{
    std::string name;
    std::vector<GameObject*> gameObjects;
    Scene();
    ~Scene();
    void SaveScene(const std::string& filepath);
    void LoadScene(const std::string& filepath);
};
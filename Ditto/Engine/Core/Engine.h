#pragma once
#define GLFW_INCLUDE_NONE
#include "Scene.h"
#include "../../Editor/Editor.h"
#include "../../Engine/Graphics/Shader.h"
#include "../../Engine/Graphics/Camera.h"
#include "../../Engine/Resources/Resource.h"
#include "../../3rdParty/GLFW/glfw3.h"
#include "../../3rdParty/ImGui/imgui.h"

struct Engine
{
	GLFWwindow* window; 
	int window_width, window_height;
	Resource* resource;
	Scene* scene;
	Editor* editor;
	Camera* camera;
	bool enableMouse, firstMouse; float keySpeed = 0.01, mouseSpeed = 0.01; double lastX, lastY;
	Shader* shader;
	unsigned int VAO, VBO; size_t MAX_VERTICES = 1e8;
	bool isRunning;
	Engine();
	~Engine();
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;
	void Run();
	void ProcessInput();
	void RenderScene();
	static void MouseCallBack(GLFWwindow* window, double xpos, double ypos);
};
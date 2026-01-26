#include "Engine.h"
#include <iostream>
#include <stdexcept>
#include "../../Editor/Editor.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
using namespace std;

Engine::Engine()
{
    isRunning = true; enableMouse = false; window_width = 1200; window_height = 900;
    
    if (!glfwInit()) throw std::runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(window_width, window_height, "Ditto", nullptr, nullptr);
    if (!window) glfwTerminate(), throw std::runtime_error("Window create failed");
    glfwMakeContextCurrent(window); 
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, Engine::MouseCallBack);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) throw std::runtime_error("Failed to initialize GLAD");

    resource = new Resource();
	scene = new Scene();
	camera = new Camera(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    shader = new Shader("../../Ditto/Ditto/Assets/Shaders/Vertex.glsl", "../../Ditto/Ditto/Assets/Shaders/Fragment.glsl");
	editor = new Editor(window); editor->engine = this;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Engine::~Engine()
{
    delete editor;
    if (window) glfwDestroyWindow(window);
    glfwTerminate();

}

void Engine::Run()
{
    while (isRunning && !glfwWindowShouldClose(window)) 
    {
        // 1. 处理输入
        ProcessInput();

        // 2. 处理事件（这会更新窗口状态）
        glfwPollEvents();

        // 3. 更新游戏逻辑（这里可以添加你的游戏更新逻辑）
        // Update();

        // 4. 渲染（如果是No API，需要自己实现渲染）
        glfwGetFramebufferSize(window, &window_width, &window_height);
        glViewport(0, 0, window_width, window_height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderScene();
        editor->Draw();

        glfwSwapBuffers(window);
    }
}

void Engine::RenderScene()
{
    // 使用白色背景
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    assert(resource->cubeModel->vertexData.size() <= MAX_VERTICES);
    void* ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    memcpy(ptr, resource->cubeModel->vertexData.data(), resource->cubeModel->vertexData.size() * sizeof(float));
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, resource->cubeModel->vertexData.size());
    glBindVertexArray(0);

    // 设置模型矩阵
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader->id, "model"), 1, GL_FALSE, glm::value_ptr(model));

	glm::mat4 view = camera->GetViewMatrix();
    glUniformMatrix4fv(glGetUniformLocation(shader->id, "view"), 1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader->id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    // 绘制立方体
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, resource->cubeModel->vertexCount);

    // 清理
    glBindVertexArray(0);
}

void Engine::ProcessInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) isRunning = false;
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) enableMouse = !enableMouse;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera->position += camera->forward * keySpeed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera->position -= camera->forward * keySpeed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera->position -= camera->right * keySpeed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera->position += camera->right * keySpeed;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera->position += camera->up * keySpeed;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera->position -= camera->up * keySpeed;
}

void Engine::MouseCallBack(GLFWwindow* window, double xpos, double ypos)
{
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine->enableMouse) { engine->lastX = xpos, engine->lastY = ypos; return; }
    engine->camera->ProcessMouseMovement(engine->mouseSpeed * (xpos - engine->lastX), engine->mouseSpeed * (ypos - engine->lastY));
    engine->lastX = xpos; engine->lastY = ypos;
}
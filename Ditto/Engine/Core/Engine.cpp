#include "Engine.h"
#include <iostream>
#include <stdexcept>
#include "../../Editor/Editor.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

using namespace std;
using namespace glm;

Engine::Engine()
{
    isRunning = true;
    enableMouse = false;
    window_width = 1200;
    window_height = 900;

    if (!glfwInit()) throw runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(window_width, window_height, "Ditto", nullptr, nullptr);
    if (!window) glfwTerminate(), throw runtime_error("Window create failed");
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, Engine::MouseCallBack);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) throw runtime_error("Failed to initialize GLAD");

    resource = new Resource();
    scene = new Scene();
    camera = new Camera(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    shader = new Shader("../../Ditto/Ditto/Assets/Shaders/Vertex.glsl", "../../Ditto/Ditto/Assets/Shaders/Fragment.glsl");
    editor = new Editor(window);
    editor->engine = this;

    scene->InitializeBaseGeometries(resource);
}

Engine::~Engine()
{
    delete editor;
    delete shader;
    delete camera;
    delete scene;
    delete resource;
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

void Engine::Run()
{
    while (isRunning && !glfwWindowShouldClose(window))
    {
        ProcessInput();
        glfwPollEvents();

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
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    mat4 view = camera->GetViewMatrix();
    mat4 projection = perspective(radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);

    scene->Render(shader, view, projection, camera->position, window_width, window_height);
}

void Engine::ProcessInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) isRunning = false;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera->position += camera->forward * keySpeed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera->position -= camera->forward * keySpeed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera->position -= camera->right * keySpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera->position += camera->right * keySpeed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera->position += camera->up * keySpeed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera->position -= camera->up * keySpeed;

    static bool altPressedLastFrame = false;
    bool altPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    if (altPressedNow && !altPressedLastFrame) enableMouse = !enableMouse;
    altPressedLastFrame = altPressedNow;

    static bool deletePressedLastFrame = false;
    bool deletePressedNow = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (deletePressedNow && !deletePressedLastFrame) editor->DeleteSelectedObject();
    deletePressedLastFrame = deletePressedNow;

    static bool ctrlCPressedLastFrame = false;
    bool ctrlCPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    if (ctrlCPressedNow && !ctrlCPressedLastFrame) editor->CopySelectedObject();
    ctrlCPressedLastFrame = ctrlCPressedNow;
}

void Engine::MouseCallBack(GLFWwindow* window, double xpos, double ypos)
{
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine->enableMouse) { engine->lastX = xpos; engine->lastY = ypos; return; }
    engine->camera->ProcessMouseMovement(engine->mouseSpeed * (xpos - engine->lastX) / engine->window_width,
        engine->mouseSpeed * (ypos - engine->lastY) / engine->window_height);
    engine->lastX = xpos;
    engine->lastY = ypos;
}
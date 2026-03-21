#include "ModelPreviewManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#define GLFW_INCLUDE_NONE
#include "../3rdParty/GLFW/glfw3.h"
#include "../3rdParty/GLAD/glad.h"
#include "../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../3rdParty/GLM/ext/matrix_clip_space.hpp"
#include "../Engine/Graphics/Camera.h"

namespace fs = std::filesystem;

ModelPreviewManager& ModelPreviewManager::GetInstance()
{
    static ModelPreviewManager instance;
    return instance;
}

void ModelPreviewManager::Initialize(int width, int height)
{
    if (m_initialized) return;
    
    m_width = width;
    m_height = height;
    
    std::cout << "[Preview] Initializing..." << std::endl;
    
    // 检查 OpenGL 上下文
    GLenum err = glGetError();
    std::cout << "[Preview] GL error before init: " << err << std::endl;
    
    // 创建 FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    // 创建颜色纹理
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    // 创建深度渲染缓冲
    glGenRenderbuffers(1, &m_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // 创建预览相机
    m_camera = new Camera(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    
    CompileShaders();
    
    m_initialized = true;
    std::cout << "[Preview] Initialized successfully" << std::endl;
}

void ModelPreviewManager::CompileShaders()
{
    const char* vertSrc = R"(
        #version 460 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 model, view, projection;
        void main() { gl_Position = projection * view * model * vec4(aPos, 1.0); }
    )";
    
    const char* fragSrc = R"(
        #version 460 core
        out vec4 col;
        void main() { col = vec4(1.0f, 1.0f, 1.0f, 1.0f); }
    )";
    
    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, nullptr);
    glCompileShader(vert);
    
    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, nullptr);
    glCompileShader(frag);
    
    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);
    
    glDeleteShader(vert);
    glDeleteShader(frag);
    
    std::cout << "[Preview] Program: " << m_program << std::endl;
}

bool ModelPreviewManager::LoadModel(const std::string& modelPath)
{
    if (modelPath.empty()) return false;
    
    // 如果是同一个模型，不需要重新加载
    if (m_currentPath == modelPath && m_model.vao != 0) {
        return true;
    }
    
    // 清理之前的模型
    if (m_model.vao) {
        glDeleteVertexArrays(1, &m_model.vao);
        glDeleteBuffers(1, &m_model.vbo);
        if (m_model.ebo) glDeleteBuffers(1, &m_model.ebo);
        m_model = ModelData();
    }
    
    m_currentPath = modelPath;
    
    // 读取 OBJ 文件
    std::ifstream file(modelPath);
    if (!file.is_open()) {
        std::cerr << "[Preview] Failed to open: " << modelPath << std::endl;
        return false;
    }
    
    std::vector<glm::vec3> positions;
    std::vector<unsigned int> indices;
    
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;
        
        if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "f") {
            std::vector<std::string> faceTokens;
            std::string token;
            while (ss >> token) faceTokens.push_back(token);
            
            for (size_t i = 1; i + 1 < faceTokens.size(); i++) {
                for (size_t j = 0; j < 3; j++) {
                    const std::string& ft = (j == 0) ? faceTokens[0] : (j == 1) ? faceTokens[i] : faceTokens[i + 1];
                    std::istringstream ft_ss(ft);
                    std::string idx;
                    std::getline(ft_ss, idx, '/');
                    int posIdx = std::stoi(idx) - 1;
                    indices.push_back(posIdx);
                }
            }
        }
    }
    
    if (positions.empty()) {
        std::cerr << "[Preview] No vertices found" << std::endl;
        return false;
    }
    
    // 计算中心点
    glm::vec3 minPos(positions[0]), maxPos(positions[0]);
    for (const auto& pos : positions) {
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }
    m_model.center = (minPos + maxPos) * 0.5f;
    m_model.radius = glm::length(maxPos - minPos) * 0.5f;
    
    // 构建顶点数据
    std::vector<float> vertexData;
    for (size_t i = 0; i < indices.size(); i++) {
        int idx = indices[i];
        if (idx >= 0 && idx < (int)positions.size()) {
            vertexData.push_back(positions[idx].x);
            vertexData.push_back(positions[idx].y);
            vertexData.push_back(positions[idx].z);
        }
    }
    
    m_model.vertexCount = vertexData.size() / 3;
    
    // 创建 VAO/VBO
    glGenVertexArrays(1, &m_model.vao);
    glGenBuffers(1, &m_model.vbo);
    
    glBindVertexArray(m_model.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_model.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
    
    // 设置相机
    if (m_camera) {
        float distance = m_model.radius * 2.5f;
        m_camera->position = m_model.center + glm::vec3(0, distance * 0.3f, distance);
        m_camera->yaw = -90.0f;
        m_camera->pitch = -15.0f;
        m_camera->UpdateCameraVectors();
    }
    
    m_modelLoaded = true;
    std::cout << "[Preview] Loaded " << m_model.vertexCount << " vertices" << std::endl;
    
    // 重新绑定纹理到 FBO（确保渲染到这个纹理）
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    return true;
}

unsigned int ModelPreviewManager::Render()
{
    // 延迟初始化：如果未初始化，先初始化
    if (!m_initialized) {
        std::cout << "[Preview] Lazy initializing..." << std::endl;
        Initialize(m_width, m_height);
    }
    
    if (!m_initialized) {
        std::cout << "[Preview] Still not initialized!" << std::endl;
        return 0;
    }
    
    // 检查 OpenGL 状态
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cout << "[Preview] GL error before render: " << err << std::endl;
    }
    
    std::cout << "[Preview] Render: m_modelLoaded=" << m_modelLoaded << ", m_vao=" << m_model.vao << ", m_texture=" << m_texture << std::endl;
    
    // 渲染空白背景（无模型时）
    if (!m_modelLoaded || !m_model.vao) {
        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_width, m_height);
        glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        return m_texture;
    }
    
    // 保存状态
    GLint previousFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    GLint cullFaceMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
    
    // 渲染
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    
    glm::mat4 view = m_camera->GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)m_width / (float)m_height, 0.1f, 100.0f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), -m_model.center);
    
    glUseProgram(m_program);
    
    GLint modelLoc = glGetUniformLocation(m_program, "model");
    GLint viewLoc = glGetUniformLocation(m_program, "view");
    GLint projLoc = glGetUniformLocation(m_program, "projection");
    
    if (modelLoc >= 0) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    if (viewLoc >= 0) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
    if (projLoc >= 0) glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);
    
    glBindVertexArray(m_model.vao);
    glDrawArrays(GL_TRIANGLES, 0, m_model.vertexCount);
    
    // 恢复状态
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (cullFaceEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace(cullFaceMode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    return m_texture;
}

void ModelPreviewManager::Cleanup()
{
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_rbo) glDeleteRenderbuffers(1, &m_rbo);
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_camera) delete m_camera;
    if (m_program) glDeleteProgram(m_program);
    
    if (m_model.vao) {
        glDeleteVertexArrays(1, &m_model.vao);
        glDeleteBuffers(1, &m_model.vbo);
        if (m_model.ebo) glDeleteBuffers(1, &m_model.ebo);
    }
    
    m_fbo = m_rbo = m_texture = m_program = 0;
    m_camera = nullptr;
    m_model = ModelData();
    m_initialized = false;
    m_modelLoaded = false;
}
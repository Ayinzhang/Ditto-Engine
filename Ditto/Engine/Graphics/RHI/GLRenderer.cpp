#include "GLRenderer.h"
#include "../../../3rdParty/GLAD/glad.h"
#define GLFW_INCLUDE_NONE
#include "../../../3rdParty/GLFW/glfw3.h"
#include "../../../3rdParty/GLM/gtc/type_ptr.hpp"
#include "../../Core/Logger.h"
#include "../Shaders/ShaderCompiler.h"
#include "../../../3rdParty/ImGui/imgui.h"
#include "../../../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../../../3rdParty/ImGui/imgui_impl_opengl3.h"
#include <cstdint>

namespace Ditto
{
    // ---- Small handle-table helpers (id == index + 1) ----
    template<typename T>
    static uint32_t PushSlot(std::vector<T>& table, const T& res)
    {
        table.push_back(res);
        return static_cast<uint32_t>(table.size());   // id = index + 1
    }
    template<typename T>
    static T* GetSlot(std::vector<T>& table, uint32_t id)
    {
        if (id == 0 || id > table.size()) return nullptr;
        return &table[id - 1];
    }

    GLRenderer::~GLRenderer()
    {
        for (auto& m : m_meshes)
        {
            if (m.vao) glDeleteVertexArrays(1, &m.vao);
            if (m.vbo) glDeleteBuffers(1, &m.vbo);
            if (m.ebo) glDeleteBuffers(1, &m.ebo);
        }
        for (auto& b : m_buffers) if (b.ssbo) glDeleteBuffers(1, &b.ssbo);
        if (m_frameUBO) glDeleteBuffers(1, &m_frameUBO);
        if (m_defaultSampler) glDeleteSamplers(1, &m_defaultSampler);
        for (auto& p : m_pipelines) if (p.program) glDeleteProgram(p.program);
        for (auto& t : m_textures) if (t.tex) glDeleteTextures(1, &t.tex);
        for (auto& rt : m_renderTargets)
        {
            if (rt.fbo) glDeleteFramebuffers(1, &rt.fbo);
            if (rt.rbo) glDeleteRenderbuffers(1, &rt.rbo);
        }
    }

    // ----------------------------- Frame -----------------------------
    void GLRenderer::EndFrame()
    {
        if (m_window) glfwSwapBuffers(static_cast<GLFWwindow*>(m_window));
    }

    // ----------------------------- ImGui -----------------------------
    void GLRenderer::ImGuiInit(void* window)
    {
        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true);
        ImGui_ImplOpenGL3_Init("#version 450");
    }
    void GLRenderer::ImGuiShutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    void GLRenderer::ImGuiNewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }
    void GLRenderer::ImGuiRenderDrawData(void* drawData)
    {
        ImGui_ImplOpenGL3_RenderDrawData(static_cast<ImDrawData*>(drawData));
    }

    // ----------------------------- State -----------------------------
    void GLRenderer::SetViewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }

    void GLRenderer::SetScissor(bool enabled, int x, int y, int w, int h)
    {
        if (enabled) { glEnable(GL_SCISSOR_TEST); glScissor(x, y, w, h); }
        else glDisable(GL_SCISSOR_TEST);
    }

    void GLRenderer::Clear(uint32_t flags, const glm::vec4& color)
    {
        GLbitfield mask = 0;
        if (flags & ClearColor) { glClearColor(color.r, color.g, color.b, color.a); mask |= GL_COLOR_BUFFER_BIT; }
        GLboolean previousDepthMask = GL_TRUE;
        if (flags & ClearDepth)
        {
            glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
            glDepthMask(GL_TRUE);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
        if (flags & ClearDepth) glDepthMask(previousDepthMask);
    }

    void GLRenderer::SetDepthState(bool enabled, DepthFunc func)
    {
        if (enabled)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(func == DepthFunc::LessEqual ? GL_LEQUAL : GL_LESS);
        }
        else glDisable(GL_DEPTH_TEST);
    }

    void GLRenderer::SetBlendState(bool enabled)
    {
        if (enabled) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        else glDisable(GL_BLEND);
    }

    void GLRenderer::SetWireframe(bool enabled)
    {
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }

    void GLRenderer::SetCullState(bool enabled)
    {
        if (enabled) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
        else glDisable(GL_CULL_FACE);
    }

    // --------------------------- Resources ---------------------------
    MeshHandle GLRenderer::CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                                      const std::vector<VertexAttrib>& attribs,
                                      const uint32_t* indices, size_t indexCount)
    {
        GLMesh m;
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glBindVertexArray(m.vao);

        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, floatCount * sizeof(float), vertexData, GL_STATIC_DRAW);

        const GLsizei stride = static_cast<GLsizei>(strideFloats * sizeof(float));
        for (const VertexAttrib& a : attribs)
        {
            glVertexAttribPointer(a.location, a.componentCount, GL_FLOAT, GL_FALSE, stride,
                (void*)(static_cast<uintptr_t>(a.offsetFloats) * sizeof(float)));
            glEnableVertexAttribArray(a.location);
        }

        if (indices && indexCount > 0)
        {
            glGenBuffers(1, &m.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(uint32_t), indices, GL_STATIC_DRAW);
            m.indexCount = static_cast<uint32_t>(indexCount);
        }

        m.vertexCount = strideFloats > 0 ? static_cast<uint32_t>(floatCount / strideFloats) : 0;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return MeshHandle{ PushSlot(m_meshes, m) };
    }

    void GLRenderer::DestroyMesh(MeshHandle h)
    {
        GLMesh* m = GetSlot(m_meshes, h.id);
        if (!m) return;
        if (m->vao) glDeleteVertexArrays(1, &m->vao);
        if (m->vbo) glDeleteBuffers(1, &m->vbo);
        if (m->ebo) glDeleteBuffers(1, &m->ebo);
        *m = GLMesh{};
    }

    StorageBufferHandle GLRenderer::CreateStorageBuffer(size_t sizeBytes, bool dynamic)
    {
        GLBuffer b;
        glGenBuffers(1, &b.ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, b.ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeBytes, nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        b.size = sizeBytes;
        return StorageBufferHandle{ PushSlot(m_buffers, b) };
    }

    void GLRenderer::UpdateStorageBuffer(StorageBufferHandle h, const void* data, size_t sizeBytes)
    {
        GLBuffer* b = GetSlot(m_buffers, h.id);
        if (!b || !b->ssbo) return;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, b->ssbo);
        // Re-upload (orphans the old store), matching the previous per-frame path.
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeBytes, data, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        b->size = sizeBytes;
    }

    void GLRenderer::DestroyStorageBuffer(StorageBufferHandle h)
    {
        GLBuffer* b = GetSlot(m_buffers, h.id);
        if (!b) return;
        if (b->ssbo) glDeleteBuffers(1, &b->ssbo);
        *b = GLBuffer{};
    }

    static unsigned int CompileStage(GLenum type, const std::string& src, const char* label)
    {
        unsigned int shader = glCreateShader(type);
        const char* s = src.c_str();
        glShaderSource(shader, 1, &s, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024]; GLsizei len = 0;
            glGetShaderInfoLog(shader, sizeof(log), &len, log);
            Ditto::Logger::Get().Error(std::string("[RHI] ") + label + " shader compile failed: " + std::string(log, len));
        }
        return shader;
    }

    PipelineHandle GLRenderer::CreatePipeline(const std::string& hlslSource, const PipelineState& state)
    {
        // HLSL -> SPIR-V -> GLSL (via the shared ShaderCompiler), then the usual
        // GL compile/link.
        CompiledShader vs = ShaderCompiler::Compile(hlslSource, ShaderStage::Vertex, "VSMain", true);
        CompiledShader ps = ShaderCompiler::Compile(hlslSource, ShaderStage::Pixel, "PSMain", true);
        if (!vs.ok || !ps.ok)
        {
            Ditto::Logger::Get().Error("[GLRenderer] pipeline shader compile failed");
            return {};
        }

        unsigned int vert = CompileStage(GL_VERTEX_SHADER, vs.glsl, "vertex");
        unsigned int frag = CompileStage(GL_FRAGMENT_SHADER, ps.glsl, "fragment");

        GLPipeline p;
        p.program = glCreateProgram();
        p.state = state;
        glAttachShader(p.program, vert);
        glAttachShader(p.program, frag);
        glLinkProgram(p.program);

        GLint ok = GL_FALSE;
        glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024]; GLsizei len = 0;
            glGetProgramInfoLog(p.program, sizeof(log), &len, log);
            Ditto::Logger::Get().Error(std::string("[RHI] program link failed: ") + std::string(log, len));
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
        return PipelineHandle{ PushSlot(m_pipelines, p) };
    }

    void GLRenderer::DestroyPipeline(PipelineHandle h)
    {
        GLPipeline* p = GetSlot(m_pipelines, h.id);
        if (!p) return;
        if (p->program) glDeleteProgram(p->program);
        *p = GLPipeline{};
    }

    TextureHandle GLRenderer::CreateTexture(const unsigned char* pixels, int w, int h, int channels)
    {
        GLenum format = (channels == 1) ? GL_RED : (channels == 3) ? GL_RGB : GL_RGBA;

        GLTexture t;
        glGenTextures(1, &t.tex);
        glBindTexture(GL_TEXTURE_2D, t.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        return TextureHandle{ PushSlot(m_textures, t) };
    }

    void GLRenderer::DestroyTexture(TextureHandle h)
    {
        GLTexture* t = GetSlot(m_textures, h.id);
        if (!t) return;
        if (t->tex) glDeleteTextures(1, &t->tex);
        *t = GLTexture{};
    }

    void* GLRenderer::GetImGuiTextureID(TextureHandle h)
    {
        GLTexture* t = GetSlot(m_textures, h.id);
        return t ? (void*)(intptr_t)t->tex : nullptr;
    }

    void GLRenderer::BindTexture(int binding, TextureHandle h)
    {
        GLTexture* t = GetSlot(m_textures, h.id);
        if (!m_defaultSampler)
        {
            glGenSamplers(1, &m_defaultSampler);
            glSamplerParameteri(m_defaultSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glSamplerParameteri(m_defaultSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glSamplerParameteri(m_defaultSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glSamplerParameteri(m_defaultSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTextureUnit(static_cast<GLuint>(binding), t ? t->tex : 0);
        glBindSampler(static_cast<GLuint>(binding), m_defaultSampler);
    }

    RenderTargetHandle GLRenderer::CreateRenderTarget(int w, int h)
    {
        GLRenderTargetRes rt;
        rt.w = w; rt.h = h;

        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);

        GLTexture color;
        glGenTextures(1, &color.tex);
        glBindTexture(GL_TEXTURE_2D, color.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.tex, 0);
        rt.color = TextureHandle{ PushSlot(m_textures, color) };

        glGenRenderbuffers(1, &rt.rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rt.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.rbo);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            Ditto::Logger::Get().Error("[RHI] render target framebuffer incomplete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        return RenderTargetHandle{ PushSlot(m_renderTargets, rt) };
    }

    void GLRenderer::BeginRenderTarget(RenderTargetHandle h)
    {
        GLRenderTargetRes* rt = GetSlot(m_renderTargets, h.id);
        if (!rt) return;
        // Save the bound framebuffer + viewport so EndRenderTarget can restore the
        // editor's in-progress ImGui frame.
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&m_savedFBO));
        glGetIntegerv(GL_VIEWPORT, m_savedViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        glViewport(0, 0, rt->w, rt->h);
    }

    void GLRenderer::EndRenderTarget()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_savedFBO);
        glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
    }

    TextureHandle GLRenderer::GetColorTexture(RenderTargetHandle h)
    {
        GLRenderTargetRes* rt = GetSlot(m_renderTargets, h.id);
        return rt ? rt->color : TextureHandle{};
    }

    bool GLRenderer::ReadRenderTargetPixels(RenderTargetHandle h, std::vector<unsigned char>& rgba)
    {
        GLRenderTargetRes* rt = GetSlot(m_renderTargets, h.id);
        if (!rt || !rt->fbo || rt->w <= 0 || rt->h <= 0) return false;

        rgba.resize(static_cast<size_t>(rt->w * rt->h * 4));
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        glReadPixels(0, 0, rt->w, rt->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
        return true;
    }

    void GLRenderer::DestroyRenderTarget(RenderTargetHandle h)
    {
        GLRenderTargetRes* rt = GetSlot(m_renderTargets, h.id);
        if (!rt) return;
        if (rt->fbo) glDeleteFramebuffers(1, &rt->fbo);
        if (rt->rbo) glDeleteRenderbuffers(1, &rt->rbo);
        DestroyTexture(rt->color);   // frees the color attachment's GL texture + slot
        *rt = GLRenderTargetRes{};
    }

    // ----------------------------- Draw ------------------------------
    void GLRenderer::BindPipeline(PipelineHandle h)
    {
        GLPipeline* p = GetSlot(m_pipelines, h.id);
        m_currentProgram = p ? p->program : 0;
        glUseProgram(m_currentProgram);
        if (!p) return;

        if (p->state.depthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(p->state.depthFunc == DepthFunc::LessEqual ? GL_LEQUAL : GL_LESS);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(p->state.depthWrite ? GL_TRUE : GL_FALSE);

        if (p->state.blend)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
        glPolygonMode(GL_FRONT_AND_BACK, p->state.wireframe ? GL_LINE : GL_FILL);

        if (p->state.cull == CullMode::Off)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(p->state.cull == CullMode::Front ? GL_FRONT : GL_BACK);
        }
    }

    void GLRenderer::SetFrameUniforms(const FrameUniforms& u)
    {
        // std140 mirror of the HLSL `FrameUniforms` cbuffer (vec3 + pad pairs).
        // Matrices are uploaded as-is (GLM column-major); the generated GLSL marks
        // them row_major, which composes with its row-vector multiplies to the
        // same result. Layout = 176 bytes.
        struct Std140
        {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec3 viewPos;     float _p0;
            glm::vec3 lightColor;  float _p1;
            glm::vec3 lightDir;    float lightIntensity;
            glm::vec4 time;
            glm::vec4 sinTime;
            glm::vec4 cosTime;
            glm::vec4 deltaTime;
            glm::vec4 screenParams;
        } data;
        data.view = u.view;
        data.projection = u.projection;
        data.viewPos = u.viewPos;          data._p0 = 0.0f;
        data.lightColor = u.lightColor;    data._p1 = 0.0f;
        data.lightDir = u.lightDir;        data.lightIntensity = u.lightIntensity;
        data.time = u.time;
        data.sinTime = u.sinTime;
        data.cosTime = u.cosTime;
        data.deltaTime = u.deltaTime;
        data.screenParams = u.screenParams;

        if (!m_frameUBO)
        {
            glGenBuffers(1, &m_frameUBO);
            glBindBuffer(GL_UNIFORM_BUFFER, m_frameUBO);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(Std140), nullptr, GL_DYNAMIC_DRAW);
        }
        glBindBuffer(GL_UNIFORM_BUFFER, m_frameUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Std140), &data);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_frameUBO);   // FrameUniforms @ binding 0
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void GLRenderer::BindStorageBuffer(int binding, StorageBufferHandle h)
    {
        GLBuffer* b = GetSlot(m_buffers, h.id);
        if (!b || !b->ssbo) return;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, b->ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, b->ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void GLRenderer::DrawInstanced(MeshHandle h, int instanceCount)
    {
        GLMesh* m = GetSlot(m_meshes, h.id);
        if (!m || !m->vao || instanceCount <= 0) return;
        glBindVertexArray(m->vao);
        if (m->indexCount > 0)
            glDrawElementsInstanced(GL_TRIANGLES, m->indexCount, GL_UNSIGNED_INT, 0, instanceCount);
        else
            glDrawArraysInstanced(GL_TRIANGLES, 0, m->vertexCount, instanceCount);
        glBindVertexArray(0);
    }
}

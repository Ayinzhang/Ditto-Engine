#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/PathUtils.h"
#include "../Engine/Core/Logger.h"
#include "../Engine/Resources/Resource.h"
#include "../Engine/Graphics/RHI/GLRenderer.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Graphics/Shaders/ShaderAsset.h"

#include "../3rdParty/GLAD/glad.h"
#define GLFW_INCLUDE_NONE
#include "../3rdParty/GLFW/glfw3.h"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Options
    {
        std::string shader;
        fs::path outDir = "TestOutput/RenderSmoke";
        int width = 256;
        int height = 256;
    };

    Options ParseArgs(int argc, char** argv)
    {
        Options opt;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--shader" && i + 1 < argc) opt.shader = argv[++i];
            else if (arg == "--out" && i + 1 < argc) opt.outDir = argv[++i];
            else if (arg == "--size" && i + 1 < argc)
            {
                opt.width = opt.height = std::stoi(argv[++i]);
            }
        }
        return opt;
    }

    fs::path WriteDefaultUnlitShader(const fs::path& outDir)
    {
        fs::path path = outDir / "UnlitSmoke.shader";
        std::ofstream shader(path, std::ios::trunc);
        shader << "Shader \"Ditto/Test/UnlitSmoke\"\n";
        shader << "{\n";
        shader << "    Properties\n";
        shader << "    {\n";
        shader << "        _Color (\"Color\", Color) = (1, 0.05, 0.02, 1)\n";
        shader << "    }\n";
        shader << "    SubShader\n";
        shader << "    {\n";
        shader << "        Tags { \"RenderType\" = \"Opaque\" \"Queue\" = \"Geometry\" }\n";
        shader << "        Pass\n";
        shader << "        {\n";
        shader << "            CGPROGRAM\n";
        shader << "            struct appdata { float4 vertex : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };\n";
        shader << "            struct v2f { float4 pos : SV_Position; };\n";
        shader << "            v2f vert(appdata v) { v2f o; o.pos = ObjectToClipPos(v.vertex); return o; }\n";
        shader << "            fixed4 frag(v2f i) : SV_Target { return _Color; }\n";
        shader << "            ENDCG\n";
        shader << "        }\n";
        shader << "    }\n";
        shader << "}\n";
        return fs::absolute(path);
    }

    std::string EscapeJson(const std::string& value)
    {
        std::string out;
        for (char c : value)
        {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    }

    void WritePPM(const fs::path& path, const std::vector<unsigned char>& rgba, int w, int h)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "P6\n" << w << " " << h << "\n255\n";
        for (int y = h - 1; y >= 0; --y)
        {
            for (int x = 0; x < w; ++x)
            {
                const size_t i = static_cast<size_t>((y * w + x) * 4);
                out.put(static_cast<char>(rgba[i + 0]));
                out.put(static_cast<char>(rgba[i + 1]));
                out.put(static_cast<char>(rgba[i + 2]));
            }
        }
    }

    void WriteSceneJson(const fs::path& path, const Options& opt)
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{\n";
        out << "  \"scene\":\"RenderSmoke\",\n";
        out << "  \"objects\":[\n";
        out << "    {\"name\":\"ShaderCube\",\"components\":[\"Transform\",\"Renderer\"],";
        out << "\"shader\":\"" << EscapeJson(opt.shader) << "\",";
        out << "\"material\":\"RenderSmoke.mat\",\"mesh\":\"Cube\"}\n";
        out << "  ]\n";
        out << "}\n";
    }

    void WriteShaderJson(const fs::path& path, const Ditto::ShaderAsset& shader)
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{\n";
        out << "  \"ok\":" << (shader.ok ? "true" : "false") << ",\n";
        out << "  \"name\":\"" << EscapeJson(shader.shaderName) << "\",\n";
        out << "  \"source\":\"" << EscapeJson(shader.sourcePath) << "\",\n";
        out << "  \"renderQueue\":" << shader.pipelineState.renderQueue << ",\n";
        out << "  \"blend\":" << (shader.pipelineState.blend ? "true" : "false") << ",\n";
        out << "  \"depthTest\":" << (shader.pipelineState.depthTest ? "true" : "false") << ",\n";
        out << "  \"properties\":[";
        for (size_t i = 0; i < shader.properties.size(); ++i)
        {
            if (i) out << ",";
            out << "\"" << EscapeJson(shader.properties[i].name) << "\"";
        }
        out << "]\n";
        out << "}\n";
    }

    void WriteMaterialJson(const fs::path& path, const Ditto::MaterialAsset& material)
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{\n";
        out << "  \"ok\":" << (material.ok ? "true" : "false") << ",\n";
        out << "  \"name\":\"" << EscapeJson(material.materialName) << "\",\n";
        out << "  \"source\":\"" << EscapeJson(material.sourcePath) << "\",\n";
        out << "  \"shader\":\"" << EscapeJson(material.shaderName) << "\",\n";
        out << "  \"mainTexture\":\"" << EscapeJson(material.mainTexturePath) << "\",\n";
        out << "  \"color\":[" << material.color.r << "," << material.color.g << ","
            << material.color.b << "," << material.color.a << "]\n";
        out << "}\n";
    }
}

int main(int argc, char** argv)
{
    const Options opt = ParseArgs(argc, argv);
    fs::create_directories(opt.outDir);
    Options runOpt = opt;
    if (runOpt.shader.empty())
        runOpt.shader = WriteDefaultUnlitShader(runOpt.outDir).string();

    if (!glfwInit())
    {
        std::cerr << "[FAIL][render] glfwInit failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "DittoRenderSmoke", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[FAIL][render] glfwCreateWindow failed\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[FAIL][render] gladLoadGLLoader failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Ditto::ShaderAsset shader = Ditto::LoadShaderAsset(runOpt.shader);
    WriteShaderJson(opt.outDir / "render.shader.json", shader);
    if (!shader.ok)
    {
        std::cerr << "[FAIL][render] shader load failed: " << shader.error << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Ditto::MaterialAsset material = Ditto::MakeDefaultMaterial("RenderSmoke");
    material.sourcePath = (opt.outDir / "RenderSmoke.mat").string();
    material.shaderName = runOpt.shader;
    material.color = glm::vec4(1.0f, 0.05f, 0.02f, 1.0f);
    if (!Ditto::SaveMaterialAsset(material, opt.outDir / "RenderSmoke.mat"))
    {
        std::cerr << "[FAIL][render] material save failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    material = Ditto::LoadMaterialAsset((opt.outDir / "RenderSmoke.mat").string());
    WriteMaterialJson(opt.outDir / "render.material.json", material);
    if (!material.ok)
    {
        std::cerr << "[FAIL][render] material load failed: " << material.error << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int exitCode = 0;
    {
        Ditto::GLRenderer renderer(window);
        Resource resource;
        Scene scene;
        scene.name = "RenderSmoke";
        scene.rootGameObject->name = scene.name;
        scene.InitializeBaseGeometries(&resource, &renderer);

        auto cube = std::make_unique<GameObject>("ShaderCube");
        auto* transform = cube->GetComponent<TransformComponent>();
        transform->position = glm::vec3(0.0f);
        transform->scale = glm::vec3(1.0f);
        transform->localDirty = true;
        transform->UpdateTransform();
        auto* rc = cube->AddComponent<RendererComponent>();
        rc->meshPath = "Models/Cube.obj";
        rc->materialPath = (opt.outDir / "RenderSmoke.mat").string();
        scene.rootGameObject->AddChild(std::move(cube));

        auto sprite = std::make_unique<GameObject>("SmokeSprite");
        auto* spriteTransform = sprite->GetComponent<TransformComponent>();
        spriteTransform->position = glm::vec3(1.1f, 0.0f, 0.0f);
        spriteTransform->scale = glm::vec3(0.8f);
        spriteTransform->localDirty = true;
        spriteTransform->UpdateTransform();
        auto* spriteRenderer = sprite->AddComponent<SpriteRendererComponent>();
        spriteRenderer->spritePath = "Sprites/Square.png";
        spriteRenderer->materialPath = "Materials/Lit_Sprite.mat";
        spriteRenderer->color = glm::vec4(0.05f, 0.25f, 1.0f, 1.0f);
        scene.rootGameObject->AddChild(std::move(sprite));

        auto ui = std::make_unique<GameObject>("SmokeUIImage");
        auto* uiImage = ui->AddComponent<UIImageComponent>();
        uiImage->anchor = UIAnchor::TopLeft;
        uiImage->offset = glm::vec2(16.0f, 16.0f);
        uiImage->size = glm::vec2(64.0f, 64.0f);
        uiImage->color = glm::vec4(0.02f, 1.0f, 0.08f, 1.0f);
        scene.rootGameObject->AddChild(std::move(ui));

        Ditto::PipelineHandle pipeline = renderer.CreatePipeline(shader.engineHLSL, shader.pipelineState);
        if (!pipeline)
        {
            std::cerr << "[FAIL][render] CreatePipeline failed\n";
            exitCode = 1;
        }
        else
        {
            Ditto::RenderTargetHandle rt = renderer.CreateRenderTarget(opt.width, opt.height);
            renderer.BeginFrame();
            renderer.BeginRenderTarget(rt);
            renderer.SetViewport(0, 0, opt.width, opt.height);
            renderer.Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.02f, 0.03f, 0.04f, 1.0f));

            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
            scene.Render(pipeline, view, proj, glm::vec3(0.0f, 0.0f, 3.0f), opt.width, opt.height, true);

            std::vector<unsigned char> pixels(static_cast<size_t>(opt.width * opt.height * 4));
            glReadPixels(0, 0, opt.width, opt.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            renderer.EndRenderTarget();

            WritePPM(opt.outDir / "render.ppm", pixels, opt.width, opt.height);
            WriteSceneJson(opt.outDir / "render.scene.json", runOpt);

            renderer.BeginRenderTarget(rt);
            renderer.SetViewport(0, 0, opt.width, opt.height);
            renderer.Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.02f, 0.03f, 0.04f, 1.0f));
            glm::mat4 orthoView = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 orthoProj = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.01f, 100.0f);
            scene.Render(pipeline, orthoView, orthoProj, glm::vec3(0.0f, 0.0f, 3.0f), opt.width, opt.height, false);
            std::vector<unsigned char> orthoPixels(static_cast<size_t>(opt.width * opt.height * 4));
            glReadPixels(0, 0, opt.width, opt.height, GL_RGBA, GL_UNSIGNED_BYTE, orthoPixels.data());
            renderer.EndRenderTarget();

            int nonBackground = 0;
            int orthoNonBackground = 0;
            int uiPixels = 0;
            int spritePixels = 0;
            unsigned long long sumR = 0, sumG = 0, sumB = 0;
            const unsigned char bgR = 5, bgG = 8, bgB = 10;
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                const int dr = std::abs(static_cast<int>(pixels[i + 0]) - bgR);
                const int dg = std::abs(static_cast<int>(pixels[i + 1]) - bgG);
                const int db = std::abs(static_cast<int>(pixels[i + 2]) - bgB);
                if (dr + dg + db > 24) ++nonBackground;
                if (pixels[i + 1] > 180 && pixels[i + 0] < 80 && pixels[i + 2] < 80) ++uiPixels;
                sumR += pixels[i + 0];
                sumG += pixels[i + 1];
                sumB += pixels[i + 2];
            }
            for (size_t i = 0; i < orthoPixels.size(); i += 4)
            {
                const int dr = std::abs(static_cast<int>(orthoPixels[i + 0]) - bgR);
                const int dg = std::abs(static_cast<int>(orthoPixels[i + 1]) - bgG);
                const int db = std::abs(static_cast<int>(orthoPixels[i + 2]) - bgB);
                if (dr + dg + db > 24) ++orthoNonBackground;
                if (orthoPixels[i + 2] > 160 && orthoPixels[i + 0] < 80 && orthoPixels[i + 1] < 120)
                    ++spritePixels;
            }
            const size_t center = static_cast<size_t>(((opt.height / 2) * opt.width + (opt.width / 2)) * 4);

            std::ofstream stats(opt.outDir / "render.pixels.json", std::ios::trunc);
            stats << "{\n";
            stats << "  \"width\":" << opt.width << ",\n";
            stats << "  \"height\":" << opt.height << ",\n";
            stats << "  \"nonBackgroundPixels\":" << nonBackground << ",\n";
            stats << "  \"orthographicNonBackgroundPixels\":" << orthoNonBackground << ",\n";
            stats << "  \"spritePixels\":" << spritePixels << ",\n";
            stats << "  \"uiPixels\":" << uiPixels << ",\n";
            stats << "  \"centerRGBA\":[" << (int)pixels[center] << "," << (int)pixels[center + 1]
                  << "," << (int)pixels[center + 2] << "," << (int)pixels[center + 3] << "],\n";
            stats << "  \"averageRGB\":[" << (sumR / (opt.width * opt.height)) << ","
                  << (sumG / (opt.width * opt.height)) << "," << (sumB / (opt.width * opt.height)) << "]\n";
            stats << "}\n";

            const int minPixels = (opt.width * opt.height) / 32;
            if (nonBackground < minPixels)
            {
                std::cerr << "[FAIL][render] rendered image is mostly background: " << nonBackground << " pixels\n";
                exitCode = 1;
            }
            else if (orthoNonBackground < minPixels)
            {
                std::cerr << "[FAIL][render] orthographic rendered image is mostly background: " << orthoNonBackground << " pixels\n";
                exitCode = 1;
            }
            else if (spritePixels < 512)
            {
                std::cerr << "[FAIL][render] sprite did not render enough pixels: " << spritePixels << "\n";
                exitCode = 1;
            }
            else if (uiPixels < 512)
            {
                std::cerr << "[FAIL][render] UI overlay did not render enough pixels: " << uiPixels << "\n";
                exitCode = 1;
            }
            else
            {
                std::cout << "[PASS][render] ShaderRenderSmoke nonBackgroundPixels=" << nonBackground
                          << " orthographicNonBackgroundPixels=" << orthoNonBackground
                          << " spritePixels=" << spritePixels
                          << " uiPixels=" << uiPixels << " output=" << opt.outDir.string() << "\n";
            }
            renderer.DestroyRenderTarget(rt);
            renderer.DestroyPipeline(pipeline);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

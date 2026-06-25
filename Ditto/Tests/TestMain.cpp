#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/EngineLifecycle.h"
#include "../Engine/Core/JsonConfig.h"
#include "../Engine/Core/JsonSceneSchema.h"
#include "../Engine/Core/JsonValue.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/PathUtils.h"
#include "../Engine/Core/PrefabAsset.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../Engine/Core/CSharpScript.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Animation/AnimatorComponent.h"
#include "../Engine/Graphics/Camera.h"
#include "../Engine/Graphics/ParticleSystemComponent.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Graphics/Shaders/ShaderAsset.h"
#include "../Engine/Physics/Physics.h"
#include "../Engine/Physics/Physics2D.h"
#include "../Engine/Physics/PhysicsMaterial2DAsset.h"
#include "../Engine/Resources/AssetDatabase.h"
#include "../Engine/Resources/AssetPath.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct TestCase
    {
        const char* stage;
        const char* name;
        void (*fn)();
    };

    std::vector<TestCase>& Tests()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct RegisterTest
    {
        RegisterTest(const char* stage, const char* name, void (*fn)())
        {
            Tests().push_back({ stage, name, fn });
        }
    };

    struct Failure : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    void Require(bool condition, const char* expr, const char* file, int line)
    {
        if (condition) return;
        std::ostringstream oss;
        oss << file << ":" << line << " requirement failed: " << expr;
        throw Failure(oss.str());
    }

    bool NearlyEqual(float a, float b, float eps = 0.0001f)
    {
        return std::fabs(a - b) <= eps;
    }

    fs::path WriteTempCubeObj(const std::string& name)
    {
        fs::path dir = fs::temp_directory_path() / "ditto_tests_meshes";
        fs::create_directories(dir);
        fs::path path = dir / name;
        std::ofstream obj(path, std::ios::trunc);
        obj << "v -0.5 -0.5 -0.5\n";
        obj << "v 0.5 -0.5 -0.5\n";
        obj << "v 0.5 0.5 -0.5\n";
        obj << "v -0.5 0.5 -0.5\n";
        obj << "v -0.5 -0.5 0.5\n";
        obj << "v 0.5 -0.5 0.5\n";
        obj << "v 0.5 0.5 0.5\n";
        obj << "v -0.5 0.5 0.5\n";
        obj << "f 1 2 3 4\n";
        obj << "f 5 8 7 6\n";
        obj << "f 1 5 6 2\n";
        obj << "f 2 6 7 3\n";
        obj << "f 3 7 8 4\n";
        obj << "f 5 1 4 8\n";
        return path;
    }

    std::unique_ptr<Collider> MakeTestCollider(GameObject& obj, MeshData& mesh)
    {
        auto collider = std::make_unique<Collider>();
        auto* transform = obj.GetComponent<TransformComponent>();
        auto* body = obj.GetComponent<RigidbodyComponent>();
        Require(transform != nullptr, "transform != nullptr", __FILE__, __LINE__);
        Require(body != nullptr, "body != nullptr", __FILE__, __LINE__);
        transform->localDirty = true;
        transform->UpdateTransform();
        collider->objectTransform = transform;
        collider->bodyTransform = transform;
        collider->rigidbody = body;
        collider->mesh = &mesh;
        collider->biasLocalModel = glm::mat4(1.0f);
        collider->isTrigger = false;
        collider->isDirty = true;
        collider->localAABB = AABB(mesh.aabbMin, mesh.aabbMax);
        collider->UpdateWorldAABB();
        return collider;
    }

    const ScriptField* FindField(const CSharpScriptComponent& component, const std::string& name)
    {
        for (const auto& field : component.fields)
            if (field.name == name) return &field;
        return nullptr;
    }

    fs::path WriteCSharpFixture(const std::string& name)
    {
        fs::path dir = fs::temp_directory_path() / "ditto_tests_csharp" / "Assets" / "Scripts";
        fs::create_directories(dir);
        fs::path path = dir / name;
        fs::create_directories(path.parent_path());
        std::ofstream cs(path, std::ios::trunc);
        cs << "using DittoEngine;\n";
        cs << "public class ScriptProbe : MonoBehaviour\n";
        cs << "{\n";
        cs << "    public float speed = 2.5f;\n";
        cs << "    public int health = 7;\n";
        cs << "    public bool enabledFlag = true;\n";
        cs << "    public string label = \"alpha\";\n";
        cs << "    public Vector2 uv = new Vector2(1, 2);\n";
        cs << "    public Vector3 direction = new Vector3(3, 4, 5);\n";
        cs << "    public Vector4 tint = new Vector4(0.1f, 0.2f, 0.3f, 1.0f);\n";
        cs << "    [SerializeField] private float privateWeight = 9.0f;\n";
        cs << "    [HideInInspector] public float hidden = 99.0f;\n";
        cs << "    public static float staticValue = 1.0f;\n";
        cs << "    public float PropertyValue { get; set; }\n";
        cs << "    public override void Update() { Debug.Log(label + privateWeight.ToString()); }\n";
        cs << "}\n";
        return path;
    }

    fs::path WriteTwoDProjectileCapabilityFixture(const std::string& name)
    {
        fs::path dir = fs::temp_directory_path() / "ditto_tests_csharp" / "Assets" / "Scripts";
        fs::create_directories(dir);
        fs::path path = dir / name;
        fs::create_directories(path.parent_path());
        std::ofstream cs(path, std::ios::trunc);
        cs << "using DittoEngine;\n";
        cs << "public class ProjectileClientProbe : MonoBehaviour\n";
        cs << "{\n";
        cs << "    public GameObject spawned;\n";
        cs << "    public float launchPower = 12.0f;\n";
        cs << "    public override void Update()\n";
        cs << "    {\n";
        cs << "        Camera camera = Camera.main;\n";
        cs << "        Vector2 viewport = Input.gameViewportSize;\n";
        cs << "        Vector2 mouse = Input.mousePosition;\n";
        cs << "        if (camera == null || viewport.x <= 0.0f || viewport.y <= 0.0f) return;\n";
        cs << "        Vector3 world = camera.ScreenToWorldPoint(mouse, 0.0f);\n";
        cs << "        Rigidbody2D body = gameObject.GetComponent<Rigidbody2D>();\n";
        cs << "        Collider2D collider = gameObject.GetComponent<Collider2D>();\n";
        cs << "        SpriteRenderer sprite = gameObject.GetComponent<SpriteRenderer>();\n";
        cs << "        if (sprite != null) sprite.color = new Vector4(1, 1, 1, 1);\n";
        cs << "        if (body != null && collider != null && Input.GetMouseButtonUp(0))\n";
        cs << "        {\n";
        cs << "            Vector2 launch = new Vector2(transform.position.x - world.x, transform.position.y - world.y);\n";
        cs << "            body.bodyType = Rigidbody2D.BodyType.Dynamic;\n";
        cs << "            body.useGravity = true;\n";
        cs << "            body.AddForce(launch * launchPower, ForceMode2D.Impulse);\n";
        cs << "        }\n";
        cs << "        if (Input.GetButtonDown(\"Fire2\")) spawned = Object.Instantiate(gameObject);\n";
        cs << "        if (Input.GetKeyDown(KeyCode.Delete) && spawned != null) Object.Destroy(spawned);\n";
        cs << "    }\n";
        cs << "    public override void OnCollisionEnter(Collision collision)\n";
        cs << "    {\n";
        cs << "        Debug.Log(collision.depth);\n";
        cs << "    }\n";
        cs << "}\n";
        return path;
    }

    std::string EscapeJson(const std::string& value)
    {
        std::string out;
        out.reserve(value.size() + 8);
        for (char c : value)
        {
            switch (c)
            {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
            }
        }
        return out;
    }

    std::string ReadMetaGuid(const fs::path& metaPath)
    {
        std::ifstream file(metaPath);
        std::string line;
        while (std::getline(file, line))
        {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            key.erase(std::remove_if(key.begin(), key.end(),
                [](unsigned char c) { return std::isspace(c); }), key.end());
            if (key != "guid") continue;

            std::string value = line.substr(eq + 1);
            value.erase(std::remove_if(value.begin(), value.end(),
                [](unsigned char c) { return std::isspace(c) || c == '"'; }), value.end());
            return value;
        }
        return {};
    }

    const char* ComponentName(const Component* component)
    {
        if (!component) return "Unknown";
        switch (component->index)
        {
        case ComponentIndex::Transform: return "Transform";
        case ComponentIndex::Light: return "Light";
        case ComponentIndex::Renderer: return "Renderer";
        case ComponentIndex::Rigidbody: return "Rigidbody";
        case ComponentIndex::Collider: return "Collider";
        case ComponentIndex::CSharpScript: return "CSharpScript";
        default: return "Unknown";
        }
    }

    void DumpObjectJson(std::ostream& os, const GameObject* obj, int indent)
    {
        const std::string pad(indent, ' ');
        const std::string childPad(indent + 2, ' ');

        os << pad << "{\n";
        os << childPad << "\"name\":\"" << EscapeJson(obj ? obj->name : "") << "\",\n";
        os << childPad << "\"enabled\":" << (obj && obj->enabled ? "true" : "false") << ",\n";
        os << childPad << "\"components\":[";
        if (obj)
        {
            for (size_t i = 0; i < obj->components.size(); ++i)
            {
                if (i) os << ",";
                os << "\"" << ComponentName(obj->components[i].get()) << "\"";
            }
        }
        os << "],\n";
        os << childPad << "\"children\":[";
        if (obj && !obj->children.empty())
        {
            os << "\n";
            for (size_t i = 0; i < obj->children.size(); ++i)
            {
                if (i) os << ",\n";
                DumpObjectJson(os, obj->children[i].get(), indent + 4);
            }
            os << "\n" << childPad;
        }
        os << "]\n";
        os << pad << "}";
    }

    void DumpSampleSceneJson()
    {
        Scene scene;
        scene.name = "DumpSample";
        scene.rootGameObject->name = scene.name;

        auto player = std::make_unique<GameObject>("Player");
        player->AddComponent<RendererComponent>();
        player->AddComponent<RigidbodyComponent>();
        player->AddComponent<ColliderComponent>(ColliderComponent::Box);
        scene.rootGameObject->AddChild(std::move(player));

        std::cout << "{\n  \"scene\":";
        DumpObjectJson(std::cout, scene.rootGameObject.get(), 2);
        std::cout << "\n}\n";
    }

    GameObject* FindDirectChild(GameObject* parent, const std::string& name)
    {
        if (!parent) return nullptr;
        for (const auto& child : parent->children)
            if (child && child->name == name)
                return child.get();
        return nullptr;
    }

    struct ScopedConsoleLogSilence
    {
        ScopedConsoleLogSilence()
        {
            Ditto::Logger::Get().SetConsoleMinLevel(Ditto::LogLevel::None);
        }

        ~ScopedConsoleLogSilence()
        {
            Ditto::Logger::Get().SetConsoleMinLevel(Ditto::LogLevel::Info);
        }
    };
}

#define TEST_CASE(stage, name) static void name(); static RegisterTest reg_##name(stage, #name, &name); static void name()
#define REQUIRE(expr) Require(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

TEST_CASE("file", CameraProjectionUsesZeroToOneDepthRange)
{
    Camera camera(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.SetOrthographic(6.0f, 0.01f, 1000.0f);

    glm::vec4 clip = camera.GetProjectionMatrix(1.0f) * camera.GetViewMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float ndcZ = clip.z / clip.w;

    REQUIRE(ndcZ >= 0.0f);
    REQUIRE(ndcZ <= 1.0f);
}

TEST_CASE("file", OrthographicCameraScreenRayMapsViewportToWorldPlane)
{
    Camera camera(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    camera.SetOrthographic(5.0f, 0.1f, 100.0f);

    Camera::Ray center = camera.ScreenPointToRayFull(glm::vec2(400.0f, 300.0f), 800, 600);
    REQUIRE(NearlyEqual(center.origin.x, 0.0f));
    REQUIRE(NearlyEqual(center.origin.y, 0.0f));
    REQUIRE(NearlyEqual(center.origin.z, 10.0f));
    REQUIRE(NearlyEqual(center.direction.x, 0.0f));
    REQUIRE(NearlyEqual(center.direction.y, 0.0f));
    REQUIRE(NearlyEqual(center.direction.z, -1.0f));

    Camera::Ray topRight = camera.ScreenPointToRayFull(glm::vec2(800.0f, 0.0f), 800, 600);
    REQUIRE(NearlyEqual(topRight.origin.x, 5.0f * (800.0f / 600.0f), 0.0005f));
    REQUIRE(NearlyEqual(topRight.origin.y, 5.0f, 0.0005f));
    REQUIRE(NearlyEqual(topRight.direction.z, -1.0f));
}

TEST_CASE("file", GameObjectComponentsAndRemoval)
{
    GameObject obj("Actor");
    REQUIRE(obj.GetComponent<TransformComponent>() != nullptr);
    REQUIRE((obj.compMask & ComponentIndex::Transform) != 0);

    auto* renderer = obj.AddComponent<RendererComponent>();
    auto* rigidbody = obj.AddComponent<RigidbodyComponent>();
    REQUIRE(obj.GetComponent<RendererComponent>() == renderer);
    REQUIRE(obj.GetComponent<RigidbodyComponent>() == rigidbody);
    REQUIRE((obj.compMask & ComponentIndex::Renderer) != 0);
    REQUIRE((obj.compMask & ComponentIndex::Rigidbody) != 0);

    obj.RemoveComponent(renderer);
    obj.ProcessRemovals();
    REQUIRE(obj.GetComponent<RendererComponent>() == nullptr);
    REQUIRE((obj.compMask & ComponentIndex::Renderer) == 0);
    REQUIRE(obj.GetComponent<RigidbodyComponent>() == rigidbody);
}

TEST_CASE("file", GameObjectReparentAndCycleGuard)
{
    GameObject root("Root");
    GameObject* parent = root.AddChild(std::make_unique<GameObject>("Parent"));
    GameObject* child = parent->AddChild(std::make_unique<GameObject>("Child"));

    REQUIRE(parent != nullptr);
    REQUIRE(child != nullptr);
    REQUIRE(parent->parent == &root);
    REQUIRE(child->parent == parent);

    root.AddChild(child);
    REQUIRE(child->parent == &root);
    REQUIRE(parent->children.empty());
    REQUIRE(root.children.size() == 2);

    child->AddChild(&root);
    REQUIRE(root.parent == nullptr);
    REQUIRE(child->children.empty());
}

TEST_CASE("file", SceneSnapshotRoundTrip)
{
    Scene scene;
    scene.name = "RoundTrip";
    scene.rootGameObject->name = scene.name;

    auto actor = std::make_unique<GameObject>("Actor");
    auto* transform = actor->GetComponent<TransformComponent>();
    transform->position = { 1.0f, 2.0f, 3.0f };
    transform->rotation = { 10.0f, 20.0f, 30.0f };
    transform->scale = { 2.0f, 3.0f, 4.0f };
    transform->localDirty = true;
    transform->UpdateTransform();

    auto* renderer = actor->AddComponent<RendererComponent>();
    renderer->color = { 0.25f, 0.5f, 0.75f, 1.0f };
    renderer->meshPath = "Models/Custom.obj";
    renderer->materialPath = "Materials/Test.mat";
    renderer->mainTexturePath = "Textures/Diffuse.png";

    auto* body = actor->AddComponent<RigidbodyComponent>();
    body->type = RigidbodyComponent::Kinematic;
    body->mass = 3.5f;
    body->useGravity = false;

    auto* collider = actor->AddComponent<ColliderComponent>(ColliderComponent::MeshConvex);
    collider->isTrigger = true;
    collider->biasPosition = { 0.1f, 0.2f, 0.3f };
    collider->meshPath = "Models/Collider.obj";

    scene.rootGameObject->AddChild(std::move(actor));

    const std::string snapshot = scene.CaptureSnapshot();
    REQUIRE(!snapshot.empty());

    Scene loaded;
    REQUIRE(loaded.RestoreSnapshot(snapshot));
    REQUIRE(loaded.name == "RoundTrip");
    REQUIRE(loaded.rootGameObject->children.size() >= 2);
    REQUIRE(FindDirectChild(loaded.rootGameObject.get(), "Main Camera") != nullptr);
    REQUIRE(FindDirectChild(loaded.rootGameObject.get(), "Main Camera")->GetComponent<CameraComponent>() != nullptr);

    GameObject* loadedActor = FindDirectChild(loaded.rootGameObject.get(), "Actor");
    REQUIRE(loadedActor != nullptr);
    REQUIRE(loadedActor->name == "Actor");

    auto* loadedTransform = loadedActor->GetComponent<TransformComponent>();
    auto* loadedRenderer = loadedActor->GetComponent<RendererComponent>();
    auto* loadedBody = loadedActor->GetComponent<RigidbodyComponent>();
    auto* loadedCollider = loadedActor->GetComponent<ColliderComponent>();
    REQUIRE(loadedTransform != nullptr);
    REQUIRE(loadedRenderer != nullptr);
    REQUIRE(loadedBody != nullptr);
    REQUIRE(loadedCollider != nullptr);

    REQUIRE(NearlyEqual(loadedTransform->position.x, 1.0f));
    REQUIRE(NearlyEqual(loadedTransform->scale.z, 4.0f));
    REQUIRE(loadedRenderer->meshPath == "Models/Custom.obj");
    REQUIRE(loadedRenderer->materialPath == "Materials/Test.mat");
    REQUIRE(loadedRenderer->mainTexturePath == "Textures/Diffuse.png");
    REQUIRE(loadedBody->type == RigidbodyComponent::Kinematic);
    REQUIRE(!loadedBody->useGravity);
    REQUIRE(loadedCollider->type == ColliderComponent::MeshConvex);
    REQUIRE(loadedCollider->isTrigger);
    REQUIRE(loadedCollider->meshPath == "Models/Collider.obj");
}

TEST_CASE("file", MaterialAssetRoundTrip)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_materials";
    fs::create_directories(base);
    fs::path materialPath = base / "Blue.mat";

    Ditto::MaterialAsset material = Ditto::MakeDefaultMaterial("Blue");
    material.shaderName = "Lit_Toon";
    material.color = { 0.1f, 0.25f, 0.8f, 1.0f };
    material.mainTexturePath = "Textures/Blue.png";

    REQUIRE(Ditto::SaveMaterialAsset(material, materialPath));
    Ditto::MaterialAsset loaded = Ditto::LoadMaterialAsset(materialPath.string());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.shaderName == "Lit_Toon");
    REQUIRE(loaded.mainTexturePath == "Textures/Blue.png");
    REQUIRE(NearlyEqual(loaded.color.r, 0.1f));
    REQUIRE(NearlyEqual(loaded.color.g, 0.25f));
    REQUIRE(NearlyEqual(loaded.color.b, 0.8f));
    REQUIRE(NearlyEqual(loaded.color.a, 1.0f));
}

TEST_CASE("file", PhysicsMaterial2DAssetRoundTrip)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_physics_materials_2d";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path materialPath = base / "Bouncy.physmat2d";

    Ditto::PhysicsMaterial2DAsset material = Ditto::MakeDefaultPhysicsMaterial2D("Bouncy");
    material.friction = 0.15f;
    material.restitution = 0.9f;

    REQUIRE(Ditto::SavePhysicsMaterial2DAsset(material, materialPath));
    Ditto::PhysicsMaterial2DAsset loaded = Ditto::LoadPhysicsMaterial2DAsset(materialPath.string());
    REQUIRE(loaded.ok);
    REQUIRE(NearlyEqual(loaded.friction, 0.15f));
    REQUIRE(NearlyEqual(loaded.restitution, 0.9f));

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", MaterialAssetSaveNormalizesTextureReference)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_material_asset_refs";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("MaterialRefs"));

    fs::path projectPath = projectsRoot / "MaterialRefs";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path texturePath = projectPath / "Assets" / "Textures" / "Albedo.png";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "UsesTexture.mat";
    fs::create_directories(texturePath.parent_path());
    fs::create_directories(materialPath.parent_path());
    std::ofstream(texturePath, std::ios::binary) << "png";

    Ditto::MaterialAsset material = Ditto::MakeDefaultMaterial("UsesTexture");
    material.shaderName = "Shaders/Lit_Toon.shader";
    material.mainTexturePath = texturePath.string();

    REQUIRE(Ditto::SaveMaterialAsset(material, materialPath));
    Ditto::MaterialAsset loaded = Ditto::LoadMaterialAsset(materialPath.string());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.mainTexturePath == "Textures/Albedo.png");

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", MaterialAssetTextureReferenceSurvivesRenameViaGuid)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_material_asset_refs";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("MaterialGuidRefs"));

    fs::path projectPath = projectsRoot / "MaterialGuidRefs";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path texturePath = projectPath / "Assets" / "Textures" / "Original.png";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "UsesTexture.mat";
    fs::create_directories(texturePath.parent_path());
    fs::create_directories(materialPath.parent_path());
    std::ofstream(texturePath, std::ios::binary) << "png";
    std::string guid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(texturePath);
    REQUIRE(!guid.empty());

    Ditto::MaterialAsset material = Ditto::MakeDefaultMaterial("UsesTexture");
    material.shaderName = "Shaders/Lit_Toon.shader";
    material.mainTexturePath = texturePath.string();
    REQUIRE(Ditto::SaveMaterialAsset(material, materialPath));

    fs::path movedPath = projectPath / "Assets" / "Textures" / "Renamed.png";
    fs::rename(texturePath, movedPath);
    fs::rename(texturePath.string() + ".meta", movedPath.string() + ".meta");
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);

    Ditto::MaterialAsset loaded = Ditto::LoadMaterialAsset(materialPath.string());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.mainTexturePath == "Textures/Renamed.png");

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", ProjectDefaultsIncludeLitToonMaterial)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_project_defaults";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("Defaults"));

    fs::path projectPath = projectsRoot / "Defaults";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "Lit_Toon.mat";
    fs::path physicsMaterialPath = projectPath / "Assets" / "PhysicsMaterials2D" / "Default.physmat2d";
    REQUIRE(fs::exists(materialPath));
    REQUIRE(fs::exists(physicsMaterialPath));

    Ditto::MaterialAsset material = Ditto::LoadMaterialAsset(materialPath.string());
    REQUIRE(material.ok);
    REQUIRE(material.shaderName == "Shaders/Lit_Toon.shader");
    REQUIRE(NearlyEqual(material.color.r, 1.0f));
    REQUIRE(NearlyEqual(material.color.g, 1.0f));
    REQUIRE(NearlyEqual(material.color.b, 1.0f));
    REQUIRE(pm.OpenProject(projectPath.string()));
    REQUIRE(fs::exists(materialPath));
    REQUIRE(fs::exists(physicsMaterialPath));
    pm.CloseProject();
}

TEST_CASE("file", OpenProjectRestoresDefaultSpriteAssets)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_project_defaults";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("SpriteDefaults"));

    fs::path projectPath = projectsRoot / "SpriteDefaults";
    fs::path squarePath = projectPath / "Assets" / "Sprites" / "Square.png";
    fs::path circlePath = projectPath / "Assets" / "Sprites" / "Circle.png";
    REQUIRE(fs::exists(squarePath));
    REQUIRE(fs::exists(circlePath));

    fs::remove(squarePath);
    fs::remove(circlePath);
    REQUIRE(!fs::exists(squarePath));
    REQUIRE(!fs::exists(circlePath));

    REQUIRE(pm.OpenProject(projectPath.string()));
    REQUIRE(fs::exists(squarePath));
    REQUIRE(fs::exists(circlePath));

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", OpenProjectRestoresDefaultPhysicsMaterial2D)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_project_defaults";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("PhysicsMaterialDefaults"));

    fs::path projectPath = projectsRoot / "PhysicsMaterialDefaults";
    fs::path materialPath = projectPath / "Assets" / "PhysicsMaterials2D" / "Default.physmat2d";
    REQUIRE(fs::exists(materialPath));

    fs::remove(materialPath);
    REQUIRE(!fs::exists(materialPath));

    REQUIRE(pm.OpenProject(projectPath.string()));
    REQUIRE(fs::exists(materialPath));
    Ditto::PhysicsMaterial2DAsset material = Ditto::LoadPhysicsMaterial2DAsset(materialPath.string());
    REQUIRE(material.ok);
    REQUIRE(NearlyEqual(material.friction, 0.6f));
    REQUIRE(NearlyEqual(material.restitution, 0.2f));

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", SceneSaveLoadAndCorruptFileHandling)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "ditto_tests_scene_files";
    fs::create_directories(base);
    fs::path scenePath = base / "scene.scene";
    fs::path corruptPath = base / "corrupt.scene";

    Scene scene;
    scene.name = "FileRoundTrip";
    scene.rootGameObject->name = scene.name;
    auto obj = std::make_unique<GameObject>("DiskActor");
    obj->AddComponent<RendererComponent>();
    scene.rootGameObject->AddChild(std::move(obj));

    REQUIRE(scene.SaveScene(scenePath.string()));
    REQUIRE(fs::exists(scenePath));
    REQUIRE(fs::file_size(scenePath) > 0);
    {
        std::ifstream saved(scenePath);
        std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        REQUIRE(text.find("\"format\": \"DittoScene\"") != std::string::npos);
    }

    Scene loaded;
    REQUIRE(loaded.LoadScene(scenePath.string()));
    REQUIRE(loaded.name == "FileRoundTrip");
    REQUIRE(loaded.rootGameObject->children.size() >= 2);
    REQUIRE(FindDirectChild(loaded.rootGameObject.get(), "Main Camera") != nullptr);
    REQUIRE(FindDirectChild(loaded.rootGameObject.get(), "Main Camera")->GetComponent<CameraComponent>() != nullptr);
    GameObject* diskActor = FindDirectChild(loaded.rootGameObject.get(), "DiskActor");
    REQUIRE(diskActor != nullptr);
    REQUIRE(diskActor->GetComponent<RendererComponent>() != nullptr);

    {
        std::ofstream corrupt(corruptPath, std::ios::binary | std::ios::trunc);
        corrupt << "not a ditto scene";
    }
    Scene failed;
    {
        ScopedConsoleLogSilence silence;
        REQUIRE(!failed.LoadScene(corruptPath.string()));
    }

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", SceneRejectsOlderDevelopmentVersion)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_scene_version";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path scenePath = base / "scene.scene";

    Scene scene;
    scene.name = "VersionProbe";
    scene.rootGameObject->name = scene.name;
    REQUIRE(scene.SaveScene(scenePath.string()));

    {
        std::ifstream in(scenePath);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string current = "\"version\": 17";
        const size_t versionPos = text.find(current);
        REQUIRE(versionPos != std::string::npos);
        text.replace(versionPos, current.size(), "\"version\": 16");

        std::ofstream out(scenePath, std::ios::trunc);
        out << text;
    }

    Scene loaded;
    {
        ScopedConsoleLogSilence silence;
        REQUIRE(!loaded.LoadScene(scenePath.string()));
    }

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", PrefabAssetSavesLoadsAndInstantiatesGameObjectTrees)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_prefab_asset";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path prefabPath = base / "Actor.prefab";

    GameObject actor("Actor");
    actor.enabled = false;
    auto* renderer = actor.AddComponent<RendererComponent>();
    renderer->meshPath = "Meshes/Actor.obj";
    renderer->materialPath = "Materials/Actor.mat";

    auto child = std::make_unique<GameObject>("Child");
    child->AddComponent<LightComponent>()->intensity = 3.5f;
    actor.AddChild(std::move(child));

    REQUIRE(Ditto::PrefabAsset::Save(actor, prefabPath));
    REQUIRE(fs::exists(prefabPath));
    {
        std::ifstream saved(prefabPath);
        std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        REQUIRE(text.find("\"format\": \"DittoPrefab\"") != std::string::npos);
    }

    std::unique_ptr<GameObject> loaded = Ditto::PrefabAsset::Load(prefabPath);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->name == "Actor");
    REQUIRE(!loaded->enabled);
    REQUIRE(loaded->parent == nullptr);
    REQUIRE(loaded->children.size() == 1);
    REQUIRE(loaded->children[0]->name == "Child");
    REQUIRE(loaded->children[0]->parent == loaded.get());
    REQUIRE(loaded->GetComponent<RendererComponent>() != nullptr);
    REQUIRE(loaded->GetComponent<RendererComponent>()->meshPath == "Meshes/Actor.obj");
    REQUIRE(loaded->children[0]->GetComponent<LightComponent>() != nullptr);
    REQUIRE(std::abs(loaded->children[0]->GetComponent<LightComponent>()->intensity - 3.5f) < 0.0001f);

    std::unique_ptr<GameObject> instance = Ditto::PrefabAsset::Instantiate(prefabPath);
    REQUIRE(instance != nullptr);
    REQUIRE(instance->name == "Actor");
    REQUIRE(instance.get() != loaded.get());
    REQUIRE(!instance->prefabSourcePath.empty());
    REQUIRE(!instance->prefabSourceGuid.empty());
    REQUIRE(instance->children.size() == 1);
    REQUIRE(instance->children[0].get() != loaded->children[0].get());

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", PrefabAssetApplyAndRevertRoundTripInstanceState)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_prefab_apply_revert";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path prefabPath = base / "Actor.prefab";

    GameObject source("Actor");
    source.AddComponent<RendererComponent>()->meshPath = "Meshes/Original.obj";
    REQUIRE(Ditto::PrefabAsset::Save(source, prefabPath));
    Ditto::AssetDatabase::Get().EnsureMetaForAsset(prefabPath);

    std::unique_ptr<GameObject> instance = Ditto::PrefabAsset::Instantiate(prefabPath);
    REQUIRE(instance != nullptr);
    REQUIRE(instance->GetComponent<RendererComponent>() != nullptr);
    instance->GetComponent<RendererComponent>()->meshPath = "Meshes/Applied.obj";
    instance->AddChild(std::make_unique<GameObject>("AppliedChild"));

    REQUIRE(Ditto::PrefabAsset::Apply(*instance));

    std::unique_ptr<GameObject> applied = Ditto::PrefabAsset::Load(prefabPath);
    REQUIRE(applied != nullptr);
    REQUIRE(applied->GetComponent<RendererComponent>() != nullptr);
    REQUIRE(applied->GetComponent<RendererComponent>()->meshPath == "Meshes/Applied.obj");
    REQUIRE(applied->children.size() == 1);
    REQUIRE(applied->children[0]->name == "AppliedChild");
    REQUIRE(applied->prefabSourcePath.empty());

    instance->name = "DirtyInstance";
    instance->GetComponent<RendererComponent>()->meshPath = "Meshes/Dirty.obj";
    instance->children.clear();
    REQUIRE(Ditto::PrefabAsset::Revert(*instance));
    REQUIRE(instance->name == "Actor");
    REQUIRE(instance->GetComponent<RendererComponent>() != nullptr);
    REQUIRE(instance->GetComponent<RendererComponent>()->meshPath == "Meshes/Applied.obj");
    REQUIRE(instance->children.size() == 1);
    REQUIRE(instance->children[0]->parent == instance.get());
    REQUIRE(!instance->prefabSourcePath.empty());

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", PrefabAssetCollectsFieldLevelOverrideSummary)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_prefab_overrides";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path prefabPath = base / "Actor.prefab";

    GameObject source("Actor");
    source.AddComponent<RendererComponent>()->meshPath = "Meshes/Base.obj";
    REQUIRE(Ditto::PrefabAsset::Save(source, prefabPath));
    Ditto::AssetDatabase::Get().EnsureMetaForAsset(prefabPath);

    std::unique_ptr<GameObject> instance = Ditto::PrefabAsset::Instantiate(prefabPath);
    REQUIRE(instance != nullptr);
    instance->name = "ActorInstance";
    instance->GetComponent<RendererComponent>()->meshPath = "Meshes/Override.obj";
    instance->AddChild(std::make_unique<GameObject>("ExtraChild"));

    std::vector<Ditto::PrefabAsset::Override> overrides = Ditto::PrefabAsset::CollectOverrides(*instance);
    REQUIRE(!overrides.empty());
    REQUIRE(std::find_if(overrides.begin(), overrides.end(), [](const auto& entry) {
        return entry.path == "root.name" && entry.kind == "changed";
    }) != overrides.end());
    REQUIRE(std::find_if(overrides.begin(), overrides.end(), [](const auto& entry) {
        return entry.path.find("meshPath") != std::string::npos && entry.kind == "changed";
    }) != overrides.end());
    REQUIRE(std::find_if(overrides.begin(), overrides.end(), [](const auto& entry) {
        return entry.path.find("children") != std::string::npos && entry.kind == "added";
    }) != overrides.end());

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", SceneRegisterSubtreeTracksPrefabInstances)
{
    Scene scene;
    auto prefab = std::make_unique<GameObject>("PrefabInstance");
    prefab->AddChild(std::make_unique<GameObject>("Nested"));

    GameObject* instance = scene.rootGameObject->AddChild(std::move(prefab));
    scene.RegisterSubtree(instance);

    REQUIRE(std::find(scene.gameObjects.begin(), scene.gameObjects.end(), instance) != scene.gameObjects.end());
    REQUIRE(std::find(scene.gameObjects.begin(), scene.gameObjects.end(), instance->children[0].get()) != scene.gameObjects.end());

    scene.UnregisterSubtree(instance);
    REQUIRE(std::find(scene.gameObjects.begin(), scene.gameObjects.end(), instance) == scene.gameObjects.end());
    REQUIRE(std::find(scene.gameObjects.begin(), scene.gameObjects.end(), instance->children[0].get()) == scene.gameObjects.end());
}

TEST_CASE("file", PathUtilsFindAncestorContaining)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "ditto_tests_pathutils";
    fs::path nested = base / "A" / "B" / "C";
    fs::create_directories(nested);
    fs::create_directories(base / "Assets");

    fs::path found = PathUtils::FindAncestorContaining(nested, "Assets");
    REQUIRE(found.lexically_normal() == base.lexically_normal());

    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("file", AssetPathResolvesProjectRelativeAndTypedAssets)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_assetpath";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("AssetPathProject"));

    fs::path projectPath = projectsRoot / "AssetPathProject";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path texturePath = projectPath / "Assets" / "Textures" / "Probe.png";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "Probe.mat";
    fs::path physicsMaterialPath = projectPath / "Assets" / "PhysicsMaterials2D" / "Probe.physmat2d";
    fs::path shaderPath = projectPath / "Assets" / "Shaders" / "ProbeShader.shader";
    fs::path preferredTexturePath = projectsRoot / "PreferredRoot" / "Assets" / "Textures" / "Preferred.png";
    fs::create_directories(texturePath.parent_path());
    fs::create_directories(materialPath.parent_path());
    fs::create_directories(physicsMaterialPath.parent_path());
    fs::create_directories(shaderPath.parent_path());
    fs::create_directories(preferredTexturePath.parent_path());
    std::ofstream(texturePath, std::ios::binary) << "png";
    std::ofstream(materialPath, std::ios::binary) << "mat";
    std::ofstream(physicsMaterialPath, std::ios::binary) << "physmat2d";
    std::ofstream(shaderPath, std::ios::binary) << "Shader \"Probe\" { HLSLPROGRAM ENDHLSL }";
    std::ofstream(preferredTexturePath, std::ios::binary) << "png";

    REQUIRE(Ditto::AssetPath::NormalizeAssetKey("Assets\\Textures\\Probe.png") == "Textures/Probe.png");
    REQUIRE(Ditto::AssetPath::NormalizeAssetKey(texturePath.string()) == "Textures/Probe.png");
    REQUIRE(Ditto::AssetPath::ToProjectRelativeAssetPath(texturePath) == "Textures/Probe.png");
    REQUIRE(Ditto::AssetPath::ResolveAssetPath("Textures/Probe.png") == texturePath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveAssetPath("Assets/Textures/Probe.png") == texturePath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveAssetPath(texturePath.string()) == texturePath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveTypedAssetPath("Probe", "Materials", ".mat") == materialPath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveTypedAssetPath("Materials/Probe.mat", "Materials") == materialPath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveTypedAssetPath("Probe", "PhysicsMaterials2D", ".physmat2d") == physicsMaterialPath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveAssetPath("Textures/Preferred.png", projectsRoot / "PreferredRoot") == preferredTexturePath.lexically_normal());
    REQUIRE(Ditto::ResolveMaterialPath("Probe") == materialPath.lexically_normal());
    REQUIRE(Ditto::ResolvePhysicsMaterial2DPath("Probe") == physicsMaterialPath.lexically_normal());
    REQUIRE(Ditto::ResolveShaderPath("ProbeShader") == shaderPath.lexically_normal());

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", AssetDatabaseCreatesMetaAndResolvesGuidReferences)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_asset_database";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("AssetDatabaseProject"));

    fs::path projectPath = projectsRoot / "AssetDatabaseProject";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path texturePath = projectPath / "Assets" / "Textures" / "GuidProbe.png";
    fs::create_directories(texturePath.parent_path());
    std::ofstream(texturePath, std::ios::binary) << "png";

    std::string guid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(texturePath);
    REQUIRE(!guid.empty());
    REQUIRE(fs::exists(texturePath.string() + ".meta"));
    REQUIRE(ReadMetaGuid(texturePath.string() + ".meta") == guid);
    REQUIRE(Ditto::AssetDatabase::Get().PathForGuid(guid) == texturePath.lexically_normal());
    REQUIRE(Ditto::AssetPath::ResolveAssetPath("guid:" + guid) == texturePath.lexically_normal());
    const Ditto::AssetRecord* record = Ditto::AssetDatabase::Get().RecordForGuid(guid);
    REQUIRE(record != nullptr);
    REQUIRE(record->relativePath == "Textures/GuidProbe.png");
    REQUIRE(record->extension == ".png");
    REQUIRE(record->importerType == "Texture");
    REQUIRE(record->sizeBytes == 3);
    REQUIRE(!record->contentHash.empty());
    REQUIRE(!record->imported);
    REQUIRE(std::find(record->artifactPaths.begin(), record->artifactPaths.end(),
        ".ditto/artifacts/" + guid + ".artifact") != record->artifactPaths.end());
    REQUIRE(record->dependencies.empty());
    REQUIRE(Ditto::AssetDatabase::Get().NeedsReimport(guid));
    REQUIRE(Ditto::AssetDatabase::Get().AssetsNeedingImport().size() >= 1);
    REQUIRE(Ditto::AssetDatabase::Get().ImportAsset(guid));
    REQUIRE(fs::exists(projectPath / ".ditto" / "artifacts" / (guid + ".artifact")));
    record = Ditto::AssetDatabase::Get().RecordForGuid(guid);
    REQUIRE(record != nullptr);
    REQUIRE(record->imported);
    REQUIRE(!Ditto::AssetDatabase::Get().NeedsReimport(guid));
    REQUIRE(fs::exists(projectPath / ".ditto" / "import-cache.txt"));
    std::vector<Ditto::AssetRecord> cachedRecords;
    REQUIRE(Ditto::AssetDatabase::Get().LoadImportCache(cachedRecords));
    REQUIRE(std::find_if(cachedRecords.begin(), cachedRecords.end(), [&](const Ditto::AssetRecord& cached) {
        return cached.guid == guid && cached.relativePath == "Textures/GuidProbe.png"
            && cached.sizeBytes == 3 && !cached.contentHash.empty() && cached.imported
            && cached.importerType == "Texture"
            && std::find(cached.artifactPaths.begin(), cached.artifactPaths.end(),
                ".ditto/artifacts/" + guid + ".artifact") != cached.artifactPaths.end()
            && cached.dependencies.empty();
    }) != cachedRecords.end());

    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    REQUIRE(!Ditto::AssetDatabase::Get().NeedsReimport(guid));
    std::ofstream(texturePath, std::ios::binary | std::ios::app) << "changed";
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    REQUIRE(Ditto::AssetDatabase::Get().NeedsReimport(guid));
    REQUIRE(Ditto::AssetDatabase::Get().ImportAsset(guid));
    REQUIRE(!Ditto::AssetDatabase::Get().NeedsReimport(guid));

    fs::path movedPath = projectPath / "Assets" / "Textures" / "MovedProbe.png";
    fs::rename(texturePath, movedPath);
    fs::rename(texturePath.string() + ".meta", movedPath.string() + ".meta");
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    REQUIRE(Ditto::AssetDatabase::Get().RelativePathForGuid(guid) == "Textures/MovedProbe.png");
    REQUIRE(Ditto::AssetPath::ResolveAssetPath("guid:" + guid) == movedPath.lexically_normal());
    REQUIRE(ReadMetaGuid(movedPath.string() + ".meta") == guid);
    {
        std::ifstream meta(movedPath.string() + ".meta");
        std::string text((std::istreambuf_iterator<char>(meta)), std::istreambuf_iterator<char>());
        REQUIRE(text.find("asset = \"Textures/MovedProbe.png\"") != std::string::npos);
    }

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", AssetDatabaseDependenciesUseGuidGraph)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_asset_dependencies";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("AssetDependencyProject"));

    fs::path projectPath = projectsRoot / "AssetDependencyProject";
    REQUIRE(pm.OpenProject(projectPath.string()));

    fs::path shaderPath = projectPath / "Assets" / "Shaders" / "Dependency.shader";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "Dependent.mat";
    fs::create_directories(shaderPath.parent_path());
    fs::create_directories(materialPath.parent_path());
    std::ofstream(shaderPath, std::ios::binary) << "shader";
    std::ofstream(materialPath, std::ios::binary) << "material";

    std::string shaderGuid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(shaderPath);
    std::string materialGuid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(materialPath);
    REQUIRE(!shaderGuid.empty());
    REQUIRE(!materialGuid.empty());

    std::vector<Ditto::AssetRecord> records = Ditto::AssetDatabase::Get().Records();
    for (Ditto::AssetRecord& record : records)
    {
        if (record.guid == materialGuid)
            record.dependencies = { shaderGuid };
        REQUIRE(std::find(record.dependencies.begin(), record.dependencies.end(),
            "Shaders/Dependency.shader") == record.dependencies.end());
    }

    {
        std::ofstream cache(projectPath / ".ditto" / "import-cache.txt", std::ios::trunc);
        cache << "DittoImportCache 4\n";
        for (const Ditto::AssetRecord& record : records)
        {
            cache << record.guid << "\t"
                  << record.relativePath << "\t"
                  << record.extension << "\t"
                  << record.sizeBytes << "\t"
                  << record.contentHash << "\t"
                  << (record.imported ? "1" : "0") << "\t"
                  << ".ditto/artifacts/" << record.guid << ".artifact\t";
            for (size_t i = 0; i < record.dependencies.size(); ++i)
            {
                if (i) cache << ";";
                cache << record.dependencies[i];
            }
            cache << "\t" << record.importerType << "\t\n";
        }
    }

    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    std::vector<std::string> dependents = Ditto::AssetDatabase::Get().GetDependents(shaderGuid);
    REQUIRE(std::find(dependents.begin(), dependents.end(), materialGuid) != dependents.end());

    std::vector<std::string> allDeps = Ditto::AssetDatabase::Get().GetAllDependencies(materialGuid);
    REQUIRE(std::find(allDeps.begin(), allDeps.end(), shaderGuid) != allDeps.end());

    Ditto::AssetDatabase::Get().MarkDependentsForReimport(shaderGuid);
    std::vector<Ditto::AssetRecord> cachedRecords;
    REQUIRE(Ditto::AssetDatabase::Get().LoadImportCache(cachedRecords));
    REQUIRE(std::find_if(cachedRecords.begin(), cachedRecords.end(), [&](const Ditto::AssetRecord& cached) {
        return cached.guid == materialGuid && !cached.imported
            && std::find(cached.dependencies.begin(), cached.dependencies.end(), shaderGuid) != cached.dependencies.end();
    }) != cachedRecords.end());

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", JsonConfigRoundTripsProjectAndGameConfig)
{
    fs::path dir = fs::temp_directory_path() / "ditto_tests_json_config";
    fs::remove_all(dir);
    fs::create_directories(dir);

    ProjectConfig project;
    project.name = "Config Project";
    project.version = "1.2";
    project.engineVersion = "3.4";
    project.lastScene = "Assets/Scenes/A.bin";
    fs::path projectConfigPath = dir / "project.json";
    REQUIRE(Ditto::JsonConfig::WriteProjectConfig(projectConfigPath, project));

    ProjectConfig loadedProject;
    REQUIRE(Ditto::JsonConfig::ReadProjectConfig(projectConfigPath, loadedProject));
    REQUIRE(loadedProject.name == "Config Project");
    REQUIRE(loadedProject.version == "1.2");
    REQUIRE(loadedProject.engineVersion == "3.4");
    REQUIRE(loadedProject.lastScene == "Assets/Scenes/A.bin");
    REQUIRE(Ditto::JsonConfig::UpdateProjectLastScene(projectConfigPath, "Assets/Scenes/B.bin"));
    REQUIRE(Ditto::JsonConfig::ReadProjectConfig(projectConfigPath, loadedProject));
    REQUIRE(loadedProject.lastScene == "Assets/Scenes/B.bin");

    GameConfig game;
    game.productName = "Ditto Test";
    game.companyName = "Ditto";
    game.version = "5.6";
    game.startupScene = "Default";
    game.scenes = { "Default", "Arena" };
    game.developmentBuild = true;
    game.enableScriptDebugging = true;
    fs::path gameConfigPath = dir / "game.config";
    REQUIRE(Ditto::JsonConfig::WriteGameConfig(gameConfigPath, game));

    GameConfig loadedGame;
    REQUIRE(Ditto::JsonConfig::ReadGameConfig(gameConfigPath, loadedGame));
    REQUIRE(loadedGame.productName == "Ditto Test");
    REQUIRE(loadedGame.startupScene == "Default");
    REQUIRE(loadedGame.scenes.size() == 2);
    REQUIRE(loadedGame.scenes[0] == "Default");
    REQUIRE(loadedGame.scenes[1] == "Arena");
    REQUIRE(loadedGame.developmentBuild);
    REQUIRE(loadedGame.enableScriptDebugging);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("file", JsonSceneSchemaRejectsInvalidGameObjectData)
{
    const char* sceneText =
        "{"
        "\"format\":\"DittoScene\","
        "\"version\":17,"
        "\"name\":\"Bad\","
        "\"root\":{"
        "\"name\":\"Bad\","
        "\"enabled\":true,"
        "\"components\":[{\"type\":\"MissingComponent\"}],"
        "\"children\":[]"
        "}"
        "}";

    Ditto::Json::Value sceneJson;
    REQUIRE(Ditto::Json::Parse(sceneText, sceneJson));
    Ditto::JsonSceneSchema::ValidationResult result =
        Ditto::JsonSceneSchema::ValidateScene(sceneJson, 17);
    REQUIRE(!result.ok);
    REQUIRE(result.Summary().find("MissingComponent") != std::string::npos);

    Scene scene;
    std::istringstream input(sceneText);
    ScopedConsoleLogSilence silence;
    REQUIRE(!scene.ReadFromStream(input));
}

TEST_CASE("file", RuntimeContextTracksCurrentSceneAndLoadingVersion)
{
    REQUIRE(Ditto::RuntimeContext::SceneLoadingVersion() == 0);
    {
        Ditto::RuntimeContext::SceneLoadingVersionScope scope(42);
        REQUIRE(Ditto::RuntimeContext::SceneLoadingVersion() == 42);
    }
    REQUIRE(Ditto::RuntimeContext::SceneLoadingVersion() == 0);

    Scene* previous = Ditto::RuntimeContext::CurrentScene();
    {
        Scene scene;
        REQUIRE(Ditto::RuntimeContext::CurrentScene() == &scene);
    }
    REQUIRE(Ditto::RuntimeContext::CurrentScene() == nullptr);
    if (previous)
        Ditto::RuntimeContext::SetCurrentScene(previous);
}

TEST_CASE("file", AssetDatabaseReportsMetaAndGuidDiagnostics)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_asset_database_diagnostics";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("DiagnosticsProject"));

    fs::path projectPath = projectsRoot / "DiagnosticsProject";
    REQUIRE(pm.OpenProject(projectPath.string()));

    fs::path missingMetaPath = projectPath / "Assets" / "Textures" / "NoMeta.png";
    fs::path badMetaPath = projectPath / "Assets" / "Textures" / "BadMeta.png";
    fs::path firstDupPath = projectPath / "Assets" / "Textures" / "DupA.png";
    fs::path secondDupPath = projectPath / "Assets" / "Textures" / "DupB.png";
    fs::create_directories(missingMetaPath.parent_path());
    std::ofstream(missingMetaPath, std::ios::binary) << "png";
    std::ofstream(badMetaPath, std::ios::binary) << "png";
    std::ofstream(firstDupPath, std::ios::binary) << "png";
    std::ofstream(secondDupPath, std::ios::binary) << "png";
    std::ofstream(badMetaPath.string() + ".meta", std::ios::trunc)
        << "DittoMeta 1\n"
        << "guid = \"not-a-valid-guid\"\n"
        << "asset = \"Textures/BadMeta.png\"\n";

    const std::string duplicateGuid = "11111111111111111111111111111111";
    std::ofstream(firstDupPath.string() + ".meta", std::ios::trunc)
        << "DittoMeta 1\n"
        << "guid = \"" << duplicateGuid << "\"\n"
        << "asset = \"Textures/DupA.png\"\n";
    std::ofstream(secondDupPath.string() + ".meta", std::ios::trunc)
        << "DittoMeta 1\n"
        << "guid = \"" << duplicateGuid << "\"\n"
        << "asset = \"Textures/DupB.png\"\n";

    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    const auto& diagnostics = Ditto::AssetDatabase::Get().Diagnostics();
    REQUIRE(std::find(diagnostics.missingMeta.begin(), diagnostics.missingMeta.end(), "Textures/NoMeta.png") != diagnostics.missingMeta.end());
    REQUIRE(std::find(diagnostics.invalidMeta.begin(), diagnostics.invalidMeta.end(), "Textures/BadMeta.png") != diagnostics.invalidMeta.end());
    REQUIRE(std::find(diagnostics.duplicateGuid.begin(), diagnostics.duplicateGuid.end(), duplicateGuid) != diagnostics.duplicateGuid.end());

    auto missing = Ditto::AssetDatabase::Get().ValidateReferences({ "guid:22222222222222222222222222222222" });
    REQUIRE(missing.size() == 1);
    REQUIRE(missing[0] == "22222222222222222222222222222222");
    REQUIRE(!Ditto::AssetDatabase::Get().Diagnostics().missingGuidReference.empty());

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", SceneAssetReferencesSurviveAssetRenameViaGuid)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_asset_database_scene";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("SceneGuidProject"));

    fs::path projectPath = projectsRoot / "SceneGuidProject";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path spritePath = projectPath / "Assets" / "Sprites" / "Original.png";
    fs::create_directories(spritePath.parent_path());
    std::ofstream(spritePath, std::ios::binary) << "png";
    std::string guid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(spritePath);
    REQUIRE(!guid.empty());

    Scene scene;
    scene.name = "GuidScene";
    scene.rootGameObject->name = scene.name;
    auto obj = std::make_unique<GameObject>("SpriteHost");
    auto* sprite = obj->AddComponent<SpriteRendererComponent>();
    sprite->spritePath = spritePath.string();
    scene.rootGameObject->AddChild(std::move(obj));

    std::string snapshot = scene.CaptureSnapshot();

    fs::path movedPath = projectPath / "Assets" / "Sprites" / "Renamed.png";
    fs::rename(spritePath, movedPath);
    fs::rename(spritePath.string() + ".meta", movedPath.string() + ".meta");
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);

    Scene loaded;
    REQUIRE(loaded.RestoreSnapshot(snapshot));
    GameObject* host = FindDirectChild(loaded.rootGameObject.get(), "SpriteHost");
    REQUIRE(host != nullptr);
    auto* loadedSprite = host->GetComponent<SpriteRendererComponent>();
    REQUIRE(loadedSprite != nullptr);
    REQUIRE(loadedSprite->spritePath == "Sprites/Renamed.png");

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", ScriptAssetReferenceSurvivesAssetRenameViaGuid)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_script_guid_scene";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("ScriptGuidProject"));

    fs::path projectPath = projectsRoot / "ScriptGuidProject";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path scriptPath = projectPath / "Assets" / "Scripts" / "Original.cs";
    fs::create_directories(scriptPath.parent_path());
    std::ofstream script(scriptPath, std::ios::trunc);
    script << "using DittoEngine;\n";
    script << "public class Original : MonoBehaviour { public float speed = 1.0f; }\n";
    script.close();
    std::string guid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(scriptPath);
    REQUIRE(!guid.empty());

    Scene scene;
    scene.name = "ScriptGuidScene";
    scene.rootGameObject->name = scene.name;
    auto obj = std::make_unique<GameObject>("ScriptHost");
    auto* scriptComp = obj->AddComponent<CSharpScriptComponent>();
    scriptComp->scriptName = "Original";
    scriptComp->scriptPath = scriptPath.string();
    scene.rootGameObject->AddChild(std::move(obj));

    std::string snapshot = scene.CaptureSnapshot();

    fs::path movedPath = projectPath / "Assets" / "Scripts" / "Renamed.cs";
    fs::rename(scriptPath, movedPath);
    fs::rename(scriptPath.string() + ".meta", movedPath.string() + ".meta");
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);

    Scene loaded;
    REQUIRE(loaded.RestoreSnapshot(snapshot));
    GameObject* host = FindDirectChild(loaded.rootGameObject.get(), "ScriptHost");
    REQUIRE(host != nullptr);
    auto* loadedScript = host->GetComponent<CSharpScriptComponent>();
    REQUIRE(loadedScript != nullptr);
    REQUIRE(loadedScript->scriptPath == "Scripts/Renamed.cs");

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("file", SceneSerializationNormalizesAssetReferences)
{
    fs::path projectsRoot = fs::temp_directory_path() / "ditto_tests_scene_asset_refs";
    fs::remove_all(projectsRoot);
    fs::create_directories(projectsRoot);

    ProjectManager& pm = ProjectManager::GetInstance();
    pm.Initialize(projectsRoot.string());
    REQUIRE(pm.CreateProject("SceneAssetRefs"));

    fs::path projectPath = projectsRoot / "SceneAssetRefs";
    REQUIRE(pm.OpenProject(projectPath.string()));
    fs::path meshPath = projectPath / "Assets" / "Models" / "Probe.obj";
    fs::path materialPath = projectPath / "Assets" / "Materials" / "Probe.mat";
    fs::path physicsMaterialPath = projectPath / "Assets" / "PhysicsMaterials2D" / "Probe.physmat2d";
    fs::path texturePath = projectPath / "Assets" / "Textures" / "Probe.png";
    fs::path spritePath = projectPath / "Assets" / "Sprites" / "Probe.png";
    fs::path audioPath = projectPath / "Assets" / "Audio" / "Probe.wav";
    fs::path fontPath = projectPath / "Assets" / "Fonts" / "Probe.ttf";
    fs::create_directories(meshPath.parent_path());
    fs::create_directories(materialPath.parent_path());
    fs::create_directories(physicsMaterialPath.parent_path());
    fs::create_directories(texturePath.parent_path());
    fs::create_directories(spritePath.parent_path());
    fs::create_directories(audioPath.parent_path());
    fs::create_directories(fontPath.parent_path());
    std::ofstream(meshPath, std::ios::binary) << "obj";
    std::ofstream(materialPath, std::ios::binary) << "mat";
    std::ofstream(physicsMaterialPath, std::ios::binary) << "physmat2d";
    std::ofstream(texturePath, std::ios::binary) << "png";
    std::ofstream(spritePath, std::ios::binary) << "png";
    std::ofstream(audioPath, std::ios::binary) << "wav";
    std::ofstream(fontPath, std::ios::binary) << "ttf";

    Scene scene;
    scene.name = "AssetRefs";
    scene.rootGameObject->name = scene.name;

    auto obj = std::make_unique<GameObject>("AssetHost");
    auto* renderer = obj->AddComponent<RendererComponent>();
    renderer->meshPath = meshPath.string();
    renderer->materialPath = materialPath.string();
    renderer->mainTexturePath = texturePath.string();

    auto* sprite = obj->AddComponent<SpriteRendererComponent>();
    sprite->spritePath = spritePath.string();
    sprite->materialPath = materialPath.string();

    auto* collider = obj->AddComponent<ColliderComponent>(ColliderComponent::MeshConvex);
    collider->meshPath = meshPath.string();

    auto* body2D = obj->AddComponent<Rigidbody2DComponent>();
    body2D->materialPath = physicsMaterialPath.string();

    auto* audio = obj->AddComponent<AudioSourceComponent>();
    audio->clipPath = audioPath.string();
    audio->outputPath = "Master";

    auto* image = obj->AddComponent<UIImageComponent>();
    image->texturePath = texturePath.string();

    auto* text = obj->AddComponent<UITextComponent>();
    text->fontPath = fontPath.string();

    scene.rootGameObject->AddChild(std::move(obj));

    Scene loaded;
    REQUIRE(loaded.RestoreSnapshot(scene.CaptureSnapshot()));
    GameObject* host = FindDirectChild(loaded.rootGameObject.get(), "AssetHost");
    REQUIRE(host != nullptr);

    auto* loadedRenderer = host->GetComponent<RendererComponent>();
    auto* loadedSprite = host->GetComponent<SpriteRendererComponent>();
    auto* loadedCollider = host->GetComponent<ColliderComponent>();
    auto* loadedBody2D = host->GetComponent<Rigidbody2DComponent>();
    auto* loadedAudio = host->GetComponent<AudioSourceComponent>();
    auto* loadedImage = host->GetComponent<UIImageComponent>();
    auto* loadedText = host->GetComponent<UITextComponent>();
    REQUIRE(loadedRenderer != nullptr);
    REQUIRE(loadedSprite != nullptr);
    REQUIRE(loadedCollider != nullptr);
    REQUIRE(loadedBody2D != nullptr);
    REQUIRE(loadedAudio != nullptr);
    REQUIRE(loadedImage != nullptr);
    REQUIRE(loadedText != nullptr);

    REQUIRE(loadedRenderer->meshPath == "Models/Probe.obj");
    REQUIRE(loadedRenderer->materialPath == "Materials/Probe.mat");
    REQUIRE(loadedRenderer->mainTexturePath == "Textures/Probe.png");
    REQUIRE(loadedSprite->spritePath == "Sprites/Probe.png");
    REQUIRE(loadedSprite->materialPath == "Materials/Probe.mat");
    REQUIRE(loadedCollider->meshPath == "Models/Probe.obj");
    REQUIRE(loadedBody2D->materialPath == "PhysicsMaterials2D/Probe.physmat2d");
    REQUIRE(loadedAudio->clipPath == "Audio/Probe.wav");
    REQUIRE(loadedAudio->outputPath == "Master");
    REQUIRE(loadedImage->texturePath == "Textures/Probe.png");
    REQUIRE(loadedText->fontPath == "Fonts/Probe.ttf");

    pm.CloseProject();
    std::error_code ec;
    fs::remove_all(projectsRoot, ec);
}

TEST_CASE("csharp", ScriptFieldParserCoversSupportedTypes)
{
    fs::path scriptPath = WriteCSharpFixture("ScriptProbe.cs");
    CSharpScriptComponent component;
    component.scriptPath = scriptPath.string();
    component.ParseScriptFields();

    REQUIRE(component.fields.size() == 8);
    REQUIRE(FindField(component, "speed") != nullptr);
    REQUIRE(FindField(component, "health") != nullptr);
    REQUIRE(FindField(component, "enabledFlag") != nullptr);
    REQUIRE(FindField(component, "label") != nullptr);
    REQUIRE(FindField(component, "uv") != nullptr);
    REQUIRE(FindField(component, "direction") != nullptr);
    REQUIRE(FindField(component, "tint") != nullptr);
    REQUIRE(FindField(component, "privateWeight") != nullptr);
    REQUIRE(FindField(component, "hidden") == nullptr);
    REQUIRE(FindField(component, "staticValue") == nullptr);
    REQUIRE(FindField(component, "PropertyValue") == nullptr);

    REQUIRE(NearlyEqual(std::get<float>(FindField(component, "speed")->value), 2.5f));
    REQUIRE(std::get<int>(FindField(component, "health")->value) == 7);
    REQUIRE(std::get<bool>(FindField(component, "enabledFlag")->value));
    REQUIRE(std::get<std::string>(FindField(component, "label")->value) == "alpha");
    REQUIRE(NearlyEqual(std::get<glm::vec3>(FindField(component, "direction")->value).z, 5.0f));
}

TEST_CASE("csharp", ScriptComponentSerializeRoundTripPreservesEditedValues)
{
    fs::path scriptPath = WriteCSharpFixture("ScriptProbeSerialize.cs");
    CSharpScriptComponent component;
    component.scriptName = "ScriptProbe";
    component.scriptPath = scriptPath.string();
    component.ParseScriptFields();

    ScriptField* speed = nullptr;
    for (auto& field : component.fields)
        if (field.name == "speed") speed = &field;
    REQUIRE(speed != nullptr);
    speed->value = 12.0f;

    std::ostringstream oss(std::ios::binary);
    component.Serialize(oss);

    CSharpScriptComponent loaded;
    std::istringstream iss(oss.str(), std::ios::binary);
    loaded.Deserialize(iss);

    REQUIRE(loaded.scriptName == "ScriptProbe");
    REQUIRE(fs::path(loaded.scriptPath).lexically_normal() == scriptPath.lexically_normal());
    const ScriptField* loadedSpeed = FindField(loaded, "speed");
    REQUIRE(loadedSpeed != nullptr);
    REQUIRE(NearlyEqual(std::get<float>(loadedSpeed->value), 12.0f));
}

TEST_CASE("csharp", ScriptComponentCanAttachToGameObject)
{
    fs::path scriptPath = WriteCSharpFixture("ScriptProbeAttach.cs");
    GameObject obj("ScriptHost");
    CSharpScriptComponent* script = obj.AddComponent<CSharpScriptComponent>();
    script->scriptName = "ScriptProbe";
    script->scriptPath = scriptPath.string();
    script->ParseScriptFields();

    REQUIRE(script->gameObject == &obj);
    REQUIRE(obj.GetComponent<CSharpScriptComponent>() == script);
    REQUIRE((obj.compMask & ComponentIndex::CSharpScript) != 0);
    REQUIRE(FindField(*script, "direction") != nullptr);
}

TEST_CASE("csharp", ScriptCompileSmokeUsesCurrentDittoEngineApi)
{
    fs::path scriptPath = WriteCSharpFixture("ScriptProbeCompile.cs");
    fs::path outDll = fs::temp_directory_path() / "ditto_tests_csharp" / "ScriptProbeCompile.dll";
    std::string out = outDll.string();
    CSharpCompileResult result = CSharpScriptSystem::CompileScriptDetailed(scriptPath.string(), out);
    REQUIRE(result.ok);
    REQUIRE(result.warningCount == 0);
    REQUIRE(result.errorCount == 0);
    REQUIRE(fs::exists(outDll));
}

TEST_CASE("csharp", TwoDProjectileClientScriptCompilesAgainstEngineApi)
{
    fs::path scriptPath = WriteTwoDProjectileCapabilityFixture("ProjectileClientProbe.cs");
    fs::path outDll = fs::temp_directory_path() / "ditto_tests_csharp" / "ProjectileClientProbe.dll";
    std::string out = outDll.string();
    CSharpCompileResult result = CSharpScriptSystem::CompileScriptDetailed(scriptPath.string(), out);
    REQUIRE(result.ok);
    REQUIRE(result.warningCount == 0);
    REQUIRE(result.errorCount == 0);
    REQUIRE(fs::exists(outDll));
}

TEST_CASE("csharp", ScriptCompileDefaultOutputUsesProjectTemp)
{
    fs::path scriptPath = WriteCSharpFixture("Nested/ScriptProbeTempOutput.cs");
    fs::path scriptDirDll = scriptPath.parent_path() / "ScriptProbeTempOutput.dll";
    fs::path tempDll = scriptPath.parent_path().parent_path().parent_path().parent_path() / "Temp" / "ScriptProbeTempOutput.dll";
    std::error_code ec;
    fs::remove(scriptDirDll, ec);
    fs::remove(tempDll, ec);

    std::string out;
    CSharpCompileResult result = CSharpScriptSystem::CompileScriptDetailed(scriptPath.string(), out);
    REQUIRE(result.ok);
    REQUIRE(fs::path(out).lexically_normal() == tempDll.lexically_normal());
    REQUIRE(fs::exists(tempDll));
    REQUIRE(!fs::exists(scriptDirDll));
}

TEST_CASE("csharp", ScriptCompileDetailedReportsErrors)
{
    fs::path dir = fs::temp_directory_path() / "ditto_tests_csharp" / "Assets" / "Scripts";
    fs::create_directories(dir);
    fs::path scriptPath = dir / "BrokenScript.cs";
    std::ofstream cs(scriptPath, std::ios::trunc);
    cs << "using DittoEngine;\n";
    cs << "public class BrokenScript : MonoBehaviour\n";
    cs << "{\n";
    cs << "    public override void Update() { MissingSymbol(); }\n";
    cs << "}\n";
    cs.close();

    fs::path outDll = fs::temp_directory_path() / "ditto_tests_csharp" / "BrokenScript.dll";
    std::string out = outDll.string();
    CSharpCompileResult result = CSharpScriptSystem::CompileScriptDetailed(scriptPath.string(), out);
    REQUIRE(!result.ok);
    REQUIRE(fs::path(result.scriptPath).lexically_normal() == scriptPath.lexically_normal());
    REQUIRE(fs::path(result.outputDllPath).lexically_normal() == outDll.lexically_normal());
    REQUIRE(result.errorCount >= 1);
    REQUIRE(result.output.find("error CS") != std::string::npos);
    REQUIRE(!result.diagnostics.empty());
    REQUIRE(result.diagnostics[0].line >= 0);
    REQUIRE(result.diagnostics[0].column >= 0);
    REQUIRE(result.diagnostics[0].severity == "error");
    REQUIRE(result.diagnostics[0].code.rfind("CS", 0) == 0);
    REQUIRE(!result.diagnostics[0].message.empty());

    int infoBefore = 0, warningBefore = 0, errorBefore = 0;
    Ditto::Logger::Get().GetCounts(infoBefore, warningBefore, errorBefore);
    {
        ScopedConsoleLogSilence silence;
        REQUIRE(!CSharpScriptSystem::CompileScript(scriptPath.string(), out));
    }
    int infoAfter = 0, warningAfter = 0, errorAfter = 0;
    Ditto::Logger::Get().GetCounts(infoAfter, warningAfter, errorAfter);
    REQUIRE(errorAfter > errorBefore);

    CSharpScriptComponent component;
    {
        ScopedConsoleLogSilence silence;
        REQUIRE(!CSharpScriptSystem::LoadScript(scriptPath.string(), &component));
    }
    REQUIRE(!component.lastCompileResult.ok);
    REQUIRE(component.lastCompileResult.errorCount >= 1);
    REQUIRE(component.lastCompileResult.output.find("error CS") != std::string::npos);
    REQUIRE(!component.lastCompileResult.diagnostics.empty());
}

TEST_CASE("csharp", ScriptLifecycleIsIdempotentWithoutRuntimeInstance)
{
    GameObject obj("ScriptLifecycleHost");
    CSharpScriptComponent* script = obj.AddComponent<CSharpScriptComponent>();
    script->scriptName = "MissingRuntimeScript";
    script->enabled = true;
    REQUIRE(!script->started);

    script->Start();
    REQUIRE(script->started);
    script->Start();
    REQUIRE(script->started);
    script->Update();
    script->FixedUpdate();
    script->OnDestroy();
    REQUIRE(!script->started);

    script->Start();
    REQUIRE(script->started);

    script->enabled = false;
    script->started = false;
    script->Start();
    REQUIRE(!script->started);
}

TEST_CASE("simulation", EngineLifecycleEnterExitOrdersRuntimeState)
{
    Scene scene;
    scene.ClearScene();

    GameObject* actor = scene.rootGameObject->AddChild(std::make_unique<GameObject>("LifecycleActor"));
    scene.RegisterSubtree(actor);
    CSharpScriptComponent* script = actor->AddComponent<CSharpScriptComponent>();
    auto* transform = actor->GetComponent<TransformComponent>();
    transform->useQuatRotation = true;
    transform->localDirty = false;

    Physics physics;
    Physics2DWorld physics2D;
    float accumulator = 12.0f;
    CSharpScriptSystem::SetTime(99.0f);

    Ditto::EngineLifecycle::EnterPlayMode(&scene, &physics, &physics2D, accumulator);
    REQUIRE(script->started);
    REQUIRE(accumulator == 0.0f);
    REQUIRE(CSharpScriptSystem::GetTime() == 0.0f);

    Ditto::EngineLifecycle::ExitPlayMode(&scene, &physics, &physics2D, accumulator);
    REQUIRE(!script->started);
    REQUIRE(!transform->useQuatRotation);
    REQUIRE(transform->localDirty);
    REQUIRE(accumulator == 0.0f);
}

TEST_CASE("simulation", PlayModeHarnessStepsRuntimeAndRestoresOnExit)
{
    Scene scene;
    Physics physics;
    Physics2DWorld physics2D;
    float accumulator = 0.0f;

    auto actor = std::make_unique<GameObject>("AnimatedParticleActor");
    auto* transform = actor->GetComponent<TransformComponent>();
    REQUIRE(transform != nullptr);

    auto* animator = actor->AddComponent<AnimatorComponent>();
    AnimationClip clip;
    clip.name = "Move";
    clip.length = 1.0f;
    clip.loop = false;
    AnimationKeyframe a;
    a.time = 0.0f;
    a.position = glm::vec3(0.0f);
    a.rotation = glm::vec3(0.0f);
    a.scale = glm::vec3(1.0f);
    AnimationKeyframe b = a;
    b.time = 1.0f;
    b.position = glm::vec3(10.0f, 0.0f, 0.0f);
    clip.keyframes = { a, b };
    animator->AddClip(clip);
    animator->defaultClip = "Move";
    animator->playOnAwake = true;

    auto* particles = actor->AddComponent<ParticleSystemComponent>();
    particles->playOnAwake = true;
    particles->emissionRate = 100.0f;
    particles->startLifetime = 1.0f;

    GameObject* actorPtr = scene.rootGameObject->AddChild(std::move(actor));
    scene.RegisterSubtree(actorPtr);

    Ditto::EngineLifecycle::EnterPlayMode(&scene, &physics, &physics2D, accumulator);
    REQUIRE(animator->IsPlaying());
    REQUIRE(particles->IsPlaying());

    Ditto::EngineLifecycle::StepPlayModeFrame(&scene, &physics, &physics2D, 0.25f, accumulator);
    REQUIRE(CSharpScriptSystem::GetTime() >= 0.249f);
    REQUIRE(transform->position.x > 2.0f);
    int alive = 0;
    for (const Particle& particle : particles->particles)
        if (particle.alive) ++alive;
    REQUIRE(alive > 0);

    transform->useQuatRotation = true;
    Ditto::EngineLifecycle::ExitPlayMode(&scene, &physics, &physics2D, accumulator);
    REQUIRE(!transform->useQuatRotation);
    REQUIRE(accumulator == 0.0f);
}

TEST_CASE("simulation", DynamicBodyGravityIntegratesOnce)
{
    fs::path cubePath = WriteTempCubeObj("gravity_cube.obj");
    MeshData cube(cubePath.string(), false);

    GameObject body("FallingBody");
    auto* transform = body.GetComponent<TransformComponent>();
    auto* rb = body.AddComponent<RigidbodyComponent>();
    rb->type = RigidbodyComponent::Dynamic;
    rb->useGravity = true;
    rb->velocity = glm::vec3(0.0f);

    Physics physics;
    physics.linearDamping = 0.0f;
    physics.gravity = 9.8f;
    physics.colliders.push_back(MakeTestCollider(body, cube));
    physics.UpdatePhysics(physics.deltaTime);

    REQUIRE(rb->velocity.y < 0.0f);
    REQUIRE(transform->position.y < 0.0f);
}

TEST_CASE("simulation", SharedRigidbodyIntegratesOnlyOnce)
{
    fs::path cubePath = WriteTempCubeObj("shared_body_cube.obj");
    MeshData cube(cubePath.string(), false);

    GameObject body("CompoundBody");
    auto* transform = body.GetComponent<TransformComponent>();
    auto* rb = body.AddComponent<RigidbodyComponent>();
    rb->type = RigidbodyComponent::Dynamic;
    rb->useGravity = true;
    rb->velocity = glm::vec3(0.0f);

    auto first = MakeTestCollider(body, cube);
    auto second = MakeTestCollider(body, cube);

    Physics physics;
    physics.linearDamping = 0.0f;
    physics.gravity = 9.8f;
    physics.colliders.push_back(std::move(first));
    physics.colliders.push_back(std::move(second));
    physics.IntegrateForce(physics.deltaTime);

    const float expectedVelocityY = -physics.gravity * physics.deltaTime;
    const float expectedPositionY = expectedVelocityY * physics.deltaTime;
    REQUIRE(NearlyEqual(rb->velocity.y, expectedVelocityY, 0.0005f));
    REQUIRE(NearlyEqual(transform->position.y, expectedPositionY, 0.0005f));
}

TEST_CASE("simulation", KinematicBodyDoesNotIntegrate)
{
    fs::path cubePath = WriteTempCubeObj("kinematic_cube.obj");
    MeshData cube(cubePath.string(), false);

    GameObject body("KinematicBody");
    auto* transform = body.GetComponent<TransformComponent>();
    auto* rb = body.AddComponent<RigidbodyComponent>();
    rb->type = RigidbodyComponent::Kinematic;
    rb->useGravity = true;
    rb->velocity = glm::vec3(10.0f, 10.0f, 0.0f);
    transform->position = glm::vec3(3.0f, 4.0f, 5.0f);

    Physics physics;
    physics.colliders.push_back(MakeTestCollider(body, cube));
    physics.IntegrateForce(physics.deltaTime);

    REQUIRE(NearlyEqual(transform->position.x, 3.0f));
    REQUIRE(NearlyEqual(transform->position.y, 4.0f));
    REQUIRE(NearlyEqual(rb->velocity.y, 10.0f));
}

TEST_CASE("simulation", DynamicStaticPositionCorrectionSeparates)
{
    fs::path cubePath = WriteTempCubeObj("collision_cube.obj");
    MeshData cube(cubePath.string(), false);

    GameObject dynamicObj("Dynamic");
    auto* dynamicTransform = dynamicObj.GetComponent<TransformComponent>();
    auto* dynamicRb = dynamicObj.AddComponent<RigidbodyComponent>();
    dynamicRb->type = RigidbodyComponent::Dynamic;
    dynamicRb->useGravity = false;
    dynamicTransform->position = glm::vec3(0.0f, 0.0f, 0.0f);

    GameObject staticObj("Static");
    auto* staticTransform = staticObj.GetComponent<TransformComponent>();
    auto* staticRb = staticObj.AddComponent<RigidbodyComponent>();
    staticRb->type = RigidbodyComponent::Static;
    staticRb->useGravity = false;
    staticTransform->position = glm::vec3(0.25f, 0.0f, 0.0f);

    Physics physics;
    physics.gravity = 0.0f;
    physics.linearDamping = 0.0f;
    physics.colliders.push_back(MakeTestCollider(dynamicObj, cube));
    physics.colliders.push_back(MakeTestCollider(staticObj, cube));

    const glm::vec3 before = dynamicTransform->position;
    CollisionInfo info;
    info.flag = true;
    info.depth = 0.5f;
    info.normal = glm::vec3(1.0f, 0.0f, 0.0f);
    info.contactPointA = dynamicTransform->position;
    info.contactPointB = staticTransform->position;
    physics.collisionData.emplace_back(physics.colliders[0].get(), physics.colliders[1].get(), info);
    physics.ApplyPositionCorrections();
    const glm::vec3 after = dynamicTransform->position;

    REQUIRE(glm::length(after - before) > 0.0001f);
    REQUIRE(NearlyEqual(staticTransform->position.x, 0.25f));
}

TEST_CASE("simulation", Physics2DImpulseLaunchMovesDynamicBody)
{
    Scene scene;
    scene.name = "Physics2DImpulseLaunch";
    scene.rootGameObject->name = scene.name;
    scene.rootGameObject->children.clear();
    scene.gameObjects.clear();
    scene.mainCamera = nullptr;

    auto projectile = std::make_unique<GameObject>("Projectile");
    auto* transform = projectile->GetComponent<TransformComponent>();
    REQUIRE(transform != nullptr);
    transform->position = glm::vec3(0.0f);
    transform->UpdateTransform();

    auto* rb = projectile->AddComponent<Rigidbody2DComponent>();
    rb->type = Rigidbody2DComponent::Dynamic;
    rb->mass = 2.0f;
    rb->useGravity = false;
    rb->linearDamping = 0.0f;
    rb->angularDamping = 0.0f;
    rb->velocity = glm::vec2(0.0f);

    auto* collider = projectile->AddComponent<Collider2DComponent>(Collider2DComponent::Circle);
    collider->radius = 0.25f;

    rb->AddForce(glm::vec2(10.0f, 4.0f), Rigidbody2DComponent::Impulse);
    REQUIRE(NearlyEqual(rb->velocity.x, 5.0f));
    REQUIRE(NearlyEqual(rb->velocity.y, 2.0f));

    scene.rootGameObject->AddChild(std::move(projectile));

    Physics2DWorld world;
    world.gravity = glm::vec2(0.0f);
    world.StepFixed(&scene, 0.1f);

    REQUIRE(NearlyEqual(rb->velocity.x, 5.0f, 0.0005f));
    REQUIRE(NearlyEqual(rb->velocity.y, 2.0f, 0.0005f));
    REQUIRE(NearlyEqual(transform->position.x, 0.5f, 0.0005f));
    REQUIRE(NearlyEqual(transform->position.y, 0.2f, 0.0005f));
}

TEST_CASE("simulation", Physics2DUsesRigidbodyMaterialAsset)
{
    fs::path base = fs::temp_directory_path() / "ditto_tests_physics_materials_2d";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path materialPath = base / "Bouncy.physmat2d";

    Ditto::PhysicsMaterial2DAsset material = Ditto::MakeDefaultPhysicsMaterial2D("Bouncy");
    material.friction = 0.0f;
    material.restitution = 1.0f;
    REQUIRE(Ditto::SavePhysicsMaterial2DAsset(material, materialPath));

    Scene scene;
    scene.name = "Physics2DMaterial";
    scene.rootGameObject->name = scene.name;
    scene.rootGameObject->children.clear();

    auto left = std::make_unique<GameObject>("Left");
    auto* leftTransform = left->GetComponent<TransformComponent>();
    leftTransform->position = glm::vec3(-0.2f, 0.0f, 0.0f);
    leftTransform->UpdateTransform();
    auto* leftRb = left->AddComponent<Rigidbody2DComponent>();
    leftRb->type = Rigidbody2DComponent::Dynamic;
    leftRb->useGravity = false;
    leftRb->linearDamping = 0.0f;
    leftRb->velocity = glm::vec2(1.0f, 0.0f);
    leftRb->materialPath = materialPath.string();
    auto* leftCollider = left->AddComponent<Collider2DComponent>(Collider2DComponent::Circle);
    leftCollider->radius = 0.5f;
    leftCollider->restitution = 0.0f;

    auto right = std::make_unique<GameObject>("Right");
    auto* rightTransform = right->GetComponent<TransformComponent>();
    rightTransform->position = glm::vec3(0.2f, 0.0f, 0.0f);
    rightTransform->UpdateTransform();
    auto* rightRb = right->AddComponent<Rigidbody2DComponent>();
    rightRb->type = Rigidbody2DComponent::Dynamic;
    rightRb->useGravity = false;
    rightRb->linearDamping = 0.0f;
    rightRb->velocity = glm::vec2(-1.0f, 0.0f);
    auto* rightCollider = right->AddComponent<Collider2DComponent>(Collider2DComponent::Circle);
    rightCollider->radius = 0.5f;
    rightCollider->restitution = 0.0f;

    scene.rootGameObject->AddChild(std::move(left));
    scene.rootGameObject->AddChild(std::move(right));

    Physics2DWorld world;
    world.gravity = glm::vec2(0.0f);
    world.velocityIterations = 1;
    world.positionIterations = 0;
    world.StepFixed(&scene, 0.001f);

    REQUIRE(leftRb->velocity.x < -0.5f);
    REQUIRE(rightRb->velocity.x > 0.5f);

    std::error_code ec;
    fs::remove_all(base, ec);
}

int main(int argc, char** argv)
{
    std::string requestedStage;
    if (argc > 1 && std::string(argv[1]) == "--dump-scene")
    {
        DumpSampleSceneJson();
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--stage")
    {
        requestedStage = argv[2];
    }
    else if (argc > 1 && std::string(argv[1]) == "--list-stages")
    {
        std::cout << "file\ncsharp\nrender\nsimulation\n";
        return 0;
    }

    int failed = 0;
    int selected = 0;
    const char* orderedStages[] = { "file", "csharp", "render", "simulation" };
    for (const char* stage : orderedStages)
    {
        if (!requestedStage.empty() && requestedStage != stage) continue;

        int stageSelected = 0;
        int stageFailed = 0;
        for (const TestCase& test : Tests())
        {
            if (std::string(test.stage) != stage) continue;
            ++selected;
            ++stageSelected;
            try
            {
                std::cout << "[RUN][" << test.stage << "] " << test.name << "\n";
                test.fn();
                std::cout << "[PASS][" << test.stage << "] " << test.name << "\n";
            }
            catch (const std::exception& e)
            {
                ++failed;
                ++stageFailed;
                std::cerr << "[FAIL][" << test.stage << "] " << test.name << ": " << e.what() << "\n";
            }
        }

        if (stageSelected == 0)
            std::cout << "[PENDING][" << stage << "] no registered tests\n";
        else
            std::cout << "[STAGE][" << stage << "] " << stageSelected << " tests, " << stageFailed << " failed\n";
    }

    if (!requestedStage.empty() && selected == 0)
    {
        std::cerr << "Unknown or empty test stage: " << requestedStage << "\n";
        return 1;
    }

    std::cout << selected << " selected tests, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/PathUtils.h"
#include "../Engine/Core/CSharpScript.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Physics/Physics.h"

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
        cs << "    public void Update() { Debug.Log(label); }\n";
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
        player->AddComponent<RendererComponent>(RendererComponent::Cube);
        player->AddComponent<RigidbodyComponent>();
        player->AddComponent<ColliderComponent>(ColliderComponent::Box);
        scene.rootGameObject->AddChild(std::move(player));

        std::cout << "{\n  \"scene\":";
        DumpObjectJson(std::cout, scene.rootGameObject.get(), 2);
        std::cout << "\n}\n";
    }
}

#define TEST_CASE(stage, name) static void name(); static RegisterTest reg_##name(stage, #name, &name); static void name()
#define REQUIRE(expr) Require(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

TEST_CASE("file", GameObjectComponentsAndRemoval)
{
    GameObject obj("Actor");
    REQUIRE(obj.GetComponent<TransformComponent>() != nullptr);
    REQUIRE((obj.compMask & ComponentIndex::Transform) != 0);

    auto* renderer = obj.AddComponent<RendererComponent>(RendererComponent::Sphere);
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

    auto* renderer = actor->AddComponent<RendererComponent>(RendererComponent::Sphere);
    renderer->color = { 0.25f, 0.5f, 0.75f, 1.0f };
    renderer->meshSource = RendererComponent::File;
    renderer->meshPath = "Models/Custom.obj";
    renderer->materialPath = "Materials/Test.mat";
    renderer->shaderName = "Lit_Toon";
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
    REQUIRE(loaded.rootGameObject->children.size() == 1);

    GameObject* loadedActor = loaded.rootGameObject->children[0].get();
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
    REQUIRE(loadedRenderer->type == RendererComponent::Sphere);
    REQUIRE(loadedRenderer->meshSource == RendererComponent::File);
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
    REQUIRE(fs::exists(materialPath));

    Ditto::MaterialAsset material = Ditto::LoadMaterialAsset(materialPath.string());
    REQUIRE(material.ok);
    REQUIRE(material.shaderName == "Shaders/Lit_Toon.shader");
    REQUIRE(NearlyEqual(material.color.r, 1.0f));
    REQUIRE(NearlyEqual(material.color.g, 1.0f));
    REQUIRE(NearlyEqual(material.color.b, 1.0f));
    REQUIRE(pm.OpenProject(projectPath.string()));
    REQUIRE(fs::exists(materialPath));
    pm.CloseProject();
}

TEST_CASE("file", SceneSaveLoadAndCorruptFileHandling)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "ditto_tests_scene_files";
    fs::create_directories(base);
    fs::path scenePath = base / "scene.bin";
    fs::path corruptPath = base / "corrupt.bin";

    Scene scene;
    scene.name = "FileRoundTrip";
    scene.rootGameObject->name = scene.name;
    auto obj = std::make_unique<GameObject>("DiskActor");
    obj->AddComponent<RendererComponent>(RendererComponent::Cube);
    scene.rootGameObject->AddChild(std::move(obj));

    REQUIRE(scene.SaveScene(scenePath.string()));
    REQUIRE(fs::exists(scenePath));
    REQUIRE(fs::file_size(scenePath) > 0);

    Scene loaded;
    REQUIRE(loaded.LoadScene(scenePath.string()));
    REQUIRE(loaded.name == "FileRoundTrip");
    REQUIRE(loaded.rootGameObject->children.size() == 1);
    REQUIRE(loaded.rootGameObject->children[0]->name == "DiskActor");
    REQUIRE(loaded.rootGameObject->children[0]->GetComponent<RendererComponent>() != nullptr);

    {
        std::ofstream corrupt(corruptPath, std::ios::binary | std::ios::trunc);
        corrupt << "not a ditto scene";
    }
    Scene failed;
    REQUIRE(!failed.LoadScene(corruptPath.string()));

    std::error_code ec;
    fs::remove_all(base, ec);
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
    REQUIRE(loaded.scriptPath == scriptPath.string());
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
    REQUIRE(CSharpScriptSystem::CompileScript(scriptPath.string(), out));
    REQUIRE(fs::exists(outDll));
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

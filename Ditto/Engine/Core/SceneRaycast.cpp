#include "Scene.h"

#ifndef DITTO_HEADLESS_SCENE
#include "../Resources/AssetPath.h"
#include "../../Engine/Resources/Resource.h"
#include "Logger.h"

#include <cfloat>
#include <functional>

namespace
{
    bool RayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
        float& t)
    {
        constexpr float EPSILON = 0.000001f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(dir, edge2);
        float a = glm::dot(edge1, h);
        if (a > -EPSILON && a < EPSILON) return false;
        float f = 1.0f / a;
        glm::vec3 s = orig - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) return false;
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(dir, q);
        if (v < 0.0f || u + v > 1.0f) return false;
        t = f * glm::dot(edge2, q);
        return t > EPSILON;
    }

    void GetRaycastMeshForPath(const std::string& meshPath,
        std::unordered_map<std::string, std::pair<std::vector<glm::vec3>, std::vector<unsigned int>>>& cache,
        const std::vector<glm::vec3>*& vertices, const std::vector<unsigned int>*& indices)
    {
        static const std::vector<glm::vec3> quadVertices = {
            {-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
            {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}
        };
        static const std::vector<unsigned int> quadIndices = { 0, 1, 2, 0, 2, 3 };

        vertices = nullptr;
        indices = nullptr;

        if (meshPath == "__sprite_quad__" || meshPath == "__particle_quad__" || meshPath.empty())
        {
            vertices = &quadVertices;
            indices = &quadIndices;
            return;
        }

        auto it = cache.find(meshPath);
        if (it != cache.end())
        {
            vertices = &it->second.first;
            indices = &it->second.second;
            return;
        }

        std::string resolved = Ditto::AssetPath::ResolveTypedAssetPath(meshPath, "Models").string();
        ModelData model(resolved);
        if (model.vertexData.empty())
        {
            cache[meshPath] = {};
            return;
        }

        auto& entry = cache[meshPath];
        for (size_t i = 0; i + 2 < model.vertexData.size(); i += 8)
            entry.first.push_back(glm::vec3(model.vertexData[i], model.vertexData[i + 1], model.vertexData[i + 2]));
        entry.second = model.indices;

        vertices = &entry.first;
        indices = &entry.second;
    }

    std::string GetRaycastPath(GameObject* obj)
    {
        RendererComponent* renderer = obj->GetComponent<RendererComponent>();
        if (renderer && renderer->enabled)
            return renderer->meshPath;

        SpriteRendererComponent* spriteRenderer = obj->GetComponent<SpriteRendererComponent>();
        if (spriteRenderer && spriteRenderer->enabled && !spriteRenderer->spritePath.empty())
            return "__sprite_quad__";

        return {};
    }
}

GameObject* Scene::RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight)
{
    float ndcX = (2.0f * mousePos.x / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mousePos.y / viewportHeight);
    glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::mat4 invProj = glm::inverse(projection);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 rayOrigin = glm::vec3(invView[3]);
    glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));

    DITTO_LOG_INFO_STREAM("[Raycast] MousePos: (" << mousePos.x << ", " << mousePos.y << ")");
    DITTO_LOG_INFO_STREAM("[Raycast] RayOrigin: (" << rayOrigin.x << ", " << rayOrigin.y << ", " << rayOrigin.z << ")");
    DITTO_LOG_INFO_STREAM("[Raycast] RayDir: (" << rayDir.x << ", " << rayDir.y << ", " << rayDir.z << ")");

    GameObject* closest = nullptr;
    float closestDist = FLT_MAX;
    int checkedCount = 0;

    std::function<void(GameObject*)> checkObject = [&](GameObject* obj)
    {
        if (!obj->enabled) return;
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (!transform || !transform->enabled) return;

        std::string raycastPath = GetRaycastPath(obj);
        if (raycastPath.empty()) return;

        checkedCount++;
        glm::mat4 worldMat = transform->GetWorldModel();
        const std::vector<glm::vec3>* vertices = nullptr;
        const std::vector<unsigned int>* indices = nullptr;
        GetRaycastMeshForPath(raycastPath, raycastMeshCache, vertices, indices);

        if (!vertices || !indices)
        {
            DITTO_LOG_INFO_STREAM("[Raycast] Object '" << obj->name << "' has no mesh data");
            return;
        }

        DITTO_LOG_INFO_STREAM("[Raycast] Checking object: " << obj->name << " (mesh=" << raycastPath
            << ", vertices: " << vertices->size() << ", indices: " << indices->size() << ")");

        float tMin = FLT_MAX;
        bool hit = false;

        for (size_t i = 0; i < indices->size(); i += 3)
        {
            if (i + 2 >= indices->size()) break;

            unsigned int idx0 = (*indices)[i];
            unsigned int idx1 = (*indices)[i + 1];
            unsigned int idx2 = (*indices)[i + 2];
            if (idx0 >= vertices->size() || idx1 >= vertices->size() || idx2 >= vertices->size())
                continue;

            glm::vec3 v0 = glm::vec3(worldMat * glm::vec4((*vertices)[idx0], 1.0f));
            glm::vec3 v1 = glm::vec3(worldMat * glm::vec4((*vertices)[idx1], 1.0f));
            glm::vec3 v2 = glm::vec3(worldMat * glm::vec4((*vertices)[idx2], 1.0f));

            float t = 0.0f;
            if (RayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t) && t < tMin)
            {
                tMin = t;
                hit = true;
            }
        }

        if (hit)
        {
            DITTO_LOG_INFO_STREAM("[Raycast] Hit object: " << obj->name << " at distance " << tMin);
            if (tMin < closestDist)
            {
                closestDist = tMin;
                closest = obj;
            }
        }
    };

    DITTO_LOG_INFO_STREAM("[Raycast] Traversing rootGameObject hierarchy: " << rootGameObject->name);
    std::function<void(GameObject*)> traverseHierarchy = [&](GameObject* obj)
    {
        if (!obj) return;
        checkObject(obj);
        for (const auto& child : obj->children)
            traverseHierarchy(child.get());
    };
    traverseHierarchy(rootGameObject.get());

    DITTO_LOG_INFO_STREAM("[Raycast] Checked " << checkedCount << " objects, closest: " << (closest ? closest->name : "none"));
    return closest;
}

GameObject* Scene::RaycastGameObjects(const glm::vec2& mousePos, const Camera& camera, int viewportWidth, int viewportHeight)
{
    const Camera::Ray ray = camera.ScreenPointToRayFull(mousePos, viewportWidth, viewportHeight);

    GameObject* closest = nullptr;
    float closestDist = FLT_MAX;

    std::function<void(GameObject*)> checkObject = [&](GameObject* obj)
    {
        if (!obj->enabled) return;
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (!transform || !transform->enabled) return;

        std::string raycastPath = GetRaycastPath(obj);
        if (raycastPath.empty()) return;

        glm::mat4 worldMat = transform->GetWorldModel();
        const std::vector<glm::vec3>* vertices = nullptr;
        const std::vector<unsigned int>* indices = nullptr;
        GetRaycastMeshForPath(raycastPath, raycastMeshCache, vertices, indices);
        if (!vertices || !indices) return;

        float tMin = FLT_MAX;
        bool hit = false;

        for (size_t i = 0; i < indices->size(); i += 3)
        {
            if (i + 2 >= indices->size()) break;

            unsigned int idx0 = (*indices)[i];
            unsigned int idx1 = (*indices)[i + 1];
            unsigned int idx2 = (*indices)[i + 2];
            if (idx0 >= vertices->size() || idx1 >= vertices->size() || idx2 >= vertices->size())
                continue;

            glm::vec3 v0 = glm::vec3(worldMat * glm::vec4((*vertices)[idx0], 1.0f));
            glm::vec3 v1 = glm::vec3(worldMat * glm::vec4((*vertices)[idx1], 1.0f));
            glm::vec3 v2 = glm::vec3(worldMat * glm::vec4((*vertices)[idx2], 1.0f));

            float t = 0.0f;
            if (RayTriangleIntersect(ray.origin, ray.direction, v0, v1, v2, t) && t < tMin)
            {
                tMin = t;
                hit = true;
            }
        }

        if (hit && tMin < closestDist)
        {
            closestDist = tMin;
            closest = obj;
        }
    };

    std::function<void(GameObject*)> traverseHierarchy = [&](GameObject* obj)
    {
        if (!obj) return;
        checkObject(obj);
        for (const auto& child : obj->children)
            traverseHierarchy(child.get());
    };
    traverseHierarchy(rootGameObject.get());
    return closest;
}

#else
GameObject* Scene::RaycastGameObjects(const glm::vec2&, const glm::mat4&, const glm::mat4&, int, int)
{
    return nullptr;
}

GameObject* Scene::RaycastGameObjects(const glm::vec2&, const Camera&, int, int)
{
    return nullptr;
}
#endif

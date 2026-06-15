#include "Physics2D.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
    float InvMass(const Rigidbody2DComponent* rb)
    {
        return rb && rb->enabled && rb->type == Rigidbody2DComponent::Dynamic
            ? 1.0f / glm::max(0.0001f, rb->mass) : 0.0f;
    }

    glm::vec2 Position2D(const TransformComponent* t, const Collider2DComponent* c)
    {
        return glm::vec2(t->position.x, t->position.y) + (c ? c->offset : glm::vec2(0.0f));
    }

    glm::vec2 HalfSize(const TransformComponent* t, const Collider2DComponent* c)
    {
        glm::vec2 base = c ? c->size : glm::vec2(1.0f);
        return glm::max(glm::abs(glm::vec2(t->scale.x, t->scale.y)) * base * 0.5f, glm::vec2(0.0005f));
    }

    float Radius(const TransformComponent* t, const Collider2DComponent* c)
    {
        float scale = glm::max(std::abs(t->scale.x), std::abs(t->scale.y));
        return glm::max(0.0005f, (c ? c->radius : 0.5f) * scale);
    }

    bool CircleCircle(const Physics2DBody& a, const Physics2DBody& b, Physics2DWorld::Contact& out)
    {
        glm::vec2 pa = Position2D(a.transform, a.collider);
        glm::vec2 pb = Position2D(b.transform, b.collider);
        glm::vec2 delta = pb - pa;
        float dist2 = glm::dot(delta, delta);
        float r = Radius(a.transform, a.collider) + Radius(b.transform, b.collider);
        if (dist2 > r * r) return false;

        float dist = std::sqrt(glm::max(0.0f, dist2));
        out.normal = dist > 0.0001f ? delta / dist : glm::vec2(1.0f, 0.0f);
        out.depth = r - dist;
        out.point = pa + out.normal * (Radius(a.transform, a.collider) - out.depth * 0.5f);
        return true;
    }

    bool BoxBox(const Physics2DBody& a, const Physics2DBody& b, Physics2DWorld::Contact& out)
    {
        glm::vec2 pa = Position2D(a.transform, a.collider);
        glm::vec2 pb = Position2D(b.transform, b.collider);
        glm::vec2 ha = HalfSize(a.transform, a.collider);
        glm::vec2 hb = HalfSize(b.transform, b.collider);
        glm::vec2 delta = pb - pa;
        float overlapX = ha.x + hb.x - std::abs(delta.x);
        float overlapY = ha.y + hb.y - std::abs(delta.y);
        if (overlapX <= 0.0f || overlapY <= 0.0f) return false;

        if (overlapX < overlapY)
        {
            out.normal = glm::vec2(delta.x < 0.0f ? -1.0f : 1.0f, 0.0f);
            out.depth = overlapX;
        }
        else
        {
            out.normal = glm::vec2(0.0f, delta.y < 0.0f ? -1.0f : 1.0f);
            out.depth = overlapY;
        }
        out.point = (pa + pb) * 0.5f;
        return true;
    }

    bool CircleBox(const Physics2DBody& circle, const Physics2DBody& box, Physics2DWorld::Contact& out)
    {
        glm::vec2 pc = Position2D(circle.transform, circle.collider);
        glm::vec2 pb = Position2D(box.transform, box.collider);
        glm::vec2 hb = HalfSize(box.transform, box.collider);
        glm::vec2 closest = glm::clamp(pc, pb - hb, pb + hb);
        glm::vec2 fromCircle = closest - pc;
        float dist2 = glm::dot(fromCircle, fromCircle);
        float r = Radius(circle.transform, circle.collider);
        if (dist2 > r * r) return false;

        float dist = std::sqrt(glm::max(0.0f, dist2));
        if (dist > 0.0001f)
        {
            out.normal = fromCircle / dist;
            out.depth = r - dist;
            out.point = closest;
        }
        else
        {
            glm::vec2 delta = pc - pb;
            float px = hb.x - std::abs(delta.x);
            float py = hb.y - std::abs(delta.y);
            if (px < py)
            {
                out.normal = glm::vec2(delta.x < 0.0f ? -1.0f : 1.0f, 0.0f);
                out.depth = r + px;
            }
            else
            {
                out.normal = glm::vec2(0.0f, delta.y < 0.0f ? -1.0f : 1.0f);
                out.depth = r + py;
            }
            out.point = pc;
        }
        return true;
    }

    bool TestCollision(const Physics2DBody& a, const Physics2DBody& b, Physics2DWorld::Contact& out)
    {
        const bool aCircle = a.collider->type == Collider2DComponent::Circle;
        const bool bCircle = b.collider->type == Collider2DComponent::Circle;
        if (aCircle && bCircle) return CircleCircle(a, b, out);
        if (!aCircle && !bCircle) return BoxBox(a, b, out);
        if (aCircle && !bCircle) return CircleBox(a, b, out);
        bool hit = CircleBox(b, a, out);
        out.normal = -out.normal;
        return hit;
    }
}

void Physics2DWorld::Step(Scene* scene, float dt)
{
    if (!scene || dt <= 0.0f) return;
    accumulator += glm::min(dt, 0.25f);
    int steps = 0;
    while (accumulator >= fixedDeltaTime && steps < 5)
    {
        FixedStep(scene, fixedDeltaTime);
        accumulator -= fixedDeltaTime;
        steps++;
    }
}

void Physics2DWorld::StepFixed(Scene* scene, float dt)
{
    if (!scene || dt <= 0.0f) return;
    FixedStep(scene, dt);
}

void Physics2DWorld::Rebuild(Scene* scene)
{
    CollectBodies(scene);
    previousContacts.clear();
    enterEvents.clear();
    exitEvents.clear();
    accumulator = 0.0f;
}

void Physics2DWorld::Clear()
{
    bodies.clear();
    contacts.clear();
    previousContacts.clear();
    enterEvents.clear();
    exitEvents.clear();
    accumulator = 0.0f;
}

void Physics2DWorld::FixedStep(Scene* scene, float dt)
{
    CollectBodies(scene);
    IntegrateForces(dt);
    DetectCollisions();
    DetectContactEvents();
    for (int i = 0; i < velocityIterations; ++i) SolveVelocity(dt);
    for (int i = 0; i < positionIterations; ++i) SolvePositions();
    SyncTransforms();
}

void Physics2DWorld::CollectBodies(Scene* scene)
{
    bodies.clear();
    if (!scene || !scene->rootGameObject) return;

    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        Collider2DComponent* collider = obj->GetComponent<Collider2DComponent>();
        if (transform && transform->enabled && collider && collider->enabled)
        {
            Rigidbody2DComponent* rb = obj->GetComponent<Rigidbody2DComponent>();
            bodies.push_back({ obj, transform, rb, collider });
        }
        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(scene->rootGameObject.get());
}

void Physics2DWorld::IntegrateForces(float dt)
{
    for (Physics2DBody& body : bodies)
    {
        Rigidbody2DComponent* rb = body.rigidbody;
        if (!rb || !rb->enabled || rb->type != Rigidbody2DComponent::Dynamic) continue;

        glm::vec2 acceleration = rb->forceAccum / glm::max(0.0001f, rb->mass);
        if (rb->useGravity) acceleration += gravity * rb->gravityScale;
        rb->velocity += acceleration * dt;
        rb->angularVelocity += rb->torqueAccum / glm::max(0.0001f, rb->mass) * dt;
        rb->velocity *= glm::max(0.0f, 1.0f - rb->linearDamping * dt);
        rb->angularVelocity *= glm::max(0.0f, 1.0f - rb->angularDamping * dt);

        body.transform->position.x += rb->velocity.x * dt;
        body.transform->position.y += rb->velocity.y * dt;
        body.transform->rotation.z += rb->angularVelocity * dt;
        body.transform->localDirty = true;
        rb->ClearAccumulators();
    }
}

void Physics2DWorld::DetectCollisions()
{
    contacts.clear();
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i)
    {
        for (int j = i + 1; j < static_cast<int>(bodies.size()); ++j)
        {
            float invA = InvMass(bodies[i].rigidbody);
            float invB = InvMass(bodies[j].rigidbody);
            if (invA == 0.0f && invB == 0.0f) continue;

            Contact contact;
            contact.a = i;
            contact.b = j;
            if (!TestCollision(bodies[i], bodies[j], contact)) continue;
            contact.isTrigger = bodies[i].collider->isTrigger || bodies[j].collider->isTrigger;
            contacts.push_back(contact);
        }
    }
}

void Physics2DWorld::SolveVelocity(float)
{
    for (const Contact& c : contacts)
    {
        Physics2DBody& a = bodies[c.a];
        Physics2DBody& b = bodies[c.b];
        if (c.isTrigger) continue;

        float invA = InvMass(a.rigidbody);
        float invB = InvMass(b.rigidbody);
        float invTotal = invA + invB;
        if (invTotal <= 0.0f) continue;

        glm::vec2 va = a.rigidbody ? a.rigidbody->velocity : glm::vec2(0.0f);
        glm::vec2 vb = b.rigidbody ? b.rigidbody->velocity : glm::vec2(0.0f);
        float normalVel = glm::dot(vb - va, c.normal);
        if (normalVel > 0.0f) continue;

        float restitution = glm::max(a.collider->restitution, b.collider->restitution);
        float j = -(1.0f + restitution) * normalVel / invTotal;
        glm::vec2 impulse = j * c.normal;
        if (a.rigidbody && invA > 0.0f) a.rigidbody->velocity -= impulse * invA;
        if (b.rigidbody && invB > 0.0f) b.rigidbody->velocity += impulse * invB;

        va = a.rigidbody ? a.rigidbody->velocity : glm::vec2(0.0f);
        vb = b.rigidbody ? b.rigidbody->velocity : glm::vec2(0.0f);
        glm::vec2 tangent = (vb - va) - glm::dot(vb - va, c.normal) * c.normal;
        float tangentLen = glm::length(tangent);
        if (tangentLen <= 0.0001f) continue;
        tangent /= tangentLen;
        float jt = -glm::dot(vb - va, tangent) / invTotal;
        float friction = std::sqrt(glm::max(0.0f, a.collider->friction * b.collider->friction));
        jt = glm::clamp(jt, -j * friction, j * friction);
        glm::vec2 frictionImpulse = jt * tangent;
        if (a.rigidbody && invA > 0.0f) a.rigidbody->velocity -= frictionImpulse * invA;
        if (b.rigidbody && invB > 0.0f) b.rigidbody->velocity += frictionImpulse * invB;
    }
}

void Physics2DWorld::SolvePositions()
{
    for (const Contact& c : contacts)
    {
        if (c.isTrigger) continue;
        Physics2DBody& a = bodies[c.a];
        Physics2DBody& b = bodies[c.b];
        float invA = InvMass(a.rigidbody);
        float invB = InvMass(b.rigidbody);
        float invTotal = invA + invB;
        if (invTotal <= 0.0f) continue;

        float correctionDepth = glm::max(c.depth - 0.01f, 0.0f) * 0.8f;
        glm::vec2 correction = c.normal * (correctionDepth / invTotal);
        if (invA > 0.0f)
        {
            a.transform->position.x -= correction.x * invA;
            a.transform->position.y -= correction.y * invA;
            a.transform->localDirty = true;
        }
        if (invB > 0.0f)
        {
            b.transform->position.x += correction.x * invB;
            b.transform->position.y += correction.y * invB;
            b.transform->localDirty = true;
        }
    }
}

void Physics2DWorld::SyncTransforms()
{
    for (Physics2DBody& body : bodies)
        if (body.transform) body.transform->UpdateTransform();
}

void Physics2DWorld::DetectContactEvents()
{
    enterEvents.clear();
    exitEvents.clear();
    std::map<ContactKey, const Contact*> current;

    for (const Contact& c : contacts)
    {
        GameObject* a = bodies[c.a].object;
        GameObject* b = bodies[c.b].object;
        if (!a || !b) continue;
        current[MakeKey(a, b)] = &c;
    }

    for (const auto& [key, c] : current)
    {
        if (previousContacts.count(key)) continue;
        ContactEvent2D ev;
        ev.a = key.first;
        ev.b = key.second;
        bool flipped = bodies[c->a].object != key.first;
        ev.normal = flipped ? -c->normal : c->normal;
        ev.point = c->point;
        ev.depth = c->depth;
        ev.isTrigger = c->isTrigger;
        enterEvents.push_back(ev);
    }

    for (const ContactKey& key : previousContacts)
    {
        if (current.count(key)) continue;
        ContactEvent2D ev;
        ev.a = key.first;
        ev.b = key.second;
        exitEvents.push_back(ev);
    }

    previousContacts.clear();
    for (const auto& [key, c] : current)
        previousContacts.insert(key);
}

Physics2DWorld::ContactKey Physics2DWorld::MakeKey(GameObject* a, GameObject* b)
{
    return a < b ? ContactKey{ a, b } : ContactKey{ b, a };
}
